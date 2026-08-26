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

static int test_push_collects_column_each_buffer(void) {
	BufferScroller bs = { 0 };
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		pushBufferScrollerFrame(&bs, 0.0f);
	}
	ASSERT_TRUE(bs.pendingCount == 1);
	ASSERT_TRUE(bs.framePos == 0);
	pushBufferScrollerFrame(&bs, 0.0f);
	ASSERT_TRUE(bs.framePos == 1);
	ASSERT_TRUE(bs.pendingCount == 1);
	printf("PASS test_push_collects_column_each_buffer\n");
	return 0;
}

static int test_push_ring_never_exceeds_capacity(void) {
	BufferScroller bs = { 0 };
	int total = SCROLLER_BUFFER_SIZE * (SCROLLER_PENDING_CAPACITY + 1);
	for(int i = 0; i < total; i++) {
		pushBufferScrollerFrame(&bs, 1.0f);
	}
	ASSERT_TRUE(bs.pendingCount == SCROLLER_PENDING_CAPACITY);
	ASSERT_TRUE(bs.pendingHead == 1);
	ASSERT_TRUE(bs.pendingTail == 1);
	printf("PASS test_push_ring_never_exceeds_capacity\n");
	return 0;
}

static int test_pack_column_silence_center_pixel(void) {
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	int mid = (int)(0.5f * (float)(SCROLLER_COLUMN_HEIGHT - 1));
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		lo[r] = mid;
		hi[r] = mid;
	}
	Color wave = { 60, 255, 150, 255 };
	Color out[SCROLLER_COLUMN_HEIGHT];
	packScrollerColumn(lo, hi, SCROLLER_COLUMN_HEIGHT, out, wave);
	int painted = 0;
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		if(out[r].r == wave.r && out[r].g == wave.g) {
			painted++;
		}
	}
	ASSERT_TRUE(painted == 1);
	ASSERT_TRUE(out[SCROLLER_COLUMN_HEIGHT - 1 - mid].r == wave.r);
	printf("PASS test_pack_column_silence_center_pixel\n");
	return 0;
}

static int test_pack_column_full_scale_top_row(void) {
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		lo[r] = SCROLLER_COLUMN_HEIGHT - 1;
		hi[r] = SCROLLER_COLUMN_HEIGHT - 1;
	}
	Color wave = { 60, 255, 150, 255 };
	Color out[SCROLLER_COLUMN_HEIGHT];
	packScrollerColumn(lo, hi, SCROLLER_COLUMN_HEIGHT, out, wave);
	int painted = 0;
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		if(out[r].r == wave.r) {
			painted++;
		}
	}
	ASSERT_TRUE(painted == 1);
	ASSERT_TRUE(out[0].r == wave.r);
	printf("PASS test_pack_column_full_scale_top_row\n");
	return 0;
}

static int test_pack_column_sine_edges(void) {
	float buf[SCROLLER_BUFFER_SIZE];
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		buf[i] = sinf(2.0f * 3.14159265f * i / SCROLLER_BUFFER_SIZE);
	}
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT);
	Color wave = { 60, 255, 150, 255 };
	Color out[SCROLLER_COLUMN_HEIGHT];
	packScrollerColumn(lo, hi, SCROLLER_COLUMN_HEIGHT, out, wave);
	ASSERT_TRUE(out[0].r == wave.r);
	ASSERT_TRUE(out[SCROLLER_COLUMN_HEIGHT - 1].r == wave.r);
	printf("PASS test_pack_column_sine_edges\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_silence_collapses_to_middle_row();
	failed |= test_full_scale_collapses_to_top_row();
	failed |= test_sine_lo_hi_hit_both_edges();
	failed |= test_push_collects_column_each_buffer();
	failed |= test_push_ring_never_exceeds_capacity();
	failed |= test_pack_column_silence_center_pixel();
	failed |= test_pack_column_full_scale_top_row();
	failed |= test_pack_column_sine_edges();

	printf("\n%s (8 tests, %s)\n",
	       failed ? "FAILED" : "PASSED", failed ? ">=1 failed" : "all passed");
	return failed;
}