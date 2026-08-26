#include <math.h>
#include <stdio.h>

#include "vizfx.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static int test_silence_collapses_to_middle_row(void) {
	float buf[SCROLLER_BUFFER_SIZE];
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		buf[i] = 0.0f;
	}
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT);
	int mid = (int)((0.0f + 1.0f) * 0.5f * (float)(SCROLLER_COLUMN_HEIGHT - 1));
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		ASSERT_TRUE(lo[r] == mid);
		ASSERT_TRUE(hi[r] == mid);
	}
	printf("PASS test_silence_collapses_to_middle_row\n");
	return 0;
}

static int test_full_scale_collapses_to_top_row(void) {
	float buf[SCROLLER_BUFFER_SIZE];
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		buf[i] = 1.0f;
	}
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT);
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		ASSERT_TRUE(lo[r] == SCROLLER_COLUMN_HEIGHT - 1);
		ASSERT_TRUE(hi[r] == SCROLLER_COLUMN_HEIGHT - 1);
	}
	printf("PASS test_full_scale_collapses_to_top_row\n");
	return 0;
}

static int test_sine_lo_hi_hit_both_edges(void) {
	float buf[SCROLLER_BUFFER_SIZE];
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		buf[i] = sinf(2.0f * 3.14159265f * i / SCROLLER_BUFFER_SIZE);
	}
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT);
	int hitLo = 0;
	int hitHi = 0;
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		if(lo[r] == 0) {
			hitLo = 1;
		}
		if(hi[r] == SCROLLER_COLUMN_HEIGHT - 1) {
			hitHi = 1;
		}
	}
	ASSERT_TRUE(hitLo);
	ASSERT_TRUE(hitHi);
	printf("PASS test_sine_lo_hi_hit_both_edges\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_silence_collapses_to_middle_row();
	failed |= test_full_scale_collapses_to_top_row();
	failed |= test_sine_lo_hi_hit_both_edges();

	printf("\n%s (3 tests, %s)\n",
	       failed ? "FAILED" : "PASSED", failed ? ">=1 failed" : "all passed");
	return failed;
}