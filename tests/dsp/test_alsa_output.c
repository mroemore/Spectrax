/* test_alsa_output.c — the float -> integer conversion on the device's audio
 * path, plus the request validation.
 *
 * The H700 codec accepts S16_LE and S32_LE only (no float), so every engine
 * sample crosses this conversion. Two things are worth pinning down:
 *   - a sample beyond full scale must clamp instead of wrapping (a wrapped
 *     sample is a full-scale click, the loudest possible artefact), and
 *   - NaN must not reach lrintf (its result is implementation-defined).
 *
 * The pacing half of alsa_output.c needs a PCM and is verified on the device
 * (audio_probe + --probe-tempo); this file is the part a host can prove. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "alsa_output.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while(0)

static int test_s32_conversion(void) {
	int32_t out[8] = { 0 };
	float in[8] = { 0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, -0.5f, NAN };
	alsaOutputConvertFloatToS32(in, out, 8);

	ASSERT_TRUE(out[0] == 0, "silence converts to 0");
	ASSERT_TRUE(out[1] == INT32_MAX, "full scale positive is INT32_MAX");
	ASSERT_TRUE(out[2] == -INT32_MAX, "full scale negative is -INT32_MAX");
	ASSERT_TRUE(out[3] == INT32_MAX, "over full scale clamps positive");
	ASSERT_TRUE(out[4] == -INT32_MAX, "under full scale clamps negative");
	ASSERT_TRUE(labs((long)out[5] - (long)(0.5 * 2147483647.0)) <= 1, "half scale");
	ASSERT_TRUE(labs((long)out[6] + (long)(0.5 * 2147483647.0)) <= 1, "half scale negative");
	ASSERT_TRUE(out[7] == 0, "NaN converts to silence, not an implementation-defined clamp");

	printf("PASS test_s32_conversion\n");
	return 0;
}

static int test_s16_conversion(void) {
	int16_t out[8] = { 0 };
	float in[8] = { 0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.25f, -0.25f, NAN };
	alsaOutputConvertFloatToS16(in, out, 8);

	ASSERT_TRUE(out[0] == 0, "silence converts to 0");
	ASSERT_TRUE(out[1] == 32767, "full scale positive is 32767");
	ASSERT_TRUE(out[2] == -32767, "full scale negative is -32767");
	ASSERT_TRUE(out[3] == 32767, "over full scale clamps positive");
	ASSERT_TRUE(out[4] == -32767, "under full scale clamps negative");
	ASSERT_TRUE(out[5] == 8192, "quarter scale");
	ASSERT_TRUE(out[6] == -8192, "quarter scale negative");
	ASSERT_TRUE(out[7] == 0, "NaN converts to silence");

	printf("PASS test_s16_conversion\n");
	return 0;
}

static int test_start_rejects_bad_requests(void) {
	char err[128];
	/* A missing data pointer, a missing PCM name and an empty buffer are all
	 * refused before any ALSA call, so nothing is left half-open. */
	ASSERT_TRUE(!alsaOutputStart(NULL, "hw:0,0", 256, err, sizeof(err)), "NULL data rejected");
	ASSERT_TRUE(!alsaOutputStart((paTestData *)1, "", 256, err, sizeof(err)), "empty PCM rejected");
	ASSERT_TRUE(!alsaOutputStart((paTestData *)1, NULL, 256, err, sizeof(err)), "NULL PCM rejected");
	ASSERT_TRUE(!alsaOutputStart((paTestData *)1, "hw:0,0", 0, err, sizeof(err)), "zero frames rejected");
	ASSERT_TRUE(!alsaOutputActive(), "nothing started after a rejected request");
	ASSERT_TRUE(err[0] != '\0', "a rejected request explains itself");

	printf("PASS test_start_rejects_bad_requests\n");
	return 0;
}

static int test_stop_is_idempotent(void) {
	/* Called on the shutdown and error paths both; it must not touch a PCM
	 * that was never started. */
	alsaOutputStop();
	alsaOutputStop();
	ASSERT_TRUE(!alsaOutputActive(), "stop without start stays stopped");
	AlsaOutputStats stats;
	alsaOutputStats(&stats);
	ASSERT_TRUE(stats.frames == 0, "no frames written before start");
	ASSERT_TRUE(alsaOutputRate() == 0, "no rate before start");

	printf("PASS test_stop_is_idempotent\n");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_s32_conversion();
	rc |= test_s16_conversion();
	rc |= test_start_rejects_bad_requests();
	rc |= test_stop_is_idempotent();
	if(rc != 0) {
		fprintf(stderr, "FAILED (alsa output)\n");
		return 1;
	}
	printf("PASSED (alsa output)\n");
	return 0;
}
