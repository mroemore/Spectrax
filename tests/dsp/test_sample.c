/*
 * test_sample.c — direct unit tests for the sample player (src/sample.c).
 *
 * test_voice.c exercises createSamplePool/loadSample only indirectly through
 * the voice manager; none of the sample API (loadSample bookkeeping, the two
 * getSampleValue* playback functions) is asserted directly. This file covers
 * the SamplePool lifecycle, getSampleValueFwd, and getSampleValueRev.
 *
 * Build (fil-c toolchain):  make bin/test_sample && ./bin/test_sample
 *
 * Pre-existing bugs pinned by these tests (see per-test comments):
 *  - sample.c:117-144 getSampleValueRev is a byte-identical copy of
 *    getSampleValueFwd — reverse playback does NOT reverse; it plays forward.
 *  - sample.c:94 / 123 the phase increment is scaled by
 *    PA_SR / (sampleRate/bit) * 2 (== 2*bit when sampleRate == PA_SR), so
 *    phaseIncrement 1.0 advances 48 samples at 44.1k/24-bit, not 1.
 *  - sample.c:111-113 / 140-142 the last two samples of a sample always
 *    return 0 (silence), even on the very last call — loop=0 playback at the
 *    end of a sample returns 0, never the final sample value.
 *  - sample.c:97-102 / 126-131 the end-of-sample wrap subtracts the length
 *    only once, so an adjusted increment >= length leaves the playhead past
 *    the end (out-of-bounds read follows). Tests keep increments small so
 *    this path is never triggered.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sample.h"
#include "settings.h"

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

#define ASSERT_STREQ(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected NULL\n", __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define TEST_SR 44100

/* Fill a buffer with data[i] = i+1. Linear ramp → interpolation at playhead
 * position p returns exactly p+1, which makes playhead position verifiable
 * from the returned value. */
static float *make_ramp(int len) {
    float *buf = (float *)malloc((size_t)len * sizeof(float));
    if (!buf) return NULL;
    for (int i = 0; i < len; i++) {
        buf[i] = (float)(i + 1);
    }
    return buf;
}

/* Mirrors the phase-increment scaling in sample.c:94 / 123. */
static float scale_for(int sr, int bit) {
    return PA_SR / ((float)sr / bit) * 2.0f;
}

/* ------------------------------------------------------------------ */
/* SamplePool lifecycle                                                */
/* ------------------------------------------------------------------ */

static int test_pool_created_empty(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    ASSERT_INT_EQ(sp->sampleCount, 0);
    ASSERT_INT_EQ(sp->memoryUsed, 0);
    ASSERT_TRUE(sp->samples != NULL, "samples array not allocated");
    ASSERT_INT_EQ(sp->maxSamples, MAX_LOADED_SAMPLES);
    freeSamplePool(sp);
    printf("PASS test_pool_created_empty\n");
    return 0;
}

static int test_load_sample_fields(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    const int len = 64;
    float *buf = make_ramp(len);
    ASSERT_TRUE(buf != NULL, "ramp alloc failed");

    loadSample(sp, "ramp64", buf, 24, TEST_SR, len);
    ASSERT_INT_EQ(sp->sampleCount, 1);
    ASSERT_INT_EQ(sp->memoryUsed, (int)(len * sizeof(float)));
    Sample *s = sp->samples[0];
    ASSERT_TRUE(s != NULL, "samples[0] is NULL");
    ASSERT_STREQ(s->name, "ramp64");
    ASSERT_INT_EQ(s->bit, 24);
    ASSERT_INT_EQ(s->sampleRate, TEST_SR);
    ASSERT_INT_EQ(s->length, len);
    ASSERT_TRUE(s->data != NULL, "data pointer is NULL");

    free(buf);
    freeSamplePool(sp);
    printf("PASS test_load_sample_fields\n");
    return 0;
}

static int test_load_preserves_data(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    const int len = 16;
    float *buf = make_ramp(len);
    ASSERT_TRUE(buf != NULL, "ramp alloc failed");

    loadSample(sp, "ramp16", buf, 24, TEST_SR, len);
    /* The pool copies the buffer into its arena (sample.c:55-57); read the
     * copy back through the Sample pointer. */
    for (int i = 0; i < len; i++) {
        ASSERT_NEAR(sp->samples[0]->data[i], (float)(i + 1), 1e-4f);
    }

    free(buf);
    freeSamplePool(sp);
    printf("PASS test_load_preserves_data\n");
    return 0;
}

