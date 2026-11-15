/* test_ui_preview_mock.c — the preview's synthetic data, as pure functions.
 *
 * The panel window draws whatever the mock driver last wrote, so the driver's
 * arithmetic is what decides whether a shot is reproducible. It is therefore
 * a function of the frame counter alone (no wall clock, no rand()), and it
 * avoids libm: the mock runs in the same 60 fps loop as the app's own maths,
 * which never calls sinf/cosf. */

#include <stdio.h>
#include <string.h>

#include "ui_preview_mock.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while(0)

#define ASSERT_INT(got, want, msg) do { \
	long g_ = (long)(got), w_ = (long)(want); \
	if(g_ != w_) { \
		fprintf(stderr, "FAIL %s:%d: %s (got %ld, want %ld)\n", \
		        __FILE__, __LINE__, msg, g_, w_); \
		return 1; \
	} \
} while(0)

static int test_subject(void) {
	ASSERT_INT(previewMockSubject(0, 3, 90), 0, "first hold is subject 0");
	ASSERT_INT(previewMockSubject(89, 3, 90), 0, "last frame of the hold");
	ASSERT_INT(previewMockSubject(90, 3, 90), 1, "next hold advances");
	ASSERT_INT(previewMockSubject(270, 3, 90), 0, "wraps after the last subject");
	ASSERT_INT(previewMockSubject(5, 0, 90), 0, "no subjects is 0");
	ASSERT_INT(previewMockSubject(5, 3, 0), 0, "no hold window is 0");
	return 0;
}

static int test_triangle(void) {
	/* Sweep a period that does not divide the frame range: both extremes
	 * must be reached, and never exceeded. */
	int lo = 100, hi = -100;
	for(int f = 0; f < 400; f++) {
		int v = previewMockTriangle(f, 37, 10, 0);
		if(v < lo) lo = v;
		if(v > hi) hi = v;
	}
	ASSERT_INT(lo, -10, "triangle reaches the negative peak");
	ASSERT_INT(hi, 10, "triangle reaches the positive peak");
	ASSERT_INT(previewMockTriangle(0, 0, 10, 0), 0, "no period means no motion");
	ASSERT_INT(previewMockTriangle(0, 40, 10, 128), 0, "half-period phase starts centred");
	return 0;
}

static int test_sample(void) {
	float lo = 9.0f, hi = -9.0f;
	for(int f = 0; f < 480; f++) {
		float s = previewMockSample(f);
		if(s < lo) lo = s;
		if(s > hi) hi = s;
	}
	ASSERT_TRUE(lo >= -1.0f && hi <= 1.0f, "samples stay in [-1,1]");
	ASSERT_TRUE(hi - lo > 0.5f, "samples are not a constant");
	ASSERT_TRUE(previewMockSample(0) != previewMockSample(7), "samples vary per frame");
	return 0;
}

static int test_playhead(void) {
	ASSERT_INT(previewMockPlayhead(0, 16, 8), 0, "starts at row 0");
	ASSERT_INT(previewMockPlayhead(7, 16, 8), 0, "holds for framesPerRow");
	ASSERT_INT(previewMockPlayhead(8, 16, 8), 1, "advances after framesPerRow");
	ASSERT_INT(previewMockPlayhead(120, 16, 8), 15, "reaches the last row");
	ASSERT_INT(previewMockPlayhead(128, 16, 8), 0, "wraps to row 0");
	ASSERT_INT(previewMockPlayhead(9, 0, 8), 0, "no rows is row 0");
	ASSERT_INT(previewMockPlayhead(9, 16, 0), 0, "no step length is row 0");
	return 0;
}

static int test_clamp(void) {
	ASSERT_TRUE(previewMockClampToRange(5.0f, 0.0f, 3.0f) == 3.0f, "clamps high");
	ASSERT_TRUE(previewMockClampToRange(-5.0f, 0.0f, 3.0f) == 0.0f, "clamps low");
	ASSERT_TRUE(previewMockClampToRange(2.0f, 0.0f, 3.0f) == 2.0f, "leaves inside values");
	ASSERT_TRUE(previewMockClampToRange(1.0f, 3.0f, 0.0f) == 3.0f, "an inverted range clamps to min");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_subject();
	rc |= test_triangle();
	rc |= test_sample();
	rc |= test_playhead();
	rc |= test_clamp();
	if(rc) {
		fprintf(stderr, "FAILED (ui preview mock)\n");
		return 1;
	}
	printf("PASSED (ui preview mock)\n");
	return 0;
}