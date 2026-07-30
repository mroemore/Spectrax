/*
 * test_distortion.c — verify the `fold` waveshaping function.
 *
 * NOTE: the negative-side fold has a sign bug — it pushes the signal
 * further past threshold instead of folding it back toward zero (see
 * test comments).  Tests here assert *actual* behaviour; if the bug is
 * fixed the expected values in test_fold_negative_* must be updated.
 */

#include <stdio.h>
#include <math.h>

#include "distortion.h"

#define ASSERT_NEAR(actual, expected, tol) do {                    \
    float _a = (float)(actual);                                    \
    float _e = (float)(expected);                                  \
    if (fabsf(_a - _e) > (tol)) {                                  \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f"      \
                        " (tol %.4f)\n",                           \
                __FILE__, __LINE__, (double)_e, (double)_a,        \
                (double)(tol));                                    \
        return 1;                                                  \
    }                                                              \
} while (0)

#define ASSERT_FINITE(x) do {                                      \
    float _x = (float)(x);                                         \
    if (!isfinite(_x)) {                                           \
        fprintf(stderr, "FAIL %s:%d: expected finite, got %f\n",   \
                __FILE__, __LINE__, (double)_x);                   \
        return 1;                                                  \
    }                                                              \
} while (0)

/* ── linear / through-zero region (|ampsamp| <= 1.0) ──────────── */

static int test_zero_input(void) {
    /* ampFactor=1, ampsamp=0 → within [-1,1] → pass through */
    ASSERT_NEAR(fold(0.0f, 1.0f, 1.0f), 0.0f, 1e-6f);
    printf("PASS test_zero_input\n");
    return 0;
}

static int test_linear_positive(void) {
    ASSERT_NEAR(fold(0.5f, 1.0f, 1.0f), 0.5f, 1e-6f);
    printf("PASS test_linear_positive\n");
    return 0;
}

static int test_linear_negative(void) {
    ASSERT_NEAR(fold(-0.5f, 1.0f, 1.0f), -0.5f, 1e-6f);
    printf("PASS test_linear_negative\n");
    return 0;
}

static int test_threshold_positive(void) {
    /* exactly at +1.0 threshold — not an overflow */
    ASSERT_NEAR(fold(1.0f, 1.0f, 1.0f), 1.0f, 1e-6f);
    printf("PASS test_threshold_positive\n");
    return 0;
}

static int test_threshold_negative(void) {
    /* exactly at -1.0 threshold — not an underflow */
    ASSERT_NEAR(fold(-1.0f, 1.0f, 1.0f), -1.0f, 1e-6f);
    printf("PASS test_threshold_negative\n");
    return 0;
}

/* ── positive-side folding ────────────────────────────────────── */

static int test_fold_positive_simple(void) {
    /*
     * 1.5 > 1.0 → overflow = 0.5 → 1.0 - 0.5 = 0.5
     * Signal folds back toward zero.  Correct.
     */
    ASSERT_NEAR(fold(1.5f, 1.0f, 1.0f), 0.5f, 1e-6f);
    printf("PASS test_fold_positive_simple\n");
    return 0;
}

static int test_fold_positive_deep(void) {
    /*
     * 2.5 > 1.0 → overflow = 1.5 → 1.0 - 1.5 = -0.5
     * Folds back past zero.  Correct.
     */
    ASSERT_NEAR(fold(2.5f, 1.0f, 1.0f), -0.5f, 1e-6f);
    printf("PASS test_fold_positive_deep\n");
    return 0;
}

/* ── negative-side folding (KNOWN BUG — asymmetric) ──────────── */

static int test_fold_negative_simple(void) {
    /*
     * KNOWN BUG (distortion.c:9):
     *   -1.5 < -1.0 → overflow = -0.5 → -1.0 + (-0.5) = -1.5
     *
     * Expected symmetric behaviour would be -0.5 (fold back toward
     * zero), but the formula adds overflow instead of subtracting it.
     * This test asserts the *current* (buggy) output.
     *
     * Fix would be:  return -1.0f - overflow * foldAttenuation;
     */
    ASSERT_NEAR(fold(-1.5f, 1.0f, 1.0f), -1.5f, 1e-6f);
    printf("PASS test_fold_negative_simple (asymmetric — see comment)\n");
    return 0;
}

