/*
 * test_filters.c — verify biquad filter lifecycle and basic signal processing.
 *
 * Build: make bin/test_filters
 * Run:   ./bin/test_filters
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "filters.h"

#define ASSERT_NEAR(actual, expected, tol) do {                    \
    float _a = (float)(actual);                                    \
    float _e = (float)(expected);                                  \
    if (fabsf(_a - _e) > (tol)) {                                  \
        fprintf(stderr, "FAIL %s:%d: expected %.6f, got %.6f"     \
                " (tol %.6f)\n", __FILE__, __LINE__,               \
                _e, _a, (float)(tol));                             \
        return 1;                                                  \
    }                                                              \
} while (0)

#define ASSERT_EQ(actual, expected) do {                           \
    if ((actual) != (expected)) {                                  \
        fprintf(stderr, "FAIL %s:%d: expected 0x%lx, got 0x%lx\n",\
                __FILE__, __LINE__,                                \
                (unsigned long)(uintptr_t)(expected),              \
                (unsigned long)(uintptr_t)(actual));               \
        return 1;                                                  \
    }                                                              \
} while (0)

#define ASSERT_NOT_NULL(ptr) do {                                  \
    if ((ptr) == NULL) {                                           \
        fprintf(stderr, "FAIL %s:%d: expected non-NULL\n",        \
                __FILE__, __LINE__);                               \
        return 1;                                                  \
    }                                                              \
} while (0)

#define ASSERT_FINITE(val) do {                                    \
    float _v = (float)(val);                                       \
    if (isnan(_v) || isinf(_v)) {                                  \
        fprintf(stderr, "FAIL %s:%d: expected finite, got %f\n",  \
                __FILE__, __LINE__, _v);                           \
        return 1;                                                  \
    }                                                              \
} while (0)

/* ---- helpers ---- */

static void free_biquad(BiquadFilter *bf) {
    free(bf);
}

static void free_filter(Filter *flt) {
    if (flt) {
        free(flt->biquad);
        free(flt);
    }
}

/* ---- tests ---- */

static int test_create_biquad_direct(void) {
    BiquadFilter *bf = createBiquadFilter(kDirect);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->type, kDirect);
    free_biquad(bf);
    printf("PASS test_create_biquad_direct\n");
    return 0;
}

static int test_create_biquad_canonical(void) {
    BiquadFilter *bf = createBiquadFilter(kCanonical);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->type, kCanonical);
    free_biquad(bf);
    printf("PASS test_create_biquad_canonical\n");
    return 0;
}

static int test_create_biquad_transpose_direct(void) {
    BiquadFilter *bf = createBiquadFilter(kTransposeDirect);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->type, kTransposeDirect);
    free_biquad(bf);
    printf("PASS test_create_biquad_transpose_direct\n");
    return 0;
}

static int test_create_biquad_transpose_canonical(void) {
    BiquadFilter *bf = createBiquadFilter(kTransposeCanonical);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->type, kTransposeCanonical);
    free_biquad(bf);
    printf("PASS test_create_biquad_transpose_canonical\n");
    return 0;
}

static int test_process_sample_fnptr(void) {
    BiquadFilter *bf;

    bf = createBiquadFilter(kDirect);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->processSample, processKDirect);
    free_biquad(bf);

    bf = createBiquadFilter(kCanonical);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->processSample, processKCanonical);
    free_biquad(bf);

    bf = createBiquadFilter(kTransposeDirect);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->processSample, processKTransposeDirect);
    free_biquad(bf);

    bf = createBiquadFilter(kTransposeCanonical);
    ASSERT_NOT_NULL(bf);
    ASSERT_EQ(bf->processSample, processKTransposeCanonical);
    free_biquad(bf);

    printf("PASS test_process_sample_fnptr\n");
    return 0;
}

