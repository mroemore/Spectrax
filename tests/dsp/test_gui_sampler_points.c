/* Tests for the waveform's points model (src/gui_sampler_points.c).
 *
 * The model is UI state only -- no synthesis, no parameters, no drawing
 * -- so it is tested directly: the cursor moves and clamps, a selected
 * point follows the cursor, the loop points are created and deleted,
 * and a loop that starts at or past its end is rejected.
 *
 * Headless: the model is pure arithmetic. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui_sampler_points.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define ASSERT_EQ(actual, expected, msg) do { \
	long _a = (long)(actual); \
	long _e = (long)(expected); \
	if(_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: %s (got %ld, expected %ld)\n", \
		        __FILE__, __LINE__, msg, _a, _e); \
		return 1; \
	} \
} while (0)

#define SAMPLE_LEN 1024

/* The cursor moves by the given frames and clamps to the sample. */
static int test_cursor_moves_and_clamps(void) {
	SamplerPoints p;
	samplerPointsInit(&p, SAMPLE_LEN);
	ASSERT_EQ(p.cursor.frame, 0, "the cursor starts at the start");

	samplerPointsMoveCursor(&p, 100);
	ASSERT_EQ(p.cursor.frame, 100, "the cursor moves forwards");
	samplerPointsMoveCursor(&p, -40);
	ASSERT_EQ(p.cursor.frame, 60, "the cursor moves backwards");

	/* It clamps at both ends. */
	samplerPointsMoveCursor(&p, -1000);
	ASSERT_EQ(p.cursor.frame, 0, "the cursor clamps at the start");
	samplerPointsMoveCursor(&p, 100000);
	ASSERT_EQ(p.cursor.frame, SAMPLE_LEN - 1, "the cursor clamps at the end");

	printf("PASS test_cursor_moves_and_clamps\n");
	return 0;
}

/* A selected point follows the cursor: that is what makes the
 * interaction direct manipulation rather than a form. */
static int test_selected_point_follows_the_cursor(void) {
	SamplerPoints p;
	samplerPointsInit(&p, SAMPLE_LEN);

	/* Selecting the loop start creates it at the cursor and puts the
	 * cursor on it. */
	samplerPointsMoveCursor(&p, 200);
	samplerPointsSelect(&p, PT_LOOP_START);
	ASSERT_TRUE(samplerPointsHasPoint(&p, PT_LOOP_START), "the loop start now exists");
	ASSERT_EQ(p.loopStart.frame, 200, "the loop start is created at the cursor");
	ASSERT_EQ(p.cursor.frame, 200, "the cursor jumps to the point it selected");
	ASSERT_TRUE(p.loopStart.selected, "the loop start is the selected point");

	/* From then on, moving the cursor moves the point. */
	samplerPointsMoveCursor(&p, 50);
	ASSERT_EQ(p.loopStart.frame, 250, "the selected point follows the cursor");
	ASSERT_EQ(p.cursor.frame, 250, "the cursor moved too");

	/* Selecting the other point moves the selection, and the first one
	 * stops following. */
	samplerPointsSelect(&p, PT_LOOP_END);
	ASSERT_TRUE(p.loopEnd.selected, "the loop end is now selected");
	ASSERT_TRUE(!p.loopStart.selected, "the loop start is no longer selected");
	samplerPointsMoveCursor(&p, 10);
	ASSERT_EQ(p.loopEnd.frame, 260, "the newly selected point follows the cursor");
	ASSERT_EQ(p.loopStart.frame, 250, "the deselected point stays put");

	printf("PASS test_selected_point_follows_the_cursor\n");
	return 0;
}

/* The points' existence: the cursor always does, the loop points only
 * once they have been created. */
static int test_point_existence(void) {
	SamplerPoints p;
	samplerPointsInit(&p, SAMPLE_LEN);
	ASSERT_TRUE(samplerPointsHasPoint(&p, PT_CURSOR), "the cursor exists");
	ASSERT_TRUE(!samplerPointsHasPoint(&p, PT_LOOP_START), "the loop start does not exist yet");
	ASSERT_TRUE(!samplerPointsHasPoint(&p, PT_LOOP_END), "the loop end does not exist yet");

	samplerPointsSelect(&p, PT_LOOP_END);
	ASSERT_TRUE(samplerPointsHasPoint(&p, PT_LOOP_END), "selecting creates the loop end");
	ASSERT_TRUE(!samplerPointsHasPoint(&p, PT_LOOP_START), "the loop start is still absent");

	printf("PASS test_point_existence\n");
	return 0;
}

/* Deleting: the selected point goes, and the cursor is re-created by the
 * next movement at the position it had. */
