/*
 * test_voice.c — lifecycle, trigger, render, polyphony, and teardown tests
 * for the Spectrax voice manager.
 *
 * Build (fil-c toolchain):  make bin/test_voice && ./bin/test_voice
 *
 * Scope notes:
 *  - The generator functions that are NOT implemented in the port
 *    (band_limited_sawtooth/square, init_blit/blit_synth) are intentionally
 *    NOT exercised. blep_tri is exercised nowhere on purpose: its BLEP
 *    corrections are disabled in src/blit_synth.c.
 *  - Despite the "opaque" reputation, struct Voice and GranularProcessor are
 *    fully defined in src/voice.h, so the tests below reach into fields only
 *    where a pre-existing bug leaves no public API path — each use is
 *    commented.
 *  - There is NO note-off API in voice.h/voice.c (searched: no noteOff,
 *    no release, no deactivate). The AD envelope decaying to 0 is the only
 *    release-like behaviour, so the "release" test renders until the
 *    envelope ends and asserts silence.
 *
 * Known pre-existing bugs pinned by these tests (see per-test comments):
 *  - voice.c generateBlep never advances leftPhase → BLEP output is a
 *    constant; default BLEP_RAMP at phase 0 is exactly 0 (silent voice).
 *  - voice.c freeVoice double-frees (env base mod freed by freeModList,
 *    then its output param again by freeParamList, then the env struct
 *    again by freeEnvelope; volume freed by both freeParamList and
 *    freeParameter). freeVoiceManager on populated pools crashes.
 *  - voice.c getFreeVoice (VA_FREE_OR_ZERO) returns the LAST free voice,
 *    not the first (index is overwritten instead of break-ing).
 *  - voice.c createGranularProcessor seeds grainReadPos with an overflowing
 *    rand() expression; granularProcess then reads out of bounds.
 *  - voice.c freeVoice frees instrument-owned FM operator ratio/level params
 *    and (VOICE_TYPE_SAMPLE) calls freeSample on a sample whose data lives
 *    inside the SamplePool arena (invalid free).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/wait.h>
#include <unistd.h>

#include "voice.h"
#include "sample.h"
#include "wavetable.h"
#include "notes.h"
#include "modsystem.h"
#include "sequencer.h"
#include "appstate.h"
#include "io.h"

#define TEST_SAMPLE_LEN 4096

/* ------------------------------------------------------------------ */
/* assertion helpers (same style as tests/dsp/test_notes.c)            */
/* ------------------------------------------------------------------ */

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(actual, expected) do { \
    int _a = (int)(actual); \
    int _e = (int)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* test environment                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    Settings settings;
    SamplePool *sp;
    WavetablePool *wtp;
    PresetBank pb;
    VoiceManager *vm;
} TestEnv;

/*
 * Build a sample pool with one 4096-sample sine at 44.1k/24-bit. The
 * manager needs >=1 sample because init_instrument() dereferences
 * samplePool->samples[0] even for non-sample voice types, and the bank
 * needs >=1 preset because createVoiceManager() applies patches[0].
 */
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
    initDefaultFmPreset(&p); /* patches[0] becomes the FM default */
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

/*
 * Render `n` samples for `voice` the same way main.c does (per-sample
 * processModulations + generateVoice), returning the peak |L| amplitude.
 */
static float render_voice(TestEnv *e, Voice *voice, int n) {
    float phaseInc = 440.0f / (float)SAMPLE_RATE;
    float maxAbs = 0.0f;
    for (int i = 0; i < n; i++) {
        processModulations(voice->paramList, voice->modList,
                           1.0f / (float)SAMPLE_RATE);
        OutVal out = generateVoice(e->vm, voice, phaseInc, 440.0f);
        float a = fabsf(out.L);
        if (a > maxAbs) maxAbs = a;
    }
    return maxAbs;
}

/* ------------------------------------------------------------------ */
/* tests                                                               */
/* ------------------------------------------------------------------ */

static int test_create_voice_manager(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;
    ASSERT_TRUE(e.vm != NULL, "createVoiceManager() returned NULL");
    printf("PASS test_create_voice_manager\n");
    return 0;
}

static int test_default_voice_type_is_fm(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    Voice *v = getFreeVoice(e.vm, 0);
    ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
    /* struct Voice is exposed in voice.h — direct field check. */
    ASSERT_INT_EQ(v->type, VOICE_TYPE_FM);
    ASSERT_TRUE(v->type != VOICE_TYPE_SPECTRAL,
                "default voice type must not be SPECTRAL (needs FFT setup)");
    printf("PASS test_default_voice_type_is_fm\n");
    return 0;
}