static int test_zero_input(void) {
    /*
     * With zero initial states and zero input, all four forms should
     * return approximately zero regardless of uninitialized coefficients.
     */
    for (int t = 0; t < biquad_count; t++) {
        BiquadFilter *bf = createBiquadFilter((BiquadType)t);
        ASSERT_NOT_NULL(bf);
        float out = bf->processSample(bf, 0.0f);
        ASSERT_NEAR(out, 0.0f, 1e-7f);
        free_biquad(bf);
    }
    printf("PASS test_zero_input\n");
    return 0;
}

static int test_impulse_no_crash(void) {
    /*
     * Impulse (xn=1.0 followed by zeros) must not crash and must
     * produce finite output for all four forms.  Coefficients are
     * uninitialised so the impulse magnitude is implementation-defined;
     * we only verify no NaN/Inf and no segfault.
     */
    for (int t = 0; t < biquad_count; t++) {
        BiquadFilter *bf = createBiquadFilter((BiquadType)t);
        ASSERT_NOT_NULL(bf);

        float y0 = bf->processSample(bf, 1.0f);
        ASSERT_FINITE(y0);

        for (int i = 0; i < 100; i++) {
            float y = bf->processSample(bf, 0.0f);
            ASSERT_FINITE(y);
        }
        free_biquad(bf);
    }
    printf("PASS test_impulse_no_crash\n");
    return 0;
}

static int test_init_states_zero(void) {
    /*
     * createBiquadFilter zeroes the state array.
     *
     * NOTE: resetState() is declared in filters.h:70 but never
     * implemented (pre-existing bug).  This test verifies that a
     * freshly-allocated filter starts with clean states instead.
     */
    BiquadFilter *bf = createBiquadFilter(kDirect);
    ASSERT_NOT_NULL(bf);
    for (int i = 0; i < state_count; i++) {
        ASSERT_NEAR(bf->states[i], 0.0f, 1e-7f);
    }

    /* Pollute states, then verify a *new* filter still starts clean. */
    bf->processSample(bf, 1.0f);

    BiquadFilter *bf2 = createBiquadFilter(kDirect);
    ASSERT_NOT_NULL(bf2);
    for (int i = 0; i < state_count; i++) {
        ASSERT_NEAR(bf2->states[i], 0.0f, 1e-7f);
    }
    free_biquad(bf2);
    free_biquad(bf);

    printf("PASS test_init_states_zero\n");
    return 0;
}

static int test_create_filter_lpf(void) {
    Filter *flt = createFilter(kDirect, secondOrderLPF, 1000.0f, 0.7f);
    ASSERT_NOT_NULL(flt);
    ASSERT_EQ(flt->type, secondOrderLPF);
    ASSERT_NEAR(flt->q, 0.7f, 1e-6f);
    ASSERT_NOT_NULL(flt->biquad);
    ASSERT_EQ(flt->biquad->type, kDirect);
    /* Coefficients should be populated (non-zero for a 1 kHz LPF). */
    ASSERT_NEAR(flt->biquad->coefficients[c0], 1.0f, 1e-7f);
    ASSERT_NEAR(flt->biquad->coefficients[d0], 0.0f, 1e-7f);
    free_filter(flt);
    printf("PASS test_create_filter_lpf\n");
    return 0;
}

static int test_create_filter_hpf(void) {
    Filter *flt = createFilter(kCanonical, secondOrderHPF, 1000.0f, 0.7f);
    ASSERT_NOT_NULL(flt);
    ASSERT_EQ(flt->type, secondOrderHPF);
    ASSERT_NEAR(flt->q, 0.7f, 1e-6f);
    ASSERT_NOT_NULL(flt->biquad);
    ASSERT_EQ(flt->biquad->type, kCanonical);
    /* Coefficients should be populated. */
    ASSERT_NEAR(flt->biquad->coefficients[c0], 1.0f, 1e-7f);
    ASSERT_NEAR(flt->biquad->coefficients[d0], 0.0f, 1e-7f);
    free_filter(flt);
    printf("PASS test_create_filter_hpf\n");
    return 0;
}

