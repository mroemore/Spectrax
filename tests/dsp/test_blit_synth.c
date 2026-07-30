/*
 * test_blit_synth.c — verify poly_blep, noblep_sine, and BLEP waveform generators.
 *
 * Pre-existing issues found:
 *  - src/blit_synth.c: lines 32, 35 — blep_tri poly_blep corrections are
 *    commented out, so blep_tri outputs a naive triangle wave, not band-limited.
 *  - init_blit() and blit_synth() are declared in blit_synth.h but have no
 *    implementation anywhere in this branch — tests for them cannot link.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "blit_synth.h"

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.6f, got %.6f (tol %.6f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        fprintf(stderr, "FAIL %s:%d: expected false\n", __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* ---------------------------------------------------------------------------
 * poly_blep
 *   dt = phaseIncrement / TWOPI
 *   branch 1: t < dt  → return t + t - t*t - 1.0   (t = t/dt)
 *   branch 2: t > 1-dt → return t*t + t + t + 1.0   (t = (t-1)/dt)
 *   else:               → return 0.0
 * -------------------------------------------------------------------------*/

static int test_poly_blep_mid_t(void)
{
    /* At mid-t with tiny dt, neither branch triggers — returns 0. */
    float r = poly_blep(0.01f, 0.5f);
    ASSERT_NEAR(r, 0.0f, 0.0001f);
    printf("PASS test_poly_blep_mid_t\n");
    return 0;
}

static int test_poly_blep_at_discontinuity(void)
{
    /* At t = 0 exactly (the discontinuity), t < dt triggers, t /= dt = 0,
     * return = 0 + 0 - 0 - 1 = -1.  This is the peak correction value —
     * the integrator that applies the BLEP correction will ramp it in. */
    float r = poly_blep(0.5f, 0.0f);
    ASSERT_NEAR(r, -1.0f, 0.0001f);
    printf("PASS test_poly_blep_at_discontinuity\n");
    return 0;
}

static int test_poly_blep_at_one(void)
{
    /* At t = 1 exactly, t > 1-dt triggers, t = (1-1)/dt = 0,
     * return = 0 + 0 + 0 + 1 = 1. */
    float r = poly_blep(0.5f, 1.0f);
    ASSERT_NEAR(r, 1.0f, 0.0001f);
    printf("PASS test_poly_blep_at_one\n");
    return 0;
}

static int test_poly_blep_small_t(void)
{
    /* t = 0.01, dt ≈ 0.07958, t < dt triggers, t /= dt ≈ 0.12566,
     * return = 0.12566 + 0.12566 - 0.01579 - 1.0 ≈ -0.76447 */
    float r = poly_blep(0.5f, 0.01f);
    ASSERT_NEAR(r, -0.764465f, 0.001f);
    printf("PASS test_poly_blep_small_t\n");
    return 0;
}

static int test_poly_blep_near_one(void)
{
    /* t = 0.99, dt ≈ 0.07958, t > 1-dt triggers,
     * t = (0.99-1)/dt ≈ -0.12566, return ≈ 0.76447 */
    float r = poly_blep(0.5f, 0.99f);
    ASSERT_NEAR(r, 0.764465f, 0.001f);
    printf("PASS test_poly_blep_near_one\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * noblep_sine — sin(TWOPI * phase)
 * -------------------------------------------------------------------------*/

static int test_noblep_sine_zero(void)
{
    float r = noblep_sine(0.0f);
    ASSERT_NEAR(r, 0.0f, 0.0001f);
    printf("PASS test_noblep_sine_zero\n");
    return 0;
}

static int test_noblep_sine_quarter(void)
{
    /* sin(TWOPI * M_PI/2) = sin(pi^2) ≈ -0.4303 */
    float r = noblep_sine((float)(M_PI / 2.0));
    ASSERT_NEAR(r, -0.4301f, 0.001f);
    printf("PASS test_noblep_sine_quarter\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * blep_square — naive square with poly_blep correction
 *   At phase=0: value=1, value -= poly_blep(inc, 0) = 1 - (-1) = 2
 * -------------------------------------------------------------------------*/

static int test_blep_square_zero(void)
{
    float r = blep_square(0.0f, 0.01f);
    ASSERT_NEAR(r, 2.0f, 0.0001f);
    printf("PASS test_blep_square_zero\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * blep_saw — naive saw with poly_blep correction
 *   At phase=0: value = -1, value -= poly_blep(inc, 0) = -1 - (-1) = 0
 * -------------------------------------------------------------------------*/

static int test_blep_saw_zero(void)
{
    float r = blep_saw(0.0f, 0.01f);
    ASSERT_NEAR(r, 0.0f, 0.0001f);
    printf("PASS test_blep_saw_zero\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * blep_tri — naive triangle (BLEP corrections commented out)
 *   At phase=0: value = ttwo = 0, value -= 1 = -1
 *   Pre-existing issue: BLEP is disabled, output is raw naive waveform.
 * -------------------------------------------------------------------------*/

static int test_blep_tri_zero(void)
{
    float r = blep_tri(0.0f, 0.01f);
    ASSERT_NEAR(r, -1.0f, 0.0001f);
    printf("PASS test_blep_tri_zero\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * init_blit / blit_synth — NOT IMPLEMENTED
 *
 * Both functions are declared in blit_synth.h but have no definition in
 * src/blit_synth.c or anywhere else in this branch.  The Makefile target
 * test_blit_synth links only src_blit_synth.o, so calling them here would
 * cause a linker error.  Tests are omitted until an implementation exists.
 * -------------------------------------------------------------------------*/

int main(void)
{
    int failed = 0;
    failed |= test_poly_blep_mid_t();
    failed |= test_poly_blep_at_discontinuity();
    failed |= test_poly_blep_at_one();
    failed |= test_poly_blep_small_t();
    failed |= test_poly_blep_near_one();
    failed |= test_noblep_sine_zero();
    failed |= test_noblep_sine_quarter();
    failed |= test_blep_square_zero();
    failed |= test_blep_saw_zero();
    failed |= test_blep_tri_zero();
    return failed;
}