static int test_fm_voice_renders_after_trigger(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    int note[NOTE_INFO_SIZE] = { A, 4 };
    Voice *v = getFreeVoice(e.vm, 0);
    ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
    triggerVoice(v, note);

    float maxAbs = render_voice(&e, v, 2048);
    /* Attack is ~0.1s (4410 samples at 1x) so 2048 samples still lands in
     * the attack; output must be non-zero while the envelope is active. */
    ASSERT_TRUE(maxAbs > 1e-4f,
                "FM voice silent after trigger (envelope/operator path broken?)");
    printf("PASS test_fm_voice_renders_after_trigger (peak %.5f)\n", maxAbs);
    return 0;
}

static int test_fm_voice_envelope_attack_then_silence(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    /* There is no noteOff() in the API (searched voice.h/voice.c), so the
     * only release-like behaviour is the AD envelope (0.1s attack, 4.5s
     * decay) running to completion. processModulations advances the env at
     * 2x (updateMod + generateEnvelope both add dt), so it ends ~2.3s. */
    int note[NOTE_INFO_SIZE] = { A, 4 };
    Voice *v = getFreeVoice(e.vm, 0);
    ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
    triggerVoice(v, note);

    int total = 5 * SAMPLE_RATE; /* 5 s */
    float phaseInc = 440.0f / (float)SAMPLE_RATE;
    float attackMax = 0.0f, tailMax = 0.0f;
    for (int i = 0; i < total; i++) {
        processModulations(v->paramList, v->modList, 1.0f / (float)SAMPLE_RATE);
        OutVal out = generateVoice(e.vm, v, phaseInc, 440.0f);
        float a = fabsf(out.L);
        /* generous attack window (covers both 1x and 2x env rate) */
        if (i > 1000 && i < 4500 && a > attackMax) attackMax = a;
        if (i > total - (SAMPLE_RATE / 10) && a > tailMax) tailMax = a;
    }
    ASSERT_TRUE(attackMax > 0.005f,
                "envelope never produced audible attack output");
    ASSERT_TRUE(tailMax < 1e-6f,
                "envelope did not decay to silence (tail still sounding)");
    printf("PASS test_fm_voice_envelope_attack_then_silence "
           "(attack peak %.5f, tail peak %.2e)\n", attackMax, tailMax);
    return 0;
}

static int test_sample_voice_renders(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_SAMPLE, e.sp, &e.pb);
    ASSERT_TRUE(inst != NULL, "init_instrument(SAMPLE) failed");
    initVoicePool(e.vm, 1, 2, inst);

    int note[NOTE_INFO_SIZE] = { A, 4 };
    Voice *v = getFreeVoice(e.vm, 1);
    ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
    triggerVoice(v, note);

    float maxAbs = render_voice(&e, v, 512);
    ASSERT_TRUE(maxAbs > 1e-4f, "sample voice silent after trigger");
    printf("PASS test_sample_voice_renders (peak %.5f)\n", maxAbs);
    return 0;
}

static int test_blep_voice_square_renders(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_BLEP, e.sp, &e.pb);
    ASSERT_TRUE(inst != NULL, "init_instrument(BLEP) failed");
    initVoicePool(e.vm, 2, 2, inst);

    /* WORKAROUND for voice.c generateBlep: leftPhase is never advanced, so
     * the BLEP oscillator sits at phase 0 forever. blep_saw(0,inc)==0 (and
     * noblep_sine(0)==0), so the default BLEP_RAMP voice is silent. Setting
     * the shape to BLEP_SQUARE yields a constant non-zero output at phase 0
     * (blep_square(0,inc) == 2.0), which still proves the voice lifecycle +
     * generator wiring. See test_blep_default_shape_is_silent_bug. */
    setParameterValue(inst->id.blep.shape, (float)BLEP_SQUARE);

    int note[NOTE_INFO_SIZE] = { A, 4 };
    Voice *v = getFreeVoice(e.vm, 2);
    ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
    triggerVoice(v, note);

    float maxAbs = render_voice(&e, v, 512);
    ASSERT_TRUE(maxAbs > 1e-3f, "BLEP voice silent after trigger");
    printf("PASS test_blep_voice_square_renders (peak %.5f)\n", maxAbs);
    return 0;
}

