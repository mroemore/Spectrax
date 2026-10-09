#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sample.h"

/* samplePeakEnvelope builds the min/max peak envelope that the sampler
 * waveform node draws. It replaces the old draw loop whose integer
 * `length / width` ratio collapsed to 0 for short samples and divided by
 * zero (SIGFPE) when drawing the loop markers. This helper is pure and
 * headless-testable. */

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while(0)

static Sample make_sample(float *data, int length) {
	Sample s;
	memset(&s, 0, sizeof(s));
	s.data = data;
	s.length = length;
	s.sampleRate = 44100;
	s.bit = 16;
	return s;
}

/* A ramp 0, step, 2*step ... (length samples) ending just below 1.0. */
static void fill_ramp(float *buf, int length) {
	for(int i = 0; i < length; i++) {
		buf[i] = (float)i / (float)length;
	}
}

static int test_envelope_ramp_columns_span_data(void) {
	float buf[1000];
	fill_ramp(buf, 1000);
	Sample s = make_sample(buf, 1000);

	float outMin[64];
	float outMax[64];
	samplePeakEnvelope(&s, 0.0f, 1000.0f, 64, outMin, outMax);

	for(int c = 0; c < 64; c++) {
		ASSERT_TRUE(outMin[c] <= outMax[c]);
	}
	/* first column covers samples [0,15] -> min 0.0 */
	ASSERT_TRUE(outMin[0] <= 0.0f + 0.001f);
	/* last column covers [984,999] -> max ~0.999 */
	ASSERT_TRUE(outMax[63] >= 0.998f);
	printf("PASS test_envelope_ramp_columns_span_data\n");
	return 0;
}

static int test_envelope_dc_is_flat(void) {
	float buf[500];
	for(int i = 0; i < 500; i++) {
		buf[i] = 0.5f;
	}
	Sample s = make_sample(buf, 500);

	float outMin[32];
	float outMax[32];
	samplePeakEnvelope(&s, 0.0f, 500.0f, 32, outMin, outMax);

	for(int c = 0; c < 32; c++) {
		ASSERT_TRUE(fabsf(outMin[c] - 0.5f) < 0.0001f);
		ASSERT_TRUE(fabsf(outMax[c] - 0.5f) < 0.0001f);
	}
	printf("PASS test_envelope_dc_is_flat\n");
	return 0;
}

/* Regression for the reported crash: the old draw computed an integer
 * `length / width`, which is 0 when the sample is shorter than the node
 * width, then divided the loop markers by it (SIGFPE). The envelope must
 * handle cols > length without any division by a zero span. */
static int test_envelope_short_sample_no_divide_by_zero(void) {
	float buf[10];
	for(int i = 0; i < 10; i++) {
		buf[i] = (float)i / 10.0f;
	}
	Sample s = make_sample(buf, 10);

	float outMin[200];
	float outMax[200];
	samplePeakEnvelope(&s, 0.0f, 10.0f, 200, outMin, outMax);

	for(int c = 0; c < 200; c++) {
		ASSERT_TRUE(outMin[c] >= 0.0f && outMin[c] <= 0.9f);
		ASSERT_TRUE(outMax[c] >= 0.0f && outMax[c] <= 0.9f);
		ASSERT_TRUE(outMin[c] <= outMax[c]);
	}
	printf("PASS test_envelope_short_sample_no_divide_by_zero\n");
	return 0;
}

static int test_envelope_window_subrange(void) {
	float buf[100];
	for(int i = 0; i < 100; i++) {
		buf[i] = (float)i / 100.0f;
	}
	Sample s = make_sample(buf, 100);

	float outMin[16];
	float outMax[16];
	/* zoom to samples [25,75) -> values in [0.25, 0.75) */
	samplePeakEnvelope(&s, 25.0f, 75.0f, 16, outMin, outMax);

	for(int c = 0; c < 16; c++) {
		ASSERT_TRUE(outMin[c] >= 0.24f);
		ASSERT_TRUE(outMax[c] <= 0.76f);
	}
	ASSERT_TRUE(outMin[0] <= 0.26f);
	printf("PASS test_envelope_window_subrange\n");
	return 0;
}

static int test_envelope_single_column_hits_extremes(void) {
	float buf[64];
	for(int i = 0; i < 64; i++) {
		buf[i] = (i == 0) ? -1.0f : ((i == 63) ? 1.0f : 0.0f);
	}
	Sample s = make_sample(buf, 64);

	float outMin[1];
	float outMax[1];
	samplePeakEnvelope(&s, 0.0f, 64.0f, 1, outMin, outMax);
	ASSERT_TRUE(outMin[0] == -1.0f);
	ASSERT_TRUE(outMax[0] == 1.0f);
	printf("PASS test_envelope_single_column_hits_extremes\n");
	return 0;
}

static int test_envelope_degenerate_inputs_safe(void) {
	float buf[32];
	fill_ramp(buf, 32);
	Sample s = make_sample(buf, 32);

	float outMin[8];
	float outMax[8];
	/* NULL sample: no-op, no crash. */
	samplePeakEnvelope(NULL, 0.0f, 32.0f, 8, outMin, outMax);
	/* NULL outputs: no-op. */
	samplePeakEnvelope(&s, 0.0f, 32.0f, 8, NULL, NULL);
	/* cols <= 0: no-op. */
	samplePeakEnvelope(&s, 0.0f, 32.0f, 0, outMin, outMax);
	/* lo > hi: clamped, must not read out of bounds. */
	samplePeakEnvelope(&s, 40.0f, 10.0f, 8, outMin, outMax);
	/* empty sample. */
	Sample empty = make_sample(buf, 0);
	samplePeakEnvelope(&empty, 0.0f, 1.0f, 8, outMin, outMax);
	printf("PASS test_envelope_degenerate_inputs_safe\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_envelope_ramp_columns_span_data();
	failed |= test_envelope_dc_is_flat();
	failed |= test_envelope_short_sample_no_divide_by_zero();
	failed |= test_envelope_window_subrange();
	failed |= test_envelope_single_column_hits_extremes();
	failed |= test_envelope_degenerate_inputs_safe();

	printf("\n%s (6 tests, %s)\n",
	       failed ? "FAILED" : "PASSED", failed ? ">=1 failed" : "all passed");
	return failed;
}
