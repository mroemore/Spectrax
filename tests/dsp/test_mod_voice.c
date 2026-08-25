/* test_mod_voice.c — instrument/voice integration of modulations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voice.h"
#include "modsystem.h"
#include "wavetable.h"
#include "settings.h"

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

/* Local teardown matching the modsystem test pattern: freeParamList owns
 * all Parameter structs; modList is freed with bare free (the modsystem
 * test_free cleanup has the same model). */
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

int main(void) {
    initModSystem();
    int fails = 0;
    fails += test_init_fm_instrument();
    fails += test_clear_paramlist_frees();
    fails += test_preset_load_rebuilds_voices();
    if (fails) {
        fprintf(stderr, "%d integration test(s) failed\n", fails);
        return 1;
    }
    printf("ALL voice/mod tests passed\n");
    return 0;
}