# Unified Mod-Voice Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make mod types realtime-interchangeable via a union'd `Mod`, run every modulator per-voice through full graph copies (instrument = control-plane blueprint), expose voice gain/outLevel/pitch routing as visible, reroutable connections, and add LFO/RND play modes.

**Architecture:** `Mod` becomes a tagged union (`type` + named `union { EnvState env; LfoState lfo; RndState rnd; AttenState atten; } data;`); `changeModType` swaps the active payload in place so `ModConnection.source` (a stable `Mod *`) survives retype. The instrument graph becomes control-plane; each voice owns a full copy of sources + connections + destination params and syncs base values from the instrument each buffer (live modulation, zero raw-pointer aliasing). Voice `gain` / per-op `outLevel` / per-op `pitch` become real destinations seeded with default connections per type.

**Tech Stack:** C99, meson+ninja, raylib (UI), Unity test framework. Build: `ninja -C build`. Tests: `meson test -C build`. Scripted fixtures: `src/tools/instrument_harness/run_scripted.sh fixtures/<name>.txt` (run from repo root; PASS self-exits).

## Global Constraints

- **Audio thread safety:** every GUI-thread mutation of instrument param/mod lists or the voice pool holds `g_audioLock` (voice.h:309) across its free+rebuild window and sets `inst->rebuilding`; the PortAudio callback checks `rebuilding` and skips that channel's buffer window. No new code may free/reallocate anything the audio callback derefs without this guard.
- **TDD order:** write the failing test first, verify it fails, implement, verify it passes, commit. Full gate after each task: `ninja -C build && meson test -C build` (15/15) and the scripted fixtures still PASS.
- **Pinned semantics:** existing mod-system behaviors pinned by tests must not change unless a task explicitly re-pins (notably: `processModulations`' apply pass ignores `conn->amount` — the attenuator handles amount; looped-feedback fixed points).
- **Preset format is versioned.** `.ipb` V2 magic + `.sng` chunks. Any new serialized field requires a version/migration story in the same task.
- Run `meson install -C build` after building before launching anything from `bin/`.
- Do NOT push. Merge to local `main` is the user's call after review (rule #1078).

---

## Phase 1 — `Mod` union refactor (no behavior change)

Migrates the mod system from `LFO`/`Random`/`Envelope` structs that embed `Mod base` to a single union'd `Mod`. Everything keeps working identically; this phase only restructures + re-types.

### Task 1.1: Union'd `Mod` structs + re-typed creators in modsystem.h

**Files:**
- Modify: `src/modsystem.h`
- Test: `tests/dsp/test_modsystem.c` (new union-layout tests; existing ones compile from Task 1.2)

**Interfaces:**
- Consumes: existing `ModType`, `Parameter`, `EnvelopeStage`, `ModGenerate`, `ModPreset`.
- Produces (all later tasks + callers depend on these):
  - Payload structs `EnvState`, `LfoState`, `RndState`, `AttenState` (definitions below).
  - `typedef struct Mod { ModType type; Parameter *output; char name[MAX_NAME_LEN]; int dependency_count; bool processed; bool visiting; ModGenerate generate; union { EnvState env; LfoState lfo; RndState rnd; AttenState atten; } data; } Mod;`
  - Creators now return `Mod *`: `createEnvelope`, `createADSR`, `createAD`, `createParamPointerADSR`, `createParamPointerAD`, `createLFO`, `createRandom`; `createAttenuatorMod` already returns `Mod *`. The `LFO`/`Random`/`Envelope` typedefs are REMOVED.
  - Stage helpers take `Mod *env`: `addEnvelopeStage`, `addParamPointerEnvelopeStage`.
  - `initEnvelopeDefaults(Mod*)`, `initLfoDefaults(Mod*, ...)`, `initRandDefaults(Mod*, ...)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/test_modsystem.c` and register in `main`:

```c
void test_mod_union_payload(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Mod *env = createAD(pl, ml, 0.1f, 0.2f, "UENV");
    TEST_ASSERT_NOT_NULL(env);
    TEST_ASSERT_EQUAL(MT_ENV, env->type);
    TEST_ASSERT_EQUAL(2, env->data.env.stageCount);
    TEST_ASSERT_EQUAL(0.1f, env->data.env.stages[0].duration->baseValue);
    Mod *lfo = createLFO(pl, ml, 0, 0.4f, LS_SIN, "ULFO");
    TEST_ASSERT_EQUAL(MT_LFO, lfo->type);
    TEST_ASSERT_EQUAL(0.4f, lfo->data.lfo.rate->baseValue);
}
```

- [ ] **Step 2: Rewrite `src/modsystem.h`**

Replace modsystem.h:74-177 (`Mod`, `LFO`, `Random`, `Envelope`) with the payload + union structs. `AttenState` before `Mod` (uses `struct Mod *` tag only — legal). `EnvState`/`LfoState`/`RndState` reference `EnvelopeStage`/`Parameter` (defined above). Re-type the creator + stage-helper + init prototypes to `Mod *`/take `Mod *`. `ModPreset` is unchanged.

- [ ] **Step 3: Commit the header**

```bash
git add src/modsystem.h tests/dsp/test_modsystem.c
git commit -m "refactor(mod): union'd Mod structs + re-typed creators (header)"
```

### Task 1.2: modsystem.c — creators, generators, in-place changeModType

**Files:**
- Modify: `src/modsystem.c`

- [ ] **Step 1: Convert creators to build union'd Mods**

`LFO *createLFO` → `Mod *createLFO`: `calloc(1, sizeof(Mod))`, `initMod(mod, paramList, name, MT_LFO, generateLFO)`, init `mod->data.lfo` fields + params. Same for `createRandom` (MT_RND, `data.rnd`), `createEnvelope`/`createADSR`/`createAD`/`createParamPointerADSR`/`createParamPointerAD` (MT_ENV, `data.env` + stages via `addEnvelopeStage`/`addParamPointerEnvelopeStage`). `initEnvelopeDefaults`/`initLfoDefaults`/`initRandDefaults` init the payload. `addEnvelopeStage`/`addParamPointerEnvelopeStage` append into `env->data.env.stages[env->data.env.stageCount++]` + register params. `createAttenuatorMod` stores its fields in `mod->data.atten`.

- [ ] **Step 2: Convert generators + helpers**

`generateEnvelope(void *self)` reads/writes `((Mod *)self)->data.env` (currentTime/currentStageIndex/currentLevel/stages...). Same for `generateLFO` (`data.lfo`), `generateRandom` (`data.rnd`), the attenuator generator (`data.atten`), `updateMod` (modsystem.c:931), `cbLfoShapeOnChange`/`cbRandShapeOnChange`, `saveLfoPreset`/`initLfoFromPreset`/`saveRandPreset`/`initRandFromPreset`/`saveEnvPreset`/`initEnvelopeFromPreset` (live `Mod *` side only — `ModPreset` untouched).

- [ ] **Step 3: Rewrite `changeModType` as an in-place swap** (modsystem.c:650+)

1. Free the old payload's params from `paramList` (the current switch at modsystem.c:676-701, but reading `mod->data.env/lfo/rnd`).
2. Preserve `output` (Parameter*) + `name` on the same `Mod`.
3. `memset(&mod->data, 0, sizeof(mod->data))`; set `mod->type = newType`; run the new type's payload init (params into `paramList`, `generate`, LFO/RND default rate/shape/playMode).
4. Do NOT touch the `modList` slot or any `ModConnection` — the `Mod *` is stable, so routes survive.

- [ ] **Step 4: Compile the object**

Run: `ninja -C build src/spectrax.p/modsystem.c.o` → 0 errors. (Other files still fail to compile until Tasks 1.3-1.4; that is expected.)

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.c
git commit -m "refactor(mod): union'd Mod creators, generators, in-place changeModType"
```

### Task 1.3: Migrate non-test call sites (voice.c, voice.h, main.c, gui_inst_mod.c, vizfx.c, preset io)

**Files:**
- Modify: `src/voice.c`, `src/voice.h`, `src/main.c`, `src/gui_inst_mod.c`, `src/vizfx.c`, `src/io/preset_io.c`

**Interfaces:**
- Consumes: union'd `Mod` + `Mod *` creators (Tasks 1.1-1.2).
- Produces: `Instrument.envelopes[]` becomes `Mod *envelopes[MAX_ENVELOPES]`; `Voice.envelope[4]`/`Voice.lfo[2]` become `Mod *envelope[4]`/`Mod *lfo[2]`; attenuator fields read via `mod->data.atten.*`. No behavior change.

- [ ] **Step 1: Convert every cast + field access**

Use `git grep -n "Envelope \*\|LFO \*\|Random \*\|(Envelope \*)\|(LFO \*)\|(Random \*)\|->stages\[\|->data\."` under `src/` and convert mechanically:
- Creator-result locals `Envelope *`/`LFO *`/`Random *` → `Mod *` (voice.c `initialize_voice`'s `voice->envelope[i] = createParamPointerAD(...)`; gui_inst_mod.c `addRuntimeSource`'s `createAD`).
- `(Envelope *)mod`/`(LFO *)mod`/`(Random *)mod` casts → `mod->data.env/lfo/rnd` (gui_inst_mod.c:1648-1664 `cbCycleSourceType`-adjacent code, `gui_inst_mod.c` atten handling, modsystem internals).
- Envelope stage reads `e->stages[i].duration/curvature` where `e` is an env Mod → `e->data.env.stages[i].duration/curvature` (gui_inst_mod.c:1649-1651 AD dial builders).
- `l->rate/phase/shape/shapeValue` (LFO) / `r->rate/.../lastPhase/lastRandom` (RND) → `mod->data.lfo.*` / `mod->data.rnd.*`.
- Array element types `Envelope *envelopes[MAX_ENVELOPES]` (voice.h:181), `Envelope *envelope[4]` + `LFO *lfo[2]` (voice.h:269-270) → `Mod *`.
- Attenuator fields `mod->input`/`mod->attenAmount`/`mod->attenPolarity`/`mod->attenCurve` → `mod->data.atten.*`.

- [ ] **Step 2: Full build + full suite**

Run: `ninja -C build` then `meson test -C build`
Expected: 0 errors; 15/15 pass (the suite pins behavior — this is the migration's correctness check).

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor(mod): migrate voice/gui/vizfx/preset-io call sites to union'd Mod"
```

### Task 1.4: Migrate the test files

**Files:**
- Modify: `tests/dsp/test_modsystem.c`, `tests/dsp/test_mod_voice.c`, `tests/dsp/test_voice.c`, `tests/dsp/test_io.c` (wherever `Envelope *`/`LFO *`/`Random *` types or `->stages[...]` reads appear)

- [ ] **Step 1: Mechanically convert test locals + reads**

`Envelope *env = createAD(...)` → `Mod *env`; `env->stages[i].duration` → `env->data.env.stages[i].duration`; `lfo->rate` → `lfo->data.lfo.rate`. Assertions unchanged.

- [ ] **Step 2: Verify the full gate**

Run: `ninja -C build && meson test -C build` → 0 errors, 15/15.

- [ ] **Step 3: Run scripted fixtures**

Run `src/tools/instrument_harness/run_scripted.sh fixtures/<f>.txt` for chip_meta, add_route_delete, clear_routes, mod_sources, preset_save_load, save_overwrite → PASS each. (`chip_meta` needs a pristine `bin/s1.sng` — restore from HEAD if it fails.)

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "test(mod): migrate test suites to union'd Mod API"
```

**Phase 1 gate:** clean build, 15/15 tests, 6/6 fixtures, app boots (5 presets).

---

## Phase 2 — Voice decoupling: destinations, defaults, per-voice graph

The core architectural change. Sequencing note: the instrument-side destination params + default connections are created FIRST (Task 2.2) because the per-voice graph copy (Task 2.3) mirrors whatever connections the instrument holds.

### Task 2.1: Voice-owned FM operator params (kill `createParamPointerOperator` aliasing)

**Files:**
- Modify: `src/voice.c`, `src/voice.h`, `src/oscillator.c`, `src/oscillator.h`
- Test: `tests/dsp/test_voice.c`

**Interfaces:**
- Produces (oscillator.h): `Operator *createVoiceOperator(ParamList *paramList, const Operator *proto)` — creates an operator owning fresh params (via `createOperator`) then copies the proto's baseValues. `void syncOperatorFromInstrument(Operator *voiceOp, const Operator *instOp)` — copies feedback/ratio/level/outLevel (+ pitch once it exists) baseValues instrument → voice.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/test_voice.c`:

```c
void test_voice_operator_owns_params(void) {
    ParamList *pl = createParamList();
    Operator *proto = createOperator(pl, 2.0f);
    setParameterBaseValue(proto->feedbackAmount, 0.4f);
    setParameterBaseValue(proto->level, 0.7f);

    ParamList *vpl = createParamList();
    Operator *vop = createVoiceOperator(vpl, proto);
    TEST_ASSERT_NOT_NULL(vop);
    TEST_ASSERT_TRUE(vop->feedbackAmount != proto->feedbackAmount); /* owned, not aliased */
    TEST_ASSERT_EQUAL(0.4f, vop->feedbackAmount->baseValue);
    TEST_ASSERT_EQUAL(0.7f, vop->level->baseValue);

    setParameterBaseValue(proto->level, 0.3f);
    syncOperatorFromInstrument(vop, proto);
    TEST_ASSERT_EQUAL(0.3f, vop->level->baseValue);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_voice` → FAIL (`createVoiceOperator` undefined).

- [ ] **Step 3: Implement** in oscillator.c/h per the interfaces.

- [ ] **Step 4: Migrate the FM voice path**

In `initialize_voice` (voice.c:293-296), replace `createParamPointerOperator(voice->paramList, inst-op->feedbackAmount, inst-op->ratio, inst-op->level)` with `createVoiceOperator(voice->paramList, inst->id.fm.ops[i])`. The voice's operators now own their params (incl. outLevel).

- [ ] **Step 5: Verify + commit**

`ninja -C build && meson test -C build` → 15/15. Commit: `git add -A && git commit -m "feat(voice): voice-owned FM operator params (no aliasing)"`.

### Task 2.2: Instrument dest params (gain/pitch/outLevel/pitch) + op pitch + seeded default connections

**Files:**
- Modify: `src/voice.c`, `src/voice.h`, `src/oscillator.h`, `src/oscillator.c`, `src/io/preset_io.c`
- Test: `tests/dsp/test_voice.c`, `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Produces:
  - `Instrument.gain` (`Parameter *`, base 1.0, range 0..2, MUL semantics) — voice gain dest.
  - `Instrument.pitch` (`Parameter *`, BLEP pitch dest; base 1.0, ADD semantics).
  - `Operator.pitch` (`Parameter *`) per-op continuous Hz offset, range ±1000, created in `createOperator` (fine 0.1/coarse 1.0).
  - `float opFrequencyAt(const Operator *op, float fundamental)` = `fundamental * getParameterValue(op->ratio) + getParameterValue(op->pitch)`.
  - Seeded default connections (in `init_instrument` after sources/params exist, under the audio lock): all types `modList->mods[0] → gain` (MUL 1.0); FM additionally `modList->mods[0] → ops[i]->outLevel` (MUL 1.0, i=0..3); BLEP `modList->mods[1] → pitch` (ADD 400.5). No seed for per-op pitch.
- `outLevel` + `pitch` already exist in `inst->paramList` per-op; they become routable in Phase 3/5.

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/test_voice.c`:

```c
void test_default_gain_connections_seeded(void) {
    VoiceManager *vm = createVoiceManager(NULL, NULL, NULL, NULL);
    Instrument *inst = vm->instruments[0]; /* boot creates a default instrument per channel */
    TEST_ASSERT_NOT_NULL(inst->gain);
    TEST_ASSERT_EQUAL(1, inst->gain->modulator_count);
    TEST_ASSERT_EQUAL(inst->modList->mods[0], inst->gain->modulators->source);
    if(inst->voiceType == VOICE_TYPE_FM) {
        for(int i = 0; i < MAX_FM_OPERATORS; i++) {
            TEST_ASSERT_EQUAL(1, inst->id.fm.ops[i]->outLevel->modulator_count);
        }
        TEST_ASSERT_NOT_NULL(inst->id.fm.ops[0]->pitch);
        TEST_ASSERT_EQUAL(0.0f, inst->id.fm.ops[0]->pitch->baseValue);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

`ninja -C build && meson test -C build test_voice` → FAIL (`inst->gain` absent).

- [ ] **Step 3: Implement**

Add the dest params (`gain` to Instrument; `pitch` to BLEP as a voice-level param; per-op `pitch` to Operator + `createVoiceOperator` copy + `sine_op` uses `opFrequencyAt`). Seed the default connections in `init_instrument` after the envelope sources exist. `makeDefaultFmPreset`/`fillEmptyBankSlots` + `applyInstrumentPreset` must re-seed the same connections on load so the voice graph and the route overlay agree. Add `pitch` to `OperatorData`/`FmPatch` preset round-trip (default 0 when absent — V2-peek migration).

- [ ] **Step 4: Gate + commit**

15/15 + fixtures PASS. Commit: `git add -A && git commit -m "feat(inst): gain/pitch/outLevel dests, per-op pitch, seeded default connections"`.

### Task 2.3: Per-voice full graph copy + base sync

**Files:**
- Modify: `src/voice.c`, `src/voice.h`, `src/modsystem.h`, `src/modsystem.c`, `src/main.c`
- Test: `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Consumes: union'd `Mod`, voice-owned operator params (2.1), instrument dests + seeded connections (2.2).
- Produces (modsystem.c/h — implemented here so tests link them):
  - `Mod *cloneMod(ParamList *voicePl, ModList *voiceMl, const Mod *src)` — deep-copy a source into the voice's lists: same type, own payload params (values/ranges copied from `src`), own `output`, same `name`.
  - `void syncModValues(Mod *clone, const Mod *src)` — copy each payload param baseValue from `src` into the clone's own params (env stage durations/curvatures, LFO/RND rate/phase/shape).
  - `void syncVoiceGraph(Voice *v)` (voice.c) — per clone/inst-source pair call `syncModValues`, then copy the voice's destination params (volume/frequency + per-op feedback/ratio/level/outLevel/pitch) from their instrument counterparts' baseValues.
- Voice.h struct additions:
  ```c
  struct Voice {
      ...
      int cloneCount;                       /* voices mirror the instrument's source mods */
      Mod *clones[MAX_MODS];
      Mod **instSources[MAX_MODS];          /* pointer to the instrument's Mod* slot each clone mirrors */
  };
  ```

- [ ] **Step 1: Write the failing test**

Append to `tests/dsp/test_mod_voice.c`:

```c
void test_voice_graph_copy_syncs_and_isolates(void) {
    ParamList *ipl = createParamList();
    ModList *iml = createModList();
    Mod *env = createAD(ipl, iml, 0.25f, 4.25f, "AD");
    Parameter *gain = createParameter(ipl, "gain", 1.0f, 0.0f, 2.0f);
    addModulation(ipl, iml, env, gain, 1.0f, MO_MUL);

    ParamList *vpl = createParamList();
    ModList *vml = createModList();
    Mod *clone = cloneMod(vpl, vml, env);
    TEST_ASSERT_NOT_NULL(clone);
    TEST_ASSERT_EQUAL(MT_ENV, clone->type);
    TEST_ASSERT_TRUE(clone->data.env.stages[0].duration != env->data.env.stages[0].duration);
    TEST_ASSERT_EQUAL(0.25f, clone->data.env.stages[0].duration->baseValue);

    setParameterBaseValue(env->data.env.stages[0].duration, 1.0f);
    syncModValues(clone, env);
    TEST_ASSERT_EQUAL(1.0f, clone->data.env.stages[0].duration->baseValue);
}
```

- [ ] **Step 2: Run to verify it fails**

`ninja -C build && meson test -C build test_mod_voice` → FAIL (`cloneMod` undefined).

- [ ] **Step 3: Implement the clone + sync primitives** per the interfaces (dispatch on `src->type`; MT_ATTEN mirrors its input + atten params).

- [ ] **Step 4: Build the per-voice graph copy in `initialize_voice`**

Replace the aliasing loop (voice.c:263-272) + the hard-wired per-type `addModulation` calls (voice.c:279-302): after the voice's own params exist, clone each `inst->modList` source into `voice->clones[]` (record `instSources[i] = &inst->modList->mods[i]`), then replicate every instrument connection onto the voice's mirror destination params. Mirror lookup: destination params whose name matches an existing voice param (`volume`, `frequency`, per-op `feedback/ratio/level/outLevel/pitch` — per-op mirrors are keyed by op INDEX, not name); the connection's source maps through the clone table. The per-voice `processModulations` (main.c:345) then runs this graph. Remove the old `voice->envelope[]` wiring (keep the arrays until Task 2.4 for a clean compile boundary, but stop using them for audio).

- [ ] **Step 5: Per-buffer base sync**

Call `syncVoiceGraph(v)` from the audio callback (main.c, before each active voice's per-sample `processModulations`) once per buffer. Also retrigger envelope payloads on note-on here (see Task 2.4 trigger).

- [ ] **Step 6: Gate + commit**

15/15 + fixtures (instrument sound sanity via a voice-level check or fixture). Commit: `git add -A && git commit -m "feat(voice): full per-voice mod graph copy with base-value sync"`.

### Task 2.4: Drop aliasing + trigger semantics + rebuild policy + audio-path cleanup

**Files:**
- Modify: `src/voice.c`, `src/voice.h`, `src/modsystem.c`, `src/modsystem.h`, `src/main.c`, `src/gui_inst_mod.c`

- [ ] **Step 1: Delete the aliasing path**

Remove `createParamPointerAD`/`createParamPointerADSR` (modsystem.c/h) + `addParamPointerEnvelopeStage` if unused elsewhere. Remove `Envelope *envelope[4]`, `LFO *lfo[2]`, `envCount`, `lfoCount` from `struct Voice` (voice.h) + all references (the graph copy replaced them).

- [ ] **Step 2: Trigger semantics**

Add `void triggerVoiceMods(Voice *v)` (voice.c) — called per note-on: every clone with `type == MT_ENV` gets `isTriggered = true; currentStageIndex = 0;` in its payload; MT_LFO/MT_RND clones follow their play mode (Part 4 adds the param — until then, no-op / free-run). Call it wherever notes are allocated (`getFreeVoice`/the voice trigger site in main.c).

- [ ] **Step 3: Rebuild-on-structural-edit policy**

`rebuildVoicesForInstrument(vm, inst)` (voice.h:320) re-clones every voice's graph (its doc comment updated). Ensure each STRUCTURAL mutation calls it under `g_audioLock` + `inst->rebuilding`: route/unroute (gui_inst_mod.c cbRouteToDest/cbClearAll paths), source retype (cbCycleSourceType), source add/delete (addRuntimeSource/removeSource), `applyInstrumentPreset` + `cb_setInstrumentPreset`, `setInstrumentVoiceType`, `setChannelVoiceCount`. Value edits (dial callbacks) must NOT rebuild — the per-buffer sync covers them.

- [ ] **Step 4: Drop the instrument-level processModulations from the audio callback**

In main.c, remove the `processModulations(chInst->paramList, chInst->modList, ...)` call (main.c:325) — audio now comes solely from the per-voice graphs. Before removing, grep for any UI read of instrument `currentValue` (dials use baseValue; strip reads the voice modList) and confirm none break. If a UI readout does depend on it, keep the call but note why in the commit.

- [ ] **Step 5: Regression test**

In `tests/dsp/test_mod_voice.c`:

```c
void test_voice_graph_survives_source_retype(void) {
    /* env0 with a gain connection on an instrument; build a voice graph copy via
     * the task 2.3 helpers; changeModType(env0 -> MT_LFO); rebuild the voice copy;
     * assert the new clone is MT_LFO with its own rate param and no dangling reads. */
}
```

- [ ] **Step 6: Full gate + commit**

`ninja -C build && meson test -C build` (15/15) + fixtures PASS. Commit: `git add -A && git commit -m "feat(voice): drop aliasing, add trigger/rebuild policy, per-voice audio"`.

**Phase 2 gate:** clean build, 15/15, fixtures PASS. Instrument still sounds (no silence/click regression on NEXT/reroute/type-cycle). ASan/PERTURB run clean (meson test env already perturbs).

---

## Phase 3 — Routable surface + presets

### Task 3.1: Promote gain/outLevel/pitch into the picker's dest collection

**Files:**
- Modify: `src/gui_inst_mod.c` (`cbOpenRouteLayer` dest walk, `findDialRectForParam`), `src/gui_instrument.c`
- Test/fixture: a routing fixture asserting the picker lists gain/outLevel/pitch dests.

- [ ] **Step 1: Failing fixture**

Extend `mod_sources.txt`: open the picker from source[0], JUMPTL `gain`, assert selection lands on the gain dest (SHOWTL prints `gain`), EDIT routes, close.

- [ ] **Step 2: Implement**

Ensure the dest collection walks all of `inst->paramList` (it does) so `gain`, per-op `outLevel` + `pitch` are collected. `findDialRectForParam` returns a fallback rect (centred in the op row area) for dests that have no dial yet (gain/pitch until Task 5.2 adds dials) so the dest buttons pin + nav works. Dest count grows to ~40 on FM — confirm geometric nav still resolves (grid-based fallback already exists).

- [ ] **Step 3: Gate + commit**

15/15 + fixture PASS. Commit: `git add -A && git commit -m "feat(gui): gain/outLevel/pitch join picker dest collection"`.

### Task 3.2: Preset round-trip for the new fields

**Files:**
- Modify: `src/io/preset_io.c`, `src/voice.c`, `src/voice.h`
- Test: `tests/dsp/test_io.c`

- [ ] **Step 1: Failing test**

```c
void test_preset_pitch_and_gain_roundtrip(void) {
    Preset p = makeDefaultFmPreset();
    p.pd.fm.ops[0].pitch = 55.0f;
    Instrument *inst = makeFmInstrumentFromPreset(p);
    TEST_ASSERT_EQUAL(55.0f, inst->id.fm.ops[0]->pitch->baseValue);
    /* gain is a derived seed (re-seeded on load); assert outLevel seeds exist */
    TEST_ASSERT_EQUAL(1, inst->gain->modulator_count);
}
```

- [ ] **Step 2: Implement**

`OperatorData`/`FmPatch` gains `pitch`; preset save/load round-trips it (V2-peek default 0). `applyInstrumentPreset` re-seeds the default gain/outLevel/pitch connections (they are derived from the source list, not stored) + `gain`/`pitch` dest params are created on rebuild. `presetFromInstrument` captures the new fields.

- [ ] **Step 3: Gate + commit**

15/15 + fixtures. Commit: `git add -A && git commit -m "feat(preset): round-trip op pitch; re-seed default connections on load"`.

**Phase 3 gate:** clean build, 15/15, fixtures. A saved FM preset reloads with per-op pitch intact + gain/outLevel routes present.

---

## Phase 4 — LFO / RND play mode

### Task 4.1: `playMode` param + FREERUN/RETRIGGER behavior

**Files:**
- Modify: `src/modsystem.h`, `src/modsystem.c`, `src/voice.c` (trigger), `src/io/preset_io.c`
- Test: `tests/dsp/test_modsystem.c`

- [ ] **Step 1: Failing test**

```c
void test_lfo_playmode_retrigger(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Mod *lfo = createLFO(pl, ml, 0, 2.0f, LS_SIN, "PLFO");
    setParameterBaseValue(lfo->data.lfo.playMode, 1.0f); /* RETRIGGER */
    TEST_ASSERT_EQUAL(1.0f, getParameterValue(lfo->data.lfo.playMode));
    setParameterValue(lfo->data.lfo.phase, 0.4f);
    resetModPhaseForTrigger(lfo);
    TEST_ASSERT_EQUAL(0.0f, getParameterValue(lfo->data.lfo.phase));
}
```

- [ ] **Step 2: Implement**

Add `Parameter *playMode` to `LfoState`/`RndState` (created in `initLfoDefaults`/`initRandDefaults`, discrete 0=FREERUN/1=RETRIGGER, default 0). Add `void resetModPhaseForTrigger(Mod *m)`: MT_ENV resets stage; MT_LFO/MT_RND with RETRIGGER zero `phase` (+ RND `lastPhase`); FREERUN no-op. Wire into `triggerVoiceMods` (Task 2.4). Round-trip in `LfoPresetData`/`RandPresetData` (+ save/init helpers).

- [ ] **Step 3: Gate + commit**

15/15 + fixtures. Commit: `git add -A && git commit -m "feat(mod): LFO/RND play mode (free-run vs retrigger)"`.

**Phase 4 gate:** clean build, 15/15, fixtures.

---

## Phase 5 — UI

### Task 5.1: Type-cycle + DEL on core rows; play-mode control on LFO/RND rows

**Files:**
- Modify: `src/gui_inst_mod.c` (`appendModSourceEntry`, `cbCycleSourceType`, `removeSource`), `src/gui_inst_internal.h`

- [ ] **Step 1: Failing test (fixture)**

Extend `mod_sources.txt`: select source[0]'s TYPE control (now present on core rows) → cycle ENV→LFO→RND→ENV (must succeed on a core row + dials become the type's); LFO-typed rows show a play-mode control; delete source[0] via its DEL button succeeds + gain goes flat (modulator_count 0).

- [ ] **Step 2: Implement**

Remove the `idx < inst->coreEnvelopeCount` gates in `appendModSourceEntry` (TYPE + DEL controls now render on every row). `cbCycleSourceType` calls `changeModType` (already in-place + safe — per-voice rebuild happens via the structural-edit policy). `removeSource` (gui_inst_mod.c:271) drops the `srcIndex < coreEnvelopeCount` rejection; deleting any source removes its connections (`removeMod`) — gain/outLevel fall flat. Add the play-mode dial to LFO/RND rows bound to `mod->data.lfo.playMode`/`mod->data.rnd.playMode`.

- [ ] **Step 3: Gate + commit**

15/15 + extended fixture PASS. Commit: `git add -A && git commit -m "feat(gui): type-cycle/delete on core rows; LFO play-mode control"`.

### Task 5.2: FM layout — 5 dials per op + GAIN; picker density + route lines

**Files:**
- Modify: `src/gui_inst_fm.c`, `src/gui_inst_sample.c`, `src/gui_inst_blep.c`, `src/gui_inst_mod.c` (route-lines node + picker)
- Fixture: FM-layout geometry + a route-line fixture

- [ ] **Step 1: Failing fixture**

Extend `task4_size_verify.txt`: assert the FM section renders RATIO/FEEDBACK/LEVEL/OUTLV/PITCH per op + a GAIN dial, with the dial rows at the AD-env height standard and no 65534-wrap (reuse the geometry-dump approach). A routing fixture asserts the ROUTE overlay draws a line from source[0] to the gain dest by default.

- [ ] **Step 2: Implement**

`appendFMInstControlNode` (gui_inst_fm.c): add per-op `outLevel` + `pitch` dials + a `gain` dial (all types; BLEP gets `pitch` + `gain`). Reflow the FM rows to fit 5 dials per op without overlap (tighter spacing or a second row per op group; row heights stay at the 35px standard). Ensure `drawRouteLinesNode` iterates the seeded connections (it already walks `paramList` modulators) + dest buttons pin to the new dial rects. Add gain/pitch dials to the sample/blep control builders too.

- [ ] **Step 3: Gate + commit**

15/15 + fixtures (visual + routing). Commit: `git add -A && git commit -m "feat(gui): FM 5-dial op rows + GAIN; default route lines visible"`.

**Phase 5 gate:** clean build, 15/15, all fixtures, visual capture shows FM rows + gain route line + play-mode control.

---

## Phase 6 — Full gate + hardening

### Task 6.1: Whole-branch review + end-to-end gate

**Files:** any fixes surfaced by review.

- [ ] **Step 1: Full gate**

`ninja -C build` (0 errors), `meson test -C build` (15/15), every scripted fixture PASS, app boots (5 presets), `--probe-route` sanity (linepx/warmpx nonzero), a type-cycle + route + reroute smoke under Xvfb is clean.

- [ ] **Step 2: Audit**

Grep src/ + tests/ for `createParamPointer`, `voice->envelope[`, `(Envelope *)`/`(LFO *)`/`(Random *)` casts, `->stages[` outside `->data.env.stages[` — must be zero. Confirm union double-free safety: payload params owned by paramList, `output` by paramList, one free per object (extend `freeVoice`/`clearModList` clean-free tests if needed).

- [ ] **Step 3: Summary + commit**

Write a short behavior-change summary (default connections now visible; deleting a source removes its routes + flat gain; per-voice modulators; play mode default free-run) into the commit. Final: `git add -A && git commit -m "chore(voice): phase 6 hardening + migration audit clean"`.

---

## Self-review notes

- **Per-op param mirroring:** `feedback`/`ratio`/`level`/`outLevel`/`pitch` names repeat per operator, so voice-mirror lookups must be op-index-keyed, not name-keyed. The replication walk in Task 2.3 explicitly pairs `inst->id.fm.ops[i]` params with `voice->vd.fm.operators[i]` params.
- `rebuildVoicesForInstrument` (voice.h:320) changes contract from re-alias to re-clone; update its doc comment in Task 2.4.
- The seeded default connections must be produced identically by `init_instrument`, `makeDefaultFmPreset`/`fillEmptyBankSlots`, and `applyInstrumentPreset`, or presets/voice copies diverge from the route overlay.
- Phase 1 keeps behavior identical; the aliasing removal in Phase 2 is where sound/gain behavior can change — the fixtures + a listening check gate it.
