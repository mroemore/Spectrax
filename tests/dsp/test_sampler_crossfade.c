/* Tests for the loop crossfade (src/sampler_crossfade.c).
 *
 * The crossfade is what keeps a loop from thumping: the loop's last
 * frames fade out while its first frames fade in, over the same span.
 * The law depends on the material -- equal-gain for a correlated loop,
 * equal-power for uncorrelated material -- and which one it is, is
 * MEASURED rather than asked for.
 *
 * Headless: the module is pure arithmetic over a baked buffer. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canonical_math.h"
#include "sampler_crossfade.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define ASSERT_NEAR(actual, expected, tol, msg) do { \
	double _a = (double)(actual); \
	double _e = (double)(expected); \
	if(fabs(_a - _e) > (double)(tol)) { \
		fprintf(stderr, "FAIL %s:%d: %s (got %f, expected %f +/- %f)\n", \
		        __FILE__, __LINE__, msg, _a, _e, (double)(tol)); \
		return 1; \
	} \
} while (0)

/* A baked buffer of `length` samples, filled by the caller. */
static BakedSample *make_bake(int length) {
	BakedSample *b = (BakedSample *)calloc(1, sizeof(BakedSample));
	if(!b) {
		return NULL;
	}
	b->data = (float *)calloc((size_t)length, sizeof(float));
	if(!b->data) {
		free(b);
		return NULL;
	}
	b->length = length;
	b->rate = 48000;
	return b;
}

/* The equal-gain law is a straight cross. */
static int test_equal_gain_mix(void) {
	float v = samplerCrossfadeMix(1.0f, 0.0f, 0.5f, XF_EQUAL_GAIN);
	ASSERT_NEAR(v, 0.5, 1e-6, "the equal-gain mix at the midpoint");
	v = samplerCrossfadeMix(2.0f, 0.0f, 0.5f, XF_EQUAL_GAIN);
	ASSERT_NEAR(v, 1.0, 1e-6, "the equal-gain mix scales the outgoing copy");
	v = samplerCrossfadeMix(0.0f, 2.0f, 0.5f, XF_EQUAL_GAIN);
	ASSERT_NEAR(v, 1.0, 1e-6, "the equal-gain mix scales the incoming copy");
	/* The ends are the copies themselves. */
	ASSERT_NEAR(samplerCrossfadeMix(3.0f, 7.0f, 0.0f, XF_EQUAL_GAIN), 3.0, 1e-6,
	            "the fade starts on the outgoing copy");
	ASSERT_NEAR(samplerCrossfadeMix(3.0f, 7.0f, 1.0f, XF_EQUAL_GAIN), 7.0, 1e-6,
	            "the fade ends on the incoming copy");
	printf("PASS test_equal_gain_mix\n");
	return 0;
}

/* The equal-power law is the quarter-circle: the two copies' squares sum
 * to one, so the sum's POWER is constant across the fade. */
static int test_equal_power_mix(void) {
	float v = samplerCrossfadeMix(1.0f, 0.0f, 0.5f, XF_EQUAL_POWER);
	ASSERT_NEAR(v, 0.70710678, 1e-6, "the equal-power mix at the midpoint");
	v = samplerCrossfadeMix(0.0f, 1.0f, 0.5f, XF_EQUAL_POWER);
	ASSERT_NEAR(v, 0.70710678, 1e-6, "the equal-power mix of the incoming copy");
	/* The equal-power law's two weights satisfy w1^2 + w2^2 = 1, so the
	 * two UNCORRELATED copies' powers sum to a constant. The weights are
	 * read off one copy at a time: a copy fading in alone comes out
	 * scaled by sin, and a copy fading out alone by cos. */
	for(float p = 0.05f; p < 1.0f; p += 0.05f) {
		double wOut = (double)samplerCrossfadeMix(1.0f, 0.0f, p, XF_EQUAL_POWER);
		double wIn = (double)samplerCrossfadeMix(0.0f, 1.0f, p, XF_EQUAL_POWER);
		ASSERT_NEAR(wOut * wOut + wIn * wIn, 1.0, 1e-4,
		            "the equal-power fade's weights have a constant sum of squares");
	}
	printf("PASS test_equal_power_mix\n");
	return 0;
}

/* The law follows the measured correlation. */
static int test_law_from_correlation(void) {
	ASSERT_TRUE(samplerCrossfadeLaw(0.9f) == XF_EQUAL_GAIN,
	            "a correlated loop takes the equal-gain law");
	ASSERT_TRUE(samplerCrossfadeLaw(0.1f) == XF_EQUAL_POWER,
	            "an uncorrelated loop takes the equal-power law");
	/* The threshold: above 0.5 is correlated. */
	ASSERT_TRUE(samplerCrossfadeLaw(0.51f) == XF_EQUAL_GAIN, "just above the threshold");
	ASSERT_TRUE(samplerCrossfadeLaw(0.49f) == XF_EQUAL_POWER, "just below the threshold");
	printf("PASS test_law_from_correlation\n");
	return 0;
}

