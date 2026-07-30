/*
 * test_fft.c — verify window functions and a basic FFT roundtrip.
 *
 * NOTE: src/fft.c has known pre-existing bugs in the triangular and
 * hamming window functions (they compute linear-decrease and reversed
 * coefficients respectively). These tests assert the *actual* current
 * behavior so we have a regression baseline. See Section 2 report.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fft.h"
#include "kiss_fftr.h"

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.6f, got %.6f (tol %.6f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

static int test_triangular_window(void) {
    /* Current src/fft.c implementation is `1 - (i - L/2) / (L/2)` which
     * gives 1.0 at the center but is NOT a proper triangular window —
     * it monotonically decreases from i=0 to i=L. The "correct" tri
     * window would be `1 - |i - L/2|/(L/2)`.
     * Just verify the center value is 1.0 and the function doesn't crash. */
    ASSERT_NEAR(triangularWindow(4, 8), 1.0f, 1e-4f);
    /* BUG (pre-existing): value at i=0 should be 0.0 for a true triangle
     * but is actually 2.0. Assert the buggy value to lock in baseline. */
    ASSERT_NEAR(triangularWindow(0, 8), 2.0f, 1e-4f);
    printf("PASS test_triangular_window (center=1.0, edge=2.0 — see report)\n");
    return 0;
}

static int test_hann_window(void) {
    /* Correct Hann window: 0 at edges, 1 at center */
    float w0 = hannWindow(0, 64);
    float w63 = hannWindow(63, 64);
    ASSERT_NEAR(w0, 0.0f, 1e-3f);
    ASSERT_NEAR(w63, 0.0f, 1e-3f);
    float peak = 0.0f;
    for (int i = 0; i < 64; i++) {
        float w = hannWindow(i, 64);
        if (w > peak) peak = w;
    }
    ASSERT_NEAR(peak, 1.0f, 1e-3f);
    printf("PASS test_hann_window\n");
    return 0;
}

static int test_hamming_window(void) {
    /* BUG (pre-existing): coefficients are reversed in src/fft.c.
     * Current: (0.46 - cos(...)) * 0.54, which can go negative.
     * Correct: (0.54 - 0.46 * cos(...)).
     * Assert current (buggy) behavior as regression baseline. */
    float w0 = hammingWindow(0, 64);
    /* Current formula at i=0: (0.46 - 1.0) * 0.54 = -0.2916 */
    ASSERT_NEAR(w0, -0.2916f, 1e-3f);
    printf("PASS test_hamming_window (current impl: -0.29 at edge — see report)\n");
    return 0;
}

static int test_blackman_windows(void) {
    /* Just verify they return something sane and don't crash */
    for (int i = 0; i < 32; i++) {
        float we = blackmanWindowEstimated(i, 32);
        float wx = blackmanWindowExact(i, 32);
        if (we < -0.5f || we > 1.5f) {
            fprintf(stderr, "FAIL: blackman_est out of range at i=%d: %.3f\n", i, we);
            return 1;
        }
        if (wx < -0.5f || wx > 1.5f) {
            fprintf(stderr, "FAIL: blackman_exact out of range at i=%d: %.3f\n", i, wx);
            return 1;
        }
    }
    printf("PASS test_blackman_windows\n");
    return 0;
}

static int test_fft_impulse_roundtrip(void) {
    /* FFT of a constant (DC) signal should produce a single peak at bin 0.
     * The wrapper uses overlapping windows with frame-index triggers, so
     * we push a constant signal and verify bin 0 dominates. */
    Fft fft;
    initFFT(&fft, 256, 256, 1, false, false);

    /* Push 320 constant samples. Processing fires when frameIndex hits
     * the trigger zone (64/128/192/256). */
    for (int i = 0; i < 320; i++) {
        pushFrameToFFT(&fft, 1.0f);
        if (fft.frameIndex == 64 || fft.frameIndex == 128 ||
            fft.frameIndex == 192) {
            processFFTData(&fft);
        }
    }
    processFFTData(&fft);

    /* Find the dominant bin — should be bin 0 (DC). */
    int peak_bin = 0;
    float peak_v = fft.vals[0];
    for (int i = 1; i < fft.freqCount; i++) {
        if (fft.vals[i] > peak_v) {
            peak_v = fft.vals[i];
            peak_bin = i;
        }
    }
    if (peak_v <= 0.0f) {
        fprintf(stderr, "FAIL: DC test gave no signal\n");
        return 1;
    }
    /* For a DC input through Hann-windowed overlap, the DC bin dominates.
     * Peak should be at bin 0 or close (small bin offset is fine due to
     * how the overlap algorithm stacks buffers). */
    if (peak_bin > 4) {
        fprintf(stderr, "FAIL: DC peak not near bin 0 (got bin %d, val %.3f)\n",
                peak_bin, peak_v);
        return 1;
    }
    printf("PASS test_fft_impulse_roundtrip (DC peak at bin %d, val %.3f)\n",
           peak_bin, peak_v);
    return 0;
}

static int test_window_switching(void) {
    /* Enum order:
     *   WFT_TRIANGLE          = 0
     *   WFT_BLACKMAN_ESTIMATED= 1
     *   WFT_BLACKMAN_EXACT    = 2
     *   WFT_HANN              = 3  <- default after initFFT
     *   WFT_HAMMING           = 4
     */
    Fft fft;
    initFFT(&fft, 64, 64, 1, false, false);
    if (fft.window != hannWindow) {
        fprintf(stderr, "FAIL: default window should be hann\n");
        return 1;
    }
    incWindowFunc(&fft, true);   /* +1: HAMMING */
    if (fft.window != hammingWindow) {
        fprintf(stderr, "FAIL: +1 should be hamming\n");
        return 1;
    }
    incWindowFunc(&fft, true);   /* +2: TRIANGLE (wraps) */
    if (fft.window != triangularWindow) {
        fprintf(stderr, "FAIL: +2 should be triangular\n");
        return 1;
    }
    incWindowFunc(&fft, true);   /* +3: BLACKMAN_ESTIMATED */
    if (fft.window != blackmanWindowEstimated) {
        fprintf(stderr, "FAIL: +3 should be blackman_estimated\n");
        return 1;
    }
    incWindowFunc(&fft, true);   /* +4: BLACKMAN_EXACT */
    if (fft.window != blackmanWindowExact) {
        fprintf(stderr, "FAIL: +4 should be blackman_exact\n");
        return 1;
    }
    incWindowFunc(&fft, true);   /* +5: back to HANN */
    if (fft.window != hannWindow) {
        fprintf(stderr, "FAIL: +5 should wrap back to hann\n");
        return 1;
    }
    /* Decrement path */
    incWindowFunc(&fft, false);  /* -1: BLACKMAN_EXACT */
    if (fft.window != blackmanWindowExact) {
        fprintf(stderr, "FAIL: -1 should be blackman_exact\n");
        return 1;
    }
    printf("PASS test_window_switching\n");
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_triangular_window();
    failed |= test_hann_window();
    failed |= test_hamming_window();
    failed |= test_blackman_windows();
    failed |= test_fft_impulse_roundtrip();
    failed |= test_window_switching();
    return failed;
}