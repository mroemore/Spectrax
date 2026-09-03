# Mod Sources + Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the instrument page's envelope-only mod strip into a unified mod-sources container — every source (ENV / LFO / RND) in one list with a per-entry type selector, type-specific controls, a route button (focused → connection-line overlay, pressed → destination-picking layer) and a delete button (runtime sources only, confirm modal).

**Architecture:** `inst->modList` is the single source-of-truth list (it already is — `presetFromInstrument` iterates it). The UI builds one entry per modList source; core sources (indices `[0, coreEnvelopeCount)`) keep their existing controls, runtime sources gain TYPE/ROUTE/DELETE. Routing mutates the existing `paramList` modulation graph (`addModulation`/`removeModulation`, amount 1.0/MO_ADD). The destination-picking layer and delete-confirm modal reuse the existing `Layer`/`LayerStack` overlay machinery; the focused-route overlay is a new draw-only layer that captures no input.

**Tech Stack:** C99, raylib (GUI), PortAudio (audio thread), meson/ninja, the existing `g_audioLock` + `inst->rebuilding` race guard.

## Global Constraints

- **Audio-thread safety:** every GUI-thread mutation of `inst->paramList`/`inst->modList` (add, remove, type change, route change) is wrapped in `pthread_mutex_lock(&g_audioLock)` + `inst->rebuilding = true` … `false` + `pthread_mutex_unlock(&g_audioLock)`. This is the established pattern in `addRuntimeEnvelope`/`removeRuntimeEnvelope` (gui.c). The audio callback skips a channel while `rebuilding`.
- **Core-envelope constraint:** modList entries `[0, inst->coreEnvelopeCount)` are voice-aliased (voice.c `initialize_voice`). They have NO type selector and NO delete button. `changeModType`/`removeSource` reject `idx < coreEnvelopeCount`.
- **Data model:** `inst->modList` is the unified source list. `inst->envelopes[]` holds ONLY the core envelopes (indices `< coreEnvelopeCount`). `inst->envelopeCount` is kept synced to `inst->modList->count` (a "source count") — the harness `ASSERT envcount` reads it, so it must keep counting runtime sources.
- **Entry ordering:** the mod sources container builds exactly one entry per modList source, in list order; a runtime entry's index in the container == its modList index. `removeMod` compacts the list, so indices are only valid within one graph build.
- **Route model:** fixed amount 1.0, MO_ADD. Routing state lives in the modulation graph itself (`paramList`), no separate route bookkeeping.
- **Cap:** total sources capped at `MAX_ENVELOPES` (6) for the container (existing array sizing; `addRuntimeSource` refuses when `modList->count >= MAX_ENVELOPES`).
- **Gate (every task):** `ninja -C build` builds clean (0 errors), `meson test -C build` passes all suites, all scripted fixtures PASS (run via `bin/instrument_harness --script <fixture>` under `xvfb-run`), the app boots (`bin/spectrax` under Xvfb, no FPE/segfault). Do NOT run the app outside `bin/` (assets are cwd-relative).
- **LSP note:** clangd reports `kiss_fft.h not found` / include-path noise across the tree — ignore it; meson is the source of truth.

---

### Task 1: `changeModType` + `rewireModulationsForSource`

**Files:**
- Modify: `src/modsystem.h`
- Modify: `src/modsystem.c`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: `ModList`/`ParamList`/`Mod`/`LFO`/`Random`/`Envelope` from `modsystem.h`; existing primitives `removeFromParamList`, `addModulation`/`removeModulation`, `initLfoDefaults`, `initRandDefaults`, `initEnvelopeDefaults`, `addEnvelopeStage`, `freeLFO`/`freeRandom`/`freeEnvelope`, `generateEnvelope`, `generateSine`, `generateSquare`, `generateRamp`, `generateRandom`, `generateDrunk`.
- Produces: `bool changeModType(ModList *modList, Mod *mod, ModType newType, ParamList *paramList)` — swaps a mod's type in place: same modList slot (apply-pass order preserved), same `output` Parameter (reused, never freed), existing routes preserved (rewired to the new struct). `void rewireModulationsForSource(ParamList *list, Mod *oldSource, Mod *newSource)` — rewires every connection whose `source == oldSource` to `newSource`.

- [ ] **Step 1: Write the failing tests** in `tests/dsp/test_modsystem.c`

```c
/* changeModType preserves the output param + existing routes and swaps
 * the type-specific params. Test list: the same modList slot is reused
 * (apply-pass order preserved). */
static int test_change_mod_type(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Parameter *dest = createParameter(pl, "dest", 0.5f, 0.0f, 1.0f);
    Parameter *dest2 = createParameter(pl, "dest2", 0.5f, 0.0f, 1.0f);

    Envelope *env = createAD(pl, ml, 0.25f, 4.25f, "src");
    Mod *m0 = &env->base;
    Parameter *out = m0->output;
    ASSERT_TRUE(out != NULL, "env has an output param");
    ASSERT_TRUE(addModulation(pl, m0, dest, 1.0f, MO_ADD), "route env->dest");
    ASSERT_TRUE(addModulation(pl, m0, dest2, 1.0f, MO_MUL), "route env->dest2");
    int before = ml->count;
    int beforeIdx = -1;
    for(int i = 0; i < ml->count; i++) if(ml->mods[i] == m0) beforeIdx = i;
    ASSERT_TRUE(beforeIdx >= 0, "env is registered in modList");

    /* ENV -> LFO */
    ASSERT_TRUE(changeModType(ml, m0, MT_LFO, pl), "ENV->LFO succeeds");
    Mod *m1 = ml->mods[beforeIdx];
    ASSERT_TRUE(m1 != m0, "a fresh struct was allocated");
    ASSERT_EQ(m1->type, MT_LFO, "type is now LFO");
    ASSERT_TRUE(m1->output == out, "output param is preserved (same pointer)");
    ASSERT_EQ(ml->count, before, "modList count unchanged");
    /* routes survived + rewired */
    ASSERT_TRUE(hasRouteFrom(pl, dest, m1), "dest still modulated by the (new) source");
    ASSERT_TRUE(hasRouteFrom(pl, dest2, m1), "dest2 still modulated by the (new) source");
    /* old type params are gone from the list */
    LFO *lfo = (LFO *)m1;
    ASSERT_TRUE(paramRegistered(pl, lfo->rate), "new LFO rate registered");
    ASSERT_TRUE(paramRegistered(pl, lfo->phase), "new LFO phase registered");
    ASSERT_TRUE(!env->stages[0].duration || !paramRegistered(pl, env->stages[0].duration), "old env stage params removed");
    int found = 0;
    for(int i = 0; i < pl->count; i++) {
        if(pl->params[i] && pl->params[i]->name && strcmp(pl->params[i]->name, "dest") == 0) found++;
    }
    ASSERT_EQ(found, 1, "dest param still present once");

    /* LFO -> RND */
    ASSERT_TRUE(changeModType(ml, m1, MT_RND, pl), "LFO->RND succeeds");
    Mod *m2 = ml->mods[beforeIdx];
    ASSERT_EQ(m2->type, MT_RND, "type is now RND");
    ASSERT_TRUE(m2->output == out, "output preserved again");
    ASSERT_TRUE(hasRouteFrom(pl, dest, m2), "dest still routed after LFO->RND");

    /* RND -> ENV (fresh AD) */
    ASSERT_TRUE(changeModType(ml, m2, MT_ENV, pl), "RND->ENV succeeds");
    Mod *m3 = ml->mods[beforeIdx];
    ASSERT_EQ(m3->type, MT_ENV, "type is now ENV");
    ASSERT_TRUE(m3->output == out, "output preserved");
    Envelope *env2 = (Envelope *)m3;
    ASSERT_EQ(env2->stageCount, 2, "fresh env has 2 AD stages");
    ASSERT_TRUE(hasRouteFrom(pl, dest, m3), "dest still routed after RND->ENV");

    /* invalid type rejected */
    ASSERT_TRUE(!changeModType(ml, m3, MT_OFS, pl), "MT_OFS rejected");
    ASSERT_TRUE(!changeModType(ml, m3, MT_COUNT, pl), "MT_COUNT rejected");

    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_change_mod_type\n");
    return 0;
}

static int test_change_mod_type_same_type(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.25f, 4.25f, "src");
    Mod *m0 = &env->base;
    int count = ml->count;
    ASSERT_TRUE(changeModType(ml, m0, MT_ENV, pl), "same-type change is a no-op");
    ASSERT_EQ(ml->count, count, "no structural change");
    ASSERT_TRUE(ml->mods[0] == m0, "same pointer kept");
    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_change_mod_type_same_type\n");
    return 0;
}

static int test_change_mod_type_null(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.25f, 4.25f, "src");
    ASSERT_TRUE(!changeModType(NULL, &env->base, MT_LFO, pl), "NULL list rejected");
    ASSERT_TRUE(!changeModType(ml, NULL, MT_LFO, pl), "NULL mod rejected");
    ASSERT_TRUE(!changeModType(ml, &env->base, MT_LFO, NULL), "NULL paramList rejected");
    /* unregistered mod rejected */
    Envelope *stray = createEnvelope(pl, ml, "stray2");
    removeFromModList(ml, &stray->base);
    ASSERT_TRUE(!changeModType(ml, &stray->base, MT_LFO, pl), "unregistered mod rejected");
    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_change_mod_type_null\n");
    return 0;
}
```