static int test_blep_default_shape_is_silent_bug(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    Instrument *inst = NULL;
    init_instrument(&inst, VOICE_TYPE_BLEP, e.sp, &e.pb);
    initVoicePool(e.vm, 2, 2, inst);
    /* default shape stays 0 (BLEP_RAMP) */

    int note[NOTE_INFO_SIZE] = { A, 4 };
    Voice *v = getFreeVoice(e.vm, 2);
    triggerVoice(v, note);

    /* Pins actual behaviour of the pre-existing phase bug: voice.c:94-113
     * (generateBlep) never advances leftPhase; blep_saw(0, inc) == 0. */
    float maxAbs = render_voice(&e, v, 512);
    ASSERT_NEAR(maxAbs, 0.0f, 1e-6f);
    printf("PASS test_blep_default_shape_is_silent_bug "
           "(documents voice.c phase-not-advanced bug)\n");
    return 0;
}

static int test_polyphony_all_voices_sound(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    /* NOTE: getFreeVoice uses VA_FREE_OR_ZERO which returns the LAST free
     * voice (voice.c:186-190 overwrites voiceIndex instead of break-ing),
     * so with 4 voices the allocation order is 3,2,1,0. It still hands out
     * four distinct voices, so all notes sound. */
    const int midiNotes[4] = { C, D, E, G };
    for (int i = 0; i < 4; i++) {
        int note[NOTE_INFO_SIZE] = { midiNotes[i], 4 };
        Voice *v = getFreeVoice(e.vm, 0);
        ASSERT_TRUE(v != NULL, "getFreeVoice() returned NULL");
        triggerVoice(v, note);
        float maxAbs = render_voice(&e, v, 1024);
        ASSERT_TRUE(maxAbs > 1e-4f, "a triggered voice rendered silence");
    }
    printf("PASS test_polyphony_all_voices_sound\n");
    return 0;
}

static int test_granular_processor_renders(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    GranularProcessor *gp = createGranularProcessor(e.sp->samples[0]);
    ASSERT_TRUE(gp != NULL, "createGranularProcessor() returned NULL");

    /* WORKAROUND for voice.c:544-548: grainReadPos is seeded with
     * rand() * GRANULAR_BUFFER_SIZE / 4.0 — a signed-int overflow that can
     * land anywhere (usually far outside the sample); granularProcess only
     * wraps once (voice.c:565-567), so the fresh processor reads OOB and
     * crashes. Plant sane positions to exercise the render path. */
    for (int i = 0; i < GRAIN_COUNT; i++) {
        gp->grainReadPos[i] = 100.0f + (float)i;
        gp->windowIndex[i] = 100 + i;
    }

    float maxAbs = 0.0f;
    for (int i = 0; i < 64; i++) {
        OutVal out = granularProcess(gp, 0.001f);
        float a = fabsf(out.L);
        if (a > maxAbs) maxAbs = a;
    }
    ASSERT_TRUE(maxAbs > 1e-4f, "granularProcess produced only silence");

    /* No free function exists for GranularProcessor (voice.c:50 TO-DO: free
     * grain) — deliberately leaking, documented. */
    printf("PASS test_granular_processor_renders (peak %.5f)\n", maxAbs);
    return 0;
}

/* Task 2.1: voice FM operators own their params (no aliasing). The voice
 * operator's params are created fresh in the voice's paramList and their
 * base values sync from the instrument operator via
 * syncOperatorFromInstrument. */
static int test_voice_operator_owns_params(void) {
    ParamList *pl = createParamList();
    Operator *proto = createOperator(pl, 2.0f);
    setParameterBaseValue(proto->feedbackAmount, 0.4f);
    setParameterBaseValue(proto->level, 0.7f);

    ParamList *vpl = createParamList();
    Operator *vop = createVoiceOperator(vpl, proto);
    ASSERT_TRUE(vop != NULL, "createVoiceOperator returned an op");
    ASSERT_TRUE(vop->feedbackAmount != proto->feedbackAmount,
                "voice op owns its params (not aliased)");
    ASSERT_INT_EQ((int)(vop->feedbackAmount->baseValue * 10), 4); /* 0.4 */
    ASSERT_INT_EQ((int)(vop->level->baseValue * 10), 7);          /* 0.7 */

    setParameterBaseValue(proto->level, 0.3f);
    syncOperatorFromInstrument(vop, proto);
    ASSERT_INT_EQ((int)(vop->level->baseValue * 10), 3);          /* 0.3 */

    free(vop);
    free(proto);
    freeParamList(pl);
    freeParamList(vpl);
    printf("PASS test_voice_operator_owns_params\n");
    return 0;
}

