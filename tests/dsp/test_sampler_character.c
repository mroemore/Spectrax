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
#include "sampler_crossfade.h"
#include "settings.h"
#include "voice.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

/* The linear laws keep the plan's bounds exactly. The non-linear laws are
 * bounded by their own grids' top steps instead -- the plan's bounds for them
 * were written as if the laws were linear. */
#define TPDF_MAX_ERROR_STEPS 1.5f
#define ULAW_MAX_ABS_ERROR (1.0f / 32.0f)
#define COMPAND12_MAX_ABS_ERROR (1.0f / 256.0f)

static int test_quant_off_passthrough(void) {
	uint32_t dither = 0x9E3779B9u;
	const float xs[] = {-1.0f, 0.0f, 0.503f, 1.0f};
	for(size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
		float y = samplerQuantize(xs[i], 8, SQL_OFF, &dither);
		if(y != xs[i]) {
			printf("FAIL: SQL_OFF(%g) = %.9g\n", (double)xs[i], (double)y);
			return 1;
		}
	}
	printf("PASS test_quant_off_passthrough\n");
	return 0;
}

static int test_quant_trunc_floor(void) {
	uint32_t dither = 0x9E3779B9u;
	/* 8 bits over [-1,1] is a step of 1/128. Truncation floors toward
	 * negative infinity, so -0.503 lands on -65/128. */
	float y = samplerQuantize(0.503f, 8, SQL_TRUNC, &dither);
	if(y != 0.5f) {
		printf("FAIL: SQL_TRUNC(0.503) = %.9g, want 0.5\n", (double)y);
		return 1;
	}
	y = samplerQuantize(-0.503f, 8, SQL_TRUNC, &dither);
	if(y != -65.0f / 128.0f) {
		printf("FAIL: SQL_TRUNC(-0.503) = %.9g, want %g\n", (double)y, -65.0 / 128.0);
		return 1;
	}
	/* The error always lies in [-Delta, 0]: truncation floors, so it can
	 * only undershoot, and at a grid point it undershoots by nothing. */
	float delta = 2.0f / 256.0f;
	for(int i = 0; i < 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 999.0f;
		float e = samplerQuantize(x, 8, SQL_TRUNC, &dither) - x;
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
	uint32_t dither = 0x9E3779B9u;
	float delta = 2.0f / 256.0f;
	for(int i = 0; i < 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 999.0f;
		float e = samplerQuantize(x, 8, SQL_ROUND, &dither) - x;
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
	uint32_t dither = 0x9E3779B9u;
	/* The TPDF dither spans a whole step, so the error can reach 1.5 steps:
	 * the dither moves the input by up to one step and the rounding adds
	 * half. It is the decorrelation that matters, and that is what the
	 * variance comparison pins. */
	const int N = 10000;
	float delta = 2.0f / 256.0f;
	double sumT = 0.0, sumT2 = 0.0, sumR = 0.0, sumR2 = 0.0;
	for(int i = 0; i < N; i++) {
		float x = 0.9f * sinf(2.0f * 3.14159265358979f * 997.0f * (float)i / 48000.0f);
		float et = samplerQuantize(x, 8, SQL_TPDF, &dither) - x;
		float er = samplerQuantize(x, 8, SQL_TRUNC, &dither) - x;
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
	uint32_t dither = 0x9E3779B9u;
	if(samplerQuantize(0.0f, 8, SQL_ULAW, &dither) != 0.0f) {
		printf("FAIL: SQL_ULAW(0) is not exactly zero\n");
		return 1;
	}
	/* Monotonic across the range. */
	float prev = -2.0f;
	for(int i = 0; i <= 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 1000.0f;
		float y = samplerQuantize(x, 8, SQL_ULAW, &dither);
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
		float e = fabsf(samplerQuantize(x, 8, SQL_ULAW, &dither) - x);
		if(e > ULAW_MAX_ABS_ERROR) {
			printf("FAIL: SQL_ULAW error %g exceeds %g at x=%g\n",
			       (double)e, (double)ULAW_MAX_ABS_ERROR, (double)x);
			return 1;
		}
	}
	/* The companding property: the absolute error at a low level is much
	 * smaller than at a high level. That is what mu-law buys: a lower
	 * noise floor, not a better relative error down low. */
	float lowErr = fabsf(samplerQuantize(0.001f, 8, SQL_ULAW, &dither) - 0.001f);
	float highErr = fabsf(samplerQuantize(0.9f, 8, SQL_ULAW, &dither) - 0.9f);
	if(!(lowErr < highErr)) {
		printf("FAIL: SQL_ULAW error at 0.001 (%g) is not below the one at 0.9 (%g)\n",
		       (double)lowErr, (double)highErr);
		return 1;
	}
	printf("PASS test_quant_ulaw_roundtrip\n");
	return 0;
}

static int test_quant_compand12(void) {
	uint32_t dither = 0x9E3779B9u;
	/* Bounded by the grid's top step. */
	for(int i = 0; i <= 1000; i++) {
		float x = -1.0f + 2.0f * (float)i / 1000.0f;
		float e = fabsf(samplerQuantize(x, 12, SQL_COMPAND12, &dither) - x);
		if(e > COMPAND12_MAX_ABS_ERROR) {
			printf("FAIL: SQL_COMPAND12 error %g exceeds %g at x=%g\n",
			       (double)e, (double)COMPAND12_MAX_ABS_ERROR, (double)x);
			return 1;
		}
	}
	float lowErr = fabsf(samplerQuantize(0.01f, 12, SQL_COMPAND12, &dither) - 0.01f);
	float highErr = fabsf(samplerQuantize(0.9f, 12, SQL_COMPAND12, &dither) - 0.9f);
	if(!(lowErr < highErr)) {
		printf("FAIL: SQL_COMPAND12 error at 0.01 (%g) is not below the one at 0.9 (%g)\n",
		       (double)lowErr, (double)highErr);
		return 1;
	}
	printf("PASS test_quant_compand12\n");
	return 0;
}

static int test_quant_bits_clamp(void) {
	uint32_t dither = 0x9E3779B9u;
	const int bad[] = {-3, 0, 25, 999};
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		for(int law = 0; law < SQL_COUNT; law++) {
			float y = samplerQuantize(0.37f, bad[i], (SamplerQuantLaw)law, &dither);
			if(!(y == y) || fabsf(y) > 1.0001f) {
				printf("FAIL: bits=%d law=%d gave %g\n", bad[i], law, (double)y);
				return 1;
			}
		}
	}
	printf("PASS test_quant_bits_clamp\n");
	return 0;
}


/* ------------------------------------------------------------------ */
/* Task 2: the bake -- band-limited decimation plus the coding law.     */
/* ------------------------------------------------------------------ */

/* The amplitude of the component at `freq`, by direct correlation. */
static float toneAmplitude(const float *data, int len, float freq, float sr) {
	double re = 0, im = 0;
	for(int i = 0; i < len; i++) {
		double t = (double)i / (double)sr;
		re += (double)data[i] * cos(2.0 * M_PI * (double)freq * t);
		im -= (double)data[i] * sin(2.0 * M_PI * (double)freq * t);
	}
	return (float)(2.0 * sqrt(re * re + im * im) / (double)len);
}

static int test_bake_null_source(void) {
	BakedSample b;
	memset(&b, 0xAB, sizeof(b));
	if(bakeSamplerCharacter(NULL, 26040, 12, SQL_TRUNC, &b)) {
		printf("FAIL: baking a NULL source succeeded\n");
		return 1;
	}
	if(b.data != NULL || b.length != 0) {
		printf("FAIL: baking a NULL source left data behind\n");
		return 1;
	}
	printf("PASS test_bake_null_source\n");
	return 0;
}

static int test_bake_identity_exact(void) {
	const int N = 512;
	Sample src;
	float data[512];
	for(int i = 0; i < N; i++) {
		data[i] = (float)i / (float)N;
	}
	src.data = data;
	src.length = N;
	src.sampleRate = 44100;
	src.bit = 16;
	BakedSample b;
	memset(&b, 0, sizeof(b));
	if(!bakeSamplerCharacter(&src, 44100, 24, SQL_OFF, &b)) {
		printf("FAIL: the identity bake failed\n");
		return 1;
	}
	if(b.length != N) {
		printf("FAIL: the identity bake has length %d, want %d\n", b.length, N);
		freeBakedSample(&b);
		return 1;
	}
	if(memcmp(b.data, data, sizeof(data)) != 0) {
		printf("FAIL: the identity bake is not an exact copy\n");
		freeBakedSample(&b);
		return 1;
	}
	if(b.rate != 44100 || b.srcBit != 16) {
		printf("FAIL: the identity bake reports rate=%d srcBit=%d\n", b.rate, b.srcBit);
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_bake_identity_exact\n");
	return 0;
}

static int test_bake_upsample_length(void) {
	const int N = 256;
	Sample src;
	float data[256];
	for(int i = 0; i < N; i++) {
		data[i] = 0.5f;
	}
	src.data = data;
	src.length = N;
	src.sampleRate = 22050;
	src.bit = 16;
	BakedSample b;
	memset(&b, 0, sizeof(b));
	if(!bakeSamplerCharacter(&src, 44100, 24, SQL_OFF, &b)) {
		printf("FAIL: the upsampling bake failed\n");
		return 1;
	}
	if(b.length != N * 2) {
		printf("FAIL: the upsampling bake has length %d, want %d\n", b.length, N * 2);
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_bake_upsample_length\n");
	return 0;
}

static int test_bake_bandlimit(void) {
	/* One second of a 1 kHz + 18 kHz two-tone at 44100, baked down to the
	 * SP-1200's 26040. The 18 kHz component is above the bake's cutoff and
	 * must be gone; the 1 kHz one must survive at its own amplitude. */
	const int N = 44100;
	float *data = (float *)malloc(sizeof(float) * N);
	if(!data) {
		printf("FAIL: out of memory\n");
		return 1;
	}
	for(int i = 0; i < N; i++) {
		double t = (double)i / 44100.0;
		data[i] = (float)(0.5 * sin(2.0 * M_PI * 1000.0 * t) + 0.5 * sin(2.0 * M_PI * 18000.0 * t));
	}
	Sample src;
	src.data = data;
	src.length = N;
	src.sampleRate = 44100;
	src.bit = 16;
	BakedSample b;
	memset(&b, 0, sizeof(b));
	if(!bakeSamplerCharacter(&src, 26040, 24, SQL_OFF, &b)) {
		printf("FAIL: the band-limit bake failed\n");
		free(data);
		return 1;
	}
	float low = toneAmplitude(b.data, b.length, 1000.0f, 26040.0f);
	float high = toneAmplitude(b.data, b.length, 18000.0f, 26040.0f);
	float lowDb = 20.0f * log10f(low / 0.5f);
	float highDb = 20.0f * log10f(high / 0.5f + 1e-12f);
	if(lowDb < -0.5f || lowDb > 0.5f) {
		printf("FAIL: the 1 kHz component is %.2f dB, want within 0.5 dB\n", (double)lowDb);
		freeBakedSample(&b);
		free(data);
		return 1;
	}
	if(highDb > -40.0f) {
		printf("FAIL: the 18 kHz component is %.2f dB, want at most -40 dB\n", (double)highDb);
		freeBakedSample(&b);
		free(data);
		return 1;
	}
	freeBakedSample(&b);
	free(data);
	printf("PASS test_bake_bandlimit\n");
	return 0;
}

static int test_bake_trunc_floor(void) {
	/* A 1260 Hz full-scale sine. 1260 divides both 44100 and 26040, so
	 * the source and the baked copy each hold a whole number of periods.
	 *
	 * The measurement compares two bakes of the same source at the same
	 * rate -- one quantized to 12 bits with truncation, one not quantized
	 * at all -- so the difference is exactly the quantizer's error and the
	 * resampling filter's own deviation cancels. */
	const int N = 26040;
	float *data = (float *)malloc(sizeof(float) * N);
	if(!data) {
		printf("FAIL: out of memory\n");
		return 1;
	}
	for(int i = 0; i < N; i++) {
		double t = (double)i / 44100.0;
		data[i] = (float)sin(2.0 * M_PI * 1260.0 * t);
	}
	Sample src;
	src.data = data;
	src.length = N;
	src.sampleRate = 44100;
	src.bit = 16;

	BakedSample quant, plain;
	memset(&quant, 0, sizeof(quant));
	memset(&plain, 0, sizeof(plain));
	if(!bakeSamplerCharacter(&src, 26040, 12, SQL_TRUNC, &quant) ||
	   !bakeSamplerCharacter(&src, 26040, 24, SQL_OFF, &plain)) {
		printf("FAIL: the bakes failed\n");
		free(data);
		return 1;
	}
	if(quant.length != plain.length) {
		printf("FAIL: the two bakes have different lengths\n");
		freeBakedSample(&quant);
		freeBakedSample(&plain);
		free(data);
		return 1;
	}
	/* The first and last filter-length samples are the resampling filter's
	 * transient: its support runs past the ends of the source, so the
	 * edges are interpolated from a mirrored signal and the filter rings
	 * there a little. The interior is the bake's steady state, and that is
	 * what the quantizer's error is measured over. */
	const int edge = 64;
	double sum = 0.0;
	int n = 0;
	for(int i = edge; i < quant.length - edge; i++) {
		double e = (double)quant.data[i] - (double)plain.data[i];
		sum += e * e;
		n++;
	}
	double rms = sqrt(sum / (double)n);
	double delta = 2.0 / 4096.0;
	/* Truncation's error spans a whole step, not half of one, so its RMS
	 * is Delta/sqrt(3) -- twice the Delta/sqrt(12) that rounding's is. The
	 * plan's expectation was the rounding one. */
	double expect = delta / sqrt(3.0);
	if(rms < expect * 0.4 || rms > expect * 2.0) {
		printf("FAIL: the residual RMS is %g, want between %g and %g\n",
		       rms, expect * 0.4, expect * 2.0);
		freeBakedSample(&quant);
		freeBakedSample(&plain);
		free(data);
		return 1;
	}
	freeBakedSample(&quant);
	freeBakedSample(&plain);
	free(data);
	printf("PASS test_bake_trunc_floor\n");
	return 0;
}

static int test_baked_matches(void) {
	const int N = 128;
	float data[128];
	for(int i = 0; i < N; i++) {
		data[i] = 0.25f;
	}
	Sample src;
	src.data = data;
	src.length = N;
	src.sampleRate = 44100;
	src.bit = 16;
	BakedSample b;
	memset(&b, 0, sizeof(b));
	if(!bakeSamplerCharacter(&src, 26040, 12, SQL_TRUNC, &b)) {
		printf("FAIL: the bake failed\n");
		return 1;
	}
	if(!bakedMatches(&b, &src, 26040, 12, SQL_TRUNC)) {
		printf("FAIL: a freshly baked sample does not match itself\n");
		freeBakedSample(&b);
		return 1;
	}
	if(bakedMatches(&b, &src, 44100, 12, SQL_TRUNC)) {
		printf("FAIL: a different rate still matches\n");
		freeBakedSample(&b);
		return 1;
	}
	if(bakedMatches(&b, &src, 26040, 8, SQL_TRUNC)) {
		printf("FAIL: different bits still match\n");
		freeBakedSample(&b);
		return 1;
	}
	if(bakedMatches(&b, &src, 26040, 12, SQL_OFF)) {
		printf("FAIL: a different law still matches\n");
		freeBakedSample(&b);
		return 1;
	}
	Sample other = src;
	other.length = N - 1;
	if(bakedMatches(&b, &other, 26040, 12, SQL_TRUNC)) {
		printf("FAIL: a different source still matches\n");
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_baked_matches\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* Task 3: the machine profiles.                                       */
/* ------------------------------------------------------------------ */

static int test_profile_table_complete(void) {
	for(int p = 0; p < SMP_PROFILE_COUNT; p++) {
		int rate = 0, bits = 0, filterOn = 0;
		float cutoff = 0, res = 0;
		SamplerQuantLaw law = SQL_OFF;
		SamplerInterp interp = SPI_LINEAR;
		samplerProfileConfig((SamplerProfile)p, &rate, &bits, &law, &interp, &filterOn, &cutoff, &res);
		if(rate != 0 && (rate < 2000 || rate > 48000)) {
			printf("FAIL: profile %d has rate %d\n", p, rate);
			return 1;
		}
		if(bits < 1 || bits > 24) {
			printf("FAIL: profile %d has bits %d\n", p, bits);
			return 1;
		}
		if(law < 0 || law >= SQL_COUNT || interp < 0 || interp >= SPI_INTERP_COUNT) {
			printf("FAIL: profile %d has a bad enum\n", p);
			return 1;
		}
		if(filterOn != 0 && filterOn != 1) {
			printf("FAIL: profile %d has filterOn %d\n", p, filterOn);
			return 1;
		}
		if(cutoff < 20.0f || cutoff > 20000.0f) {
			printf("FAIL: profile %d has cutoff %g\n", p, (double)cutoff);
			return 1;
		}
		if(res < 0.0f || res > 0.9f) {
			printf("FAIL: profile %d has res %g\n", p, (double)res);
			return 1;
		}
	}
	printf("PASS test_profile_table_complete\n");
	return 0;
}

static int test_profile_values(void) {
	struct {
		SamplerProfile p;
		int rate;
		int bits;
		SamplerQuantLaw law;
		SamplerInterp interp;
		int filterOn;
		float cutoff;
		float res;
	} cases[] = {
		{ SMP_CLEAN, 0, 24, SQL_OFF, SPI_LINEAR, 0, 20000.0f, 0.0f },
		{ SMP_SP1200, 26040, 12, SQL_TRUNC, SPI_DROPSAMPLE, 1, 12000.0f, 0.05f },
		{ SMP_MPC60, 40000, 12, SQL_COMPAND12, SPI_DROPSAMPLE, 0, 20000.0f, 0.0f },
		{ SMP_S950, 30000, 12, SQL_TRUNC, SPI_LINEAR, 1, 16000.0f, 0.1f },
		{ SMP_MIRAGE, 27000, 8, SQL_TRUNC, SPI_LINEAR, 1, 8000.0f, 0.2f },
		{ SMP_SK1, 9380, 8, SQL_TRUNC, SPI_LINEAR, 0, 20000.0f, 0.0f },
	};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int rate = 0, bits = 0, filterOn = 0;
		float cutoff = 0, res = 0;
		SamplerQuantLaw law = SQL_OFF;
		SamplerInterp interp = SPI_LINEAR;
		samplerProfileConfig(cases[i].p, &rate, &bits, &law, &interp, &filterOn, &cutoff, &res);
		if(rate != cases[i].rate || bits != cases[i].bits || law != cases[i].law ||
		   interp != cases[i].interp || filterOn != cases[i].filterOn ||
		   cutoff != cases[i].cutoff || res != cases[i].res) {
			printf("FAIL: profile %d is rate=%d bits=%d law=%d interp=%d filter=%d cutoff=%g res=%g\n",
			       (int)cases[i].p, rate, bits, (int)law, (int)interp, filterOn,
			       (double)cutoff, (double)res);
			return 1;
		}
	}
	printf("PASS test_profile_values\n");
	return 0;
}

static int test_profile_clean_is_passthrough(void) {
	/* The CLEAN profile's rate is 0, meaning "keep the source's own rate",
	 * so baking it is the identity path: the same length, and the same
	 * samples when no law is applied. */
	const int N = 256;
	float data[256];
	for(int i = 0; i < N; i++) {
		data[i] = (float)i / 256.0f - 0.5f;
	}
	Sample src;
	src.data = data;
	src.length = N;
	src.sampleRate = 30000;
	src.bit = 16;
	int rate = 0, bits = 0, filterOn = 0;
	float cutoff = 0, res = 0;
	SamplerQuantLaw law = SQL_OFF;
	SamplerInterp interp = SPI_LINEAR;
	samplerProfileConfig(SMP_CLEAN, &rate, &bits, &law, &interp, &filterOn, &cutoff, &res);
	if(rate != 0) {
		printf("FAIL: CLEAN's rate is %d, want 0 (keep the source's)\n", rate);
		return 1;
	}
	BakedSample b;
	memset(&b, 0, sizeof(b));
	if(!bakeSamplerCharacter(&src, src.sampleRate, bits, law, &b)) {
		printf("FAIL: the CLEAN bake failed\n");
		return 1;
	}
	if(b.length != N || memcmp(b.data, data, sizeof(data)) != 0) {
		printf("FAIL: the CLEAN bake is not a pass-through\n");
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_profile_clean_is_passthrough\n");
	return 0;
}


/* ------------------------------------------------------------------ */
/* Task 4: the playback read path, and the intended tuning.            */
/* ------------------------------------------------------------------ */

/* The advance is in BAKED frames per output frame. At unity -- the root
 * note unshifted, the tune at 1, and the baked rate equal to the engine's
 * rate -- it is exactly 1.0, which is the whole point: the sample plays at
 * its recorded pitch.
 *
 * The plan's original advance (phaseIncrement * PA_SR * 2 * srcBit /
 * srcRate) was the opposite: it made the pitch depend on the device's audio
 * rate and on the sample's bit depth and channel count, so the same
 * recording played at a different pitch on every machine, and an 8-bit mono
 * sample and a 16-bit stereo one played at different pitches on the same
 * machine. */
static int test_advance_is_exactly_one_at_unity(void) {
	/* The note played is the root note: the recording sounds as recorded. */
	float a = samplerAdvance(9, 9, 1.0f, SPECTRAX_ENGINE_RATE);
	if(a != 1.0f) {
		printf("FAIL: the unity advance is %.9g, want exactly 1\n", (double)a);
		return 1;
	}
	printf("PASS test_advance_is_exactly_one_at_unity\n");
	return 0;
}

static int test_advance_follows_the_root(void) {
	/* An octave up doubles the advance, an octave down halves it, and a
	 * semitone is the twelfth root of two. */
	if(samplerAdvance(21, 9, 1.0f, SPECTRAX_ENGINE_RATE) != 2.0f) {
		printf("FAIL: the +12 advance is %.9g, want 2\n", (double)samplerAdvance(21, 9, 1.0f, SPECTRAX_ENGINE_RATE));
		return 1;
	}
	if(samplerAdvance(-3, 9, 1.0f, SPECTRAX_ENGINE_RATE) != 0.5f) {
		printf("FAIL: the -12 advance is %.9g, want 0.5\n", (double)samplerAdvance(-3, 9, 1.0f, SPECTRAX_ENGINE_RATE));
		return 1;
	}
	float semi = samplerAdvance(10, 9, 1.0f, SPECTRAX_ENGINE_RATE);
	float expect = powf(2.0f, 1.0f / 12.0f);
	if(fabsf(semi - expect) > 1e-6f) {
		printf("FAIL: the +1 advance is %.9g, want %.9g\n", (double)semi, (double)expect);
		return 1;
	}
	printf("PASS test_advance_follows_the_root\n");
	return 0;
}

static int test_advance_follows_the_bake_rate(void) {
	/* A buffer baked at half the engine's rate advances half a frame per
	 * output frame. */
	float a = samplerAdvance(9, 9, 1.0f, SPECTRAX_ENGINE_RATE / 2);
	if(fabsf(a - 0.5f) > 1e-6f) {
		printf("FAIL: the half-rate advance is %.9g, want 0.5\n", (double)a);
		return 1;
	}
	printf("PASS test_advance_follows_the_bake_rate\n");
	return 0;
}

static int test_advance_follows_the_tune(void) {
	float a = samplerAdvance(9, 9, 1.5f, SPECTRAX_ENGINE_RATE);
	if(fabsf(a - 1.5f) > 1e-6f) {
		printf("FAIL: the tuned advance is %.9g, want 1.5\n", (double)a);
		return 1;
	}
	printf("PASS test_advance_follows_the_tune\n");
	return 0;
}

static int test_advance_ignores_the_device_rate(void) {
	/* The engine's rate is a constant, so the advance cannot move when the
	 * device's rate does. */
	float before = samplerAdvance(12, 9, 1.0f, SPECTRAX_ENGINE_RATE);
	float saved = g_deviceRate;
	g_deviceRate = 44100.0f;
	float at44 = samplerAdvance(12, 9, 1.0f, SPECTRAX_ENGINE_RATE);
	g_deviceRate = 192000.0f;
	float at192 = samplerAdvance(12, 9, 1.0f, SPECTRAX_ENGINE_RATE);
	g_deviceRate = saved;
	if(before != at44 || before != at192) {
		printf("FAIL: the advance moved with the device rate (%g, %g, %g)\n",
		       (double)before, (double)at44, (double)at192);
		return 1;
	}
	printf("PASS test_advance_ignores_the_device_rate\n");
	return 0;
}

/* A baked ramp, so a read's position is readable from its value. */
static BakedSample makeRamp(int length, int rate) {
	BakedSample b;
	memset(&b, 0, sizeof(b));
	b.data = (float *)malloc(sizeof(float) * (size_t)length);
	for(int i = 0; i < length; i++) {
		b.data[i] = (float)i / (float)length;
	}
	b.length = length;
	b.rate = rate;
	b.srcBit = 16;
	b.bits = 24;
	b.law = SQL_OFF;
	b.src = NULL;
	return b;
}

static int test_drop_never_stalls(void) {
	/* The SP-1200's accumulator truncates the fractional increment when
	 * it indexes, so a slow advance would repeat the same frame forever.
	 * Its algorithm advances at least one frame per tick. */
	BakedSample b = makeRamp(1024, SPECTRAX_ENGINE_RATE);
	uint32_t addr = 0;
	double phase = 0.0;
	uint32_t prev = addr;
	for(int i = 0; i < 500; i++) {
		getBakedSampleDrop(&b, &addr, &phase, 0.3f, 0, 0, b.length, 0, XF_EQUAL_POWER);
		if(i > 0 && addr <= prev) {
			printf("FAIL: the drop read stalled at tick %d (addr %u -> %u)\n", i, prev, addr);
			freeBakedSample(&b);
			return 1;
		}
		prev = addr;
	}
	freeBakedSample(&b);
	printf("PASS test_drop_never_stalls\n");
	return 0;
}

static int test_drop_pitch_down_repeats(void) {
	/* Drop-sample is sample-and-hold: at half speed the same frame comes
	 * back twice. */
	BakedSample b = makeRamp(64, SPECTRAX_ENGINE_RATE);
	uint32_t addr = 0;
	double phase = 0.0;
	int repeats = 0;
	float prev = getBakedSampleDrop(&b, &addr, &phase, 0.5f, 0, 0, b.length, 0, XF_EQUAL_POWER);
	for(int i = 1; i < 100; i++) {
		float v = getBakedSampleDrop(&b, &addr, &phase, 0.5f, 0, 0, b.length, 0, XF_EQUAL_POWER);
		if(v == prev) {
			repeats++;
		}
		prev = v;
	}
	if(repeats == 0) {
		printf("FAIL: the drop read never repeated a frame\n");
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_drop_pitch_down_repeats\n");
	return 0;
}

static int test_baked_drop_wrap_short(void) {
	/* A three-frame bake, looped, read fast: every value must be one of
	 * the three, and the address must stay inside. */
	BakedSample b = makeRamp(3, SPECTRAX_ENGINE_RATE);
	uint32_t addr = 0;
	double phase = 0.0;
	for(int i = 0; i < 1000; i++) {
		float v = getBakedSampleDrop(&b, &addr, &phase, 2.7f, 1, 0, b.length, 0, XF_EQUAL_POWER);
		if(!(v == b.data[0] || v == b.data[1] || v == b.data[2])) {
			printf("FAIL: the drop read returned %g, which is not in the bake\n", (double)v);
			freeBakedSample(&b);
			return 1;
		}
		if(addr >= (uint32_t)b.length) {
			printf("FAIL: the drop read's address %u is outside the bake\n", addr);
			freeBakedSample(&b);
			return 1;
		}
	}
	freeBakedSample(&b);
	printf("PASS test_baked_drop_wrap_short\n");
	return 0;
}

static int test_linear_interpolates(void) {
	/* A linear read sits between the two frames it spans. */
	BakedSample b = makeRamp(64, SPECTRAX_ENGINE_RATE);
	float pos = 0.0f;
	float v = getBakedSampleLinear(&b, &pos, 0.5f, 0, 0, b.length, 0, XF_EQUAL_POWER);
	/* After the advance the position is 0.5, halfway between frame 0 and
	 * frame 1. */
	if(fabsf(v - 0.5f * (b.data[0] + b.data[1])) > 1e-6f) {
		printf("FAIL: the linear read gave %g, want %g\n",
		       (double)v, (double)(0.5f * (b.data[0] + b.data[1])));
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_linear_interpolates\n");
	return 0;
}

static int test_linear_loops_within_the_region(void) {
	/* The loop region is honoured: the read wraps inside [start, end) and
	 * never reads outside it. */
	BakedSample b = makeRamp(64, SPECTRAX_ENGINE_RATE);
	float pos = 0.0f;
	float minSeen = 1e9f, maxSeen = -1e9f;
	/* The head is played first, and then the loop region: the position
	 * wraps to the region's start when it passes the region's end, the
	 * way the drop read does. The two interpolators have to agree. */
	for(int i = 0; i < 8; i++) {
		float v = getBakedSampleLinear(&b, &pos, 1.0f, 1, 8, 24, 0, XF_EQUAL_POWER);
		if(fabsf(v - b.data[i + 1]) > 1e-6f) {
			printf("FAIL: the linear read skipped the head at %d (%g, want %g)\n",
			       i, (double)v, (double)b.data[i + 1]);
			freeBakedSample(&b);
			return 1;
		}
	}
	for(int i = 0; i < 500; i++) {
		float v = getBakedSampleLinear(&b, &pos, 1.0f, 1, 8, 24, 0, XF_EQUAL_POWER);
		if(v < minSeen) {
			minSeen = v;
		}
		if(v > maxSeen) {
			maxSeen = v;
		}
	}
	if(minSeen < b.data[8] - 1e-4f || maxSeen > b.data[23] + 1e-4f) {
		printf("FAIL: the linear read left the region's values ([%g, %g] vs the region's [%g, %g])\n",
		       (double)minSeen, (double)maxSeen, (double)b.data[8], (double)b.data[23]);
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_linear_loops_within_the_region\n");
	return 0;
}

static int test_read_stops_at_the_end_without_looping(void) {
	/* Without a loop, the read runs out and falls silent rather than
	 * reading past the end. */
	BakedSample b = makeRamp(16, SPECTRAX_ENGINE_RATE);
	float pos = 0.0f;
	float last = 1.0f;
	for(int i = 0; i < 64; i++) {
		last = getBakedSampleLinear(&b, &pos, 1.0f, 0, 0, b.length, 0, XF_EQUAL_POWER);
	}
	if(last != 0.0f) {
		printf("FAIL: the read past the end gave %g, want silence\n", (double)last);
		freeBakedSample(&b);
		return 1;
	}
	freeBakedSample(&b);
	printf("PASS test_read_stops_at_the_end_without_looping\n");
	return 0;
}


/* ------------------------------------------------------------------ */
/* Task 5: the instrument's character parameters, and the bake cache.  */
/* ------------------------------------------------------------------ */

/* A sampler instrument with a sample in its pool. */
typedef struct {
	SamplePool *sp;
	PresetBank pb;
	Instrument *inst;
} SamplerEnv;

static int makeSamplerEnv(SamplerEnv *e) {
	memset(e, 0, sizeof(*e));
	e->sp = createSamplePool();
	if(!e->sp) {
		return 1;
	}
	float buf[512];
	for(int i = 0; i < 512; i++) {
		buf[i] = (float)i / 512.0f - 0.5f;
	}
	loadSample(e->sp, "ramp", buf, 16, SPECTRAX_ENGINE_RATE, 512);
	init_instrument(&e->inst, VOICE_TYPE_SAMPLE, e->sp, &e->pb);
	if(!e->inst) {
		return 1;
	}
	return 0;
}

/* The instrument is deliberately not freed: freeInstrument is private to
 * voice.c, and the test's own heap churn is not what it is here to check. */
static void freeSamplerEnv(SamplerEnv *e) {
	e->inst = NULL;
	if(e->sp) {
		freeSamplePool(e->sp);
		e->sp = NULL;
	}
}

static int test_sampler_params_exist(void) {
	SamplerEnv e;
	if(makeSamplerEnv(&e) != 0) {
		printf("FAIL: could not build a sampler instrument\n");
		return 1;
	}
	Instrument *i = e.inst;
	struct {
		const char *name;
		Parameter *p;
	} want[] = {
		{ "profile", i->id.sampler.profile },
		{ "law", i->id.sampler.law },
		{ "interp", i->id.sampler.interp },
		{ "filter", i->id.sampler.filter },
		{ "cutoff", i->id.sampler.cutoff },
		{ "res", i->id.sampler.res },
		{ "tune", i->id.sampler.tune },
		{ "root", i->id.sampler.root },
	};
	for(size_t k = 0; k < sizeof(want) / sizeof(want[0]); k++) {
		if(!want[k].p) {
			printf("FAIL: the sampler has no %s parameter\n", want[k].name);
			freeSamplerEnv(&e);
			return 1;
		}
	}
	/* The defaults are the neutral ones: no character, no root shift, no
	 * tuning trim. */
	if(getParameterValueAsInt(i->id.sampler.profile) != SMP_CLEAN) {
		printf("FAIL: the default profile is %d\n", getParameterValueAsInt(i->id.sampler.profile));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.law) != SQL_OFF) {
		printf("FAIL: the default law is %d\n", getParameterValueAsInt(i->id.sampler.law));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.interp) != SPI_LINEAR) {
		printf("FAIL: the default interpolator is %d\n", getParameterValueAsInt(i->id.sampler.interp));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.filter) != 0) {
		printf("FAIL: the default filter is on\n");
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.root) != 0) {
		printf("FAIL: the default root is %d\n", getParameterValueAsInt(i->id.sampler.root));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValue(i->id.sampler.tune) != 1.0f) {
		printf("FAIL: the default tune is %g\n", (double)getParameterValue(i->id.sampler.tune));
		freeSamplerEnv(&e);
		return 1;
	}
	freeSamplerEnv(&e);
	printf("PASS test_sampler_params_exist\n");
	return 0;
}

static int test_profile_applies_the_chain(void) {
	SamplerEnv e;
	if(makeSamplerEnv(&e) != 0) {
		printf("FAIL: could not build a sampler instrument\n");
		return 1;
	}
	Instrument *i = e.inst;
	/* The SP-1200's chain: 12 bits at 26040, truncation, drop-sample, the
	 * SSM2044 filter. */
	setParameterBaseValue(i->id.sampler.profile, (float)SMP_SP1200);
	setParameterValue(i->id.sampler.profile, (float)SMP_SP1200);
	applySamplerProfile(i);
	if(getParameterValueAsInt(i->id.sampler.sampleRate) != 26040) {
		printf("FAIL: the SP-1200's rate is %d\n", getParameterValueAsInt(i->id.sampler.sampleRate));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.bitDepth) != 12) {
		printf("FAIL: the SP-1200's bits are %d\n", getParameterValueAsInt(i->id.sampler.bitDepth));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.law) != SQL_TRUNC) {
		printf("FAIL: the SP-1200's law is %d\n", getParameterValueAsInt(i->id.sampler.law));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.interp) != SPI_DROPSAMPLE) {
		printf("FAIL: the SP-1200's interpolator is %d\n", getParameterValueAsInt(i->id.sampler.interp));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.filter) != 1) {
		printf("FAIL: the SP-1200's filter is off\n");
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValue(i->id.sampler.cutoff) != 12000.0f) {
		printf("FAIL: the SP-1200's cutoff is %g\n", (double)getParameterValue(i->id.sampler.cutoff));
		freeSamplerEnv(&e);
		return 1;
	}
	/* And the bake followed: it is the SP-1200's rate and word. */
	if(!i->id.sampler.baked) {
		printf("FAIL: the profile did not build a bake\n");
		freeSamplerEnv(&e);
		return 1;
	}
	if(i->id.sampler.baked->rate != 26040 || i->id.sampler.baked->bits != 12) {
		printf("FAIL: the bake is rate=%d bits=%d\n",
		       i->id.sampler.baked->rate, i->id.sampler.baked->bits);
		freeSamplerEnv(&e);
		return 1;
	}
	freeSamplerEnv(&e);
	printf("PASS test_profile_applies_the_chain\n");
	return 0;
}

static int test_params_survive_a_preset_apply(void) {
	/* applyInstrumentPreset clears and recreates the instrument's
	 * parameters, so every new one has to be recreated there too -- a
	 * parameter that is cleared and not recreated leaves a dangling
	 * pointer the next access hits. */
	SamplerEnv e;
	if(makeSamplerEnv(&e) != 0) {
		printf("FAIL: could not build a sampler instrument\n");
		return 1;
	}
	Preset p;
	memset(&p, 0, sizeof(p));
	p.voiceType = VOICE_TYPE_SAMPLE;
	p.pd.sampler.profile = SMP_SP1200;
	p.pd.sampler.law = SQL_TRUNC;
	p.pd.sampler.interp = SPI_DROPSAMPLE;
	p.pd.sampler.filterOn = 1;
	p.pd.sampler.cutoff = 12000.0f;
	p.pd.sampler.res = 0.05f;
	p.pd.sampler.tune = 1.0f;
	p.pd.sampler.root = 5;
	applyInstrumentPreset(e.inst, p);
	Instrument *i = e.inst;
	if(!i->id.sampler.profile || !i->id.sampler.law || !i->id.sampler.interp ||
	   !i->id.sampler.filter || !i->id.sampler.cutoff || !i->id.sampler.res ||
	   !i->id.sampler.tune || !i->id.sampler.root) {
		printf("FAIL: a parameter was not recreated by applyInstrumentPreset\n");
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.profile) != SMP_SP1200) {
		printf("FAIL: the applied profile is %d\n", getParameterValueAsInt(i->id.sampler.profile));
		freeSamplerEnv(&e);
		return 1;
	}
	if(getParameterValueAsInt(i->id.sampler.root) != 5) {
		printf("FAIL: the applied root is %d\n", getParameterValueAsInt(i->id.sampler.root));
		freeSamplerEnv(&e);
		return 1;
	}
	freeSamplerEnv(&e);
	printf("PASS test_params_survive_a_preset_apply\n");
	return 0;
}