The helper macros/functions used above (`hasRouteFrom`, `paramRegistered`) must exist in the test file — add them near the top:

```c
static int hasRouteFrom(ParamList *pl, Parameter *dest, Mod *src) {
    if(!pl || !dest || !src) return 0;
    ModConnection *c = dest->modulators;
    while(c) {
        if(c->source == src) return 1;
        c = c->next;
    }
    return 0;
}

static int paramRegistered(ParamList *pl, Parameter *p) {
    if(!pl || !p) return 0;
    for(int i = 0; i < pl->count; i++) {
        if(pl->params[i] == p) return 1;
    }
    return 0;
}
```

Register all three tests in `main()` of `tests/dsp/test_modsystem.c` (find the `int main(void)` and add the calls alongside the existing `fails += test_...()` lines):

```c
    fails += test_change_mod_type();
    fails += test_change_mod_type_same_type();
    fails += test_change_mod_type_null();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `ninja -C build && ./build/tests/test_modsystem 2>&1 | grep -E "FAIL|PASS test_change"`
Expected: the `test_change_mod_type` line FAILs / the binary errors on the undefined `changeModType`.

- [ ] **Step 3: Implement**

In `src/modsystem.h`, add after the `removeMod` declaration (near line 188):

```c
bool changeModType(ModList *modList, Mod *mod, ModType newType, ParamList *paramList);
void rewireModulationsForSource(ParamList *list, Mod *oldSource, Mod *newSource);
```

In `src/modsystem.c`, add after `rewireModulation` (ends line 410):

```c
void rewireModulationsForSource(ParamList *list, Mod *oldSource, Mod *newSource) {
    if(!list || !oldSource || !newSource) {
        return;
    }
    for(int i = 0; i < list->count; i++) {
        Parameter *p = list->params[i];
        if(!p) {
            continue;
        }
        ModConnection *c = p->modulators;
        while(c) {
            if(c->source == oldSource) {
                c->source = newSource;
            }
            c = c->next;
        }
    }
}

/* Swap a mod's type in place: same modList slot, same output parameter,
 * existing routes preserved. The old concrete struct is freed (its type
 * params are removed from paramList first); a fresh struct of `newType`
 * is registered in the same slot and all connections are rewired to it.
 * Runtime sources only — the caller (UI) guards against core indices. */
bool changeModType(ModList *modList, Mod *mod, ModType newType, ParamList *paramList) {
    if(!modList || !mod || !paramList) {
        return false;
    }
    if(newType != MT_ENV && newType != MT_LFO && newType != MT_RND) {
        return false;
    }
    if(mod->type == newType) {
        return true;
    }
    int slot = -1;
    for(int i = 0; i < modList->count; i++) {
        if(modList->mods[i] == mod) {
            slot = i;
            break;
        }
    }
    if(slot < 0) {
        return false;
    }
    Parameter *output = mod->output;
    char name[MAX_NAME_LEN];
    strncpy(name, mod->name, MAX_NAME_LEN);

    /* Remove the OLD type params from the list (they are freed with the
     * old struct below; the shared output param stays). */
    switch(mod->type) {
        case MT_LFO: {
            LFO *l = (LFO *)mod;
            if(l->rate) removeFromParamList(paramList, l->rate);
            if(l->phase) removeFromParamList(paramList, l->phase);
            break;
        }
        case MT_RND: {
            Random *r = (Random *)mod;
            if(r->rate) removeFromParamList(paramList, r->rate);
            if(r->phase) removeFromParamList(paramList, r->phase);
            break;
        }
        case MT_ENV: {
            Envelope *e = (Envelope *)mod;
            for(int i = 0; i < e->stageCount; i++) {
                if(e->stages[i].duration) removeFromParamList(paramList, e->stages[i].duration);
                if(e->stages[i].curvature) removeFromParamList(paramList, e->stages[i].curvature);
            }
            break;
        }
        default:
            break;
    }

    Mod *fresh = NULL;
    switch(newType) {
        case MT_LFO: {
            LFO *l = (LFO *)calloc(1, sizeof(LFO));
            memcpy(&l->base, mod, sizeof(Mod));
            l->base.output = output;
            l->base.type = MT_LFO;
            initLfoDefaults(l, paramList, 1.0f, LS_SIN);
            l->shape = LS_SIN;
            l->base.generate = generateSine;
            fresh = &l->base;
            break;
        }
        case MT_RND: {
            Random *r = (Random *)calloc(1, sizeof(Random));
            memcpy(&r->base, mod, sizeof(Mod));
            r->base.output = output;
            r->base.type = MT_RND;
            initRandDefaults(r, paramList, 1.0f, RT_SNH);
            r->shape = RT_SNH;
            r->base.generate = generateRandom;
            fresh = &r->base;
            break;
        }
        case MT_ENV: {
            Envelope *e = (Envelope *)calloc(1, sizeof(Envelope));
            memcpy(&e->base, mod, sizeof(Mod));
            e->base.output = output;
            e->base.type = MT_ENV;
            initEnvelopeDefaults(e);
            addEnvelopeStage(paramList, e, true, 0.25f, 1.0f, 0.95f, "A");
            addEnvelopeStage(paramList, e, false, 4.25f, 0.0f, 0.1f, "D");
            e->base.generate = generateEnvelope;
            fresh = &e->base;
            break;
        }
        default:
            break;
    }
    if(!fresh) {
        return false;
    }

    rewireModulationsForSource(paramList, mod, fresh);
    modList->mods[slot] = fresh;

    /* Free the old struct WITHOUT freeing the shared output param. */
    mod->output = NULL;
    switch(mod->type) {
        case MT_LFO:
            freeLFO((LFO *)mod);
            break;
        case MT_RND:
            freeRandom((Random *)mod);
            break;
        case MT_ENV:
            freeEnvelope((Envelope *)mod);
            break;
        default:
            freeMod(mod);
            break;
    }
    return true;
}
```

Note: `initLfoDefaults`/`initRandDefaults` create `rate` + `phase` only in the current code (the `shape` Parameter is added in Task 2). Task 1 does not depend on the shape param.

- [ ] **Step 4: Run the test to verify it passes**

Run: `ninja -C build && ./build/tests/test_modsystem 2>&1 | grep -E "FAIL|PASS test_change"`
Expected: all three `PASS test_change_mod_type*` lines print, no FAIL.

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.h src/modsystem.c tests/dsp/test_modsystem.c
git commit -m "feat(modsystem): changeModType swaps a mod's type in place, preserving output + routes"
```

---

### Task 2: LFO/Random shape Parameters

