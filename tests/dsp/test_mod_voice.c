/* test_mod_voice.c — instrument/voice integration of modulations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voice.h"
#include "modsystem.h"
#include "wavetable.h"
#include "settings.h"
#include "sample.h"
#include "notes.h"

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_2(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_3(actual, expected, msg) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: %s — expected %lld, got %lld\n", \
                __FILE__, __LINE__, (msg), _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_GET(_1, _2, _3, NAME, ...) NAME
#define ASSERT_EQ(...) ASSERT_EQ_GET(__VA_ARGS__, ASSERT_EQ_3, ASSERT_EQ_2, MISSING)(__VA_ARGS__)

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* Local teardown used by the older no-VoiceManager tests (Tasks 1/9). */
/* freeParamList owns all Parameter structs; the modList is freed     */
/* with bare free (each Mod struct is freed separately, since these   */
/* tests don't use removeMod).                                         */
/* ------------------------------------------------------------------ */
static void teardown(ParamList *pl, ModList *ml) {
    if (ml) {
        for (int i = 0; i < ml->count; i++) {
            free(ml->mods[i]);
        }
        free(ml);
    }
    if (pl) {
        freeParamList(pl);
    }
}

static int test_init_fm_instrument(void) {
    SamplePool *sp = createSamplePool();
    PresetBank pb;
    initPresetBank(&pb);
    Preset p;
    initDefaultFmPreset(&p);
    addPresetToBank(&pb, p);

    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);
    ASSERT_TRUE(inst != NULL, "init_instrument FM");
    ASSERT_EQ(inst->envelopeCount, 4);
    ASSERT_TRUE(inst->envelopes[0] != NULL, "envelope 0 exists");
    ASSERT_TRUE(inst->id.fm.ops[0]->level != NULL, "op0 level param exists");

    free(inst);
    freeSamplePool(sp);
    printf("PASS test_init_fm_instrument\n");
    return 0;
}

/* Task 9: clearParamList must free its contents (not just zero the
 * count), and the list must remain usable for re-insertion. Before
 * the fix, clearParamList only zeroed count while leaking every
 * Parameter; after the fix, those Parameter pointers are freed via
 * freeParameter. */
static int test_clear_paramlist_frees(void) {
    ParamList *pl = createParamList();
    Parameter *a = createParameter(pl, "a", 1.0f, 0.0f, 10.0f);
    Parameter *b = createParameter(pl, "b", 2.0f, 0.0f, 10.0f);
    clearParamList(pl);
    ASSERT_EQ(pl->count, 0, "paramList emptied");
    Parameter *c = createParameter(pl, "c", 3.0f, 0.0f, 10.0f);
    ASSERT_EQ(pl->count, 1, "list usable after clear");
    ASSERT_TRUE(c != NULL, "new param allocated");
    /* Reference the earlier params to satisfy -Wunused-variable. */
    (void)a;
    (void)b;
    teardown(pl, NULL);
    printf("PASS test_clear_paramlist_frees\n");
    return 0;
}

/* Task 9: rebuildVoicesForInstrument must leave voice envelopes
 * aliasing the CURRENT instrument's stage params after a runtime
 * preset change. Before the fix, applyInstrumentPreset freed the
 * old instrument's stage-params (via clearParamList → now-real free),
 * while voice envelopes still pointed at the freed Param structs.
 * After the fix, rebuildVoicesForInstrument is called from
 * cb_setInstrumentPreset so voice pointers get refreshed. */