/* The end-to-end proof: a 440 Hz recording played at A4 with the root set
 * to A4 comes out at 440 Hz. The advance it used to be -- phaseIncrement *
 * PA_SR * 2 * srcBit / srcRate -- put it 64x too fast at 16-bit stereo, and
 * made the pitch depend on the device's audio rate and on the sample's bit
 * depth and channel count. */
static int test_sampler_voice_plays_at_the_written_pitch(void) {
	const int N = 48000;
	float *data = (float *)malloc(sizeof(float) * N);
	if(!data) {
		printf("FAIL: out of memory\n");
		return 1;
	}
	for(int i = 0; i < N; i++) {
		double t = (double)i / (double)SPECTRAX_ENGINE_RATE;
		data[i] = (float)sin(2.0 * M_PI * 440.0 * t);
	}
	SamplePool *sp = createSamplePool();
	if(!sp) {
		printf("FAIL: no sample pool\n");
		free(data);
		return 1;
	}
	loadSample(sp, "a440", data, 16, SPECTRAX_ENGINE_RATE, N);
	PresetBank pb;
	memset(&pb, 0, sizeof(pb));
	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_SAMPLE, sp, &pb);
	if(!inst) {
		printf("FAIL: no instrument\n");
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	/* A4 in the engine's numbering: octave 4, note 9 (C is 0) -> 57. */
	setParameterBaseValue(inst->id.sampler.root, 9.0f);
	setParameterValue(inst->id.sampler.root, 9.0f);
	/* The bake is the identity at the engine's rate. */
	if(!inst->id.sampler.baked || inst->id.sampler.baked->rate != SPECTRAX_ENGINE_RATE) {
		printf("FAIL: the bake is missing or at the wrong rate\n");
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	WavetablePool *wtp = createWavetablePool();
	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = 1;
	settings.defaultVoiceCount = 1;
	settings.defaultBPM = 120;
	VoiceManager *vm = createVoiceManager(&settings, sp, wtp, &pb);
	if(!vm) {
		printf("FAIL: no voice manager\n");
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	/* The manager creates its own SAMPLE instruments; use the one it made
	 * for channel 0 so the voice pool and the instrument agree. */
	Instrument *chan0 = vm->instruments[0];
	if(!chan0) {
		printf("FAIL: the manager made no instrument\n");
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	chan0->id.sampler.sample = sp->samples[0];
	setParameterBaseValue(chan0->id.sampler.root, 9.0f);
	setParameterValue(chan0->id.sampler.root, 9.0f);
	{
		Instrument *inst2 = chan0;
		/* The bake is the identity at the engine's rate. */
		if(!inst2->id.sampler.baked || inst2->id.sampler.baked->rate != SPECTRAX_ENGINE_RATE) {
			printf("FAIL: the bake is missing or at the wrong rate\n");
			freeSamplePool(sp);
			free(data);
			return 1;
		}
	}
	inst = chan0;
	initVoicePool(vm, 0, 1, chan0);
	int note[NOTE_INFO_SIZE] = { A, 4 };
	Voice *v = getFreeVoice(vm, 0);
	if(!v) {
		printf("FAIL: no free voice\n");
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	triggerVoice(v, note, NOTE_VELOCITY_FULL);
	const int RENDER = 8192;
	float *out = (float *)malloc(sizeof(float) * RENDER);
	if(!out) {
		printf("FAIL: out of memory\n");
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	for(int i = 0; i < RENDER; i++) {
		OutVal o = generateVoice(vm, v, 440.0f / (float)SPECTRAX_ENGINE_RATE, 440.0f);
		out[i] = o.L;
	}
	float amp = toneAmplitude(out, RENDER, 440.0f, (float)SPECTRAX_ENGINE_RATE);
	/* The legacy advance put this at 64 * 440 Hz, far above Nyquist, so it
	 * aliased into the audible band. The rendered amplitude at 440 Hz must
	 * now be a good fraction of the recording's. */
	if(amp < 0.15f) {
		printf("FAIL: the rendered 440 Hz amplitude is %g, want at least 0.15\n", (double)amp);
		free(out);
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	/* And it must not be somewhere else: check that the energy is where it
	 * belongs by measuring a tone an octave up, which the old advance
	 * would have put in the band. */
	float up = toneAmplitude(out, RENDER, 880.0f, (float)SPECTRAX_ENGINE_RATE);
	if(up > amp * 0.5f) {
		printf("FAIL: the rendered 880 Hz amplitude is %g, want well under the 440 Hz one (%g)\n",
		       (double)up, (double)amp);
		free(out);
		freeSamplePool(sp);
		free(data);
		return 1;
	}
	free(out);
	freeSamplePool(sp);
	free(data);
	printf("PASS test_sampler_voice_plays_at_the_written_pitch\n");
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
	fails += test_bake_null_source();
	fails += test_bake_identity_exact();
	fails += test_bake_upsample_length();
	fails += test_bake_bandlimit();
	fails += test_bake_trunc_floor();
	fails += test_baked_matches();
	fails += test_profile_table_complete();
	fails += test_profile_values();
	fails += test_profile_clean_is_passthrough();
	fails += test_advance_is_exactly_one_at_unity();
	fails += test_advance_follows_the_root();
	fails += test_advance_follows_the_bake_rate();
	fails += test_advance_follows_the_tune();
	fails += test_advance_ignores_the_device_rate();
	fails += test_drop_never_stalls();
	fails += test_drop_pitch_down_repeats();
	fails += test_baked_drop_wrap_short();
	fails += test_linear_interpolates();
	fails += test_linear_loops_within_the_region();
	fails += test_read_stops_at_the_end_without_looping();
	fails += test_sampler_params_exist();
	fails += test_profile_applies_the_chain();
	fails += test_params_survive_a_preset_apply();
	fails += test_sampler_voice_plays_at_the_written_pitch();
	if(fails) {
		fprintf(stderr, "%d sampler-character test(s) failed\n", fails);
		return 1;
	}
	printf("ALL sampler-character tests passed\n");
	return 0;
}