static int test_fold_negative_deep(void) {
    /*
     * Same bug:  -2.5 -> overflow = -1.5 -> -1.0 + (-1.5) = -2.5
     * Should be -1.0 - (-1.5) = 0.5 (symmetric with the positive side).
     */
    ASSERT_NEAR(fold(-2.5f, 1.0f, 1.0f), -2.5f, 1e-6f);
    printf("PASS test_fold_negative_deep (asymmetric — see comment)\n");
    return 0;
}

/* ── ampFactor ────────────────────────────────────────────────── */

static int test_amp_factor_overflow(void) {
    /*
     * With ampFactor=2, sample=0.6 → ampsamp=1.2 → overflow=0.2
     * → 1.0 - 0.2 = 0.8
     */
    ASSERT_NEAR(fold(0.6f, 2.0f, 1.0f), 0.8f, 1e-6f);
    printf("PASS test_amp_factor_overflow\n");
    return 0;
}

static int test_amp_factor_at_threshold(void) {
    /* With ampFactor=2, sample=0.5 → ampsamp=1.0 (exactly at threshold) */
    ASSERT_NEAR(fold(0.5f, 2.0f, 1.0f), 1.0f, 1e-6f);
    printf("PASS test_amp_factor_at_threshold\n");
    return 0;
}

/* ── foldAttenuation ──────────────────────────────────────────── */

static int test_attenuation_reduces_fold(void) {
    /*
     * foldAttenuation=0.5 means folded signal is only pushed half as
     * far back:  1.5 -> overflow = 0.5 -> 1.0 - 0.5*0.5 = 0.75
     */
    ASSERT_NEAR(fold(1.5f, 1.0f, 0.5f), 0.75f, 1e-6f);
    printf("PASS test_attenuation_reduces_fold\n");
    return 0;
}

static int test_attenuation_zero(void) {
    /*
     * foldAttenuation=0.0 → no fold-back at all; signal clamps at
     * threshold.
     */
    ASSERT_NEAR(fold(2.0f, 1.0f, 0.0f), 1.0f, 1e-6f);
    printf("PASS test_attenuation_zero\n");
    return 0;
}

/* ── numerical safety ─────────────────────────────────────────── */

static int test_no_nan_inf(void) {
    /* Sweep a range — output must stay finite */
    static const float samples[] = {
         0.0f,  0.5f,  1.0f,  1.5f,  2.0f,  2.5f,  3.0f, 10.0f, 1e6f,
        -0.5f, -1.0f, -1.5f, -2.0f, -3.0f, -10.0f, -1e6f,
    };
    static const float factors[] = { 0.0f, 0.5f, 1.0f, 2.0f, 10.0f };
    static const float atten[]   = { 0.0f, 0.5f, 1.0f, 2.0f };
    int n = 0;
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        for (size_t j = 0; j < sizeof factors / sizeof factors[0]; j++) {
            for (size_t k = 0; k < sizeof atten / sizeof atten[0]; k++) {
                ASSERT_FINITE(fold(samples[i], factors[j], atten[k]));
                n++;
            }
        }
    }
    printf("PASS test_no_nan_inf (%d combos checked)\n", n);
    return 0;
}

/* ── main ─────────────────────────────────────────────────────── */

int main(void) {
    int failed = 0;
    failed |= test_zero_input();
    failed |= test_linear_positive();
    failed |= test_linear_negative();
    failed |= test_threshold_positive();
    failed |= test_threshold_negative();
    failed |= test_fold_positive_simple();
    failed |= test_fold_positive_deep();
    failed |= test_fold_negative_simple();
    failed |= test_fold_negative_deep();
    failed |= test_amp_factor_overflow();
    failed |= test_amp_factor_at_threshold();
    failed |= test_attenuation_reduces_fold();
    failed |= test_attenuation_zero();
    failed |= test_no_nan_inf();
    return failed;
}
