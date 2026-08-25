/* test_mod_voice.c — instrument/voice integration of modulations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voice.h"
#include "modsystem.h"

#define ASSERT_EQ(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
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

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

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

int main(void) {
    initModSystem();
    int fails = 0;
    fails += test_init_fm_instrument();
    if (fails) {
        fprintf(stderr, "%d integration test(s) failed\n", fails);
        return 1;
    }
    printf("ALL voice/mod tests passed\n");
    return 0;
}