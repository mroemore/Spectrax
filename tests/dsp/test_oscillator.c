/*
 * test_oscillator.c — verify the basic oscillator waveform functions.
 *
 * Note: band_limited_sawtooth() and band_limited_square() are declared in
 * oscillator.h but NOT implemented in oscillator.c.  Any test that calls them
 * fails at link time — this is a pre-existing bug.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "oscillator.h"

#define ASSERT_NEAR(actual, expected, tol) do { \
	float _a = (float)(actual); \
	float _e = (float)(expected); \
	if (fabsf(_a - _e) > (tol)) { \
		fprintf(stderr, "FAIL %s:%d: expected %.6f, got %.6f (tol %.6f)\n", \
		        __FILE__, __LINE__, _e, _a, (float)(tol)); \
		return 1; \
	} \
} while (0)

#define ASSERT_EQ(actual, expected) do { \
	float _a = (float)(actual); \
	float _e = (float)(expected); \
	if (_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: expected %.6f, got %.6f\n", \
		        __FILE__, __LINE__, _e, _a); \
		return 1; \
	} \
} while (0)

/* ---- sawtooth_wave ---------------------------------------------------- */

static int test_sawtooth_wave_at_phase_zero(void) {
	/* sawtooth(0) = 2*0 - 1 = -1 */
	ASSERT_EQ(sawtooth_wave(0.0f), -1.0f);
	printf("PASS test_sawtooth_wave_at_phase_zero\n");
	return 0;
}

static int test_sawtooth_wave_at_mid_phase(void) {
	/* sawtooth(0.5) = 2*0.5 - 1 = 0 */
	ASSERT_EQ(sawtooth_wave(0.5f), 0.0f);
	printf("PASS test_sawtooth_wave_at_mid_phase\n");
	return 0;
}

static int test_sawtooth_wave_at_phase_one(void) {
	/* sawtooth(1.0) = 2*1.0 - 1 = 1 */
	ASSERT_EQ(sawtooth_wave(1.0f), 1.0f);
	printf("PASS test_sawtooth_wave_at_phase_one\n");
	return 0;
}

static int test_sawtooth_wave_monotonic_rise(void) {
	/* sawtooth is strictly increasing over [0, 1) */
	float v0 = sawtooth_wave(0.1f);
	float v1 = sawtooth_wave(0.5f);
	float v2 = sawtooth_wave(0.9f);
	if (!(v0 < v1 && v1 < v2)) {
		fprintf(stderr, "FAIL %s:%d: sawtooth not monotonic: "
		        "v(0.1)=%.6f v(0.5)=%.6f v(0.9)=%.6f\n",
		        __FILE__, __LINE__, v0, v1, v2);
		return 1;
	}
	printf("PASS test_sawtooth_wave_monotonic_rise\n");
	return 0;
}

/* ---- sine_wave -------------------------------------------------------- */

static int test_sine_wave_phase_zero_no_mod(void) {
	/* sin(0) = 0 */
	ASSERT_NEAR(sine_wave(0.0f, 0.0f), 0.0f, 1e-6f);
	printf("PASS test_sine_wave_phase_zero_no_mod\n");
	return 0;
}

static int test_sine_wave_quarter_phase(void) {
	/* sin(pi/2) = 1 */
	ASSERT_NEAR(sine_wave(0.25f, 0.0f), 1.0f, 1e-6f);
	printf("PASS test_sine_wave_quarter_phase\n");
	return 0;
}

static int test_sine_wave_half_phase(void) {
	/* sin(pi) = 0 */
	ASSERT_NEAR(sine_wave(0.5f, 0.0f), 0.0f, 1e-6f);
	printf("PASS test_sine_wave_half_phase\n");
	return 0;
}

static int test_sine_wave_three_quarter_phase(void) {
	/* sin(3pi/2) = -1 */
	ASSERT_NEAR(sine_wave(0.75f, 0.0f), -1.0f, 1e-6f);
	printf("PASS test_sine_wave_three_quarter_phase\n");
	return 0;
}

static int test_sine_wave_with_mod_shift(void) {
	/* sin(TWO_PI * (0 + 0.25)) = sin(pi/2) = 1 */
	ASSERT_NEAR(sine_wave(0.0f, 0.25f), 1.0f, 1e-6f);
	printf("PASS test_sine_wave_with_mod_shift\n");
	return 0;
}

static int test_sine_wave_mod_cumulative(void) {
	/* mod shifts phase: sin(TWO_PI * (0.5 + 0.25)) = sin(3pi/2) = -1 */
	ASSERT_NEAR(sine_wave(0.5f, 0.25f), -1.0f, 1e-6f);
	printf("PASS test_sine_wave_mod_cumulative\n");
	return 0;
}

/* ---- square_wave ------------------------------------------------------ */

static int test_square_wave_at_phase_zero(void) {
	/* 0 < 0.5 -> 1 */
	ASSERT_EQ(square_wave(0.0f), 1.0f);
	printf("PASS test_square_wave_at_phase_zero\n");
	return 0;
}

static int test_square_wave_before_midpoint(void) {
	/* 0.25 < 0.5 -> 1 */
	ASSERT_EQ(square_wave(0.25f), 1.0f);
	printf("PASS test_square_wave_before_midpoint\n");
	return 0;
}

static int test_square_wave_at_midpoint(void) {
	/* 0.5 < 0.5 -> false -> -1 */
	ASSERT_EQ(square_wave(0.5f), -1.0f);
	printf("PASS test_square_wave_at_midpoint\n");
	return 0;
}

static int test_square_wave_after_midpoint(void) {
	/* 0.75 < 0.5 -> false -> -1 */
	ASSERT_EQ(square_wave(0.75f), -1.0f);
	printf("PASS test_square_wave_after_midpoint\n");
	return 0;
}

static int test_square_wave_duty_cycle_symmetry(void) {
	/* The square wave should be high for exactly half the cycle.
	 * At 0.0-ε it's high, at 0.5 it's low.
	 * Check symmetry by verifying changes sign precisely at 0.5. */
	ASSERT_EQ(square_wave(0.5f - 1e-7f), 1.0f);
	ASSERT_EQ(square_wave(0.5f), -1.0f);
	printf("PASS test_square_wave_duty_cycle_symmetry\n");
	return 0;
}

/* ---- main ------------------------------------------------------------- */

int main(void) {
	int failed = 0;
	failed |= test_sawtooth_wave_at_phase_zero();
	failed |= test_sawtooth_wave_at_mid_phase();
	failed |= test_sawtooth_wave_at_phase_one();
	failed |= test_sawtooth_wave_monotonic_rise();
	failed |= test_sine_wave_phase_zero_no_mod();
	failed |= test_sine_wave_quarter_phase();
	failed |= test_sine_wave_half_phase();
	failed |= test_sine_wave_three_quarter_phase();
	failed |= test_sine_wave_with_mod_shift();
	failed |= test_sine_wave_mod_cumulative();
	failed |= test_square_wave_at_phase_zero();
	failed |= test_square_wave_before_midpoint();
	failed |= test_square_wave_at_midpoint();
	failed |= test_square_wave_after_midpoint();
	failed |= test_square_wave_duty_cycle_symmetry();
	if (failed) {
		printf("\nSome tests FAILED.\n");
	} else {
		printf("\nAll tests PASSED.\n");
	}
	return failed;
}