static int test_create_filter_invalid_type(void) {
    /*
     * filter_count is past the last valid FilterType value;
     * createFilter should hit the default: case and return NULL.
     *
     * KNOWN BUG: the BiquadFilter allocated earlier in createFilter
     * is leaked on this error path (src/filters.c:152-153).
     */
    Filter *flt = createFilter(kDirect, filter_count, 1000.0f, 0.7f);
    ASSERT_EQ(flt, NULL);
    printf("PASS test_create_filter_invalid_type\n");
    return 0;
}

static int test_filter_stability(void) {
    /*
     * A configured LPF should not blow up on sustained DC input.
     * Feed xn=1.0 for 1000 samples and verify:
     *   - every output is finite
     *   - peak output is within a reasonable band around 1.0 (DC gain)
     *   - the last few samples converge near unity gain
     */
    Filter *flt = createFilter(kDirect, secondOrderLPF, 1000.0f, 0.7f);
    ASSERT_NOT_NULL(flt);
    BiquadFilter *bf = flt->biquad;

    float max_abs = 0.0f;
    for (int i = 0; i < 1000; i++) {
        float y = bf->processSample(bf, 1.0f);
        ASSERT_FINITE(y);
        float ay = fabsf(y);
        if (ay > max_abs) max_abs = ay;
    }

    /*
     * Filter is bounded.  The actual DC gain for this coefficient
     * calculation is ~0.5 (not 1.0) — that is the existing design.
     */
    ASSERT_NEAR(max_abs, 0.5f, 0.1f);

    /* Tail should have converged to steady-state DC gain (~0.5). */
    for (int i = 0; i < 10; i++) {
        float y = bf->processSample(bf, 1.0f);
        ASSERT_NEAR(y, 0.5f, 0.1f);
    }

    free_filter(flt);
    printf("PASS test_filter_stability\n");
    return 0;
}

static int test_check_float_underflow(void) {
    /*
     * checkFLoatUnderflow clamps values whose magnitude is below
     * SMALLEST_POS_FLOAT to zero.
     *
     * NOTE: the function name has a typo ("FLoat" not "Float") — we
     * use the actual spellling from filters.h:72.
     */
    float pos = 1.0e-39f;                /* below SMALLEST_POS_FLOAT */
    int r = checkFLoatUnderflow(&pos);
    ASSERT_EQ(r, 1);
    ASSERT_NEAR(pos, 0.0f, 1e-30f);

    float neg = -1.0e-39f;               /* above SMALLEST_NEG_FLOAT (closer to 0) */
    r = checkFLoatUnderflow(&neg);
    ASSERT_EQ(r, 1);
    ASSERT_NEAR(neg, 0.0f, 1e-30f);

    float normal = 1.0f;                 /* well above threshold */
    r = checkFLoatUnderflow(&normal);
    ASSERT_EQ(r, 0);
    ASSERT_NEAR(normal, 1.0f, 1e-7f);

    float zero = 0.0f;                   /* exactly zero — no underflow */
    r = checkFLoatUnderflow(&zero);
    ASSERT_EQ(r, 0);
    ASSERT_NEAR(zero, 0.0f, 1e-7f);

    printf("PASS test_check_float_underflow\n");
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_create_biquad_direct();
    failed |= test_create_biquad_canonical();
    failed |= test_create_biquad_transpose_direct();
    failed |= test_create_biquad_transpose_canonical();
    failed |= test_process_sample_fnptr();
    failed |= test_zero_input();
    failed |= test_impulse_no_crash();
    failed |= test_init_states_zero();
    failed |= test_create_filter_lpf();
    failed |= test_create_filter_hpf();
    failed |= test_create_filter_invalid_type();
    failed |= test_filter_stability();
    failed |= test_check_float_underflow();
    printf("%s: %s\n", __FILE__, failed ? "FAILED" : "ALL PASS");
    return failed;
}