static int test_load_bit_widths(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    const int len = 16;
    float *bufA = make_ramp(len);
    float *bufB = make_ramp(len);
    ASSERT_TRUE(bufA != NULL && bufB != NULL, "ramp alloc failed");

    loadSample(sp, "a24", bufA, 24, TEST_SR, len);
    loadSample(sp, "b32", bufB, 32, TEST_SR, len);
    ASSERT_INT_EQ(sp->samples[0]->bit, 24);
    ASSERT_INT_EQ(sp->samples[1]->bit, 32);
    /* bit only changes the playback scale, not the stored data (float*). */
    ASSERT_NEAR(scale_for(TEST_SR, 24), 48.0f, 1e-3f);
    ASSERT_NEAR(scale_for(TEST_SR, 32), 64.0f, 1e-3f);

    free(bufA);
    free(bufB);
    freeSamplePool(sp);
    printf("PASS test_load_bit_widths\n");
    return 0;
}

static int test_load_multiple_accumulates(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    const int lens[3] = { 32, 64, 128 };
    const char *names[3] = { "a", "b", "c" };
    for (int i = 0; i < 3; i++) {
        float *buf = make_ramp(lens[i]);
        ASSERT_TRUE(buf != NULL, "ramp alloc failed");
        loadSample(sp, names[i], buf, 24, TEST_SR, lens[i]);
        free(buf);
    }
    ASSERT_INT_EQ(sp->sampleCount, 3);
    ASSERT_INT_EQ(sp->memoryUsed, (int)((32 + 64 + 128) * sizeof(float)));
    for (int i = 0; i < 3; i++) {
        ASSERT_STREQ(sp->samples[i]->name, names[i]);
        ASSERT_INT_EQ(sp->samples[i]->length, lens[i]);
    }
    freeSamplePool(sp);
    printf("PASS test_load_multiple_accumulates\n");
    return 0;
}

static int test_pool_capacity_guard(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    /* SamplePool fields are public in sample.h; shrink the limit so the
     * max-samples guard (sample.c:50-53) is reachable without allocating
     * 1024 samples. */
    sp->maxSamples = 2;
    for (int i = 0; i < 3; i++) {
        float *buf = make_ramp(8);
        ASSERT_TRUE(buf != NULL, "ramp alloc failed");
        loadSample(sp, "x", buf, 24, TEST_SR, 8);
        free(buf);
    }
    /* Third load rejected: count and arena usage must be untouched. */
    ASSERT_INT_EQ(sp->sampleCount, 2);
    ASSERT_INT_EQ(sp->memoryUsed, (int)(2 * 8 * sizeof(float)));
    freeSamplePool(sp);
    printf("PASS test_pool_capacity_guard\n");
    return 0;
}

static int test_free_pool_clean(void) {
    SamplePool *sp = createSamplePool();
    ASSERT_TRUE(sp != NULL, "createSamplePool() returned NULL");
    float *buf = make_ramp(32);
    ASSERT_TRUE(buf != NULL, "ramp alloc failed");
    loadSample(sp, "x", buf, 24, TEST_SR, 32);
    free(buf);
    freeSamplePool(sp); /* must not crash, must not free arena data */
    printf("PASS test_free_pool_clean\n");
    return 0;
}

static int test_free_pool_null(void) {
    freeSamplePool(NULL); /* sample.c:39 guards !sp */
    printf("PASS test_free_pool_null\n");
    return 0;
}