/* Task 2.2: the instrument carries voice-driver destination params (gain,
 * per-op outLevel/pitch) seeded with default connections at init + on
 * preset load. The FM default preset path must produce them (createVoiceManager
 * inits a SAMPLE instrument then applies the FM default preset). */
static int test_default_gain_connections_seeded(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    Instrument *inst = e.vm->instruments[0];
    ASSERT_TRUE(inst != NULL, "instrument exists");
    ASSERT_INT_EQ((int)inst->voiceType, (int)VOICE_TYPE_FM);
    ASSERT_TRUE(inst->gain != NULL, "instrument has a gain dest param");
    ASSERT_INT_EQ(inst->gain->modulator_count, 1);
    ASSERT_TRUE(inst->gain->modulators->source->type == MT_ATTEN,
                "connection wraps source[0] in an attenuator");
    ASSERT_TRUE(inst->gain->modulators->source->data.atten.input == inst->modList->mods[0],
                "source[0] drives gain by default");
    for (int i = 0; i < MAX_FM_OPERATORS; i++) {
        ASSERT_TRUE(inst->id.fm.ops[i] != NULL, "fm op exists");
        ASSERT_INT_EQ(inst->id.fm.ops[i]->outLevel->modulator_count, 1);
        ASSERT_TRUE(inst->id.fm.ops[i]->pitch != NULL, "per-op pitch param exists");
        ASSERT_INT_EQ((int)(inst->id.fm.ops[i]->pitch->baseValue * 10), 0);
    }
    printf("PASS test_default_gain_connections_seeded\n");
    return 0;
}

static int test_free_manager_no_voices(void) {
    TestEnv e;
    if (make_env(&e, 0) != 0) return 1;

    /* With zero voices per channel freeVoiceManager only frees the manager
     * struct — this path is clean and must not crash. */
    freeVoiceManager(e.vm);
    e.vm = NULL;
    printf("PASS test_free_manager_no_voices\n");
    return 0;
}

/*
 * freeVoiceManager on a populated pool must NOT crash. This was
 * BUG-VC-1: freeVoice double-freed envelope data, parameters, and the
 * volume param (each freed 2-3 times). Fixed: each object is owned and
 * freed exactly once (mods own structs, paramList owns all params).
 */
/* appstate.c's setSelectedPattern calls rebuildPatternGraph(); the graph
 * doesn't exist in this test context, so the real symbol is stubbed. */
void rebuildPatternGraph(void) {
}

/*
 * Note-delete repro (user report: "deleting the 3rd note in the pattern
 * editor causes a segfault"). Mimics main.c's audio callback: a pattern
 * plays on channel 0, a note at step 2 triggers a voice, then the GUI
 * deletes that note (FUNCTION+EDIT -> editCurrentNote OFF) mid-playback
 * while the voice is still ringing. Renders more samples after the delete.
 */