**Files:**
- Modify: `src/modsystem.h`
- Modify: `src/modsystem.c`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: `LFO`/`Random` structs, `initLfoDefaults`, `initRandDefaults`, `createParameterPro`, `setParameterBaseValue`, `getParameterValue`, `createLFO`, `createRandom`, `initLfoFromPreset`, `initRandFromPreset`, `removeMod`, `freeLFO`, `freeRandom` (all `modsystem.{h,c}`).
- Produces: `Parameter *shape` field on `LFO` and `Random` (range `0..LS_COUNT-1` / `0..RT_COUNT-1`). `void cbLfoShapeOnChange(void *data)` / `void cbRandShapeOnChange(void *data)` — `data` is the `LFO*`/`Random*`; on change they sync the concrete `shape` int and set the `generate` fn. Helper `void setLfoShapeGenerate(LFO *lfo)` / `void setRandShapeGenerate(Random *rnd)` that derive the generate fn from the current shape. Task 3+ UI builds a SHAPE dial bound to `lfo->shape` / `rnd->shape`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/dsp/test_modsystem.c`:

```c
static int test_lfo_shape_param(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo");
    ASSERT_TRUE(lfo->shape != NULL, "LFO has a shape Parameter");
    ASSERT_EQ(getParameterValueAsInt(lfo->shape), LS_SIN, "shape param defaults to the initial shape");
    ASSERT_EQ(lfo->base.generate == generateSine ? 1 : 0, 1, "generate is generateSine for LS_SIN");

    setParameterBaseValue(lfo->shape, (float)LS_SQU);
    ASSERT_EQ(lfo->shape, LS_SQU, "onChange synced lfo->shape int");
    ASSERT_EQ(lfo->base.generate == generateSquare ? 1 : 0, 1, "generate is generateSquare for LS_SQU");

    setParameterBaseValue(lfo->shape, (float)LS_RMP);
    ASSERT_EQ(lfo->base.generate == generateRamp ? 1 : 0, 1, "generate is generateRamp for LS_RMP");

    /* clamp: out-of-range writes clamp to the range */
    setParameterBaseValue(lfo->shape, 999.0f);
    ASSERT_EQ(getParameterValueAsInt(lfo->shape), LS_RMP, "out-of-range clamps to LS_RMP");

    /* removeMod removes the shape param */
    ASSERT_TRUE(removeMod(ml, pl, &lfo->base), "removeMod removes the LFO");
    ASSERT_TRUE(!paramRegistered(pl, lfo->shape), "shape param removed with the LFO");

    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_lfo_shape_param\n");
    return 0;
}

static int test_rand_shape_param(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Random *rnd = createRandom(pl, ml, 0, 1.0f, RT_SNH, "rnd");
    ASSERT_TRUE(rnd->shape != NULL, "Random has a shape Parameter");
    ASSERT_EQ(getParameterValueAsInt(rnd->shape), RT_SNH, "shape param defaults to RT_SNH");
    ASSERT_EQ(rnd->base.generate == generateRandom ? 1 : 0, 1, "generate is generateRandom for RT_SNH");

    setParameterBaseValue(rnd->shape, (float)RT_DRK);
    ASSERT_EQ(rnd->shape, RT_DRK, "onChange synced rnd->shape int");
    ASSERT_EQ(rnd->base.generate == generateDrunk ? 1 : 0, 1, "generate is generateDrunk for RT_DRK");

    ASSERT_TRUE(removeMod(ml, pl, &rnd->base), "removeMod removes the Random");
    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_rand_shape_param\n");
    return 0;
}
```

Register in `main()`:
```c
    fails += test_lfo_shape_param();
    fails += test_rand_shape_param();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `ninja -C build && ./build/tests/test_modsystem 2>&1 | grep -E "FAIL|PASS test_lfo_shape"`
Expected: FAIL on the `LFO has a shape Parameter` assertion (the field does not exist yet).

- [ ] **Step 3: Implement**

In `src/modsystem.h`:

```c
typedef struct {
	Mod base;
	Parameter *rate;
	Parameter *phase;
	Parameter *shape;
	int shapeValue;
} LFO;
```

```c
typedef struct {
	Mod base;
	Parameter *rate;
	Parameter *phase;
	Parameter *shape;
	int shapeValue;
	float lastPhase;
	float lastRandom;
} Random;
```

Keep the field name `shape` used today (the int) as `shapeValue` so existing readers compile; add the new `Parameter *shape`. (Existing code like `lfo->shape = LS_SIN` still works on `shapeValue`.)

Add to `src/modsystem.h` prototypes:

```c
void cbLfoShapeOnChange(void *data);
void cbRandShapeOnChange(void *data);
```

In `src/modsystem.c`, replace `initLfoDefaults` (currently line 457):

```c
void cbLfoShapeOnChange(void *data) {
    LFO *lfo = (LFO *)data;
    if(!lfo) {
        return;
    }
    int sh = (lfo->shape) ? getParameterValueAsInt(lfo->shape) : lfo->shapeValue;
    if(sh < 0) sh = 0;
    if(sh >= LS_COUNT) sh = LS_COUNT - 1;
    lfo->shapeValue = sh;
    switch(sh) {
        case LS_SQU: lfo->base.generate = generateSquare; break;
        case LS_RMP: lfo->base.generate = generateRamp; break;
        default:     lfo->base.generate = generateSine; break;
    }
}

void initLfoDefaults(LFO *lfo, ParamList *paramList, float rate, int shape) {
    lfo->lastPhase = 0.0f;
    lfo->rate = createParameter(paramList, "LFO rate", rate, 0.1f, 100.0f);
    lfo->phase = createParameter(paramList, "LFO phase", 0.0f, 0.0f, 1.0f);
    lfo->shapeValue = shape;
    lfo->shape = createParameterPro(paramList, "LFO shape", (float)shape, 0.0f, (float)(LS_COUNT - 1), 1.0f, 1.0f, lfo, cbLfoShapeOnChange);
    cbLfoShapeOnChange(lfo);
}
```

Wait — `createLFO` sets `lfo->shape = shape` in `initLfoDefaults` today, and the existing struct field `int shape` becomes `shapeValue`. In `createLFO`, the `switch(shape)` sets `genFunc`; that still works, but `initLfoDefaults` now overrides generate via `cbLfoShapeOnChange` (correct). Replace `initRandDefaults` (line 430):

```c
void cbRandShapeOnChange(void *data) {
    Random *rnd = (Random *)data;
    if(!rnd) {
        return;
    }
    int sh = (rnd->shape) ? getParameterValueAsInt(rnd->shape) : rnd->shapeValue;
    if(sh < 0) sh = 0;
    if(sh >= RT_COUNT) sh = RT_COUNT - 1;
    rnd->shapeValue = sh;
    switch(sh) {
        case RT_DRK: rnd->base.generate = generateDrunk; break;
        default:     rnd->base.generate = generateRandom; break;
    }
}

void initRandDefaults(Random *rnd, ParamList *paramList, float rate, RandomType type) {
    rnd->lastPhase = 0.0f;
    rnd->lastRandom = 0.0f;
    rnd->rate = createParameter(paramList, "RNG rate", rate, 0.1f, 100.0f);
    rnd->phase = createParameter(paramList, "RNG phase", 0.0f, 0.0f, 1.0f);
    rnd->shapeValue = type;
    rnd->shape = createParameterPro(paramList, "RNG shape", (float)type, 0.0f, (float)(RT_COUNT - 1), 1.0f, 1.0f, rnd, cbRandShapeOnChange);
    cbRandShapeOnChange(rnd);
}
```

In `src/modsystem.c`, `initLfoFromPreset` and `initRandFromPreset` (they call `initLfoDefaults`/`initRandDefaults` then set the shape int) — after their `initLfoDefaults(r, ...)` call, add a sync so a preset shape lands in both the int and the param:

```c
    /* in initLfoFromPreset, after initLfoDefaults(...): */
    if(lfo->shape) {
        setParameterBaseValue(lfo->shape, (float)epd->shape);
    } else {
        lfo->shapeValue = epd->shape;
    }
    lfo->shapeValue = epd->shape;
    cbLfoShapeOnChange(lfo);
```

(Mirror the same in `initRandFromPreset` with `rand.shape` → `rnd->shape`/`cbRandShapeOnChange`.)

Update `saveLfoPreset` (modsystem.c line ~879) — it reads the struct's int `shape` field, which is now `shapeValue`:

```c
void saveLfoPreset(LfoPresetData *lpd, LFO *lfo) {
	lpd->phase = getParameterValue(lfo->phase);
	lpd->rate = getParameterValue(lfo->rate);
	lpd->shape = lfo->shapeValue;
}
```

(`saveRandPreset` and `initRandFromPreset` are currently empty stubs in `src/modsystem.c` — no change needed there.)

Update `removeMod`'s `MT_LFO` and `MT_RND` cases (line 347-357) to also remove the shape param:

```c
		case MT_LFO: {
			LFO *lfo = (LFO *)mod;
			if(lfo->rate) removeFromParamList(paramList, lfo->rate);
			if(lfo->phase) removeFromParamList(paramList, lfo->phase);
			if(lfo->shape) removeFromParamList(paramList, lfo->shape);
			break;
		}
		case MT_RND: {
			Random *rnd = (Random *)mod;
			if(rnd->rate) removeFromParamList(paramList, rnd->rate);
			if(rnd->phase) removeFromParamList(paramList, rnd->phase);
			if(rnd->shape) removeFromParamList(paramList, rnd->shape);
			break;
		}
```