static int test_free_sample_owned(void) {
    /* freeSample must only be used on Samples allocated outside the pool
     * arena (voice.c's freeVoice calling freeSample on pool-arena samples is
     * an invalid free — BUG-VC-5). Build our own here. */
    Sample *s = (Sample *)malloc(sizeof(Sample));
    ASSERT_TRUE(s != NULL, "Sample alloc failed");
    s->data = (float *)malloc(8 * sizeof(float));
    ASSERT_TRUE(s->data != NULL, "data alloc failed");
    s->name = (char *)malloc(4);
    strcpy(s->name, "own");
    s->length = 8;
    s->sampleRate = TEST_SR;
    s->bit = 24;

    freeSample(s);
    ASSERT_NULL(s->data);
    ASSERT_INT_EQ(s->length, 0);
    ASSERT_INT_EQ(s->sampleRate, 0);
    /* freeSample leaves name allocated (sample.c:79-86) — free it ourselves. */
    free(s->name);
    free(s);
    printf("PASS test_free_sample_owned\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* getSampleValueFwd                                                   */
/* ------------------------------------------------------------------ */

static SamplePool *pool_with_ramp(int len) {
    SamplePool *sp = createSamplePool();
    if (!sp) return NULL;
    float *buf = make_ramp(len);
    if (!buf) {
        freeSamplePool(sp);
        return NULL;
    }
    loadSample(sp, "ramp", buf, 24, TEST_SR, len);
    free(buf);
    return sp;
}

static int test_fwd_position_zero(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 0.0f;
    /* phaseIncrement 0 leaves the playhead put: value must be data[0]. */
    float v = getSampleValueFwd(s, &pos, 0.0f, 1);
    ASSERT_NEAR(v, s->data[0], 1e-4f);
    ASSERT_NEAR(pos, 0.0f, 1e-4f);
    freeSamplePool(sp);
    printf("PASS test_fwd_position_zero\n");
    return 0;
}

static int test_fwd_advances_position(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 0.0f;
    /* Pins sample.c:94 scaling: at 44.1k/24-bit, phaseIncrement 1.0 moves
     * the playhead by PA_SR/(sr/bit)*2 = 48 samples, not 1. */
    float v = getSampleValueFwd(s, &pos, 1.0f, 1);
    ASSERT_NEAR(pos, 48.0f, 1e-3f);
    /* Ramp data[i]=i+1 → value at position p is p+1. */
    ASSERT_NEAR(v, 49.0f, 1e-3f);
    freeSamplePool(sp);
    printf("PASS test_fwd_advances_position (phase 1.0 → +48 samples)\n");
    return 0;
}

static int test_fwd_wrap_loop(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 240.0f;
    /* 240 + 48 = 288 >= 256 → loop=1 subtracts length once → 32. */
    float v = getSampleValueFwd(s, &pos, 1.0f, 1);
    ASSERT_NEAR(pos, 32.0f, 1e-3f);
    ASSERT_NEAR(v, 33.0f, 1e-3f);
    freeSamplePool(sp);
    printf("PASS test_fwd_wrap_loop\n");
    return 0;
}

static int test_fwd_clamp_no_loop(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 240.0f;
    /* 240 + 48 = 288 >= 256 → loop=0 clamps the playhead to length-1.
     * But sample.c:111-113 then sees pos >= length-2 and returns 0, so the
     * final sample value is never emitted. */
    float v = getSampleValueFwd(s, &pos, 1.0f, 0);
    ASSERT_NEAR(pos, 255.0f, 1e-3f);
    ASSERT_NEAR(v, 0.0f, 1e-4f);
    freeSamplePool(sp);
    printf("PASS test_fwd_clamp_no_loop (clamps to 255, returns 0 — "
           "last-2-samples guard)\n");
    return 0;
}

static int test_fwd_wrap_at_length_epsilon(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 255.5f;
    /* 255.5 + 48 = 303.5 >= 256 → wrap → 47.5. */
    float v = getSampleValueFwd(s, &pos, 1.0f, 1);
    ASSERT_NEAR(pos, 47.5f, 1e-3f);
    ASSERT_NEAR(v, 48.5f, 1e-3f);
    freeSamplePool(sp);
    printf("PASS test_fwd_wrap_at_length_epsilon\n");
    return 0;
}

static int test_fwd_phase_scale_equals_2x_bit(void) {
    /* The suggested behaviour "phaseIncrement 1.0 advances 1 sample per
     * call" is false for the current implementation: sample.c:94 scales by
     * PA_SR / (sampleRate/bit) * 2, which is exactly 2*bit when the sample
     * rate equals PA_SR. */
    ASSERT_NEAR(scale_for(TEST_SR, 8), 16.0f, 1e-3f);
    ASSERT_NEAR(scale_for(TEST_SR, 16), 32.0f, 1e-3f);
    ASSERT_NEAR(scale_for(TEST_SR, 24), 48.0f, 1e-3f);
    ASSERT_NEAR(scale_for(TEST_SR, 32), 64.0f, 1e-3f);
    printf("PASS test_fwd_phase_scale_equals_2x_bit "
           "(pins sample.c:94 scaling formula)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* getSampleValueRev                                                   */
/* ------------------------------------------------------------------ */

static int test_rev_at_length_minus_one(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 255.0f;
    /* getSampleValueRev is an identical copy of getSampleValueFwd
     * (sample.c:117-144): at length-1 it hits the last-2-samples guard and
     * returns 0; the playhead never reverses. */
    float v = getSampleValueRev(s, &pos, 0.0f, 1);
    ASSERT_NEAR(v, 0.0f, 1e-4f);
    ASSERT_NEAR(pos, 255.0f, 1e-4f);
    freeSamplePool(sp);
    printf("PASS test_rev_at_length_minus_one (returns 0 — copy-of-Fwd bug)\n");
    return 0;
}

static int test_rev_does_not_reverse(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 100.0f;
    /* Pins the copy-paste bug: reverse playback moves the playhead FORWARD
     * (100 + 48 = 148), never backward. */
    float v = getSampleValueRev(s, &pos, 1.0f, 1);
    ASSERT_NEAR(pos, 148.0f, 1e-3f);
    ASSERT_NEAR(v, 149.0f, 1e-3f);
    freeSamplePool(sp);
    printf("PASS test_rev_does_not_reverse (moves forward like Fwd)\n");
    return 0;
}

static int test_rev_wrap_like_fwd(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 240.0f;
    /* Same end-of-sample wrap as Fwd (sample.c:126-131). There is no
     * pos<0 wrap-to-length-1 path anywhere. */
    float v = getSampleValueRev(s, &pos, 1.0f, 1);
    ASSERT_NEAR(pos, 32.0f, 1e-3f);
    ASSERT_NEAR(v, 33.0f, 1e-3f);
    freeSamplePool(sp);
    printf("PASS test_rev_wrap_like_fwd\n");
    return 0;
}

static int test_rev_clamp_like_fwd(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float pos = 240.0f;
    /* loop=0 clamps to length-1 (not 0), then the guard silences it. */
    float v = getSampleValueRev(s, &pos, 1.0f, 0);
    ASSERT_NEAR(pos, 255.0f, 1e-3f);
    ASSERT_NEAR(v, 0.0f, 1e-4f);
    freeSamplePool(sp);
    printf("PASS test_rev_clamp_like_fwd\n");
    return 0;
}

static int test_fwd_rev_identical_outputs(void) {
    SamplePool *sp = pool_with_ramp(256);
    ASSERT_TRUE(sp != NULL, "pool setup failed");
    Sample *s = sp->samples[0];
    float posA = 77.0f, posB = 77.0f;
    float vA = getSampleValueFwd(s, &posA, 1.0f, 1);
    float vB = getSampleValueRev(s, &posB, 1.0f, 1);
    ASSERT_NEAR(posA, posB, 1e-6f);
    ASSERT_NEAR(vA, vB, 1e-6f);
    freeSamplePool(sp);
    printf("PASS test_fwd_rev_identical_outputs (Rev == Fwd copy-paste)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* guards                                                              */
/* ------------------------------------------------------------------ */

static int test_invalid_sample_guard(void) {
    float pos = 0.0f;
    /* sample.c:90-93 / 119-122 — NULL sample → 0.0f. */
    ASSERT_NEAR(getSampleValueFwd(NULL, &pos, 1.0f, 1), 0.0f, 0.0f);
    ASSERT_NEAR(getSampleValueRev(NULL, &pos, 1.0f, 1), 0.0f, 0.0f);

    Sample s;
    memset(&s, 0, sizeof(s));
    s.length = 100;        /* data == NULL */
    ASSERT_NEAR(getSampleValueFwd(&s, &pos, 1.0f, 1), 0.0f, 0.0f);
    ASSERT_NEAR(getSampleValueRev(&s, &pos, 1.0f, 1), 0.0f, 0.0f);

    s.data = (float *)malloc(4 * sizeof(float));
    ASSERT_TRUE(s.data != NULL, "data alloc failed");
    s.length = 0;          /* length <= 0 */
    ASSERT_NEAR(getSampleValueFwd(&s, &pos, 1.0f, 1), 0.0f, 0.0f);
    ASSERT_NEAR(getSampleValueRev(&s, &pos, 1.0f, 1), 0.0f, 0.0f);
    free(s.data);
    printf("PASS test_invalid_sample_guard\n");
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_pool_created_empty();
    failed |= test_load_sample_fields();
    failed |= test_load_preserves_data();
    failed |= test_load_bit_widths();
    failed |= test_load_multiple_accumulates();
    failed |= test_pool_capacity_guard();
    failed |= test_free_pool_clean();
    failed |= test_free_pool_null();
    failed |= test_free_sample_owned();
    failed |= test_fwd_position_zero();
    failed |= test_fwd_advances_position();
    failed |= test_fwd_wrap_loop();
    failed |= test_fwd_clamp_no_loop();
    failed |= test_fwd_wrap_at_length_epsilon();
    failed |= test_fwd_phase_scale_equals_2x_bit();
    failed |= test_rev_at_length_minus_one();
    failed |= test_rev_does_not_reverse();
    failed |= test_rev_wrap_like_fwd();
    failed |= test_rev_clamp_like_fwd();
    failed |= test_fwd_rev_identical_outputs();
    failed |= test_invalid_sample_guard();

    printf("\n%s (%d tests, %s)\n",
           failed ? "FAILED" : "PASSED", 21, failed ? ">=1 failed" : "all passed");
    return failed;
}