static int test_note_delete_mid_playback_does_not_crash(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    ApplicationState *as = createApplicationState();
    ASSERT_TRUE(as != NULL, "createApplicationState");
    PatternList *pl = createPatternList(as);
    ASSERT_TRUE(pl != NULL, "createPatternList");

    /* Pattern 0: notes on steps 0,1,2 (the 3rd note is step index 2). */
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    memset(notes, OFF, sizeof(notes));
    notes[0][0] = C;  notes[0][1] = 3;
    notes[1][0] = E;  notes[1][1] = 3;
    notes[2][0] = G;  notes[2][1] = 3;
    int pi = addPattern(pl, 16, notes);
    ASSERT_INT_EQ(pi, 0);

    ParamList *gpl = createParamList();
    ASSERT_TRUE(gpl != NULL, "createParamList");
    Arranger *arr = createArranger(&e.settings, e.vm, as, gpl);
    ASSERT_TRUE(arr != NULL, "createArranger");
    arr->enabledChannels = 1;
    arr->playing = 1;
    arr->song[0][0] = 0; /* channel 0, row 0 -> pattern 0 */
    Sequencer *seq = createSequencer(arr);
    ASSERT_TRUE(seq != NULL, "createSequencer");
    seq->running[0] = 1;
    seq->pattern_index[0] = 0;

    int stepSamples = SAMPLE_RATE / 8; /* 8 steps/sec, coarse */
    for (int step = 0; step < 12; step++) {
        incrementSequencer(seq, pl, arr);
        if (seq->running[0]) {
            int *note = getCurrentStep(pl, seq->pattern_index[0], seq->playhead_index[0]);
            if (note[0] != OFF) {
                Voice *v = getFreeVoice(e.vm, 0);
                triggerVoice(v, note);
            }
        }
        /* render one "buffer" for the channel the way patestCallback does */
        for (int i = 0; i < stepSamples; i++) {
            for (int v = 0; v < e.vm->voiceCount[0]; v++) {
                Voice *cv = e.vm->voicePools[0][v];
                if (!cv->active) continue;
                syncVoiceGraph(cv);
                processModulations(cv->paramList, cv->modList, 1.0f / (float)SAMPLE_RATE);
                Mod *gainDriver = (cv->cloneCount > 0) ? cv->clones[0] : NULL;
                if (gainDriver && gainDriver->type == MT_ENV && !gainDriver->data.env.isTriggered) {
                    setParameterValue(cv->volume, 1.0f);
                    setParameterBaseValue(cv->volume, 1.0f);
                    cv->active = 0;
                }
                generateVoice(e.vm, cv, 440.0f / (float)SAMPLE_RATE, 440.0f);
            }
        }
        /* step 4: the GUI deletes step 2's note (the 3rd note). */
        if (step == 4) {
            editCurrentNote(pl, 0, 2, (int[]){ OFF, 0 });
            ASSERT_INT_EQ(pl->patterns[0].notes[2][0], OFF);
        }
    }

    /* After the delete + playback continues, the pattern data must be intact. */
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], C);
    ASSERT_INT_EQ(pl->patterns[0].notes[1][0], E);
    ASSERT_INT_EQ(pl->patterns[0].notes[2][0], OFF);

    free(seq);
    free(arr);
    freeParamList(gpl);
    free(pl);
    free(as);
    printf("PASS test_note_delete_mid_playback_does_not_crash\n");
    return 0;
}

/*
 * Note-delete repro (user report: "deleting the 3rd note in the pattern
 * editor causes a segfault") using the AUTO-LOADED save file. Loads the
 * real bin/s1.sng (the song the app boots with), then deletes EVERY note
 * in every pattern while simulating the audio callback, to find which
 * (pattern, step) crashes.
 */
static int test_note_delete_autoload_song(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    ApplicationState *as = createApplicationState();
    ASSERT_TRUE(as != NULL, "createApplicationState");
    PatternList *pl = createPatternList(as);
    ASSERT_TRUE(pl != NULL, "createPatternList");

    ParamList *gpl = createParamList();
    ASSERT_TRUE(gpl != NULL, "createParamList");
    Arranger *arr = createArranger(&e.settings, e.vm, as, gpl);
    ASSERT_TRUE(arr != NULL, "createArranger");

    /* locate the auto-load save (meson cwd == builddir; manual runs == repo root) */
    const char *song_path = "../bin/s1.sng";
    FILE *probe = fopen(song_path, "rb");
    if (!probe) { song_path = "bin/s1.sng"; probe = fopen(song_path, "rb"); }
    if (!probe) {
        fprintf(stderr, "SKIP test_note_delete_autoload_song (no %s)\n", song_path);
        free(arr); freeParamList(gpl); free(pl); free(as);
        return 0;
    }
    fclose(probe);

    SequencerFileResult r = loadSequencerState(song_path, arr, pl);
    ASSERT_INT_EQ(r, SEQ_OK);
    ASSERT_TRUE(pl->pattern_count > 0, "song has patterns");
    arr->enabledChannels = e.settings.enabledChannels;
    arr->playing = 1;
    Sequencer *seq = createSequencer(arr);
    ASSERT_TRUE(seq != NULL, "createSequencer");

    int stepSamples = SAMPLE_RATE / 8;
    for (int p = 0; p < pl->pattern_count; p++) {
        if (pl->patterns[p].pattern_size <= 0) continue;
        seq->running[0] = 1;
        seq->pattern_index[0] = p;
        seq->playhead_index[0] = 0;
        /* delete every non-blank step of this pattern while playing */
        for (int s = 0; s < pl->patterns[p].pattern_size; s++) {
            if (pl->patterns[p].notes[s][0] == OFF) continue;
            for (int step = 0; step < 4; step++) {
                incrementSequencer(seq, pl, arr);
                if (seq->running[0]) {
                    int *note = getCurrentStep(pl, seq->pattern_index[0], seq->playhead_index[0]);
                    if (note[0] != OFF) {
                        Voice *v = getFreeVoice(e.vm, 0);
                        triggerVoice(v, note);
                    }
                }
                for (int i = 0; i < stepSamples; i++) {
                    for (int v = 0; v < e.vm->voiceCount[0]; v++) {
                        Voice *cv = e.vm->voicePools[0][v];
                        if (!cv->active) continue;
                        syncVoiceGraph(cv);
                        processModulations(cv->paramList, cv->modList, 1.0f / (float)SAMPLE_RATE);
                        Mod *gainDriver = (cv->cloneCount > 0) ? cv->clones[0] : NULL;
                        if (gainDriver && gainDriver->type == MT_ENV && !gainDriver->data.env.isTriggered) {
                            setParameterValue(cv->volume, 1.0f);
                            setParameterBaseValue(cv->volume, 1.0f);
                            cv->active = 0;
                        }
                        generateVoice(e.vm, cv, 440.0f / (float)SAMPLE_RATE, 440.0f);
                    }
                }
            }
            editCurrentNote(pl, p, s, (int[]){ OFF, 0 });
            ASSERT_INT_EQ(pl->patterns[p].notes[s][0], OFF);
        }
    }

    printf("PASS test_note_delete_autoload_song (%s, %d patterns)\n", song_path, pl->pattern_count);
    free(seq);
    free(arr);
    freeParamList(gpl);
    free(pl);
    free(as);
    return 0;
}