Update `freeLFO` (line 987) and `freeRandom` (line 1007) to free the shape param:

```c
void freeLFO(LFO *lfo) {
	if(!lfo) return;
	if(lfo->shape) {
		freeParameter(lfo->shape);
		lfo->shape = NULL;
	}
	if(lfo->phase) {
		freeParameter(lfo->phase);
		lfo->phase = NULL;
	}
	if(lfo->rate) {
		freeParameter(lfo->rate);
		lfo->rate = NULL;
	}
	if(lfo->base.output) {
		freeParameter(lfo->base.output);
		lfo->base.output = NULL;
	}
	free(lfo);
}

void freeRandom(Random *rnd) {
	if(!rnd) return;
	if(rnd->shape) {
		freeParameter(rnd->shape);
		rnd->shape = NULL;
	}
	if(rnd->phase) {
		freeParameter(rnd->phase);
		rnd->phase = NULL;
	}
	if(rnd->rate) {
		freeParameter(rnd->rate);
		rnd->rate = NULL;
	}
	if(rnd->base.output) {
		freeParameter(rnd->base.output);
		rnd->base.output = NULL;
	}
	free(rnd);
}
```

Task 1's `changeModType` (MT_LFO/MT_RND branches) creates the fresh LFO/Random via `initLfoDefaults`/`initRandDefaults`, which now also create the shape param — remove its shape param too when converting away. Update the two removal branches in `changeModType` to also remove `l->shape` / `r->shape`:

```c
        case MT_LFO: {
            LFO *l = (LFO *)mod;
            if(l->rate) removeFromParamList(paramList, l->rate);
            if(l->phase) removeFromParamList(paramList, l->phase);
            if(l->shape) removeFromParamList(paramList, l->shape);
            break;
        }
        case MT_RND: {
            Random *r = (Random *)mod;
            if(r->rate) removeFromParamList(paramList, r->rate);
            if(r->phase) removeFromParamList(paramList, r->phase);
            if(r->shape) removeFromParamList(paramList, r->shape);
            break;
        }
```

Also update Task 1's fresh-struct assignments in `changeModType` — the old `l->shape = LS_SIN;` / `r->shape = RT_SNH;` (int field) become `shapeValue` after this task's struct rename:

```c
            initLfoDefaults(l, paramList, 1.0f, LS_SIN);
            l->shapeValue = LS_SIN;
            /* generate is set by cbLfoShapeOnChange via the shape param;
             * keep the explicit assignment below only if the shape param
             * creation is ever conditional. */
```

```c
            initRandDefaults(r, paramList, 1.0f, RT_SNH);
            r->shapeValue = RT_SNH;
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `ninja -C build && ./build/tests/test_modsystem 2>&1 | grep -E "FAIL|PASS"`
Expected: all tests pass, including `PASS test_lfo_shape_param` and `PASS test_rand_shape_param`. Then run the full suite: `meson test -C build` — all 8 suites green (existing LFO/RND callers still compile; the `shape` int → `shapeValue` rename is internal).

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.h src/modsystem.c tests/dsp/test_modsystem.c
git commit -m "feat(modsystem): LFO/Random shape Parameters with generate-syncing onChange"
```

---

### Task 3: `addRuntimeSource` / `removeSource` + source-count sync

**Files:**
- Modify: `src/gui.h`
- Modify: `src/gui.c`
- Modify: `src/main.c`
- Test: `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Consumes: `inst->modList`, `inst->paramList`, `inst->coreEnvelopeCount`, `createAD`, `removeMod`, `rebuildInstrumentGraph`, `g_audioLock`, `inst->rebuilding`, `getSelectedInstInstrument`, `inst->envelopeCount` (from `voice.h`/`gui.h`).
- Produces: `void addRuntimeSource(Instrument *inst)` — appends a default AD envelope to `inst->modList` (capped at `MAX_ENVELOPES`), syncs `inst->envelopeCount = inst->modList->count`, rebuilds the instrument graph, wrapped in the audio lock. `void removeSource(Instrument *inst, int srcIndex)` — rejects core indices and out-of-range, calls `removeMod`, syncs `envelopeCount`, rebuilds, wrapped in the lock. `void removeSelectedSource(void)` — walks the mod-wrap container to find the selected entry's source index and calls `removeSource` (generalises `removeSelectedEnvelope`).

- [ ] **Step 1: Write the failing test** in `tests/dsp/test_mod_voice.c`

```c
static int test_runtime_source_lifecycle(void) {
    SamplePool *sp = createSamplePool();
    PresetBank pb;
    initPresetBank(&pb);
    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);
    int core = inst->coreEnvelopeCount;   /* 4 for FM */
    int before = inst->modList->count;

    addRuntimeSource(inst);
    ASSERT_EQ(inst->modList->count, before + 1, "addRuntimeSource appends a source");
    ASSERT_EQ(inst->envelopeCount, inst->modList->count, "envelopeCount tracks modList count");
    Mod *m = inst->modList->mods[before];
    ASSERT_EQ(m->type, MT_ENV, "default source is an envelope");
    Envelope *env = (Envelope *)m;
    ASSERT_EQ(env->stageCount, 2, "default source has an AD shape");

    /* core sources cannot be removed */
    removeSource(inst, 0);
    ASSERT_EQ(inst->modList->count, before + 1, "core source removal rejected");
    /* out-of-range rejected */
    removeSource(inst, inst->modList->count);
    ASSERT_EQ(inst->modList->count, before + 1, "out-of-range removal rejected");
    /* runtime source removed */
    removeSource(inst, before);
    ASSERT_EQ(inst->modList->count, before, "runtime source removed");
    ASSERT_EQ(inst->envelopeCount, before, "envelopeCount synced after removal");

    free(inst);
    freeSamplePool(sp);
    printf("PASS test_runtime_source_lifecycle\n");
    return 0;
}
```

> Note: `freeInstrument` is `static` in `src/voice.c` and not exported — the test frees the Instrument with bare `free(inst)` (the same teardown the existing `test_mod_voice.c` tests use; `init_instrument` mallocs the Instrument + its lists, and the test's param/mod lists hold only params the teardown owns).

Check whether `freeInstrument` exists — if not, mirror the teardown used by the existing `test_mod_voice.c` tests (it likely calls `freeVoiceManager`/`free_param` helpers). Look at the file's existing teardown pattern and reuse it. Register in `main()`:
```c
    fails += test_runtime_source_lifecycle();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `ninja -C build && ./build/tests/test_mod_voice 2>&1 | grep -E "FAIL|PASS test_runtime"`
Expected: FAIL (the functions do not exist yet).

- [ ] **Step 3: Implement**

In `src/gui.h`, replace the `addRuntimeEnvelope` declaration (near line 387):

```c
void addRuntimeSource(Instrument *inst);
void removeSource(Instrument *inst, int srcIndex);
void removeSelectedSource(void);
```

In `src/gui.c`, replace `addRuntimeEnvelope` (line 2765) and `removeRuntimeEnvelope` (line 2794) with:

```c
void addRuntimeSource(Instrument *inst) {
    if(!inst || !inst->modList || inst->modList->count >= MAX_ENVELOPES) {
        return;
    }
    pthread_mutex_lock(&g_audioLock);
    inst->rebuilding = true;
    createAD(inst->paramList, inst->modList, 0.25f, 4.25f, "AD+");
    inst->envelopeCount = inst->modList->count;
    rebuildInstrumentGraph();
    inst->rebuilding = false;
    pthread_mutex_unlock(&g_audioLock);
}

void removeSource(Instrument *inst, int srcIndex) {
    if(!inst || !inst->modList || srcIndex < inst->coreEnvelopeCount || srcIndex >= inst->modList->count) {
        return;
    }
    pthread_mutex_lock(&g_audioLock);
    inst->rebuilding = true;
    removeMod(inst->modList, inst->paramList, inst->modList->mods[srcIndex]);
    inst->envelopeCount = inst->modList->count;
    rebuildInstrumentGraph();
    inst->rebuilding = false;
    pthread_mutex_unlock(&g_audioLock);
}

void removeSelectedSource(void) {
    if(!igui || !igui->vm || !igui->selectedInstrument) {
        return;
    }
    GuiNode *sel = getSelectedInstGraph()->selected;
    if(!sel) {
        return;
    }
    GuiNode *n = sel;
    while(n && n->container) {
        if(strcmp(n->container->name, "mod_wrap") == 0) {
            int idx = 0;
            ListElement *l = n->container->items->head;
            while(l && *(GuiNode **)l->data != n) {
                idx++;
                l = l->next;
            }
            /* idx counts container entries; entry 0 is the header row,
             * entries 1..N map to modList indices 0..N-1. */
            removeSource(igui->vm->instruments[*igui->selectedInstrument], idx - 1);
            return;
        }
        n = n->container;
    }
}
```

