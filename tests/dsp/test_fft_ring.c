/* test_fft_ring.c — the spectrogram's row ring, where a real device SIGSEGV
 * lived.
 *
 * The audio thread fills Fft.vals a row at a time and publishes a row cursor;
 * the main thread (updateSpectrogramData) draws the rows that appeared since
 * its last visit. The old bookkeeping wrapped the cursor with
 * `rowCount -= maxRows; prevRowCount -= maxRows;` — moving the *reader's*
 * cursor from the audio thread. When the reader lagged across a wrap,
 * prevRowCount went negative and updateSpectrogramData indexed vals with it,
 * reading far before the allocation. On the RG35XX SP that was a SIGSEGV in
 * updateSpectrogramData ~1 s after the audio fallback stream started.
 *
 * The fix moved the wrap to the write (the cursor never leaves [0, maxRows])
 * and made the reader clamp a cursor the producer passed. This test drives
 * the real FFT producer/reader pair through the interleavings that crash:
 * a reader exactly on the wrap boundary, and one far behind it. Under ASan the
 * vals reads are bounds-checked, so the old code fails here too.
 *
 * No audio device, no window: initFFT/processFFTData/fftPendingRows are pure.
 */

#include <stdio.h>
#include <string.h>

#include "fft.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while(0)

/* The transform produces one row per overlapFrames (fftSize / bufferCount)
 * pushed frames. Feeding and processing per frame keeps the count exact. */
static int feedFrames(Fft *fft, int frames) {
	for(int i = 0; i < frames; i++) {
		pushFrameToFFT(fft, 0.25f);
		processFFTData(fft);
	}
	return 0;
}

static int rowsProduced(Fft *fft) {
	return atomic_load_explicit(&fft->rowCount, memory_order_acquire);
}

/* Push frames until the producer has published `target` more rows. The
 * transform's cadence is not exactly one row per overlapFrames (the frame
 * counter wraps at fftSize), so watch the cursor rather than count frames. */
static int feedRows(Fft *fft, int target) {
	for(int r = 0; r < target; r++) {
		int before = rowsProduced(fft);
		int waited = 0;
		do {
			feedFrames(fft, 1);
			if(++waited > fft->fftSize * 2) {
				fprintf(stderr, "FAIL: no row produced in %d frames\n", fft->fftSize * 2);
				return 1;
			}
		} while(rowsProduced(fft) == before);
	}
	return 0;
}

/* The reader's whole contract: every row it is handed is a valid index into
 * vals. Reading the row here is what ASan sees (the old code read row -2). */
static int readPending(Fft *fft, int *firstOut, int *lastOut) {
	int first = 0;
	int last = 0;
	if(!fftPendingRows(fft, &first, &last)) {
		return 0;
	}
	ASSERT_TRUE(first >= 0, "first row is negative");
	ASSERT_TRUE(last <= fft->maxRows, "last row past the ring");
	ASSERT_TRUE(first < last, "empty pending range claimed as data");
	for(int r = first; r < last; r++) {
		ASSERT_TRUE(r >= 0 && r < fft->maxRows, "row index outside the ring");
		/* The read itself is the bounds assertion. */
		volatile float sample = fft->vals[r * fft->freqCount];
		(void)sample;
	}
	fft->prevRowCount = last;
	if(firstOut) {
		*firstOut = first;
	}
	if(lastOut) {
		*lastOut = last;
	}
	return 1;
}

static int test_sequential_rows(void) {
	Fft fft;
	initFFT(&fft, 512, 64, 1, false, false);
	ASSERT_TRUE(rowsProduced(&fft) == 0, "fresh FFT already has rows");
	ASSERT_TRUE(!fftPendingRows(&fft, &(int){0}, &(int){0}), "pending rows before any data");

	for(int i = 0; i < 4; i++) {
		int first = -1, last = -1;
		ASSERT_TRUE(feedRows(&fft, 1) == 0, "feeding one row");
		ASSERT_TRUE(readPending(&fft, &first, &last), "a produced row must be readable");
		ASSERT_TRUE(first == i && last == i + 1, "rows are handed over in order, once");
	}
	ASSERT_TRUE(!readPending(&fft, NULL, NULL), "a caught-up reader has nothing to draw");

	printf("PASS test_sequential_rows\n");
	return 0;
}

/* The crash interleaving: the reader catches up to row 126, the producer then
 * crosses the wrap with two rows. The old writer moved prevRowCount to -2. */
static int test_reader_on_the_wrap(void) {
	Fft fft;
	initFFT(&fft, 512, 64, 1, false, false);

	ASSERT_TRUE(feedRows(&fft, fft.maxRows - 1) == 0, "filling up to the wrap");
	ASSERT_TRUE(readPending(&fft, NULL, NULL), "pre-wrap rows readable");
	ASSERT_TRUE(fft.prevRowCount == fft.maxRows - 1, "reader caught up to the row before the wrap");

	ASSERT_TRUE(feedRows(&fft, 2) == 0, "crossing the wrap");  /* row 127, then row 0 */
	ASSERT_TRUE(rowsProduced(&fft) == 1, "the wrap leaves the cursor at 1");

	int first = -1, last = -1;
	ASSERT_TRUE(readPending(&fft, &first, &last), "post-wrap rows readable");
	ASSERT_TRUE(first == 0 && last == 1, "a reader past the wrap restarts at the oldest live row");

	printf("PASS test_reader_on_the_wrap\n");
	return 0;
}

/* A reader that fell further behind than the ring holds: the rows it missed
 * are gone, and the range still has to be a valid slice of the live ring. */
static int test_reader_far_behind(void) {
	Fft fft;
	initFFT(&fft, 512, 64, 1, false, false);

	ASSERT_TRUE(feedRows(&fft, fft.maxRows * 2 + 7) == 0, "producing two rings of rows");
	int first = -1, last = -1;
	ASSERT_TRUE(readPending(&fft, &first, &last), "a long-idle reader still has live rows");
	ASSERT_TRUE(first == 0, "the oldest live row is 0");
	ASSERT_TRUE(last == rowsProduced(&fft), "the range ends at the live edge");

	/* And it keeps working on the other side of the next wrap. */
	ASSERT_TRUE(feedRows(&fft, fft.maxRows - 1) == 0, "producing another half ring");
	ASSERT_TRUE(readPending(&fft, NULL, NULL), "rows after the next wrap are readable");

	printf("PASS test_reader_far_behind\n");
	return 0;
}

/* Sustained traffic: read with a varying lag through several wraps; every
 * range the reader is handed must stay inside the ring. */
static int test_sustained_wraps(void) {
	Fft fft;
	initFFT(&fft, 512, 64, 1, false, false);

	int lag = 0;
	for(int i = 0; i < fft.maxRows * 6; i++) {
		ASSERT_TRUE(feedRows(&fft, 1) == 0, "feeding one row");
		lag = (lag + 1) % 5;
		if(lag != 0) {
			continue;
		}
		ASSERT_TRUE(readPending(&fft, NULL, NULL), "a produced row was lost");
	}
	while(readPending(&fft, NULL, NULL)) {
	}
	ASSERT_TRUE(fft.prevRowCount >= 0 && fft.prevRowCount <= fft.maxRows, "cursor left the ring");

	printf("PASS test_sustained_wraps\n");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_sequential_rows();
	rc |= test_reader_on_the_wrap();
	rc |= test_reader_far_behind();
	rc |= test_sustained_wraps();
	if(rc != 0) {
		fprintf(stderr, "FAILED (fft ring)\n");
		return 1;
	}
	printf("PASSED (fft ring)\n");
	return 0;
}
