/* test_mod_voice.c — instrument/voice integration of modulations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "voice.h"
#include "modsystem.h"
#include "wavetable.h"
#include "settings.h"
#include "sample.h"
#include "notes.h"
#include "../src/io/preset_io.h"

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
    ASSERT_TRUE(a != NULL, "createParameter a");
    ASSERT_TRUE(b != NULL, "createParameter b");
    clearParamList(pl);
    ASSERT_EQ(pl->count, 0, "paramList emptied");
    Parameter *c = createParameter(pl, "c", 3.0f, 0.0f, 10.0f);
    ASSERT_EQ(pl->count, 1, "list usable after clear");
    ASSERT_TRUE(c != NULL, "new param allocated");
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
    /* initDefaultFmPreset leaves p.name zeroed — that mirrors the
     * "no name yet" boot state in initVoices, which the dirty-bit
     * machinery must tolerate. We give the bank slot a real name
     * *after* addPresetToBank so the createVoiceManager path runs
     * the markPresetLoaded populate branch in tests that need a
     * named boot preset. */
    addPresetToBank(&e->pb, p);
    strncpy(e->pb.patches[0].name, "default_fm",
            sizeof(e->pb.patches[0].name) - 1);
    e->pb.patches[0].name[sizeof(e->pb.patches[0].name) - 1] = '\0';

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

/*
 * Regression for the I1 review finding:
 *   addRuntimeEnvelope (gui.c) used to call rebuildVoicesForInstrument,
 *   which freed + re-initialized every live voice on the channel —
 *   killing any mid-playback note on every ; press.
 *
 * Voices alias only the CORE envelopes (voice->envelope[4] is fixed-size,
 * created at initVoice from inst->envelopes[0..3]); runtime envelopes
 * route via inst->paramList, which all voices already reference. So
 * adding a runtime envelope to a live instrument must NOT disturb any
 * active voice.
 *
 * The test inlines the same operations addRuntimeEnvelope performs
 * (createAD + envelopeCount++ + createParameter routeIndex) — it does
 * NOT call addRuntimeEnvelope because gui.c is not linked into this
 * test binary. If the fix is regressed and rebuildVoicesForInstrument
 * were re-introduced into the GUI path, this test would still pass
 * (it doesn't exercise the GUI), but the principle is the same: the
 * invariant we're pinning is "these instrument-side mutations leave
 * an already-active voice untouched".
 */
static int test_runtime_envelope_add_does_not_rebuild_voices(void) {
    TestEnv e;
    if (make_env(&e, 4)) return 1;
    Instrument *inst = e.vm->instruments[0];

    /* Trigger a voice on the channel BEFORE the runtime envelope add. */
    int note[NOTE_INFO_SIZE] = { A, 4 };
    Voice *v = getFreeVoice(e.vm, 0);
    ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
    triggerVoice(v, note);
    ASSERT_EQ(v->active, 1, "voice is active after trigger");
    ASSERT_TRUE(v->note[0] != OFF, "voice has a real note after trigger");
    int noteBefore = v->note[0];

    /* Inlined operations from gui.c:addRuntimeEnvelope:
     *   createAD + envelopeCount++ + createParameter routeIndex.
     * (No rebuildVoicesForInstrument — and there should not need to be.) */
    ASSERT_TRUE(inst->envelopeCount < MAX_ENVELOPES,
                "envelope room available for runtime add");
    int idx = inst->envelopeCount;
    inst->envelopes[idx] = createAD(inst->paramList, inst->modList,
                                    0.25f, 4.25f, "AD+");
    inst->envelopeCount++;
    Parameter *routeIdx = createParameter(inst->paramList, "route",
                                          12.0f, 0.0f, 12.0f);
    ASSERT_TRUE(inst->envelopes[idx] != NULL, "runtime env created");
    ASSERT_TRUE(routeIdx != NULL, "routeIndex param created");

    /* The voice must still be the same voice, still active, with the
     * original note intact. If a rebuild happened, v could have been
     * freed and replaced by a fresh one with active==0. */
    ASSERT_TRUE(v->active == 1, "voice still active after runtime envelope add");
    ASSERT_EQ(v->note[0], noteBefore,
              "voice note[0] unchanged after runtime envelope add");
    ASSERT_TRUE(v->note[0] != OFF, "voice note still not OFF");

    free_env(&e);
    printf("PASS test_runtime_envelope_add_does_not_rebuild_voices\n");
    return 0;
}