> Note: `removeSelectedSource` assumes the mod-wrap's first item is the header row (Task 4 adds it). The `idx - 1` mapping is only correct once Task 4 lands. If the header is not present, this walk under-counts — Task 4 finalises it. For Task 3, keep the existing `removeSelectedEnvelope` in place too (main.c still calls it) and add `removeSelectedSource` alongside.

In `src/main.c` (SCENE_INSTRUMENT input, ~line 505), switch the ADD keybind to `addRuntimeSource` and the REMOVE keybind to `removeSelectedSource`:

```c
				if(isKeyJustPressed(appState->inputState, KM_ADD)) {
					Instrument *inst = getSelectedInstInstrument();
					if(inst && inst->voiceType == VOICE_TYPE_FM) {
						addRuntimeSource(inst);
					}
				}
				if(isKeyJustPressed(appState->inputState, KM_REMOVE)) {
					removeSelectedSource();
				}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `ninja -C build && ./build/tests/test_mod_voice 2>&1 | grep -E "FAIL|PASS"`
Expected: all pass. Then `meson test -C build` — all green. The existing `add_route_delete` fixture still passes (ADD still adds an envelope; `ASSERT envcount` reads the synced count). If `removeSelectedSource`'s `idx - 1` mapping is wrong pre-Task-4, keep the old `removeSelectedEnvelope` in main.c until Task 4 and re-run the fixture to confirm.

- [ ] **Step 5: Commit**

```bash
git add src/gui.h src/gui.c src/main.c tests/dsp/test_mod_voice.c
git commit -m "feat(gui): addRuntimeSource/removeSource with modList source-count sync"
```

---

### Task 4: Mod-sources container UI + type selector + input dispatch

**Files:**
- Modify: `src/gui.c`
- Modify: `src/gui.h`
- Modify: `src/main.c`
- Modify: `src/tools/instrument_harness/fixtures/add_route_delete.txt`

**Interfaces:**
- Consumes: `addRuntimeSource`, `removeSource`, `changeModType` (Tasks 1-3), `inst->modList`, `inst->coreEnvelopeCount`, `createActionBtnGuiNode`, `createDialGuiNode`, `drawWrapperNode`, `rebuildInstrumentGraph`, `g_audioLock`.
- Produces: `static void appendModSourceEntry(Graph *g, GuiNode *container, Instrument *inst, int idx, int weight, bool selected)` — one entry row per source. `static SourceCtx { Instrument *inst; int idx; }` + `static SourceCtx g_sourceCtx[MAX_ENVELOPES]` refreshed each build. Callbacks `cbAddModSource`, `cbCycleSourceType`. The mod-wrap container gains a header row (name + ADD button) at index 0, so `removeSelectedSource`'s `idx - 1` mapping becomes correct.

- [ ] **Step 1: Write the failing test** — this task is UI/graph-structure; verify via the harness fixture (below) rather than a unit test. First update the container build in `createInstGraph` (line 2993):

Replace lines 2993-3008:

```c
	GuiNode *modwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "mod_wrap", 0, 0);
	/* Header row: container label + ADD action button. */
	GuiNode *modHdr = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "mods_hdr", 0, 0);
	modHdr->drawable = true;
	modHdr->draw = drawWrapperNode;
	GuiNode *modLabel = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODS", 0, 0);
	GuiNode *modAdd = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ADD", 0, cbAddModSource, inst);
	modAdd->name = strdup("MODS_ADD");
	appendItem(modHdr, modLabel, 4);
	appendItem(modHdr, modAdd, 1);
	appendItem(modwrap, modHdr, 1);
	for(int i = 0; i < inst->modList->count; i++) {
		appendModSourceEntry(instGraph, modwrap, inst, i, 1, false);
	}
	appendBlankNode(modwrap, 1);
	appendItem(instwrap, modwrap, 22);
```

Add `appendModSourceEntry` + the SourceCtx machinery + `cbAddModSource`/`cbCycleSourceType` before `createInstGraph`:

```c
typedef struct {
	Instrument *inst;
	int idx;
} SourceCtx;

static SourceCtx g_sourceCtx[MAX_ENVELOPES];

static void refreshSourceCtx(Instrument *inst) {
	for(int i = 0; i < MAX_ENVELOPES; i++) {
		g_sourceCtx[i].inst = inst;
		g_sourceCtx[i].idx = i;
	}
}

static void cbAddModSource(void *ctx) {
	Instrument *inst = (Instrument *)ctx;
	addRuntimeSource(inst);
}

static void cbCycleSourceType(void *ctx) {
	SourceCtx *sc = (SourceCtx *)ctx;
	if(!sc || !sc->inst || sc->idx < 0 || sc->idx >= sc->inst->modList->count) {
		return;
	}
	Mod *mod = sc->inst->modList->mods[sc->idx];
	ModType next = MT_ENV;
	switch(mod->type) {
		case MT_ENV: next = MT_LFO; break;
		case MT_LFO: next = MT_RND; break;
		default:     next = MT_ENV; break;
	}
	pthread_mutex_lock(&g_audioLock);
	sc->inst->rebuilding = true;
	if(changeModType(sc->inst->modList, mod, next, sc->inst->paramList)) {
		sc->inst->envelopeCount = sc->inst->modList->count;
		rebuildInstrumentGraph();
	}
	sc->inst->rebuilding = false;
	pthread_mutex_unlock(&g_audioLock);
}

static const char *modTypeTag(ModType t) {
	switch(t) {
		case MT_LFO: return "LFO";
		case MT_RND: return "RND";
		default:     return "ENV";
	}
}

static void appendModSourceEntry(Graph *g, GuiNode *container, Instrument *inst, int idx, int weight, bool selected) {
	Mod *mod = inst->modList->mods[idx];
	bool core = idx < inst->coreEnvelopeCount;
	GuiNode *wrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODSRC", 0, 0);
	wrap->drawable = true;
	wrap->draw = drawWrapperNode;

	if(!core) {
		refreshSourceCtx(inst);
		GuiNode *typeBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal,
		                                         modTypeTag(mod->type), 0, cbCycleSourceType, &g_sourceCtx[idx]);
		typeBtn->name = strdup(modTypeTag(mod->type));
		appendItem(wrap, typeBtn, 3);
	}

	switch(mod->type) {
		case MT_ENV: {
			Envelope *e = (Envelope *)mod;
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", selected, incParameterBaseValue, e->stages[0].duration), 4);
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, e->stages[0].curvature), 4);
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, e->stages[1].duration), 4);
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, e->stages[1].curvature), 4);
			break;
		}
		case MT_LFO: {
			LFO *l = (LFO *)mod;
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATE", selected, incParameterBaseValue, l->rate), 4);
			if(l->shape) {
				appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SHAPE", 0, incParameterBaseValue, l->shape), 4);
			}
			break;
		}
		case MT_RND: {
			Random *r = (Random *)mod;
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATE", selected, incParameterBaseValue, r->rate), 4);
			if(r->shape) {
				appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SHAPE", 0, incParameterBaseValue, r->shape), 4);
			}
			break;
		}
		default:
			break;
	}

	if(!core) {
		/* ROUTE + DELETE buttons (routed wiring arrives in Tasks 5-7;
		 * stub callbacks keep the layout stable). */
		appendItem(wrap, createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ROUTE", 0, NULL, NULL), 3);
		appendItem(wrap, createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "DEL", 0, NULL, NULL), 2);
	}
	appendItem(wrap, createBlankGuiNode(), 1);
	appendItem(container, wrap, weight);
	(void)g;
}
```

- [ ] **Step 2: Wire the KM_EDIT dispatch + ADD/REMOVE**

In `src/gui.c` `handlePresetUiInput`, replace the enumerated actionCb branch (lines 2189-2209) with a generic dispatch:

```c
	if(sel && isKeyJustPressed(is, KM_EDIT)) {
		if(sel->actionCb) {
			sel->actionCb(sel->actionCtx);
			/* PREV/NEXT apply a preset, which rebuilds the paramList; the
			 * graph's dials still point at the freed params. Rebuild so the
			 * dials re-address the new params. cbTypePrev/cbTypeNext and the
			 * source callbacks rebuild internally. */
			if(sel->actionCb == cbPresetPrev || sel->actionCb == cbPresetNext) {
				rebuildInstrumentGraph();
			}
			return true;
		}
	}