static int test_delete(void) {
	SamplerPoints p;
	samplerPointsInit(&p, SAMPLE_LEN);

	/* Deleting a loop point removes it. */
	samplerPointsSelect(&p, PT_LOOP_START);
	samplerPointsMoveCursor(&p, 300);
	ASSERT_TRUE(samplerPointsDelete(&p), "the delete reports that it deleted something");
	ASSERT_TRUE(!samplerPointsHasPoint(&p, PT_LOOP_START), "the loop start is gone");

	/* Deleting the cursor removes the marker... */
	samplerPointsSelect(&p, PT_CURSOR);
	samplerPointsMoveCursor(&p, 42);
	int where = p.cursor.frame;
	ASSERT_TRUE(samplerPointsDelete(&p), "the cursor is deleted");
	ASSERT_TRUE(!samplerPointsHasPoint(&p, PT_CURSOR), "the cursor marker is gone");
	/* ...and the next movement re-creates it at the position it had. */
	samplerPointsMoveCursor(&p, 0);
	ASSERT_TRUE(samplerPointsHasPoint(&p, PT_CURSOR), "the cursor is back");
	ASSERT_EQ(p.cursor.frame, where, "the cursor came back where it was");

	printf("PASS test_delete\n");
	return 0;
}

/* The nearest zero crossing: on a sine, a crossing is at a zero. */
static int test_nearest_zero_crossing(void) {
	const int N = 1024;
	float *data = (float *)malloc((size_t)N * sizeof(float));
	ASSERT_TRUE(data != NULL, "the buffer");
	/* A sine of 16 periods over the buffer: a period is 64 frames. */
	for(int i = 0; i < N; i++) {
		data[i] = sinf(2.0f * (float)M_PI * 16.0f * (float)i / (float)N);
	}
	/* A crossing is at a multiple of 32 frames (the sine's zeros). */
	int z = samplerPointsNearestZeroCrossing(data, N, 40);
	ASSERT_TRUE(z >= 0, "a crossing is found");
	ASSERT_TRUE(abs(z - 32) <= 1, "the crossing is the nearest one to the frame");
	/* From a frame that IS a zero, the crossing is within one frame of
	 * it: the crossing lies between two frames, and the answer is the
	 * frame on one side of it. */
	z = samplerPointsNearestZeroCrossing(data, N, 32);
	ASSERT_TRUE(abs(z - 32) <= 1, "the crossing at a zero is within one frame of it");

	/* No crossing within a whole period: a constant signal has none. */
	for(int i = 0; i < N; i++) {
		data[i] = 1.0f;
	}
	ASSERT_EQ(samplerPointsNearestZeroCrossing(data, N, 100), -1, "a constant signal has no crossing");

	/* The guards. */
	ASSERT_EQ(samplerPointsNearestZeroCrossing(NULL, N, 10), -1, "no data, no crossing");
	ASSERT_EQ(samplerPointsNearestZeroCrossing(data, 0, 0), -1, "no length, no crossing");

	free(data);
	printf("PASS test_nearest_zero_crossing\n");
	return 0;
}

/* Review Focus 2: a loop whose start is at or past its end is
 * rejected -- the read path must never read backwards. */
static int test_validate_rejects_a_backwards_loop(void) {
	SamplerPoints p;
	samplerPointsInit(&p, SAMPLE_LEN);
	ASSERT_TRUE(samplerPointsValidate(&p, SAMPLE_LEN), "the fresh state is valid");

	/* A proper loop is valid. */
	samplerPointsSelect(&p, PT_LOOP_START);
	samplerPointsMoveCursor(&p, 100);
	samplerPointsSelect(&p, PT_LOOP_END);
	samplerPointsMoveCursor(&p, 500);
	ASSERT_TRUE(p.loopStart.frame < p.loopEnd.frame, "the loop runs forwards");
	ASSERT_TRUE(samplerPointsValidate(&p, SAMPLE_LEN), "a forwards loop is valid");

	/* A loop whose start is AT its end is not. */
	p.loopEnd.frame = p.loopStart.frame;
	ASSERT_TRUE(!samplerPointsValidate(&p, SAMPLE_LEN), "a zero-length loop is rejected");

	/* And one whose start is past its end. */
	p.loopEnd.frame = p.loopStart.frame - 10;
	ASSERT_TRUE(!samplerPointsValidate(&p, SAMPLE_LEN), "a backwards loop is rejected");

	/* A loop point that does not exist cannot be backwards. */
	p.loopStart.exists = false;
	p.loopEnd.frame = 0;
	ASSERT_TRUE(samplerPointsValidate(&p, SAMPLE_LEN), "a loop with no start is not backwards");

	printf("PASS test_validate_rejects_a_backwards_loop\n");
	return 0;
}

int main(void) {
	if(test_cursor_moves_and_clamps() != 0) {
		return 1;
	}
	if(test_selected_point_follows_the_cursor() != 0) {
		return 1;
	}
	if(test_point_existence() != 0) {
		return 1;
	}
	if(test_delete() != 0) {
		return 1;
	}
	if(test_nearest_zero_crossing() != 0) {
		return 1;
	}
	if(test_validate_rejects_a_backwards_loop() != 0) {
		return 1;
	}
	printf("PASSED (gui_sampler_points)\n");
	return 0;
}
