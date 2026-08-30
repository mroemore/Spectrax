#include <math.h>
#include <stdio.h>

#include "raylib.h"
#include "theme.h"
#include "vizfx.h"

/* vizfx.c now resolves colours through getColourScheme() (gui.c symbol).
 * gui.c lives in app_only_sources and is not linked by this test (which
 * only links core_lib + vizfx_lib). Provide a stub that returns a static
 * default ColourScheme so the symbol resolves. The defaults here need not
 * match the app defaults — vizfx's data ops never read colours, so this
 * stub exists purely to satisfy the linker. See test_sequencer.c for the
 * same pattern around rebuildPatternGraph(). */
static ColourScheme test_vizfx_default_scheme = {
	.waveformBg = { 0, 0, 0, 255 },
	.vline = { 60, 255, 150, 255 },
	.poly = { 255, 80, 80, 255 },
	.modStripLfo = { 0, 255, 255, 255 },
	.modStripEnv = { 130, 255, 130, 255 },
	.modStripRnd = { 255, 80, 255, 255 },
	.modStripOfs = { 190, 190, 190, 255 },
	.modStripDefault = { 210, 210, 210, 255 },
};

ColourScheme *getColourScheme(void) {
	return &test_vizfx_default_scheme;
}

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
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT, 1.0f);
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
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT, 1.0f);
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
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT, 1.0f);
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
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT, 1.0f);
	Color wave = { 60, 255, 150, 255 };
	Color out[SCROLLER_COLUMN_HEIGHT];
	packScrollerColumn(lo, hi, SCROLLER_COLUMN_HEIGHT, out, wave);
	ASSERT_TRUE(out[0].r == wave.r);
	ASSERT_TRUE(out[SCROLLER_COLUMN_HEIGHT - 1].r == wave.r);
	printf("PASS test_pack_column_sine_edges\n");
	return 0;
}

static int test_collapse_log_scales_low_amplitude(void) {
	float buf[SCROLLER_BUFFER_SIZE];
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		buf[i] = 0.2f;
	}
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT, 1.0f);
	int mid = (int)((0.0f + 1.0f) * 0.5f * (float)(SCROLLER_COLUMN_HEIGHT - 1));
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		ASSERT_TRUE(hi[r] >= mid + 15);
	}
	printf("PASS test_collapse_log_scales_low_amplitude\n");
	return 0;
}

static int test_collapse_gain_scales_to_full_scale(void) {
	float buf[SCROLLER_BUFFER_SIZE];
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		buf[i] = 0.2f;
	}
	unsigned char lo[SCROLLER_COLUMN_HEIGHT];
	unsigned char hi[SCROLLER_COLUMN_HEIGHT];
	collapseBufferToColumn(buf, SCROLLER_BUFFER_SIZE, lo, hi, SCROLLER_COLUMN_HEIGHT, 5.0f);
	for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
		ASSERT_TRUE(hi[r] == SCROLLER_COLUMN_HEIGHT - 1);
	}
	printf("PASS test_collapse_gain_scales_to_full_scale\n");
	return 0;
}

static int test_push_gain_from_window_peak(void) {
	BufferScroller bs = { 0 };
	for(int i = 0; i < SCROLLER_BUFFER_SIZE; i++) {
		pushBufferScrollerFrame(&bs, 0.2f);
	}
	ASSERT_TRUE(bs.pendingCount == 1);
	ASSERT_TRUE(bs.pendingHi[0][0] == SCROLLER_COLUMN_HEIGHT - 1);
	ASSERT_TRUE(bs.pendingHi[0][SCROLLER_COLUMN_HEIGHT - 1] == SCROLLER_COLUMN_HEIGHT - 1);
	printf("PASS test_push_gain_from_window_peak\n");
	return 0;
}

static int test_mix_ring_read_order(void) {
	MixRing r;
	initMixRing(&r);
	for(int i = 0; i < MIX_RING_LEN; i++) {
		pushMixRingSample(&r, (float)i);
	}
	ASSERT_TRUE(r.count == MIX_RING_LEN);
	for(int k = 0; k < MIX_RING_LEN; k++) {
		float s = r.samples[(r.writeIndex + k) % MIX_RING_LEN];
		ASSERT_TRUE(fabsf(s - (float)k) < 0.001f);
	}
	printf("PASS test_mix_ring_read_order\n");
	return 0;
}

static int test_mix_ring_wrap_drops_oldest(void) {
	MixRing r;
	initMixRing(&r);
	for(int i = 0; i < MIX_RING_LEN + 100; i++) {
		pushMixRingSample(&r, (float)i);
	}
	ASSERT_TRUE(r.count == MIX_RING_LEN);
	for(int k = 0; k < MIX_RING_LEN; k++) {
		float s = r.samples[(r.writeIndex + k) % MIX_RING_LEN];
		ASSERT_TRUE(fabsf(s - (float)(k + 100)) < 0.001f);
	}
	printf("PASS test_mix_ring_wrap_drops_oldest\n");
	return 0;
}

static int test_mix_ring_partial_count(void) {
	MixRing r;
	initMixRing(&r);
	for(int i = 0; i < 50; i++) {
		pushMixRingSample(&r, (float)i);
	}
	ASSERT_TRUE(r.count == 50);
	int tailStart = MIX_RING_LEN - 50;
	for(int k = 0; k < tailStart; k++) {
		float s = r.samples[(r.writeIndex + k) % MIX_RING_LEN];
		ASSERT_TRUE(fabsf(s) < 0.001f);
	}
	for(int k = tailStart; k < MIX_RING_LEN; k++) {
		float s = r.samples[(r.writeIndex + k) % MIX_RING_LEN];
		ASSERT_TRUE(fabsf(s - (float)(k - tailStart)) < 0.001f);
	}
	printf("PASS test_mix_ring_partial_count\n");
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
	failed |= test_collapse_log_scales_low_amplitude();
	failed |= test_collapse_gain_scales_to_full_scale();
	failed |= test_push_gain_from_window_peak();
	failed |= test_mix_ring_read_order();
	failed |= test_mix_ring_wrap_drops_oldest();
	failed |= test_mix_ring_partial_count();

	printf("\n%s (14 tests, %s)\n",
	       failed ? "FAILED" : "PASSED", failed ? ">=1 failed" : "all passed");
	return failed;
}