```

Add forward declarations for the new callbacks before `handlePresetUiInput` (near the existing forward-decl block at line 1914):

```c
static void cbAddModSource(void *ctx);
static void cbCycleSourceType(void *ctx);
```

- [ ] **Step 3: Run the updated fixture + gate**

The `add_route_delete.txt` fixture navigates the OLD entry layout (ATTACK CURVE DECAY CURVE ROUTE blank). Update it to the new layout. The entry row for a runtime source is now:

`TYPE ATTACK CURVE DECAY CURVE ROUTE DEL blank`

And the mod-wrap has a header row (MODS label + ADD button) as its first child, so reaching the first source entry needs one extra DOWN. Update the fixture's nav steps to match the new geometry — the exact DOWN/RIGHT counts must be confirmed empirically. Use the harness's `REPORT` verb on the entry containers to verify the built layout, then adjust the fixture:

- `ASSERT envcount==5` stays valid after ADD.
- From RATIO1 (instwrap): DOWN (btnrow2) → DOWN (mod-wrap header) → DOWN (env[0] entry) → DOWN ×4 → env[4] entry.
- Within the entry: RIGHT from TYPE → ATTACK → CURVE → DECAY → CURVE → ROUTE → DEL.

The fixture's assertions on the ROUTE dial's cycling (`ASSERT modulators==(0,ratio,N)`) still pass because Task 4 keeps the ROUTE wiring as-is for now (the picking layer replaces it in Task 6). Run:

```bash
ninja -C build && meson install -C build >/dev/null && meson test -C build 2>&1 | grep -E "Ok:|Fail:"
cd bin && xvfb-run -a -s "-screen 0 1280x800x24" ./instrument_harness --script ../src/tools/instrument_harness/fixtures/add_route_delete.txt 2>&1 | tail -1
```

Expected: suite 8/8, `add_route_delete: PASS`. If the nav steps in the fixture are off, fix them to match the built layout (verify with REPORT). Commit only after it passes.

- [ ] **Step 4: Commit**

```bash
git add src/gui.c src/gui.h src/main.c src/tools/instrument_harness/fixtures/add_route_delete.txt
git commit -m "feat(gui): unified mod-sources container with type selector + ADD"
```

---

### Task 5: Delete-confirm modal layer

**Files:**
- Modify: `src/gui.c`
- Modify: `src/gui.h`

**Interfaces:**
- Consumes: `Layer`/`pushLayer`/`popLayer` (gui_layer.h), `createActionBtnGuiNode`, `createGraph`, `removeSource` (Task 3), `g_sourceCtx` (Task 4).
- Produces: `static void cbDeleteSource(void *ctx)` — pushes a YES/NO confirm layer (mirroring `guiBuildOverwriteLayer`). YES calls `removeSource(inst, idx)` then pops; NO pops only.

- [ ] **Step 1: Implement** in `src/gui.c` (near the other overlay builders, ~line 1724):

```c
static void cbDeleteCancel(void *ctx) {
	(void)ctx;
	InstrumentGui *ig = igui;
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
}

static void cbDeleteConfirmYes(void *ctx) {
	SourceCtx *sc = (SourceCtx *)ctx;
	InstrumentGui *ig = igui;
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
	if(sc) {
		removeSource(sc->inst, sc->idx);
	}
}

static void cbDeleteSource(void *ctx) {
	SourceCtx *sc = (SourceCtx *)ctx;
	InstrumentGui *ig = igui;
	if(!ig || !sc) {
		return;
	}
	Graph *g = createGraph(na_horizontal);
	const int py = (SCREEN_H - 80) / 2;
	const int px = (SCREEN_W - 280) / 2;
	GuiNode *noBtn = createActionBtnGuiNode(px + 30, py + 44, 100, 22, 0, na_horizontal, "NO", 0, cbDeleteCancel, NULL);
	noBtn->name = strdup("DELETE_NO");
	GuiNode *yesBtn = createActionBtnGuiNode(px + 150, py + 44, 100, 22, 0, na_horizontal, "YES", 0, cbDeleteConfirmYes, sc);
	yesBtn->name = strdup("DELETE_YES");
	appendItem(g->root, noBtn, 1);
	appendItem(g->root, yesBtn, 1);
	changeGraphSelection(g, noBtn);
	Layer *layer = createLayer(g, px, py, 280, 80, "DELETE", true, true);
	pushLayer(&ig->overlayLayers, layer);
}
```

Forward-declare `cbDeleteSource` before `handlePresetUiInput`, and in `appendModSourceEntry` wire the DEL button:

```c
		appendItem(wrap, createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "DEL", 0, cbDeleteSource, &g_sourceCtx[idx]), 2);
```

- [ ] **Step 2: Verify** — build, then extend `add_route_delete.txt` (or a scratch copy) to test the delete flow:

```bash
ninja -C build && meson install -C build >/dev/null && meson test -C build 2>&1 | grep -E "Ok:|Fail:"
```

Manual fixture check (append to the fixture): ADD → nav to the new entry's DEL → `EDIT` (opens the confirm layer) → assert the top layer is the delete layer (`ASSERT toplayer==DELETE` if the harness supports it, else verify by `ASSERT selected==DELETE_NO`) → `EDIT` on YES → `ASSERT envcount` back to the pre-add count.

- [ ] **Step 3: Commit**

```bash
git add src/gui.c src/gui.h src/tools/instrument_harness/fixtures/add_route_delete.txt
git commit -m "feat(gui): delete-confirm modal for runtime mod sources"
```

---

### Task 6: Destination-picking layer + route/unroute

**Files:**
- Modify: `src/gui.c`
- Modify: `src/gui.h`
- Modify: `src/tools/instrument_harness/fixtures/add_route_delete.txt`

**Interfaces:**
- Consumes: `Layer`/`pushLayer`/`popLayer`, `createGraph`, `createActionBtnGuiNode`/`createDialGuiNode`, `addModulation`/`removeModulation`, `changeGraphSelection`, `g_sourceCtx`, `inst->paramList`, the base instrument graph's dial nodes.
- Produces: `static void cbOpenRouteLayer(void *ctx)` — builds + pushes the destination-picking layer. `static void cbRouteToDest(void *ctx)` — `DestCtx { Instrument *inst; int srcIdx; Parameter *dest; }`; toggles the modulation (add 1.0/MO_ADD or remove if present), then pops the layer. `static int collectRoutableDials(Graph *g, GuiNode **out, Parameter **outP, int cap)` — recursive walk collecting dial nodes (draw is `drawDialGuiNode` or `drawDiscreteDialGuiNode` and `p != NULL`).

- [ ] **Step 1: Implement** in `src/gui.c` (near the overlay builders):

```c
typedef struct {
	Instrument *inst;
	int srcIdx;
	Parameter *dest;
} DestCtx;

#define MAX_ROUTABLE_DIALS 64

static DestCtx g_destCtx[MAX_ROUTABLE_DIALS];
static Parameter *g_destParams[MAX_ROUTABLE_DIALS];

static int collectRoutableDials(GuiNode *node, Parameter **outParams, GuiNode **outNodes, int cap, int *n) {
	if(!node || *n >= cap) {
		return *n;
	}
	if((node->draw == drawDialGuiNode || node->draw == drawDiscreteDialGuiNode) && node->p) {
		if(*n < cap) {
			outParams[*n] = node->p;
			outNodes[*n] = node;
			(*n)++;
		}
		return *n;
	}
	if(node->items) {
		ListElement *e = node->items->head;
		for(int i = 0; i < node->itemCount && e; i++) {
			collectRoutableDials(*(GuiNode **)e->data, outParams, outNodes, cap, n);
			e = e->next;
		}
	}
	return *n;
}

static void cbRouteToDest(void *ctx) {
	DestCtx *dc = (DestCtx *)ctx;
	InstrumentGui *ig = igui;
	if(!dc || !dc->inst || !dc->dest) {
		return;
	}
	Mod *src = (dc->srcIdx >= 0 && dc->srcIdx < dc->inst->modList->count) ? dc->inst->modList->mods[dc->srcIdx] : NULL;
	if(!src) {
		return;
	}
	pthread_mutex_lock(&g_audioLock);
	dc->inst->rebuilding = true;
	/* toggle: if already routed, un-route; else route (1.0, MO_ADD) */
	ModConnection *c = dc->dest->modulators;
	int already = 0;
	while(c) {
		if(c->source == src) {
			already = 1;
			break;
		}
		c = c->next;
	}
	if(already) {
		removeModulation(dc->inst->paramList, dc->dest, src);
	} else {
		addModulation(dc->inst->paramList, src, dc->dest, 1.0f, MO_ADD);
	}
	dc->inst->rebuilding = false;
	pthread_mutex_unlock(&g_audioLock);
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
}

