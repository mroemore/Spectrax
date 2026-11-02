/* test_sampler_character.c -- the sampler character engine.
 *
 * Task 1: the quantizer laws.
 *
 * A sampler's character is a chain, not a bit depth: degrade once at record
 * time (rate -> bits -> coding law), then pitch at playback. The coding law
 * is the "bits" stage, and it is what puts the grit in.
 *
 * The laws are the ones the research corpus names:
 *   - truncation, not dither, is the vintage quantizer error profile
 *     (signal-correlated harmonics, -Delta/2 DC bias);
 *   - TPDF dither is the "clean 8-bit" alternative: more noise, no
 *     distortion;
 *   - 8-bit mu-law (G.711) is what the Emu II and the Linn LM-1 used;
 *   - the MPC60's "12-bit proprietary NON-LINEAR" storage has no public
 *     curve, so the 12-bit companding law here is an approximation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sampler_character.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

/* The linear laws keep the plan's bounds exactly. The non-linear laws are
 * bounded by their own grids' top steps instead -- the plan's bounds for them
 * were written as if the laws were linear. */
#define TPDF_MAX_ERROR_STEPS 1.5f
#define ULAW_MAX_ABS_ERROR (1.0f / 32.0f)
#define COMPAND12_MAX_ABS_ERROR (1.0f / 256.0f)

static int test_quant_off_passthrough(void) {
	const float xs[] = {-1.0f, 0.0f, 0.503f, 1.0f};
	for(size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
		float y = samplerQuantize(xs[i], 8, SQL_OFF);
		if(y != xs[i]) {
			printf("FAIL: SQL_OFF(%g) = %.9g\n", (double)xs[i], (double)y);
			return 1;
		}
	}
	printf("PASS test_quant_off_passthrough\n");
	return 0;
}

static int test_quant_trunc_floor(void) {
	/* 8 bits over [-1,1] is a step of 1/128. Truncation floors toward
	 * negative infinity, so -0.503 lands on -65/128. */
	float y = samplerQuantize(0.503f, 8, SQL_TRUNC);
	if(y != 0.5f) {
		printf("FAIL: SQL_TRUNC(0.503) = %.9g, want 0.5\n", (double)y);
		return 1;
	}
	y = samplerQuantize(-0.503f, 8, SQL_TRUNC);
	if(y != -65.0f / 128.0f) {
		printf("FAIL: SQL_TRUNC(-0.503) = %.9g, want %g\n", (double)y, -65.0 / 128.0);
		return 1;
	}
	/* The error always lies in [-Delta, 0]: truncation floors, so it can
	 * only undershoot, and at a grid point it undershoots by nothing. */
	float delta = 2.0f / 256.0f;
	for(int i = 0; i < 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 999.0f;
		float e = samplerQuantize(x, 8, SQL_TRUNC) - x;
		if(e < -delta || e > 0.0f) {
			printf("FAIL: SQL_TRUNC error %g out of [-%g, 0] at x=%g\n",
			       (double)e, (double)delta, (double)x);
			return 1;
		}
	}
	printf("PASS test_quant_trunc_floor\n");
	return 0;
}

static int test_quant_round_bounds(void) {
	float delta = 2.0f / 256.0f;
	for(int i = 0; i < 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 999.0f;
		float e = samplerQuantize(x, 8, SQL_ROUND) - x;
		if(e < -delta * 0.5f || e > delta * 0.5f) {
			printf("FAIL: SQL_ROUND error %g out of [-%g, %g] at x=%g\n",
			       (double)e, (double)(delta * 0.5f), (double)(delta * 0.5f), (double)x);
			return 1;
		}
	}
	printf("PASS test_quant_round_bounds\n");
	return 0;
}

static int test_quant_tpdf_bounded(void) {
	/* The TPDF dither spans a whole step, so the error can reach 1.5 steps:
	 * the dither moves the input by up to one step and the rounding adds
	 * half. It is the decorrelation that matters, and that is what the
	 * variance comparison pins. */
	const int N = 10000;
	float delta = 2.0f / 256.0f;
	double sumT = 0.0, sumT2 = 0.0, sumR = 0.0, sumR2 = 0.0;
	for(int i = 0; i < N; i++) {
		float x = 0.9f * sinf(2.0f * 3.14159265358979f * 997.0f * (float)i / 48000.0f);
		float et = samplerQuantize(x, 8, SQL_TPDF) - x;
		float er = samplerQuantize(x, 8, SQL_TRUNC) - x;
		if(fabsf(et) > TPDF_MAX_ERROR_STEPS * delta) {
			printf("FAIL: SQL_TPDF error %g exceeds %g steps\n",
			       (double)et, (double)(TPDF_MAX_ERROR_STEPS * delta));
			return 1;
		}
		sumT += (double)et;
		sumT2 += (double)et * (double)et;
		sumR += (double)er;
		sumR2 += (double)er * (double)er;
	}
	double varT = sumT2 / N - (sumT / N) * (sumT / N);
	double varR = sumR2 / N - (sumR / N) * (sumR / N);
	if(!(varT >= varR * 2.0)) {
		printf("FAIL: SQL_TPDF variance %g is not twice SQL_TRUNC's %g\n", varT, varR);
		return 1;
	}
	printf("PASS test_quant_tpdf_bounded\n");
	return 0;
}