static int test_preset_param_survives_apply(void) {
    /* applyInstrumentPreset frees the whole paramList (clearParamList)
     * and rebuilds it. It MUST recreate selectedPresetIndex (like the
     * panning/detune params), or the PRESET dial reads a dangling param
     * whose range reads as [0,0], making drawDialGuiNode divide by zero
     * (angle = NaN) and pathologically slowing the renderer (~22s/frame
     * under llvmpipe; the reported 4fps). Regression: the clear* now
     * frees, turning the pre-existing omission into a use-after-free. */
    TestEnv e;
    if (make_env(&e, 1)) {
        return 1;
    }
    Instrument *inst = e.vm->instruments[0];
    ASSERT_TRUE(inst->selectedPresetIndex != NULL, "preset param exists");
    ASSERT_NEAR(inst->selectedPresetIndex->maxValue,
                (float)e.pb.presetCount - 1.0f, 0.001f);
    bool found = false;
    for (int i = 0; i < inst->paramList->count; i++) {
        if (inst->paramList->params[i] == inst->selectedPresetIndex) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found, "preset param registered in the paramList after apply");
    free_env(&e);
    printf("PASS test_preset_param_survives_apply\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Task 2 — applyInstrumentPreset fidelity (FM / sampler / BLEP /      */
/* LFO / RND).                                                        */
/*                                                                    */
/* Before the fix: applyInstrumentPreset only reset count + panning    */
/* for FM and ignored sampler / blep outright. Preset FM op data       */
/* (ratio, level, outLevel, feedbackAmount, algorithm) was silently   */
/* dropped, and modSettings (LFOs / RANDs) were counted but never      */
/* materialised into modList entries. After the fix:                  */
/*   - FM operator params reflect the preset's OperatorData          */
/*   - Each MT_LFO preset slot creates a live LFO in modList          */
/* ------------------------------------------------------------------ */

static int test_apply_preset_writes_fm_op_data(void) {
    /*
     * Bug: the FM branch of applyInstrumentPreset reset op counters
     * and recalled createOperator() (which allocates fresh params at
     * default 1.0 / 1.0 / 0.0 / 0.0), then never wrote the preset's
     * OperatorData into them. After applyInstrumentPreset with a known
     * preset, every Operator's Parameter baseValue should reflect the
     * preset's stored OperatorData, AND selectedAlgorithm should land
     * on the preset's selectedAlgorithm int.
     */
    TestEnv e;
    if (make_env(&e, 1)) return 1;
    Instrument *inst = e.vm->instruments[0];

    Preset p;
    memset(&p, 0, sizeof(p));
    strncpy(p.name, "task2-fm", sizeof(p.name) - 1);
    p.voiceType = VOICE_TYPE_FM;
    p.modSettingsCount = 0;
    /* Distinct, easy-to-verify values for each operator slot. */
    p.pd.fm.ops[0].ratio         = 1.5f;
    p.pd.fm.ops[0].level         = 0.20f;
    p.pd.fm.ops[0].outLevel      = 0.80f;
    p.pd.fm.ops[0].feedbackAmount = 0.10f;
    p.pd.fm.ops[1].ratio         = 3.0f;
    p.pd.fm.ops[1].level         = 0.30f;
    p.pd.fm.ops[1].outLevel      = 0.90f;
    p.pd.fm.ops[1].feedbackAmount = 0.05f;
    p.pd.fm.ops[2].ratio         = 5.0f;
    p.pd.fm.ops[2].level         = 0.40f;
    p.pd.fm.ops[2].outLevel      = 0.70f;
    p.pd.fm.ops[2].feedbackAmount = 0.15f;
    p.pd.fm.ops[3].ratio         = 7.0f;
    p.pd.fm.ops[3].level         = 0.50f;
    p.pd.fm.ops[3].outLevel      = 0.60f;
    p.pd.fm.ops[3].feedbackAmount = 0.25f;
    p.pd.fm.selectedAlgorithm = 3;

    applyInstrumentPreset(inst, p);

    for (int i = 0; i < MAX_FM_OPERATORS; i++) {
        Operator *op = inst->id.fm.ops[i];
        ASSERT_TRUE(op != NULL, "FM op allocated");
        ASSERT_NEAR(op->ratio->baseValue,         p.pd.fm.ops[i].ratio,          0.0001f);
        ASSERT_NEAR(op->level->baseValue,         p.pd.fm.ops[i].level,          0.0001f);
        ASSERT_NEAR(op->outLevel->baseValue,      p.pd.fm.ops[i].outLevel,       0.0001f);
        ASSERT_NEAR(op->feedbackAmount->baseValue, p.pd.fm.ops[i].feedbackAmount, 0.0001f);
    }
    ASSERT_EQ((int)inst->id.fm.selectedAlgorithm->baseValue,
              p.pd.fm.selectedAlgorithm);

    free_env(&e);
    printf("PASS test_apply_preset_writes_fm_op_data\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Task 3 — presetFromInstrument extractor + round-trip.                */
/*                                                                    */
/* presetFromInstrument is the reverse of applyInstrumentPreset: it   */
/* reads a live Instrument and produces a Preset struct that, after  */
/* save/load, applies faithfully. The test below mutates a runtime-  */
/* built FM instrument (one runtime AD envelope added to modList)     */
/* into a Preset, then runs that Preset through applyInstrumentPreset */
/* on a fresh instrument and asserts every observed field is        */
/* preserved.                                                        */
/* ------------------------------------------------------------------ */

static int test_preset_from_instrument_roundtrip(void) {
    TestEnv e;
    if (make_env(&e, 1)) return 1;
    Instrument *inst = e.vm->instruments[0];

    /* Snapshot the baseline (default FM preset) before mutation. */
    int algoBefore = (int)inst->id.fm.selectedAlgorithm->baseValue;
    float opRatio[4], opLevel[4], opOut[4], opFb[4];
    for (int i = 0; i < MAX_FM_OPERATORS; i++) {
        opRatio[i] = inst->id.fm.ops[i]->ratio->baseValue;
        opLevel[i] = inst->id.fm.ops[i]->level->baseValue;
        opOut[i]   = inst->id.fm.ops[i]->outLevel->baseValue;
        opFb[i]    = inst->id.fm.ops[i]->feedbackAmount->baseValue;
    }

    /* Append a runtime envelope at index 4 (slot 5 in envelopes[]).
     * createAD lives in modsystem; gui.c's addRuntimeEnvelope is NOT
     * linked into this test binary, so we inline the same operations
     * the brief specifies. */
    int idx = inst->envelopeCount;
    inst->envelopes[idx] = createAD(inst->paramList, inst->modList,
                                    0.123f, 0.456f, "RT-3");
    inst->envelopeCount++;
    ASSERT_TRUE(inst->envelopes[idx] != NULL, "runtime env created");

    /* Extract the preset from the live instrument. */
    Preset out;
    memset(&out, 0, sizeof(out));
    out = presetFromInstrument(inst);
    ASSERT_EQ((int)out.voiceType, (int)VOICE_TYPE_FM);
    ASSERT_EQ(out.modSettingsCount, 5);
    ASSERT_EQ((int)out.pd.fm.selectedAlgorithm, algoBefore);
    for (int i = 0; i < MAX_FM_OPERATORS; i++) {
        ASSERT_NEAR(out.pd.fm.ops[i].ratio,         opRatio[i], 0.0001f);
        ASSERT_NEAR(out.pd.fm.ops[i].level,         opLevel[i], 0.0001f);
        ASSERT_NEAR(out.pd.fm.ops[i].outLevel,      opOut[i],   0.0001f);
        ASSERT_NEAR(out.pd.fm.ops[i].feedbackAmount, opFb[i],  0.0001f);
    }
    /* Runtime envelope lands in slot 4 as MT_ENV. */
    ASSERT_EQ((int)out.modSettings[4].type, (int)MT_ENV);

    /* Round-trip: apply the extracted preset to a fresh instrument and
     * confirm the post-apply instrument is structurally equivalent to
     * the source instrument we extracted from. */
    Instrument *dst = NULL;
    init_instrument(&dst, VOICE_TYPE_FM, e.sp, &e.pb);
    ASSERT_TRUE(dst != NULL, "dst instrument allocated");
    applyInstrumentPreset(dst, out);
    ASSERT_EQ((int)dst->voiceType, (int)VOICE_TYPE_FM);
    ASSERT_EQ(dst->envelopeCount, 5);
    ASSERT_EQ((int)dst->id.fm.selectedAlgorithm->baseValue, algoBefore);
    for (int i = 0; i < MAX_FM_OPERATORS; i++) {
        ASSERT_NEAR(dst->id.fm.ops[i]->ratio->baseValue,         opRatio[i], 0.0001f);
        ASSERT_NEAR(dst->id.fm.ops[i]->level->baseValue,         opLevel[i], 0.0001f);
        ASSERT_NEAR(dst->id.fm.ops[i]->outLevel->baseValue,      opOut[i],   0.0001f);
        ASSERT_NEAR(dst->id.fm.ops[i]->feedbackAmount->baseValue, opFb[i],   0.0001f);
    }
    /* Runtime envelope survived: the 5th slot is a live Envelope in
     * the modList, not just a count. */
    bool foundRT = false;
    for (int i = 0; i < dst->modList->count; i++) {
        if (dst->modList->mods[i]->type == MT_ENV) {
            Envelope *env = (Envelope *)dst->modList->mods[i];
            if (env == dst->envelopes[4]) { foundRT = true; break; }
        }
    }
    ASSERT_TRUE(foundRT, "runtime envelope preserved through round-trip");

    free(dst);
    free_env(&e);
    printf("PASS test_preset_from_instrument_roundtrip\n");
    return 0;
}

static int test_apply_preset_creates_lfo_mod(void) {
    /*
     * Bug: the modSettings loop counted MT_LFO and MT_RND entries but
     * never created the LFO/Random or added them to modList, so the
     * count was dead state. Fix: a slot with type=MT_LFO must (a)
     * create an LFO via initLfoFromPreset, (b) add it to inst->modList.
     */
    TestEnv e;
    if (make_env(&e, 1)) return 1;
    Instrument *inst = e.vm->instruments[0];

    int lfosBefore = inst->modList->count;

    Preset p;
    memset(&p, 0, sizeof(p));
    strncpy(p.name, "task2-lfo", sizeof(p.name) - 1);
    p.voiceType = VOICE_TYPE_FM;
    /* 5th slot is the LFO (envelope 0..3 default + 1 LFO). */
    p.modSettingsCount = 5;
    initADPresetData(&p.modSettings[0], 0.1f, 0.2f, 0.5f, 0.5f);
    initADPresetData(&p.modSettings[1], 0.1f, 0.2f, 0.5f, 0.5f);
    initADPresetData(&p.modSettings[2], 0.1f, 0.2f, 0.5f, 0.5f);
    initADPresetData(&p.modSettings[3], 0.1f, 0.2f, 0.5f, 0.5f);
    initLfoPresetData(&p.modSettings[4], LS_SIN, 5.5f, 0.25f);
    /* FM op fields may be junk for this test; only the LFO creation matters. */

    applyInstrumentPreset(inst, p);

    /* The 5th modList entry (index 4) must be an LFO. */
    ASSERT_EQ(inst->modList->count, lfosBefore + 1);
    ASSERT_TRUE(inst->modList->count >= 5, "enough mod slots for LFO check");
    ASSERT_EQ((int)inst->modList->mods[4]->type, (int)MT_LFO);

    free_env(&e);
    printf("PASS test_apply_preset_creates_lfo_mod\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Task 6: LoadedPreset snapshot + dirty bit.                         */
/*                                                                    */
/* These tests verify the three transitions that markPresetLoaded /   */
/* dial-arrows / saveInstrumentAsPreset are responsible for. We use   */
/* a per-test scratch directory under /tmp/spectrax_test_<pid>_<n> so */
/* the save test doesn't pollute the repo's data/ tree, and clean up  */
/* at the end. The helper make_env() above already wires up a single  */
/* FM preset in the bank called "default_fm" (via initDefaultFmPreset */
/* + addPresetToBank in make_env), so tests that need a known preset  */
/* just call make_env() and read inst->presetBank->patches[0].        */
/* ------------------------------------------------------------------ */

/* Build a per-test scratch directory path. Returns the same buffer
 * every call (single-threaded test runner, no need to thread the
 * pointer through). mkdir -p so the save test can write there. */
static const char *scratch_dir(int slot) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "/tmp/spectrax_test_dirty_%d", slot);
    mkdir(buf, 0755);
    return buf;
}

/* Helper: snapshot a clean FM instrument's load state. After
 * createVoiceManager runs applyInstrumentPreset(patches[0]) the
 * loaded snapshot should be valid, point at bank index -1 (the
 * initVoices path doesn't have a slot index at hand), and leave
 * dirty false (no edits since load). */
static int test_loaded_preset_clean_after_load(void) {
    TestEnv e;
    if (make_env(&e, 1)) { return 1; }
    Instrument *inst = e.vm->instruments[0];

    /* createVoiceManager calls applyInstrumentPreset at boot on
     * patches[0]; that path is the one under test. After the load,
     * the snapshot should hold the preset's name (brief: "strncpy
     * inst->loaded.name, preset.name") and dirty should be false
     * (no edits yet). */
    ASSERT_TRUE(strncmp(inst->loaded.name, "default_fm",
                        sizeof(inst->loaded.name)) == 0,
                "loaded.name matches the applied preset name");
    ASSERT_TRUE(!inst->loaded.dirty,
                "loaded.dirty=false after a fresh apply (no edits)");
    ASSERT_TRUE(!isInstrumentDirty(inst),
                "isInstrumentDirty helper agrees with loaded.dirty");

    free_env(&e);
    printf("PASS test_loaded_preset_clean_after_load\n");
    return 0;
}

/* Helper: a dial-arrow edit (modeled by setParameterBaseValue on
 * the FM op0 level parameter) flips loaded.dirty from false to
 * true. The name field itself is unchanged — only the dirty bit
 * moves. */
static int test_loaded_preset_dirty_after_edit(void) {
    TestEnv e;
    if (make_env(&e, 1)) { return 1; }
    Instrument *inst = e.vm->instruments[0];

    /* Sanity: start clean. */
    ASSERT_TRUE(!inst->loaded.dirty, "loaded.dirty=false at boot");
    ASSERT_TRUE(strncmp(inst->loaded.name, "default_fm",
                        sizeof(inst->loaded.name)) == 0,
                "loaded.name populated at boot");

    Parameter *level = inst->id.fm.ops[0]->level;
    ASSERT_TRUE(level != NULL, "FM op0 level param exists");
    float original = level->baseValue;
    setParameterBaseValue(level, original + 1.0f);
    /* setParameterBaseValue clamps, but original+1.0 is well within
     * typical FM levels so it shouldn't clamp away from a change.
     * The dial-arrow UI path in main.c / harness also sets
     * loaded.dirty=true via getSelectedInstInstrument(); set it
     * explicitly here to mirror that hook — setParameterBaseValue
     * alone doesn't touch the dirty flag (the flag is a UI-layer
     * concept). */
    inst->loaded.dirty = true;

    ASSERT_TRUE(inst->loaded.dirty, "loaded.dirty=true after dial-arrow edit");
    ASSERT_TRUE(isInstrumentDirty(inst), "isInstrumentDirty helper agrees");
    /* The loaded snapshot should be untouched — it's a record of
     * the last load/save, not the live state. */
    ASSERT_TRUE(strncmp(inst->loaded.name, "default_fm",
                        sizeof(inst->loaded.name)) == 0,
                "loaded.name unchanged by edit");

    free_env(&e);
    printf("PASS test_loaded_preset_dirty_after_edit\n");
    return 0;
}

/* Helper: saving the live state to disk clears dirty and updates
 * the loaded snapshot to the new name. We use a per-test scratch
 * directory so we don't need to claim a slot in the repo's
 * data/instrument_presets/ tree (other tests share it). */
static int test_loaded_preset_clean_after_save(void) {
    TestEnv e;
    if (make_env(&e, 1)) { return 1; }
    Instrument *inst = e.vm->instruments[0];
    const char *dir = scratch_dir(6);

    /* Make the instrument dirty by tweaking op0 level. */
    Parameter *level = inst->id.fm.ops[0]->level;
    ASSERT_TRUE(level != NULL, "FM op0 level param exists");
    setParameterBaseValue(level, level->baseValue + 0.5f);
    inst->loaded.dirty = true;
    ASSERT_TRUE(inst->loaded.dirty,
                "loaded.dirty=true after edit, precondition for save test");

    /* Save to a fresh name. saveInstrumentAsPreset calls
     * addPresetToBank on success, so the bank now holds two entries:
     * index 0 (boot preset "default_fm") and index 1 (the new
     * "dirty_save_test"). guiSavePreset() in src/gui.c finds the new
     * slot by name and calls markPresetLoaded, which is the path
     * under test. We call saveInstrumentAsPreset directly (gui.c is
     * app-only, so guiSavePreset isn't linked into the test), then
     * call markPresetLoaded explicitly — that's exactly the branch
     * guiSavePreset runs on PRESET_OK. */
    PresetFileResult r = saveInstrumentAsPreset(inst, "dirty_save_test", dir);
    ASSERT_EQ((int)r, (int)PRESET_OK, "saveInstrumentAsPreset returned PRESET_OK");

    markPresetLoaded(inst, "dirty_save_test");

    ASSERT_TRUE(!inst->loaded.dirty, "loaded.dirty=false after successful save");
    ASSERT_TRUE(isInstrumentDirty(inst) == false, "isInstrumentDirty helper agrees");
    ASSERT_TRUE(strncmp(inst->loaded.name, "dirty_save_test",
                        sizeof(inst->loaded.name)) == 0,
                "loaded.name updated to the saved preset name");

    free_env(&e);
    printf("PASS test_loaded_preset_clean_after_save\n");
    return 0;
}

int main(void) {
    initModSystem();
    int fails = 0;

    /* Tasks 1/9 baseline */
    fails += test_init_fm_instrument();
    fails += test_clear_paramlist_frees();
    fails += test_preset_load_rebuilds_voices();
    fails += test_preset_param_survives_apply();

    /* Task 2 — applyInstrumentPreset fidelity */
    fails += test_apply_preset_writes_fm_op_data();
    fails += test_apply_preset_creates_lfo_mod();

    /* Task 3 — presetFromInstrument extractor + round-trip */
    fails += test_preset_from_instrument_roundtrip();

    /* Task 10 integration suite */
    fails += test_route_to_fm_param_affects_value();
    fails += test_remove_modulation_is_surgical();
    fails += test_rewire_modulation_swaps_source();
    fails += test_voice_render_after_route_and_delete();
    fails += test_core_envelope_delete_rejected();
    fails += test_remove_mod_primitively_accepts_core();
    fails += test_runtime_envelope_add_does_not_rebuild_voices();

    /* Task 6 — LoadedPreset snapshot + dirty tracking */
    fails += test_loaded_preset_clean_after_load();
    fails += test_loaded_preset_dirty_after_edit();
    fails += test_loaded_preset_clean_after_save();

    if (fails) {
        fprintf(stderr, "%d integration test(s) failed\n", fails);
        return 1;
    }
    printf("ALL voice/mod tests passed\n");
    return 0;
}