static void cbOpenRouteLayer(void *ctx) {
	SourceCtx *sc = (SourceCtx *)ctx;
	InstrumentGui *ig = igui;
	if(!ig || !sc || !sc->inst) {
		return;
	}
	Graph *base = getSelectedInstGraph();
	Parameter *params[MAX_ROUTABLE_DIALS];
	GuiNode *nodes[MAX_ROUTABLE_DIALS];
	int n = 0;
	if(base && base->root) {
		collectRoutableDials(base->root, params, nodes, MAX_ROUTABLE_DIALS, &n);
	}
	if(n == 0) {
		return;
	}
	/* The picking layer's graph: one selectable node per routable dial,
	 * positioned at the dial's screen rect, named after it. */
	Graph *g = createGraph(na_horizontal);
	GuiNode *first = NULL;
	for(int i = 0; i < n; i++) {
		g_destCtx[i].inst = sc->inst;
		g_destCtx[i].srcIdx = sc->idx;
		g_destCtx[i].dest = params[i];
		GuiNode *dn = createActionBtnGuiNode(0, 0, 60, 20, 2, na_horizontal,
		                                     params[i]->name ? params[i]->name : "DEST",
		                                     0, cbRouteToDest, &g_destCtx[i]);
		dn->name = strdup(params[i]->name ? params[i]->name : "DEST");
		dn->draw = drawRouteDestNode; /* custom highlight draw, see below */
		appendItem(g->root, dn, 1);
		/* appendItem runs reflowCoordinates, which overwrites the child
		 * rects from weights — set the dial's screen rect AFTER the
		 * append so each dest node sits exactly on its dial. */
		dn->x = nodes[i]->x;
		dn->y = nodes[i]->y;
		dn->w = nodes[i]->w > 4 ? nodes[i]->w : 60;
		dn->h = nodes[i]->h > 4 ? nodes[i]->h : 20;
		if(!first) {
			first = dn;
		}
	}
	if(first) {
		changeGraphSelection(g, first);
	}
	Layer *layer = createLayer(g, 0, 0, SCREEN_W, SCREEN_H, "ROUTEPICK", true, true);
	pushLayer(&ig->overlayLayers, layer);
}

/* Highlight draw for the destination-picking layer's nodes: a bright
 * outline + the parameter name, so the routable set reads at a glance. */
static void drawRouteDestNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	Color edge = gn->selected ? cs.labelSelected : cs.dial;
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0f, edge);
	if(gn->name) {
		DrawTextEx(pixelFont, gn->name, (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, edge);
	}
}
```

Wire the ROUTE button in `appendModSourceEntry`:

```c
		appendItem(wrap, createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ROUTE", 0, cbOpenRouteLayer, &g_sourceCtx[idx]), 3);
```

Forward-declare `cbOpenRouteLayer`, `cbRouteToDest`, `drawRouteDestNode` before `handlePresetUiInput`. Also forward-declare `drawDialGuiNode`/`drawDiscreteDialGuiNode` before `collectRoutableDials` (they are defined earlier in the file, so this is only needed if `collectRoutableDials` is placed above them — place it below them instead to avoid the forward decls).

- [ ] **Step 2: Verify** — extend `add_route_delete.txt` with a route flow:

```
# --- Route env[4] to RATIO1 via the picking layer ---
<nav to env[4] entry, select ROUTE>
EDIT                 # opens the destination-picking layer
ASSERT selected==RATIO1   # picking layer default selection = first dial
LEFT ...             # navigate to a specific dial if needed
EDIT                 # route/unroute + closes the layer
ASSERT modulators==(0,ratio,1)
<select ROUTE again> # re-open
ASSERT selected==RATIO1
EDIT                 # un-route
ASSERT modulators==(0,ratio,0)
```

Confirm the modulators assertion verb signature (`ASSERT modulators==(<op>,<kind>,<N>)` from instrument_harness.c:436) matches. Run the full gate as in Task 4.

- [ ] **Step 3: Commit**

```bash
git add src/gui.c src/gui.h src/tools/instrument_harness/fixtures/add_route_delete.txt
git commit -m "feat(gui): destination-picking route layer with route/unroute toggle"
```

---

### Task 7: Route-lines overlay (focused route button)

**Files:**
- Modify: `src/gui.c`
- Modify: `src/gui.h`

**Interfaces:**
- Consumes: `Layer`/`pushLayer`/`popLayer`/`topLayer`, `getSelectedInstGraph`, `collectRoutableDials` (Task 6), `inst->paramList` modulators, `g_sourceCtx`.
- Produces: `static void syncRouteLinesOverlay(InstrumentGui *ig)` — called from `DrawGUI`'s instrument branch each frame; pushes a draw-only "ROUTELINES" layer when the base graph's selected node is a ROUTE button, pops it otherwise. `static void drawRouteLinesNode(void *self)` — walks the source's modulations + the base graph's dial rects and draws colour-coded lines (ADD vs MUL).

- [ ] **Step 1: Implement** in `src/gui.c`:

```c
typedef struct {
	Instrument *inst;
	int srcIdx;
} RouteLinesCtx;

static RouteLinesCtx g_routeLinesCtx;

static int findDialRectForParam(GuiNode *node, Parameter *p, Rectangle *out) {
	if(!node || !p) {
		return 0;
	}
	if((node->draw == drawDialGuiNode || node->draw == drawDiscreteDialGuiNode) && node->p == p) {
		*out = (Rectangle){ node->x, node->y, node->w, node->h };
		return 1;
	}
	if(node->items) {
		ListElement *e = node->items->head;
		for(int i = 0; i < node->itemCount && e; i++) {
			if(findDialRectForParam(*(GuiNode **)e->data, p, out)) {
				return 1;
			}
			e = e->next;
		}
	}
	return 0;
}

/* Find the ROUTE button (actionCb == cbOpenRouteLayer) in the base graph. */
static int findRouteButtonRect(GuiNode *node, Rectangle *out) {
	if(!node) {
		return 0;
	}
	if(node->actionCb == cbOpenRouteLayer) {
		*out = (Rectangle){ node->x, node->y, node->w, node->h };
		return 1;
	}
	if(node->items) {
		ListElement *e = node->items->head;
		for(int i = 0; i < node->itemCount && e; i++) {
			if(findRouteButtonRect(*(GuiNode **)e->data, out)) {
				return 1;
			}
			e = e->next;
		}
	}
	return 0;
}

static void drawRouteLinesNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	RouteLinesCtx *rc = &g_routeLinesCtx;
	(void)gn;
	if(!rc->inst || rc->srcIdx < 0 || rc->srcIdx >= rc->inst->modList->count) {
		return;
	}
	Mod *src = rc->inst->modList->mods[rc->srcIdx];
	Graph *base = getSelectedInstGraph();
	if(!base || !base->root) {
		return;
	}
	/* source route-button anchor: the ROUTE button's rect in the base graph */
	Rectangle anchor = { 0, 0, 0, 0 };
	findRouteButtonRect(base->root, &anchor);
	if(anchor.width <= 0.0f || anchor.height <= 0.0f) {
		return;
	}
	Vector2 from = { anchor.x + anchor.width / 2, anchor.y + anchor.height / 2 };
	for(int i = 0; i < rc->inst->paramList->count; i++) {
		Parameter *p = rc->inst->paramList->params[i];
		if(!p) {
			continue;
		}
		ModConnection *c = p->modulators;
		while(c) {
			if(c->source == src) {
				Rectangle r;
				if(findDialRectForParam(base->root, p, &r)) {
					Vector2 to = { r.x + r.width / 2, r.y + r.height / 2 };
					Color col = (c->type && getParameterValueAsInt(c->type) == MO_MUL) ? cs.routeMul : cs.routeAdd;
					DrawLineEx(from, to, 2.0f, col);
				}
			}
			c = c->next;
		}
	}
}