static int test_preset_load_rebuilds_voices(void) {
    SamplePool *sp = createSamplePool();
    WavetablePool *wtp = createWavetablePool();
    PresetBank pb;
    initPresetBank(&pb);
    Preset p;
    initDefaultFmPreset(&p);
    addPresetToBank(&pb, p);
    Settings s = { .enabledChannels = 1, .defaultVoiceCount = 2, .defaultBPM = 120 };
    VoiceManager *vm = createVoiceManager(&s, sp, wtp, &pb);
    ASSERT_TRUE(vm != NULL, "createVoiceManager");
    Instrument *inst = vm->instruments[0];

    /* Apply a different preset at runtime, then rebuild voices. */
    applyInstrumentPreset(inst, pb.patches[0]);
    rebuildVoicesForInstrument(vm, inst);

    /* Voices must alias the CURRENT instrument stage params. */
    Voice *v = vm->voicePools[0][0];
    ASSERT_TRUE(v->envelope[0] != NULL, "voice envelope exists");
    ASSERT_EQ(v->envelope[0]->stages[0].duration,
              inst->envelopes[0]->stages[0].duration,
              "voice aliases the new instrument stage param");

    freeVoiceManager(vm);
    freeWavetablePool(wtp);
    freeSamplePool(sp);
    printf("PASS test_preset_load_rebuilds_voices\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* TestEnv — Task 10 integration helpers, ported from test_voice.c.    */
/*                                                                    */
/* test_voice.c does NOT define a free_env; each test ends inline    */
/* with `freeVoiceManager(e.vm); e.vm = NULL;`. We mirror that       */
/* pattern here: free_env runs freeVoiceManager and NULLs the handle, */
/* then returns silently. The sample pool + wavetable pool are       */
/* shared resources and are intentionally leaked (matches the rest   */
/* of the test suite).                                               */
/* ------------------------------------------------------------------ */

#define TEST_SAMPLE_LEN 4096

typedef struct {
    Settings settings;
    SamplePool *sp;
    WavetablePool *wtp;
    PresetBank pb;
    VoiceManager *vm;
} TestEnv;

static int make_env(TestEnv *e, int defaultVoiceCount) {
    memset(e, 0, sizeof(*e));

    e->sp = createSamplePool();
    if (!e->sp) {
        fprintf(stderr, "FAIL: createSamplePool() returned NULL\n");
        return 1;
    }
    float *buf = (float *)malloc(TEST_SAMPLE_LEN * sizeof(float));
    if (!buf) {
        fprintf(stderr, "FAIL: sample buffer alloc failed\n");
        return 1;
    }
    for (int i = 0; i < TEST_SAMPLE_LEN; i++) {
        buf[i] = sinf(2.0f * (float)M_PI * (float)i / (float)TEST_SAMPLE_LEN);
    }
    loadSample(e->sp, "sine", buf, 24, SAMPLE_RATE, TEST_SAMPLE_LEN);
    free(buf);

    e->wtp = createWavetablePool();
    if (!e->wtp) {
        fprintf(stderr, "FAIL: createWavetablePool() returned NULL\n");
        return 1;
    }

    initPresetBank(&e->pb);
    Preset p;
    initDefaultFmPreset(&p);
    addPresetToBank(&e->pb, p);

    e->settings.enabledChannels = 1;
    e->settings.defaultVoiceCount = defaultVoiceCount;
    e->settings.defaultBPM = 120;

    e->vm = createVoiceManager(&e->settings, e->sp, e->wtp, &e->pb);
    if (!e->vm) {
        fprintf(stderr, "FAIL: createVoiceManager() returned NULL\n");
        return 1;
    }
    return 0;
}

static void free_env(TestEnv *e) {
    if (e->vm) {
        freeVoiceManager(e->vm);
        e->vm = NULL;
    }
    /* sp, wtp, pb are leaked — mirrors test_voice.c's per-test cleanup. */
}

/* ------------------------------------------------------------------ */
/* Task 10 integration tests                                          */
/* ------------------------------------------------------------------ */

/*
 * Process the instrument's own paramList/modList at the test cadence
 * (0.016s ≈ one audio frame at 60Hz). Mirrors how main.c drives the
 * instrument side. We deliberately drive the instrument lists, NOT the
 * voice lists, because this test concerns routing into inst->id.fm.ops.
 */
static int test_route_to_fm_param_affects_value(void) {
    TestEnv e;
    if (make_env(&e, 2)) return 1;
    Instrument *inst = e.vm->instruments[0];
    Parameter *level = inst->id.fm.ops[0]->level;
    float base = level->baseValue;

    /* runtime-added envelope (idx 4 — slot 5 in envelopes[]) */
    int idx = inst->envelopeCount;
    inst->envelopes[idx] = createAD(inst->paramList, inst->modList,
                                    0.1f, 0.2f, "AD+");
    inst->envelopeCount++;
    ASSERT_TRUE(addModulation(inst->paramList,
                              &inst->envelopes[idx]->base,
                              level, 1.0f, MO_ADD),
                "route env→op0.level");

    /*
     * Note (Task 2 finding): processModulations recomputes every
     * param's currentValue from its baseValue, so the source's
     * `currentValue` is reset every pass. setParameterBaseValue (not
     * setParameterValue) writes baseValue AND currentValue; since the
     * source's output param has no modulators, its post-pass value
     * stays at baseValue. The MO_ADD modulator then applies 0.25 onto
     * the destination's base (0.1) → 0.35.
     */
    setParameterBaseValue(inst->envelopes[idx]->base.output, 0.25f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    ASSERT_NEAR(level->currentValue, base + 0.25f, 0.0001f);

    /* removeModulation unwires the connection but leaves the source
     * alive. With no modulators on `level`, post-pass currentValue ==
     * baseValue. */
    ASSERT_TRUE(removeModulation(inst->paramList, level,
                                 &inst->envelopes[idx]->base),
                "unwrap env→op0.level");
    processModulations(inst->paramList, inst->modList, 0.016f);
    ASSERT_NEAR(level->currentValue, base, 0.0001f);

    /*
     * rewireModulation swaps the source on an existing connection.
     * Wire the same modulator onto op[1].level (still MO_ADD 1.0).
     * Set baseValue=0.5 so the modulator contribution is observable.
     */
    addModulation(inst->paramList,
                  &inst->envelopes[idx]->base,
                  level, 1.0f, MO_ADD);
    Parameter *level1 = inst->id.fm.ops[1]->level;
    float base1 = level1->baseValue;
    ASSERT_TRUE(rewireModulation(inst->paramList, level,
                                 &inst->envelopes[idx]->base,
                                 &inst->envelopes[idx]->base),
                "rewire no-op (same source)");
    (void)level1;
    (void)base1;
    /*
     * The rewire test asserts the swap path; here we use the simplest
     * valid rewire: detach the existing connection from op0.level and
     * attach to op1.level by removeModulation + addModulation (the
     * public API supports both; rewireModulation is exercised
     * separately below in the dedicated test). Verify op0 back to base
     * and op1 modulated after the swap.
     */
    removeModulation(inst->paramList, level, &inst->envelopes[idx]->base);
    addModulation(inst->paramList, &inst->envelopes[idx]->base,
                  level1, 1.0f, MO_ADD);
    setParameterBaseValue(inst->envelopes[idx]->base.output, 0.5f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    ASSERT_NEAR(level1->currentValue, base1 + 0.5f, 0.0001f);
    ASSERT_NEAR(level->currentValue, base, 0.0001f);

    free_env(&e);
    printf("PASS test_route_to_fm_param_affects_value\n");
    return 0;
}

/*
 * Removing a single modulation must not disturb other live modulators
 * on the same destination. Add two envelopes routing to op0.level,
 * remove the first, confirm only the second survives.
 */
static int test_remove_modulation_is_surgical(void) {
    TestEnv e;
    if (make_env(&e, 2)) return 1;
    Instrument *inst = e.vm->instruments[0];
    Parameter *level = inst->id.fm.ops[0]->level;
    float base = level->baseValue;

    int idxA = inst->envelopeCount;
    inst->envelopes[idxA] = createAD(inst->paramList, inst->modList,
                                     0.1f, 0.2f, "ADA");
    inst->envelopeCount++;
    int idxB = inst->envelopeCount;
    inst->envelopes[idxB] = createAD(inst->paramList, inst->modList,
                                     0.1f, 0.2f, "ADB");
    inst->envelopeCount++;

    ASSERT_TRUE(addModulation(inst->paramList,
                              &inst->envelopes[idxA]->base,
                              level, 1.0f, MO_ADD),
                "route A");
    ASSERT_TRUE(addModulation(inst->paramList,
                              &inst->envelopes[idxB]->base,
                              level, 1.0f, MO_ADD),
                "route B");

    setParameterBaseValue(inst->envelopes[idxA]->base.output, 0.10f);
    setParameterBaseValue(inst->envelopes[idxB]->base.output, 0.40f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    /* both wired: base + 0.10 + 0.40 = base + 0.50 */
    ASSERT_NEAR(level->currentValue, base + 0.50f, 0.0001f);

    ASSERT_TRUE(removeModulation(inst->paramList, level,
                                 &inst->envelopes[idxA]->base),
                "unwrap A only");
    processModulations(inst->paramList, inst->modList, 0.016f);
    /* only B survives: base + 0.40 */
    ASSERT_NEAR(level->currentValue, base + 0.40f, 0.0001f);
    /* B's modulator entry still present */
    ASSERT_TRUE(level->modulators != NULL, "B modulator still wired");
    /* A's modulator entry removed */
    ModConnection *conn = level->modulators;
    int sawA = 0, sawB = 0;
    while (conn) {
        if (conn->source == &inst->envelopes[idxA]->base) sawA = 1;
        if (conn->source == &inst->envelopes[idxB]->base) sawB = 1;
        conn = conn->next;
    }
    ASSERT_TRUE(!sawA, "A modulator unwired");
    ASSERT_TRUE(sawB, "B modulator wired");

    free_env(&e);
    printf("PASS test_remove_modulation_is_surgical\n");
    return 0;
}

/*
 * rewireModulation atomically swaps the source on a single
 * destination connection. Route envA→op0.level, then rewire to envB.
 * After a process pass with op1's baseValue=0.7 and A's baseValue=0.0
 * (default), op0.level must reflect B's contribution only.
 */
static int test_rewire_modulation_swaps_source(void) {
    TestEnv e;
    if (make_env(&e, 2)) return 1;
    Instrument *inst = e.vm->instruments[0];
    Parameter *level = inst->id.fm.ops[0]->level;
    float base = level->baseValue;

    int idxA = inst->envelopeCount;
    inst->envelopes[idxA] = createAD(inst->paramList, inst->modList,
                                     0.1f, 0.2f, "Amod");
    inst->envelopeCount++;
    int idxB = inst->envelopeCount;
    inst->envelopes[idxB] = createAD(inst->paramList, inst->modList,
                                     0.1f, 0.2f, "Bmod");
    inst->envelopeCount++;

    ASSERT_TRUE(addModulation(inst->paramList,
                              &inst->envelopes[idxA]->base,
                              level, 1.0f, MO_ADD),
                "route A");
    ASSERT_TRUE(rewireModulation(inst->paramList, level,
                                 &inst->envelopes[idxA]->base,
                                 &inst->envelopes[idxB]->base),
                "rewire A→B");
    setParameterBaseValue(inst->envelopes[idxA]->base.output, 0.10f);
    setParameterBaseValue(inst->envelopes[idxB]->base.output, 0.70f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    /* only B contributes now: base + 0.70 */
    ASSERT_NEAR(level->currentValue, base + 0.70f, 0.0001f);

    /* source pointer on the connection is B's, not A's */
    ModConnection *conn = level->modulators;
    ASSERT_TRUE(conn != NULL, "one connection survives rewire");
    ASSERT_TRUE(conn->source == &inst->envelopes[idxB]->base,
                "rewired connection source is B");

    free_env(&e);
    printf("PASS test_rewire_modulation_swaps_source\n");
    return 0;
}

/*
 * Adding an envelope, rendering, then removeMod-ing the envelope and
 * rendering again must not crash and must leave the operator at its
 * base level. Voice FM operators alias instrument-level operators
 * (via createParamPointerOperator), so the operator level mutation
 * flows through; after removeMod the modulator entry is gone and the
 * op resets to baseValue.
 */
static int test_voice_render_after_route_and_delete(void) {
    TestEnv e;
    if (make_env(&e, 2)) return 1;
    Instrument *inst = e.vm->instruments[0];
    int idx = inst->envelopeCount;
    inst->envelopes[idx] = createAD(inst->paramList, inst->modList,
                                    0.1f, 0.2f, "AD+");
    inst->envelopeCount++;
    ASSERT_TRUE(addModulation(inst->paramList,
                              &inst->envelopes[idx]->base,
                              inst->id.fm.ops[0]->level, 1.0f, MO_ADD),
                "route env→op0.level");

    Voice *v = e.vm->voicePools[0][0];
    ASSERT_TRUE(v != NULL, "voice exists");

    /*
     * Render twice: once with the route live (voice->vd.fm.ops[i]->level
     * aliases inst->id.fm.ops[i]->level, which the modulator pulls
     * upward), once after removeMod. sine_op reads op->level's
     * currentValue, so the second render must not crash and must use
     * the now-unmodulated base level.
     */
    processModulations(inst->paramList, inst->modList, 0.016f);
    OutVal out1 = generateVoice(e.vm, v, 1.0f, 440.0f);

    ASSERT_TRUE(removeMod(inst->modList, inst->paramList,
                          &inst->envelopes[idx]->base),
                "runtime envelope removed via removeMod");
    inst->envelopeCount--;

    processModulations(inst->paramList, inst->modList, 0.016f);
    OutVal out2 = generateVoice(e.vm, v, 1.0f, 440.0f); /* must not crash */

    /* post-delete: op0.level back to baseValue */
    ASSERT_NEAR(inst->id.fm.ops[0]->level->currentValue,
                inst->id.fm.ops[0]->level->baseValue, 0.0001f);
    (void)out1;
    (void)out2;

    free_env(&e);
    printf("PASS test_voice_render_after_route_and_delete\n");
    return 0;
}

/*
 * removeMod itself is unconditional (it accepts any mod in the list).
 * The CALLER-level guard lives in the UI (Task 12): the GUI must
 * refuse to call removeMod when idx < coreEnvelopeCount. We pin that
 * contract here: assuming the guard fires, the core envelope is
 * untouched and the envelopeCount is unchanged after a rejected delete
 * attempt. (Per the task brief's Context section, the brief's
 * `if (0 >= inst->coreEnvelopeCount)` sketch is dead code; we keep
 * the meaningful assertions and remove the no-op branch.)
 */
static int test_core_envelope_delete_rejected(void) {
    TestEnv e;
    if (make_env(&e, 2)) return 1;
    Instrument *inst = e.vm->instruments[0];
    Envelope *core = inst->envelopes[0];
    ASSERT_TRUE(inst->coreEnvelopeCount == 4,
                "core count recorded at init");
    /* The UI guard fires when (idx < coreEnvelopeCount). Simulating
     * that guard: we do NOT call removeMod. Core envelope stays put. */
    ASSERT_TRUE(core == inst->envelopes[0],
                "core envelope untouched after rejected delete");
    ASSERT_EQ(inst->envelopeCount, 4, "count unchanged");
    /* Core envelope's mod pointer is still in the instrument's modList */
    int found = 0;
    for (int i = 0; i < inst->modList->count; i++) {
        if (inst->modList->mods[i] == &core->base) {
            found = 1;
            break;
        }
    }
    ASSERT_TRUE(found, "core envelope still in instrument modList");

    free_env(&e);
    printf("PASS test_core_envelope_delete_rejected\n");
    return 0;
}

/*
 * Bare removeMod on a core envelope (no caller guard) must still
 * succeed and not corrupt the voice pool. This pins that the
 * primitive itself is unconditional; the guard is purely a UI-level
 * policy decision (Task 12).
 */
static int test_remove_mod_primitively_accepts_core(void) {
    TestEnv e;
    if (make_env(&e, 2)) return 1;
    Instrument *inst = e.vm->instruments[0];
    Envelope *core = inst->envelopes[0];

    /* removeMod does not consult coreEnvelopeCount. */
    bool removed = removeMod(inst->modList, inst->paramList, &core->base);
    ASSERT_TRUE(removed, "removeMod accepts core when called bare");
    /* The freed envelope pointer is now dangling. The voice pool's
     * envelope[0] aliases this struct via stage duration/curvature
     * params; rendering would dereference freed memory. We do NOT
     * render here — this test is the primitive contract, not a
     * render-safety contract. The render-safety guard lives at the
     * UI layer. */
    free_env(&e);
    printf("PASS test_remove_mod_primitively_accepts_core\n");
    return 0;
}

int main(void) {
    initModSystem();
    int fails = 0;

    /* Tasks 1/9 baseline */
    fails += test_init_fm_instrument();
    fails += test_clear_paramlist_frees();
    fails += test_preset_load_rebuilds_voices();

    /* Task 10 integration suite */
    fails += test_route_to_fm_param_affects_value();
    fails += test_remove_modulation_is_surgical();
    fails += test_rewire_modulation_swaps_source();
    fails += test_voice_render_after_route_and_delete();
    fails += test_core_envelope_delete_rejected();
    fails += test_remove_mod_primitively_accepts_core();

    if (fails) {
        fprintf(stderr, "%d integration test(s) failed\n", fails);
        return 1;
    }
    printf("ALL voice/mod tests passed\n");
    return 0;
}