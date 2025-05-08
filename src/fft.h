#ifndef FFT_H
#define FFT_H

#include <stdbool.h>

#include "kiss_fft.h"
#include "kiss_fftr.h"

#define SP_MAX_ROWS 128

static const float alpha = 0.16f;
static const float bw1_alpha0 = (1.0 - alpha) / 2.0f;
static const float bw1_alpha1 = 0.5f;
static const float bw1_alpha2 = 0.8f;
static const float bw2_alpha0 = 7938.0f / 18608.0f;
static const float bw2_alpha1 = 9240.0f / 18608.0f;
static const float bw2_alpha2 = 1430.0f / 18608.0f;

typedef enum {
	WFT_TRIANGLE,
	WFT_BLACKMAN_ESTIMATED,
	WFT_BLACKMAN_EXACT,
	WFT_HANN,
	WFT_HAMMING,
	WFT_COUNT
} Wf;

typedef float (*WindowFunc)(int index, int length);

typedef struct {
	kiss_fftr_cfg cfg;
	// kiss_fft_scalar *overlapbuf;
	// kiss_fft_scalar *obuf;
	kiss_fft_scalar *tbuf;
	kiss_fft_cpx *fbuf;
	// float *mag2buf;
	int fftSize;
	int freqCount;
	int navg;
	int nbuf;
	int framesPerBuffer;
	int bufferCount;
	int overlapFrames;
	int frameIndex;
	int rowCount;
	int prevRowCount;
	int maxRows;
	int avgctr;
	bool enabled;
	bool removeDc;
	bool cpxOut;
	float *vals;
	kiss_fft_cpx *cpxvals;
	WindowFunc window;
	int selectedWf;
	char *windowFuncName;
} Fft;

float triangularWindow(int index, int length);
float hannWindow(int index, int length);
float hammingWindow(int index, int length);
float blackmanWindowEstimated(int index, int length);
float blackmanWindowExact(int index, int length);

void initFFT(Fft *fft, int fftSize, int framesPerBuffer, int toAverage, bool removeDC, bool cpxOut);
void incWindowFunc(Fft *fft, bool increment);
void pushFrameToFFT(Fft *fft, float frame);
void processFFTData(Fft *fft);
void toggleFFTProcessing(Fft *fft);

#endif