static void syncRouteLinesOverlay(InstrumentGui *ig) {
	if(!ig) {
		return;
	}
	GuiNode *sel = getSelectedInstGraph()->selected;
	bool onRouteBtn = sel && sel->actionCb == cbOpenRouteLayer;
	Layer *top = topLayer(&ig->overlayLayers);
	bool hasOverlay = top && top->name && strcmp(top->name, "ROUTELINES") == 0;
	if(onRouteBtn && !hasOverlay) {
		Graph *g = createGraph(na_horizontal);
		GuiNode *lines = createGuiNode(0, 0, SCREEN_W, SCREEN_H, 0, na_horizontal, "routelines", 0, 0);
		lines->drawable = true;
		lines->draw = drawRouteLinesNode;
		appendItem(g->root, lines, 1);
		Layer *l = createLayer(g, 0, 0, SCREEN_W, SCREEN_H, "ROUTELINES", false, true);
		pushLayer(&ig->overlayLayers, l);
	} else if(!onRouteBtn && hasOverlay) {
		Layer *l = popLayer(&ig->overlayLayers);
		if(l) {
			destroyLayer(l);
		}
	}
}
```

In `DrawGUI`'s instrument branch (line 3066), after `drawNode(...)` and before `layerStackDraw`, add the sync call:

```c
		case SCENE_INSTRUMENT:
			drawNode(igui->instrumentScreenGraphs[*igui->selectedInstrument]->root);
			if(igui) {
				syncRouteLinesOverlay(igui);
				layerStackDraw(&igui->overlayLayers);
			}
			break;
```

In `handlePresetUiInput`, when a layer is open the whole input stream routes to `layerStackInput`. The ROUTELINES overlay must NOT capture input — add a guard so a ROUTELINES top layer does not intercept the stream (only the actual modal layers do). Near the top of `handlePresetUiInput` (line 2070):

```c
	InstrumentGui *ig = getInstrumentGui();
	if(ig && !layerStackIsEmpty(&ig->overlayLayers)) {
		Layer *top = topLayer(&ig->overlayLayers);
		bool isPassiveOverlay = top && top->name && strcmp(top->name, "ROUTELINES") == 0;
		if(!isPassiveOverlay) {
			layerStackInput(&ig->overlayLayers, is);
			return true;
		}
	}
```

Add theme colours `routeAdd` / `routeMul` to `ColourScheme` in `src/theme.h` (defaults: `{255,120,80,255}` ADD, `{120,200,255,255}` MUL) and include them in the JSON load/save paths (`src/gui_io.c` `loadThemeJson`/`saveThemeJson` + the shipped `bin/clr.json`), with fallback defaults in `initDefaultColourScheme` (`src/gui_io.c` or wherever it lives — grep for `labelSelected`'s default assignment).

- [ ] **Step 2: Verify** — build + run the gate. Manual check: focus a ROUTE button → the lines overlay appears (verify via a screenshot + pixel scan for the route colour); move selection away → the overlay pops. Add a `FRAMES` + `SHOT` capture to the fixture after selecting ROUTE and confirm the overlay layer is present via the harness's top-layer assertion if available (else visual).

- [ ] **Step 3: Commit**

```bash
git add src/gui.c src/gui.h src/theme.h src/gui_io.c bin/clr.json
git commit -m "feat(gui): connection-line overlay when a route button is focused"
```

---

### Task 8: Fixture suite + full gate

**Files:**
- Modify: `src/tools/instrument_harness/fixtures/add_route_delete.txt`
- Create: `src/tools/instrument_harness/fixtures/mod_sources.txt`
- Modify: `src/tools/instrument_harness/instrument_harness.c` (only if a new assert verb is needed)

**Interfaces:**
- Consumes: everything from Tasks 1-7.

- [ ] **Step 1: Write the full `mod_sources.txt` fixture** covering the complete flow:

```
# mod_sources.txt - unified mod-sources container end-to-end
# FM instrument: core 4 envelopes, then runtime sources.
ADD
FRAMES 2
ASSERT envcount==5

# --- Type cycle ENV -> LFO -> RND on the new source ---
<nav to the env[4] entry TYPE button>
EDIT            # ENV -> LFO
ASSERT selected==LFO
EDIT            # LFO -> RND
ASSERT selected==RND
EDIT            # RND -> ENV
ASSERT selected==ENV

# --- Route env[4] -> RATIO1 via the picking layer ---
<nav to ROUTE>
EDIT
ASSERT selected==RATIO1
EDIT
ASSERT modulators==(0,ratio,1)
# re-open + un-route
<nav to ROUTE>
EDIT
ASSERT selected==RATIO1
EDIT
ASSERT modulators==(0,ratio,0)

# --- Delete via the confirm modal ---
<nav to DEL>
EDIT
ASSERT selected==DELETE_NO
EDIT            # on NO cancels
ASSERT envcount==5
<nav to DEL>
EDIT
ASSERT selected==DELETE_NO
RIGHT
ASSERT selected==DELETE_YES
EDIT
ASSERT envcount==4
QUIT
```

The exact nav steps (DOWN/RIGHT counts) depend on the built layout — confirm them with `REPORT` + `ASSERT selected==` during development.

- [ ] **Step 2: Run the full gate**

```bash
ninja -C build 2>&1 | grep -cE "error:"        # expect 0
meson install -C build >/dev/null
meson test -C build 2>&1 | grep -E "Ok:|Fail:" # expect 8 Ok, 0 Fail
cd bin && for fx in chip_meta preset_save_load save_overwrite add_route_delete task4_size_verify; do \
  xvfb-run -a -s "-screen 0 1280x800x24" ./instrument_harness --script ../src/tools/instrument_harness/fixtures/$fx.txt 2>&1 | tail -1; done
xvfb-run -a -s "-screen 0 1280x800x24" ./instrument_harness --script ../src/tools/instrument_harness/fixtures/mod_sources.txt 2>&1 | tail -1
xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax 2>&1 | grep -icE "fpe|segfault"  # expect 0
```

Expected: 0 errors, 8/8 tests, all six fixtures PASS, clean boot.

Cleanup before commit: remove any leftover runtime-source artifacts the fixture created (`git status` — restore `bin/data/instrument_presets/*.ipb` + `bin/s1.sng` to their committed state if a fixture touched them, e.g. via `git show HEAD:<path> > <path>`).

- [ ] **Step 3: Commit**

```bash
git add src/tools/instrument_harness/fixtures/mod_sources.txt src/tools/instrument_harness/fixtures/add_route_delete.txt
git commit -m "test(harness): mod-sources fixture + full gate green"
```

---

## Self-Review Checklist

- **Spec coverage:**
  - A1 `changeModType` → Task 1. ✓
  - A2 runtime lifecycle + core constraint → Tasks 3-5 (removeSource rejects core; no TYPE/DEL on core entries). ✓
  - A3 ADD behaviour → Task 3 `addRuntimeSource`. ✓
  - B mod-sources container (header ADD + entry rows) → Task 4. ✓
  - B type selector (ENV/LFO/RND cycling) → Task 4 `cbCycleSourceType`. ✓
  - B type-specific controls (env dials / LFO RATE+SHAPE / RND RATE+SHAPE) → Tasks 2 + 4. ✓
  - C1 destination set from visible dials → Task 6 `collectRoutableDials`. ✓
  - C2 route button focused → lines overlay → Task 7. ✓
  - C3 route button pressed → destination-picking layer + toggle → Task 6. ✓
  - D delete modal (core sources never show it) → Task 5. ✓
  - E pattern-sequencer readiness: the framework operates on `Mod *` — the type selector is a plain 3-way cycle; a future `MT_PATTERN` plugs in via the same switch. No framework rework needed. ✓
  - F testing (modsystem changeModType; mod_voice add/remove round-trip + routes survive; harness fixture) → Tasks 1, 3, 8. ✓
  - G out of scope respected (fixed 1.0/ADD, no non-dial destinations, no core deletion, no pattern sequencers, no chips). ✓
- **Placeholder scan:** no TBDs; all code blocks are complete. The only deliberately-loose item is the fixture nav-step counts, which the plan instructs to confirm empirically with REPORT/ASSERT (the layout is spec'd, the exact pixel steps are not). This is an implementation-measurement, not a placeholder.
- **Type consistency:** `changeModType(ModList*, Mod*, ModType, ParamList*)` consistent across Tasks 1, 4. `SourceCtx {inst, idx}` consistent across Tasks 4-7. `removeSource(inst, idx)` consistent across Tasks 3, 5, 8. `envelopeCount` = modList count everywhere. `LFO.shapeValue`/`Random.shapeValue` int fields + `.shape` Parameter — consistent across Tasks 2, 4, 6.