/* The correlation: a loop whose head and tail are the same waveform
 * correlates, and one cut mid-period does not. */
static int test_correlation(void) {
	const int N = 1024;
	BakedSample *b = make_bake(N);
	ASSERT_TRUE(b != NULL, "the bake");
	/* A sine with a whole number of periods in the loop: the head and
	 * the tail are the same waveform. 1024 samples over 8 periods of 128
	 * samples each -- the loop is the last 256 samples, so it holds two
	 * whole periods and its head and tail match. */
	for(int i = 0; i < N; i++) {
		b->data[i] = sinf(2.0f * (float)M_PI * 8.0f * (float)i / (float)N);
	}
	int loopStart = N - 256;
	int loopEnd = N;
	float corr = samplerCrossfadeCorrelation(b, loopStart, loopEnd, 128);
	ASSERT_TRUE(corr > 0.9f, "a whole-period loop correlates with itself");
	/* The same material, looped mid-period: the head and the tail are
	 * out of phase. */
	int oddStart = N - 200;
	float corrOdd = samplerCrossfadeCorrelation(b, oddStart, loopEnd, 100);
	ASSERT_TRUE(corrOdd < 0.5f, "a mid-period loop does not correlate with itself");

	/* Silence on one side: no correlation, and no crash. */
	for(int i = loopStart; i < loopEnd; i++) {
		b->data[i] = 0.0f;
	}
	float corrSilent = samplerCrossfadeCorrelation(b, loopStart, loopEnd, 128);
	ASSERT_NEAR(corrSilent, 0.0, 1e-6, "silence does not correlate");

	/* The guards. */
	ASSERT_NEAR(samplerCrossfadeCorrelation(NULL, 0, 100, 10), 0.0, 1e-6,
	            "a NULL bake does not correlate");
	ASSERT_NEAR(samplerCrossfadeCorrelation(b, 0, 100, 0), 0.0, 1e-6,
	            "a zero-length comparison does not correlate");
	ASSERT_NEAR(samplerCrossfadeCorrelation(b, 100, 100, 10), 0.0, 1e-6,
	            "an empty loop does not correlate");

	free(b->data);
	free(b);
	printf("PASS test_correlation\n");
	return 0;
}

/* Review Focus 1: a crossfade longer than the loop is clamped. The
 * helper converts the parameter's milliseconds into frames, and never
 * hands back more frames than the loop has. */
static int test_crossfade_clamped_to_the_loop(void) {
	/* 100 ms at 48 kHz is 4800 frames. */
	ASSERT_TRUE(samplerCrossfadeFrames(100.0f, 4800, 48000) == 4800,
	            "the milliseconds convert to frames");
	/* A loop of 1000 frames cannot hold a 4800-frame crossfade. */
	int clamped = samplerCrossfadeFrames(100.0f, 1000, 48000);
	ASSERT_TRUE(clamped == 1000, "a crossfade longer than the loop is clamped to it");
	ASSERT_TRUE(clamped <= 1000, "the clamp never exceeds the loop");
	/* Zero and the guards. */
	ASSERT_TRUE(samplerCrossfadeFrames(0.0f, 1000, 48000) == 0, "no crossfade is no frames");
	ASSERT_TRUE(samplerCrossfadeFrames(-5.0f, 1000, 48000) == 0, "a negative crossfade is no frames");
	ASSERT_TRUE(samplerCrossfadeFrames(10.0f, 0, 48000) == 0, "no loop is no frames");
	ASSERT_TRUE(samplerCrossfadeFrames(10.0f, 1000, 0) == 0, "no rate is no frames");
	printf("PASS test_crossfade_clamped_to_the_loop\n");
	return 0;
}

/* The measurement is made once per loop and cached on the bake. */
static int test_measurement_is_cached(void) {
	const int N = 1024;
	BakedSample *b = make_bake(N);
	ASSERT_TRUE(b != NULL, "the bake");
	for(int i = 0; i < N; i++) {
		b->data[i] = sinf(2.0f * (float)M_PI * 8.0f * (float)i / (float)N);
	}
	int loopStart = N - 256;
	int loopEnd = N;
	SamplerFadeLaw law = samplerCrossfadeMeasure(b, loopStart, loopEnd, 128);
	ASSERT_TRUE(law == XF_EQUAL_GAIN, "the correlated loop measures equal-gain");
	/* The cache holds the correlation. */
	ASSERT_TRUE(b->lawCorrelation > 0.9f, "the measurement is cached on the bake");
	ASSERT_TRUE(b->lawLoopStart == loopStart && b->lawLoopEnd == loopEnd,
	            "the cache is keyed on the loop points");
	/* Asking again with the same points gives the same answer without
	 * measuring: the correlation is unchanged. */
	float before = b->lawCorrelation;
	samplerCrossfadeMeasure(b, loopStart, loopEnd, 128);
	ASSERT_NEAR(b->lawCorrelation, before, 1e-9, "the cached measurement is reused");
	/* A different loop is measured afresh. */
	samplerCrossfadeMeasure(b, N - 200, loopEnd, 100);
	ASSERT_TRUE(b->lawLoopStart == N - 200, "a different loop is measured again");
	printf("PASS test_measurement_is_cached\n");
	return 0;
}