static int test_quant_ulaw_roundtrip(void) {
	if(samplerQuantize(0.0f, 8, SQL_ULAW) != 0.0f) {
		printf("FAIL: SQL_ULAW(0) is not exactly zero\n");
		return 1;
	}
	/* Monotonic across the range. */
	float prev = -2.0f;
	for(int i = 0; i <= 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 1000.0f;
		float y = samplerQuantize(x, 8, SQL_ULAW);
		if(y < prev) {
			printf("FAIL: SQL_ULAW is not monotonic at x=%g (%g < %g)\n",
			       (double)x, (double)y, (double)prev);
			return 1;
		}
		prev = y;
	}
	/* Bounded by the grid's top step. */
	for(int i = 0; i <= 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 1000.0f;
		float e = fabsf(samplerQuantize(x, 8, SQL_ULAW) - x);
		if(e > ULAW_MAX_ABS_ERROR) {
			printf("FAIL: SQL_ULAW error %g exceeds %g at x=%g\n",
			       (double)e, (double)ULAW_MAX_ABS_ERROR, (double)x);
			return 1;
		}
	}
	/* The companding property: the absolute error at a low level is much
	 * smaller than at a high level. That is what mu-law buys: a lower
	 * noise floor, not a better relative error down low. */
	float lowErr = fabsf(samplerQuantize(0.001f, 8, SQL_ULAW) - 0.001f);
	float highErr = fabsf(samplerQuantize(0.9f, 8, SQL_ULAW) - 0.9f);
	if(!(lowErr < highErr)) {
		printf("FAIL: SQL_ULAW error at 0.001 (%g) is not below the one at 0.9 (%g)\n",
		       (double)lowErr, (double)highErr);
		return 1;
	}
	printf("PASS test_quant_ulaw_roundtrip\n");
	return 0;
}

static int test_quant_compand12(void) {
	/* Bounded by the grid's top step. */
	for(int i = 0; i <= 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 1000.0f;
		float e = fabsf(samplerQuantize(x, 12, SQL_COMPAND12) - x);
		if(e > COMPAND12_MAX_ABS_ERROR) {
			printf("FAIL: SQL_COMPAND12 error %g exceeds %g at x=%g\n",
			       (double)e, (double)COMPAND12_MAX_ABS_ERROR, (double)x);
			return 1;
		}
	}
	float lowErr = fabsf(samplerQuantize(0.01f, 12, SQL_COMPAND12) - 0.01f);
	float highErr = fabsf(samplerQuantize(0.9f, 12, SQL_COMPAND12) - 0.9f);
	if(!(lowErr < highErr)) {
		printf("FAIL: SQL_COMPAND12 error at 0.01 (%g) is not below the one at 0.9 (%g)\n",
		       (double)lowErr, (double)highErr);
		return 1;
	}
	printf("PASS test_quant_compand12\n");
	return 0;
}

static int test_quant_bits_clamp(void) {
	const int bad[] = {-3, 0, 25, 999};
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		for(int law = 0; law < SQL_COUNT; law++) {
			float y = samplerQuantize(0.37f, bad[i], (SamplerQuantLaw)law);
			if(!(y == y) || fabsf(y) > 1.0001f) {
				printf("FAIL: bits=%d law=%d gave %g\n", bad[i], law, (double)y);
				return 1;
			}
		}
	}
	printf("PASS test_quant_bits_clamp\n");
	return 0;
}

int main(void) {
	int fails = 0;
	fails += test_quant_off_passthrough();
	fails += test_quant_trunc_floor();
	fails += test_quant_round_bounds();
	fails += test_quant_tpdf_bounded();
	fails += test_quant_ulaw_roundtrip();
	fails += test_quant_compand12();
	fails += test_quant_bits_clamp();
	if(fails) {
		fprintf(stderr, "%d sampler-character test(s) failed\n", fails);
		return 1;
	}
	printf("ALL sampler-character tests passed\n");
	return 0;
}