/*
 * Root-cause probe for the note-delete crash: setCurrentNote passes
 * `&note` to onNoteSet (setLastUsedNote), which reads the stack ADDRESS
 * of the note array into lastUsedNote. The next ADD then writes that
 * stack address into the pattern; playing it indexes noteFrequencies[addr]
 * out of bounds.
 */
static int test_last_used_note_poisoning(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    ApplicationState *as = createApplicationState();
    ASSERT_TRUE(as != NULL, "createApplicationState");
    PatternList *pl = createPatternList(as);
    ASSERT_TRUE(pl != NULL, "createPatternList");

    int init[NOTE_INFO_SIZE] = { C, 3 };
    setCurrentNote(pl, 0, 0, init);
    /* After a note-set, lastUsedNote should hold the NOTE, not an address. */
    ASSERT_INT_EQ(as->lastUsedNote[0], C);
    ASSERT_INT_EQ(as->lastUsedNote[1], 3);

    /* mimic the pattern-screen ADD path: setCurrentNote(..., lastUsedNote) */
    setCurrentNote(pl, 0, 1, as->lastUsedNote);
    ASSERT_INT_EQ(pl->patterns[0].notes[1][0], C);
    ASSERT_INT_EQ(pl->patterns[0].notes[1][1], 3);

    /* the poisoned note must not OOB-index noteFrequencies on playback */
    int *n = getStep(pl, 0, 1);
    ASSERT_TRUE(n[0] >= 0 && n[0] < NOTE_COUNT && n[1] >= 0 && n[1] < MAX_OCTAVES,
                "pattern note stays a valid pitch after ADD-via-lastUsedNote");

    free(pl);
    free(as);
    printf("PASS test_last_used_note_poisoning\n");
    return 0;
}

static int test_free_voice_manager_clean(void) {
    TestEnv e;
    if (make_env(&e, 4) != 0) return 1;

    freeVoiceManager(e.vm);
    e.vm = NULL;
    printf("PASS test_free_voice_manager_clean "
           "(populated pool freed without double-free)\n");
    return 0;
}

int main(void) {
    /* Envelope generation reads global envTables (modsystem.c:400); must be
     * initialised before the first processModulations(). */
    initModSystem();

    int failed = 0;
    failed |= test_create_voice_manager();
    failed |= test_default_voice_type_is_fm();
    failed |= test_fm_voice_renders_after_trigger();
    failed |= test_fm_voice_envelope_attack_then_silence();
    failed |= test_sample_voice_renders();
    failed |= test_blep_voice_square_renders();
    failed |= test_blep_default_shape_is_silent_bug();
    failed |= test_polyphony_all_voices_sound();
    failed |= test_granular_processor_renders();
    failed |= test_voice_operator_owns_params();
    failed |= test_default_gain_connections_seeded();
    failed |= test_note_delete_mid_playback_does_not_crash();
    failed |= test_note_delete_autoload_song();
    failed |= test_last_used_note_poisoning();
    failed |= test_free_manager_no_voices();
    failed |= test_free_voice_manager_clean();

    printf("\n%s (%d tests, %s)\n",
           failed ? "FAILED" : "PASSED", 11, failed ? ">=1 failed" : "all passed");
    return failed;
}