/* The guards: a correlation measured over a region that does not fit,
 * and a crossfade's frames in every corner. */
static int test_guards(void) {
	const int N = 512;
	BakedSample *b = make_bake(N);
	ASSERT_TRUE(b != NULL, "the bake");
	for(int i = 0; i < N; i++) {
		b->data[i] = sinf(2.0f * (float)M_PI * 4.0f * (float)i / (float)N);
	}

	/* More frames than the region holds: the measurement uses what
	 * fits, and does not read past the bake. */
	float corr = samplerCrossfadeCorrelation(b, 100, 200, 400);
	ASSERT_TRUE(corr >= -1.0f && corr <= 1.0f, "an over-long measurement stays in range");

	/* A region that is empty. */
	ASSERT_NEAR(samplerCrossfadeCorrelation(b, 100, 100, 10), 0.0, 1e-6,
	            "an empty region does not correlate");
	/* A negative start and an end past the bake. */
	ASSERT_TRUE(samplerCrossfadeCorrelation(b, -10, N + 100, 8) >= -1.0f,
	            "an out-of-range region does not read past the bake");
	/* Frames that do not fit are clamped to what the region holds, so
	 * the measurement uses the region rather than reading past it. */
	float corrSmall = samplerCrossfadeCorrelation(b, 100, 105, 50);
	ASSERT_TRUE(corrSmall >= -1.0f && corrSmall <= 1.0f,
	            "frames that do not fit are clamped to the region");

	/* A crossfade shorter than a frame rounds up to one frame rather
	 * than to none -- it still fades something. */
	ASSERT_TRUE(samplerCrossfadeFrames(0.01f, 1000, 48000) == 1,
	            "a sub-frame crossfade is one frame");
	/* A crossfade that is not a whole number of frames rounds UP, so the
	 * fade covers at least the length that was asked for. */
	ASSERT_TRUE(samplerCrossfadeFrames(100.01f, 48000, 48000) == 4801,
	            "a fractional frame count rounds up");

	/* A NULL bake has no law. */
	ASSERT_TRUE(samplerCrossfadeMeasure(NULL, 0, 100, 10) == XF_EQUAL_POWER,
	            "a NULL bake measures the equal-power law");

	free(b->data);
	free(b);
	printf("PASS test_guards\n");
	return 0;
}

/* The no-libm invariant, applied locally: the crossfade's file must not
 * call an inexact transcendental. */
static int test_no_inexact_libm(void) {
	static const char *const INEXACT_LIBM[] = {
		"sinf(", "cosf(", "expf(", "logf(", "powf(", "tanf(",
		"sin(", "cos(", "exp(", "log(", "pow(",
	};
	char path[512];
	snprintf(path, sizeof(path), "%s/sampler_crossfade.c", SPECTRAX_SRC_DIR);
	FILE *f = fopen(path, "r");
	ASSERT_TRUE(f != NULL, "the crossfade's source is readable");
	char line[1024];
	int lineno = 0;
	while(fgets(line, sizeof(line), f)) {
		lineno++;
		char *p = line;
		while(*p == ' ' || *p == '\t') {
			p++;
		}
		if(p[0] == '/' || p[0] == '*') {
			continue;
		}
		for(size_t i = 0; i < sizeof(INEXACT_LIBM) / sizeof(INEXACT_LIBM[0]); i++) {
			const char *name = INEXACT_LIBM[i];
			size_t nl = strlen(name);
			const char *q = p;
			while((q = strstr(q, name)) != NULL) {
				int boundary = (q == p);
				if(!boundary) {
					unsigned char before = (unsigned char)q[-1];
					boundary = !((before >= 'a' && before <= 'z') ||
					             (before >= 'A' && before <= 'Z') ||
					             (before >= '0' && before <= '9') || before == '_');
				}
				if(boundary) {
					fclose(f);
					printf("FAIL: sampler_crossfade.c:%i calls %s\n", lineno, name);
					return 1;
				}
				q += nl;
			}
		}
	}
	fclose(f);
	printf("PASS test_no_inexact_libm\n");
	return 0;
}

int main(void) {
	if(test_equal_gain_mix() != 0) {
		return 1;
	}
	if(test_equal_power_mix() != 0) {
		return 1;
	}
	if(test_law_from_correlation() != 0) {
		return 1;
	}
	if(test_correlation() != 0) {
		return 1;
	}
	if(test_crossfade_clamped_to_the_loop() != 0) {
		return 1;
	}
	if(test_measurement_is_cached() != 0) {
		return 1;
	}
	if(test_guards() != 0) {
		return 1;
	}
	if(test_no_inexact_libm() != 0) {
		return 1;
	}
	printf("PASSED (sampler_crossfade)\n");
	return 0;
}
