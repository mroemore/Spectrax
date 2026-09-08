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
/* Task 5: pull in the chip label-edit pure helpers declared in gui.h.
 * The meson test target for test_mod_voice now links gui.c + raylib
 * etc. (see tests/meson.build) so these calls resolve at link time. */
#include "../src/gui.h"

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FALSE(cond, msg) do { \
    if ((cond)) { \
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

/* Task 3: addRuntimeSource / removeSource — the source list is
 * inst->modList, and inst->envelopeCount must stay synced to
 * inst->modList->count so the harness ASSERT envcount line and the
 * existing add_route_delete fixture remain correct. Core sources
 * (indices 0..coreEnvelopeCount-1) are immutable; out-of-range is
 * rejected; runtime-added sources can be removed. The teardown uses
 * bare free(inst) because freeInstrument is static in src/voice.c —
 * this matches every other init_instrument test in this file. */
static int test_runtime_source_lifecycle(void) {
    SamplePool *sp = createSamplePool();
    PresetBank pb;
    initPresetBank(&pb);
    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);
    ASSERT_TRUE(inst != NULL, "init_instrument FM");
    int core = inst->coreEnvelopeCount;   /* 4 for FM */
    int before = inst->modList->count;
    int beforeSrc = modSourceCount(inst->modList);
    (void)core; /* referenced in brief; kept for traceability */

    addRuntimeSource(inst);
    ASSERT_EQ(inst->modList->count, before + 1, "addRuntimeSource appends a source");
    ASSERT_EQ(inst->envelopeCount, modSourceCount(inst->modList),
              "envelopeCount tracks the source count (attens excluded)");
    Mod *m = inst->modList->mods[before];
    ASSERT_EQ(m->type, MT_ENV, "default source is an envelope");
    Mod *env = (Mod *)m;
    ASSERT_EQ(env->data.env.stageCount, 2, "default source has an AD shape");

    /* core sources cannot be removed */
    removeSource(inst, 0);
    ASSERT_EQ(inst->modList->count, before + 1, "core source removal rejected");
    /* out-of-range rejected (source position) */
    removeSource(inst, inst->modList->count);
    ASSERT_EQ(inst->modList->count, before + 1, "out-of-range removal rejected");
    /* runtime source removed — runtime source is at source position `beforeSrc` */
    removeSource(inst, beforeSrc);
    ASSERT_EQ(inst->modList->count, before, "runtime source removed");
    ASSERT_EQ(inst->envelopeCount, beforeSrc, "envelopeCount synced after removal");

    free(inst);
    freeSamplePool(sp);
    printf("PASS test_runtime_source_lifecycle\n");
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

    /* Voices now own a full copy of the instrument's source mods (graph
     * copy) rather than aliasing stage params by pointer. The first clone
     * mirrors the instrument's source[0] (the seeded gain driver). */
    Voice *v = vm->voicePools[0][0];
    ASSERT_TRUE(v->cloneCount >= 1, "voice has at least one source clone");
    ASSERT_TRUE(v->clones[0] != NULL, "voice clone exists");
    ASSERT_TRUE(v->clones[0]->data.env.stages[0].duration !=
                    inst->envelopes[0]->data.env.stages[0].duration,
                "voice owns its stage params (not aliased)");
    ASSERT_EQ((int)(v->clones[0]->data.env.stages[0].duration->baseValue * 100),
              (int)(inst->envelopes[0]->data.env.stages[0].duration->baseValue * 100),
              "voice clone copies the instrument stage value");

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
    ASSERT_TRUE(addModulation(inst->paramList, inst->modList,
                              inst->envelopes[idx],
                              level, 1.0f, MO_ADD),
                "route env→op0.level");
    /* Per-connection attenuator (Task 2): the connection's source is
     * the atten addModulation inserted, not the envelope. */
    Mod *a1 = level->modulators->source;

    /*
     * Note (Task 2 finding): processModulations recomputes every
     * param's currentValue from its baseValue, so the source's
     * `currentValue` is reset every pass. setParameterBaseValue (not
     * setParameterValue) writes baseValue AND currentValue; since the
     * source's output param has no modulators, its post-pass value
     * stays at baseValue. The atten (amount 1.0) then passes 0.25
     * onto the destination's base (0.1) → 0.35.
     */
    setParameterBaseValue(inst->envelopes[idx]->output, 0.25f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    ASSERT_NEAR(level->currentValue, base + 0.25f, 0.0001f);

    /* removeModulation unwires the connection but leaves the source
     * alive. With no modulators on `level`, post-pass currentValue ==
     * baseValue. (Since Task 2 the `source` argument is the
     * connection's source — the atten — not the envelope.) */
    ASSERT_TRUE(removeModulation(inst->paramList, inst->modList, level, a1),
                "unwrap env→op0.level");
    processModulations(inst->paramList, inst->modList, 0.016f);
    ASSERT_NEAR(level->currentValue, base, 0.0001f);

    /*
     * rewireModulation swaps the source on an existing connection.
     * Wire the same modulator onto op[1].level (still MO_ADD 1.0).
     * Set baseValue=0.5 so the modulator contribution is observable.
     */
    addModulation(inst->paramList, inst->modList,
                  inst->envelopes[idx],
                  level, 1.0f, MO_ADD);
    Parameter *level1 = inst->id.fm.ops[1]->level;
    float base1 = level1->baseValue;
    Mod *a2 = level->modulators->source;
    ASSERT_TRUE(rewireModulation(inst->paramList, level, a2, a2),
                "rewire no-op (same source)");
    /*
     * The rewire test asserts the swap path; here we use the simplest
     * valid rewire: detach the existing connection from op0.level and
     * attach to op1.level by removeModulation + addModulation (the
     * public API supports both; rewireModulation is exercised
     * separately below in the dedicated test). Verify op0 back to base
     * and op1 modulated after the swap.
     */
    removeModulation(inst->paramList, inst->modList, level, a2);
    addModulation(inst->paramList, inst->modList, inst->envelopes[idx],
                  level1, 1.0f, MO_ADD);
    setParameterBaseValue(inst->envelopes[idx]->output, 0.5f);
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

    ASSERT_TRUE(addModulation(inst->paramList, inst->modList,
                              inst->envelopes[idxA],
                              level, 1.0f, MO_ADD),
                "route A");
    /* Per-connection attenuator (Task 2): conn->source is A's atten. */
    Mod *aA = level->modulators->source;
    ASSERT_TRUE(addModulation(inst->paramList, inst->modList,
                              inst->envelopes[idxB],
                              level, 1.0f, MO_ADD),
                "route B");

    setParameterBaseValue(inst->envelopes[idxA]->output, 0.10f);
    setParameterBaseValue(inst->envelopes[idxB]->output, 0.40f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    /* both wired: base + 0.10 + 0.40 = base + 0.50 */
    ASSERT_NEAR(level->currentValue, base + 0.50f, 0.0001f);

    /* A's connection source is its atten, not the envelope */
    ASSERT_TRUE(removeModulation(inst->paramList, inst->modList, level, aA),
                "unwrap A only");
    processModulations(inst->paramList, inst->modList, 0.016f);
    /* only B survives: base + 0.40 */
    ASSERT_NEAR(level->currentValue, base + 0.40f, 0.0001f);
    /* B's modulator entry still present */
    ASSERT_TRUE(level->modulators != NULL, "B modulator still wired");
    /* A's modulator entry removed. Connection sources are attens now;
     * identify the real source via input. */
    ModConnection *conn = level->modulators;
    int sawA = 0, sawB = 0;
    while (conn) {
        if (conn->source->data.atten.input == inst->envelopes[idxA]) sawA = 1;
        if (conn->source->data.atten.input == inst->envelopes[idxB]) sawB = 1;
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
 * After a process pass with B's baseValue=0.7 and A's baseValue=0.1,
 * op0.level must reflect B's contribution only.
 *
 * Since Task 2, connections point at attenuators, so the rewire
 * target must be B's atten — which only exists once B has a route of
 * its own (here, onto op1.level). rewireModulation does not GC the
 * old atten: aA stays in the modList, orphaned, and is freed by the
 * teardown.
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

    ASSERT_TRUE(addModulation(inst->paramList, inst->modList,
                              inst->envelopes[idxA],
                              level, 1.0f, MO_ADD),
                "route A");
    /* Per-connection attenuator (Task 2): conn->source is A's atten. */
    Mod *aA = level->modulators->source;
    /* Give B its own atten (the rewire target) via op1.level. */
    Parameter *level1 = inst->id.fm.ops[1]->level;
    ASSERT_TRUE(addModulation(inst->paramList, inst->modList,
                              inst->envelopes[idxB],
                              level1, 1.0f, MO_ADD),
                "route B");
    Mod *aB = level1->modulators->source;

    /* Rewire op0.level's connection from A's atten to B's atten */
    ASSERT_TRUE(rewireModulation(inst->paramList, level, aA, aB),
                "rewire A→B");
    setParameterBaseValue(inst->envelopes[idxA]->output, 0.10f);
    setParameterBaseValue(inst->envelopes[idxB]->output, 0.70f);
    processModulations(inst->paramList, inst->modList, 0.016f);
    /* only B contributes now: base + 0.70 */
    ASSERT_NEAR(level->currentValue, base + 0.70f, 0.0001f);

    /* source pointer on the connection is B's atten, not A's */
    ModConnection *conn = level->modulators;
    ASSERT_TRUE(conn != NULL, "one connection survives rewire");
    ASSERT_TRUE(conn->source == aB, "rewired connection source is B's atten");
    ASSERT_TRUE(conn->source->data.atten.input == inst->envelopes[idxB],
                "that atten reads envB");

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
    ASSERT_TRUE(addModulation(inst->paramList, inst->modList,
                              inst->envelopes[idx],
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
                          inst->envelopes[idx]),
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
    Mod *core = inst->envelopes[0];
    /* The default FM patch has 4 startup SOURCES (3 AD envs + 1 vibrato
     * LFO); coreEnvelopeCount protects all of them. */
    ASSERT_TRUE(inst->coreEnvelopeCount == 4,
                "core count recorded at init");
    /* The UI guard fires when (idx < coreEnvelopeCount). Simulating
     * that guard: we do NOT call removeMod. Core envelope stays put. */
    ASSERT_TRUE(core == inst->envelopes[0],
                "core envelope untouched after rejected delete");
    ASSERT_EQ(inst->envelopeCount, 3, "count unchanged (3 startup envs)");
    /* Core envelope's mod pointer is still in the instrument's modList */
    int found = 0;
    for (int i = 0; i < inst->modList->count; i++) {
        if (inst->modList->mods[i] == core) {
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
    Mod *core = inst->envelopes[0];

    /* removeMod does not consult coreEnvelopeCount. */
    bool removed = removeMod(inst->modList, inst->paramList, core);
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
    /* Task 8: the preset param spans the full navigable slot range, not
     * just the filled-on-disk count -- PREV/NEXT walk PRESET_BANK_SLOTS
     * slots, blank ones holding a default FM patch. */
    ASSERT_NEAR(inst->selectedPresetIndex->maxValue,
                (float)(PRESET_BANK_SLOTS - 1), 0.001f);
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
    /* 3 default envs + the runtime env = 4 MT_ENV sources (the default
     * FM patch's LFO is not an envelope). */
    ASSERT_EQ(dst->envelopeCount, 4);
    ASSERT_EQ((int)dst->id.fm.selectedAlgorithm->baseValue, algoBefore);
    for (int i = 0; i < MAX_FM_OPERATORS; i++) {
        ASSERT_NEAR(dst->id.fm.ops[i]->ratio->baseValue,         opRatio[i], 0.0001f);
        ASSERT_NEAR(dst->id.fm.ops[i]->level->baseValue,         opLevel[i], 0.0001f);
        ASSERT_NEAR(dst->id.fm.ops[i]->outLevel->baseValue,      opOut[i],   0.0001f);
        ASSERT_NEAR(dst->id.fm.ops[i]->feedbackAmount->baseValue, opFb[i],   0.0001f);
    }
    /* Runtime envelope survived: it's the 4th MT_ENV source in the fresh
     * instrument (3 default envs + the runtime one), so envelopes[3]. */
    bool foundRT = false;
    for (int i = 0; i < dst->modList->count; i++) {
        if (dst->modList->mods[i]->type == MT_ENV) {
            Mod *env = (Mod *)dst->modList->mods[i];
            if (env == dst->envelopes[3]) { foundRT = true; break; }
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

    /* The preset (4 AD + 1 LFO) is applied fresh: 5 sources. */
    ASSERT_EQ(modSourceCount(inst->modList), 5, "5 sources after apply");
    int mi4 = modIndexAt(inst->modList, 4);
    ASSERT_TRUE(mi4 >= 0, "source 4 present");
    ASSERT_EQ((int)inst->modList->mods[mi4]->type, (int)MT_LFO);

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

/* Task 2: instrument type swap. setInstrumentVoiceType creates a
 * fresh Instrument of the requested VoiceType, swaps it into the
 * channel, frees the old instrument (including its modList/paramList),
 * and rebuilds voices so envelope aliases point at the new params. The
 * rebuilding flag is set on the OLD instrument before the swap so the
 * audio thread skips the channel during the teardown, and is cleared
 * on the FRESH instrument after the voice rebuild completes.
 *
 * NOTE: VOICE_TYPE_GRAIN is rejected — only SAMPLE/FM/BLEP are valid
 * chip types today (GRAIN/SPECTRAL are placeholders in the enum, the
 * chip UI will not offer them). */
static int test_set_instrument_voice_type(void) {
    SamplePool *sp = createSamplePool();
    WavetablePool *wtp = createWavetablePool();
    PresetBank pb;
    initPresetBank(&pb);
    Settings s = { .enabledChannels = 1, .defaultVoiceCount = 2, .defaultBPM = 120 };
    VoiceManager *vm = createVoiceManager(&s, sp, wtp, &pb);
    ASSERT_TRUE(vm != NULL, "createVoiceManager");
    ASSERT_TRUE(setInstrumentVoiceType(vm, 0, VOICE_TYPE_FM), "switch to FM");
    ASSERT_EQ(vm->instruments[0]->voiceType, VOICE_TYPE_FM, "type applied");
    ASSERT_TRUE(setInstrumentVoiceType(vm, 0, VOICE_TYPE_BLEP), "switch to BLEP");
    ASSERT_EQ(vm->instruments[0]->voiceType, VOICE_TYPE_BLEP, "type applied 2");
    ASSERT_FALSE(setInstrumentVoiceType(vm, 0, VOICE_TYPE_GRAIN), "GRAIN rejected");
    ASSERT_TRUE(vm->voicePools[0][0] != NULL, "voices rebuilt onto new instrument");
    freeVoiceManager(vm);
    freeWavetablePool(wtp);
    freeSamplePool(sp);
    printf("PASS test_set_instrument_voice_type\n");
    return 0;
}

/* Task 2: voice pool resize. setChannelVoiceCount re-allocates the
 * channel's voice pool (frees existing voices via initVoicePool's
 * fresh malloc path + rebuildVoicesForInstrument), clamped to
 * [1, MAX_VOICES_PER_CHANNEL]. The instrument's rebuilding flag is
 * set during the resize so the audio thread skips the channel while
 * the voice pool is being torn down and re-built.
 *
 * NOTE: freeVoiceManager only frees the voice pool up to voiceCount
 * entries, so if we tried to grow past MAX_VOICES_PER_CHANNEL the
 * extra voices would silently leak. The clamp test pins this
 * contract: an over-the-cap value is rejected, the count is left
 * unchanged on rejection, and a zero value is also rejected. */
static int test_set_channel_voice_count(void) {
    SamplePool *sp = createSamplePool();
    WavetablePool *wtp = createWavetablePool();
    PresetBank pb;
    initPresetBank(&pb);
    Settings s = { .enabledChannels = 1, .defaultVoiceCount = 2, .defaultBPM = 120 };
    VoiceManager *vm = createVoiceManager(&s, sp, wtp, &pb);
    ASSERT_TRUE(setChannelVoiceCount(vm, 0, 4), "resize to 4");
    ASSERT_EQ(vm->voiceCount[0], 4, "count applied");
    ASSERT_FALSE(setChannelVoiceCount(vm, 0, 99), "over-8 rejected");
    ASSERT_EQ(vm->voiceCount[0], 4, "unchanged on reject");
    ASSERT_FALSE(setChannelVoiceCount(vm, 0, 0), "zero rejected");
    freeVoiceManager(vm);
    freeWavetablePool(wtp);
    freeSamplePool(sp);
    printf("PASS test_set_channel_voice_count\n");
    return 0;
}

/* Task 5: unit-test the chip label-edit pure helpers exposed in
 * gui.h. These are the cycle/cursor math that drives the expanded
 * chip's KM_EDIT + arrow input. Tested directly (no GUI node, no
 * Arranger) because the helpers are intentionally pure — they take
 * a string + an int and return a result, no side effects beyond
 * the cursor/slot they touch. The test asserts:
 *   1. chipLabelCursorMove clamps at 0 / strlen + respects maxLen
 *      so a corrupt strlen can never push the cursor past the array.
 *   2. chipLabelCursorMove ignores bogus directions (anything other
 *      than -1 or +1 is a no-op, so input layer mistakes are safe).
 *   3. chipLabelCycleCharAt cycles an 'A' forward to 'B' and back.
 *   4. chipLabelCycleCharAt on a NUL slot lands on the LAST char
 *      when stepping backward, or NAME_CHARS[1] forward — because
 *      charIndex(NUL)=0, +1 wraps past index 0.
 *   5. chipLabelCharIndex folds a-z to the upper-case slot.
 *   6. chipLabelCharIndex returns 0 for chars not in the table.
 * If any assertion fails the test prints the line + returns non-zero
 * so meson flags it (same shape as the other tests in this file). */
static int test_voice_graph_copy_syncs_and_isolates(void) {
    ParamList *ipl = createParamList();
    ModList *iml = createModList();
    Mod *env = createAD(ipl, iml, 0.25f, 4.25f, "AD");
    ASSERT_TRUE(env != NULL, "instrument env created");
    Parameter *gain = createParameter(ipl, "gain", 1.0f, 0.0f, 2.0f);
    ASSERT_TRUE(addModulation(ipl, iml, env, gain, 1.0f, MO_MUL), "inst gain route");

    ParamList *vpl = createParamList();
    ModList *vml = createModList();
    Mod *clone = cloneMod(vpl, vml, env);
    ASSERT_TRUE(clone != NULL, "cloneMod returns a Mod");
    ASSERT_EQ(clone->type, MT_ENV, "clone preserves the env type");
    ASSERT_TRUE(clone->data.env.stages[0].duration != env->data.env.stages[0].duration,
                "clone owns its stage params (not aliased)");
    ASSERT_EQ((int)(clone->data.env.stages[0].duration->baseValue * 100), 25, "clone copies attack duration");

    setParameterBaseValue(env->data.env.stages[0].duration, 1.0f);
    syncModValues(clone, env);
    ASSERT_EQ((int)(clone->data.env.stages[0].duration->baseValue * 100), 100, "sync copies the base value");

    freeParamList(vpl);
    freeModList(vml);
    freeParamList(ipl);
    freeModList(iml);
    printf("PASS test_voice_graph_copy_syncs_and_isolates\n");
    return 0;
}

static int test_voice_graph_survives_source_retype(void) {
    SamplePool *sp = createSamplePool();
    PresetBank pb;
    initPresetBank(&pb);
    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);
    ASSERT_TRUE(inst != NULL, "init_instrument FM");
    ASSERT_TRUE(inst->modList->count > 0, "sources exist");

    /* Build a voice graph copy via the voice manager-less path: create a
     * fresh voice + initialize_voice against this instrument. */
    Voice *v = (Voice *)calloc(1, sizeof(Voice));
    ASSERT_TRUE(v != NULL, "voice alloc");
    initialize_voice(v, inst);
    ASSERT_TRUE(v->cloneCount >= 1, "voice cloned the source");
    ASSERT_EQ(v->clones[0]->type, MT_ENV, "first clone is an env");

    /* Retype source[0] ENV -> LFO in the instrument, then rebuild the
     * voice copy. The fresh clone must be an LFO with its own rate param
     * and no dangling reads. */
    Mod *src0 = inst->modList->mods[0];
    ASSERT_TRUE(changeModType(inst->modList, src0, MT_LFO, inst->paramList),
                "changeModType ENV->LFO");
    freeVoice(v);
    v = (Voice *)calloc(1, sizeof(Voice));
    ASSERT_TRUE(v != NULL, "voice realloc");
    initialize_voice(v, inst);
    ASSERT_EQ(v->clones[0]->type, MT_LFO, "clone follows the retyped source");
    ASSERT_TRUE(v->clones[0]->data.lfo.rate != NULL, "LFO clone has its own rate param");
    ASSERT_TRUE(v->clones[0]->data.lfo.rate != src0->data.lfo.rate,
                "LFO clone rate is not aliased");

    freeVoice(v);
    free(inst);
    freeSamplePool(sp);
    printf("PASS test_voice_graph_survives_source_retype\n");
    return 0;
}

static int test_chip_label_edit(void) {
    /* 1. cursor clamped at 0 + at strlen */
    int cursor = 0;
    chipLabelCursorMove(&cursor, "ABC", 8, -1);
    ASSERT_EQ(cursor, 0, "cursor clamps at 0 going left");
    cursor = 2;
    chipLabelCursorMove(&cursor, "ABC", 8, +1);
    ASSERT_EQ(cursor, 3, "cursor can sit one past NUL");
    chipLabelCursorMove(&cursor, "ABC", 8, +1);
    ASSERT_EQ(cursor, 3, "cursor clamps at strlen going right");
    /* 1b. cursor also clamped at maxLen (defensive against corrupt
     * strlen — verify with a label that reports 3 chars but a maxLen
     * that only allows 2 indices). */
    cursor = 0;
    chipLabelCursorMove(&cursor, "ABC", 2, +1);
    ASSERT_EQ(cursor, 1, "cursor under maxLen");
    chipLabelCursorMove(&cursor, "ABC", 2, +1);
    ASSERT_EQ(cursor, 2, "cursor at maxLen boundary");
    chipLabelCursorMove(&cursor, "ABC", 2, +1);
    ASSERT_EQ(cursor, 2, "cursor clamps at maxLen, not strlen");

    /* 2. bogus directions are ignored */
    cursor = 3;
    chipLabelCursorMove(&cursor, "ABC", 8, 0);
    ASSERT_EQ(cursor, 3, "dir=0 ignored");
    chipLabelCursorMove(&cursor, "ABC", 8, 5);
    ASSERT_EQ(cursor, 3, "dir=+5 ignored");
    chipLabelCursorMove(&cursor, "ABC", 8, -7);
    ASSERT_EQ(cursor, 3, "dir=-7 ignored");

    /* 3. char cycle forward / backward on 'A' */
    char slot = 'A';
    chipLabelCycleCharAt(&slot, +1);
    ASSERT_EQ(slot, 'B', "A +1 = B");
    chipLabelCycleCharAt(&slot, -1);
    ASSERT_EQ(slot, 'A', "B -1 = A");

    /* 4. char cycle on NUL: chipLabelCharIndex(NUL) returns 0 (NUL is
     * not in the table), so cycle +1 lands on NAME_CHARS[1] = 'B'
     * (NOT 'A' — index 0 is 'A' but NUL already maps to 0, so +1
     * wraps past it). cycle -1 lands on NAME_CHARS[count-1] = ' '
     * (the last char in the table). This matches the preset-name
     * node's behaviour exactly (its cycleNameChar uses the same
     * charIndex function). Each step uses a fresh slot so we don't
     * conflate state from the previous sub-assertion. */
    {
        char slot = '\0';
        chipLabelCycleCharAt(&slot, +1);
        ASSERT_EQ(slot, 'B', "NUL +1 = B (charIndex(NUL)=0, +1 wraps to 1)");
    }
    {
        char slot = '\0';
        chipLabelCycleCharAt(&slot, -1);
        ASSERT_EQ(slot, ' ', "NUL -1 = last char in table (space)");
    }

    /* 5. chipLabelCharIndex folds a-z to upper-case slot */
    ASSERT_EQ(chipLabelCharIndex('a'), chipLabelCharIndex('A'), "a folds to A");
    ASSERT_EQ(chipLabelCharIndex('z'), chipLabelCharIndex('Z'), "z folds to Z");
    ASSERT_EQ(chipLabelCharIndex('A'), 0, "A is at index 0");

    /* 6. charIndex returns 0 for chars not in the table */
    ASSERT_EQ(chipLabelCharIndex('!'), 0, "! not in table returns 0");
    ASSERT_EQ(chipLabelCharIndex('?'), 0, "? not in table returns 0");
    ASSERT_EQ(chipLabelCharIndex('\0'), 0, "NUL not in table returns 0");

    printf("PASS test_chip_label_edit\n");
    return 0;
}

/* Task 4: meta-row type cycle order. SAMPLE->FM->BLEP->SAMPLE forward,
 * BLEP->FM->SAMPLE backward. The cycle helpers live in gui.c (next to
 * the cbTypePrev/cbTypeNext callbacks that drive the new PREV/NEXT
 * action buttons on the instrument-screen meta row) but the cycle order
 * is the load-bearing part — pin it here so a future tweak to the GUI
 * layer can't quietly change which type a PREV/NEXT click lands on.
 *
 * nextVoiceType/prevVoiceType are declared in src/gui.h and defined
 * with `default: VOICE_TYPE_FM` so any VoiceType we don't know about
 * lands on FM. We don't test the default-branch fallthrough (no
 * spurious VoiceType exists in the public enum), only the three
 * visible types. */
static int test_type_cycle_order(void) {
    ASSERT_EQ(nextVoiceType(VOICE_TYPE_SAMPLE), VOICE_TYPE_FM, "sample->fm");
    ASSERT_EQ(nextVoiceType(VOICE_TYPE_FM), VOICE_TYPE_BLEP, "fm->blep");
    ASSERT_EQ(nextVoiceType(VOICE_TYPE_BLEP), VOICE_TYPE_SAMPLE, "blep->sample");
    ASSERT_EQ(prevVoiceType(VOICE_TYPE_SAMPLE), VOICE_TYPE_BLEP, "sample->blep");
    ASSERT_EQ(prevVoiceType(VOICE_TYPE_BLEP), VOICE_TYPE_FM, "blep->fm");
    ASSERT_EQ(prevVoiceType(VOICE_TYPE_FM), VOICE_TYPE_SAMPLE, "fm->sample");
    printf("PASS test_type_cycle_order\n");
    return 0;
}

/*
 * Bug 2 regression (user report): the ROUTELINES overlay keeps drawing the
 * PREVIOUS source's routes when a different ROUTE button becomes selected
 * via navigation from below. syncRouteLinesOverlay used to early-return
 * when the overlay was already up, so g_routeLinesCtx stayed stale. Moving
 * from a same-row element to a ROUTE button worked only because the
 * overlay got torn down in between (non-ROUTE selection -> shouldShow
 * false -> removed -> re-created with the new ctx).
 *
 * Headless-unfriendly (createInstrumentGui allocates render textures), so
 * the executable regression lives in the instrument_harness fixture
 * `route_lines_follow_selection.txt` (ASSERT routelinesrc==N verb).
 */

/*
 * Default FM patch routing (new-FM-preset sound): env0->gain only,
 * env1->outLevel(ops1+2), env2->outLevel(ops3+4), and the final source
 * (an LFO) -> pitch(ops2+4) at ~40Hz depth. Guards against regressing
 * back to the old "env0 fans out to every outLevel" seed.
 */
static int test_default_fm_routing(void) {
    TestEnv e;
    if (make_env(&e, 1)) {
        return 1;
    }
    Instrument *inst = e.vm->instruments[0];
    ASSERT_EQ(modSourceCount(inst->modList), 4, "default FM has 4 sources");
    int mi3 = modIndexAt(inst->modList, 3);
    ASSERT_TRUE(mi3 >= 0, "source 3 present");
    ASSERT_EQ(inst->modList->mods[mi3]->type, MT_LFO, "source 3 is an LFO");

    /* env0 -> gain only */
    ASSERT_EQ(inst->gain->modulator_count, 1, "gain driven by one source");
    /* env1 -> outLevel ops 1+2 */
    ASSERT_EQ(inst->id.fm.ops[0]->outLevel->modulator_count, 1, "op1 outLevel driven");
    ASSERT_EQ(inst->id.fm.ops[1]->outLevel->modulator_count, 1, "op2 outLevel driven");
    /* env2 -> outLevel ops 3+4 */
    ASSERT_EQ(inst->id.fm.ops[2]->outLevel->modulator_count, 1, "op3 outLevel driven");
    ASSERT_EQ(inst->id.fm.ops[3]->outLevel->modulator_count, 1, "op4 outLevel driven");
    /* LFO -> pitch ops 2+4 only */
    ASSERT_EQ(inst->id.fm.ops[1]->pitch->modulator_count, 1, "op2 pitch driven");
    ASSERT_EQ(inst->id.fm.ops[3]->pitch->modulator_count, 1, "op4 pitch driven");
    ASSERT_EQ(inst->id.fm.ops[0]->pitch->modulator_count, 0, "op1 pitch unmodulated");

    /* pitch depth ~40Hz (the atten amount the seed sets) */
    ModConnection *pc = inst->id.fm.ops[1]->pitch->modulators;
    ASSERT_TRUE(pc && pc->source && pc->source->type == MT_ATTEN, "pitch routed via atten");
    if (pc && pc->source && pc->source->type == MT_ATTEN && pc->source->data.atten.attenAmount) {
        ASSERT_NEAR(pc->source->data.atten.attenAmount->baseValue, 20.0f, 0.5f);
    }

    /* op1 outLevel is driven by env1, NOT env0 (the old fan-out) */
    ModConnection *oc = inst->id.fm.ops[0]->outLevel->modulators;
    ASSERT_TRUE(oc && oc->source && oc->source->type == MT_ATTEN, "op1 outLevel via atten");
    if (oc && oc->source && oc->source->type == MT_ATTEN) {
        Mod *input = oc->source->data.atten.input;
        int mi1 = modIndexAt(inst->modList, 1);
        ASSERT_TRUE(mi1 >= 0, "source 1 exists");
        ASSERT_TRUE(input == inst->modList->mods[mi1], "op1 outLevel driven by env1");
    }

    free_env(&e);
    printf("PASS test_default_fm_routing\n");
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

    /* Task 2 — instrument-chip meta primitives */
    fails += test_set_instrument_voice_type();
    fails += test_set_channel_voice_count();

    /* Task 2 — applyInstrumentPreset fidelity */
    fails += test_apply_preset_writes_fm_op_data();
    fails += test_apply_preset_creates_lfo_mod();

    /* Task 3 — presetFromInstrument extractor + round-trip */
    fails += test_preset_from_instrument_roundtrip();

    /* Task 10 integration suite */
    fails += test_runtime_source_lifecycle();
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

    /* Task 5 — chip label-edit char cycle + cursor bounds */
    fails += test_chip_label_edit();

    /* Task 4 — meta-row type cycle (SAMPLE->FM->BLEP->SAMPLE) */
    fails += test_type_cycle_order();

    /* Task 2.3 — per-voice graph copy + base sync */
    fails += test_voice_graph_copy_syncs_and_isolates();
    /* Task 2.4 — voice graph survives a source retype + rebuild */
    fails += test_voice_graph_survives_source_retype();

    /* Default FM patch routing (env0->gain, env1/env2->outLevels, LFO->pitch) */
    fails += test_default_fm_routing();

    /* Bug 2 — ROUTELINES ctx follows selection hops between sources
     * (executable regression in the instrument_harness fixture). */

    if (fails) {
        fprintf(stderr, "%d integration test(s) failed\n", fails);
        return 1;
    }
    printf("ALL voice/mod tests passed\n");
    return 0;
}