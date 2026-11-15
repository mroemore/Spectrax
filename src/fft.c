#include "fft.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static char *wfNames[WFT_COUNT] = {
	"TRIANGLE",
	"BLACKMAN ESTIMATED",
	"BLACKMAN EXACT",
	"HANN",
	"HAMMING"
};

float triangularWindow(int index, int length) {
	float l2 = length / 2.0f;
	return 1.0f - (index - l2) / l2;
}

float hannWindow(int index, int length) {
	return (1.0f - cos((index * 2.0f * M_PI) / (length - 1.0f))) * 0.5f;
}

float hammingWindow(int index, int length) {
	return (0.46f - cos((index * 2.0f * M_PI) / (length - 1.0f))) * 0.54f;
}

float blackmanWindowEstimated(int index, int length) {
	return bw1_alpha0 - bw1_alpha1 * cos((2 * M_PI * index) / length) + bw1_alpha2 * cos((2 * M_PI * index) / length);
}

float blackmanWindowExact(int index, int length) {
	return bw2_alpha0 - bw2_alpha1 * cos((2 * M_PI * index) / length) + bw2_alpha2 * cos((2 * M_PI * index) / length);
}

void initFFT(Fft *fft, int fftSize, int framesPerBuffer, int toAverage, bool removeDC, bool cpxOut) {
	fft->fftSize = fftSize;
	fft->freqCount = fft->fftSize / 2 + 1;
	fft->navg = toAverage;
	fft->removeDc = removeDC;

	atomic_init(&fft->rowCount, 0);
	fft->prevRowCount = 0;
	fft->maxRows = 128;

	fft->nbuf = 0;
	fft->avgctr = 0;
	fft->frameIndex = 0;
	fft->bufferCount = 4;
	fft->framesPerBuffer = framesPerBuffer;
	fft->overlapFrames = (fftSize / fft->bufferCount);
	fft->cfg = kiss_fftr_alloc(fft->fftSize, 0, 0, 0);
	/* Zero the transform buffers: processFFTData() reads them before they are
	 * fully populated, so malloc garbage would leak uninitialised values into
	 * the spectrum (valgrind: "conditional jump depends on uninitialised
	 * value" in sqrtf). calloc makes silence the default. */
	fft->tbuf = (kiss_fft_scalar *)calloc((size_t)fft->fftSize * fft->bufferCount, sizeof(kiss_fft_scalar));
	fft->fbuf = (kiss_fft_cpx *)calloc((size_t)fft->freqCount, sizeof(kiss_fft_cpx));
	fft->vals = (float *)calloc((size_t)fft->maxRows * fft->freqCount, sizeof(float));
	fft->cpxvals = (kiss_fft_cpx *)calloc((size_t)fft->freqCount * fft->maxRows, sizeof(kiss_fft_cpx));
	fft->window = hannWindow;
	fft->selectedWf = WFT_HANN;
	fft->windowFuncName = wfNames[fft->selectedWf];

	fft->enabled = true;
}

void incWindowFunc(Fft *fft, bool increment) {
	if(increment) {
		fft->selectedWf++;
		if(fft->selectedWf >= WFT_COUNT) {
			fft->selectedWf -= WFT_COUNT;
		}
	} else {
		fft->selectedWf--;
		if(fft->selectedWf < 0) {
			fft->selectedWf += WFT_COUNT;
		}
	}

	switch(fft->selectedWf) {
		case WFT_TRIANGLE:
			fft->window = triangularWindow;
			break;
		case WFT_HANN:
			fft->window = hannWindow;
			break;
		case WFT_BLACKMAN_EXACT:
			fft->window = blackmanWindowExact;
			break;
		case WFT_HAMMING:
			fft->window = hammingWindow;
			break;
		default:
		case WFT_BLACKMAN_ESTIMATED:
			fft->window = blackmanWindowEstimated;
			break;
	}

	fft->windowFuncName = wfNames[fft->selectedWf];
}

void pushFrameToFFT(Fft *fft, float frame) {
	if(!fft->enabled) {
		return;
	}
	for(int i = 0; i < fft->bufferCount; i++) {
		int index = (fft->frameIndex - (i * fft->overlapFrames));
		index = index < 0 ? index + fft->fftSize : index;
		fft->tbuf[index + (i * fft->fftSize)] = frame * fft->window(index, fft->fftSize);
	}
	fft->frameIndex++;
	fft->frameIndex %= fft->fftSize;
}

void processFFTData(Fft *fft) {
	if(!fft->enabled) {
		return;
	}
	kiss_fft_scalar *buffPtr = NULL;
	int tbufIndex = 0;
	for(int i = fft->bufferCount - 1; i >= 0; i--) {
		if((fft->frameIndex + (i * fft->overlapFrames)) == fft->fftSize) {
			buffPtr = &fft->tbuf[(fft->bufferCount - 1 - i) * fft->fftSize];
			tbufIndex = fft->bufferCount - 1 - i;
		}
	}

	if(buffPtr) {
		int i = 0;

		// for(int i = 0; i < fft->fftSize; i++) {
		// 	fft->vals[(fft->rowCount - 1) * fft->fftSize + i] = (buffPtr[i]);
		// }
		if(fft->removeDc) {
			float avg = 0;
			for(i = 0; i < fft->fftSize; ++i) {
				avg += buffPtr[i];
			}
			avg /= fft->fftSize;
			for(i = 0; i < fft->fftSize; ++i) {
				buffPtr[i] -= (kiss_fft_scalar)avg;
			}
		}

		kiss_fftr(fft->cfg, buffPtr, fft->fbuf);

		/* Write the row first, then publish its presence. The reader runs on
		 * the main thread; incrementing the cursor before the samples were in
		 * place would let it draw a row that is still being filled. The ring
		 * wraps here, at the write, so rowCount stays in [0, maxRows] and the
		 * reader never has to subtract maxRows from its own cursor (that
		 * subtraction is what drove prevRowCount negative and OOB). */
		int row = atomic_load_explicit(&fft->rowCount, memory_order_relaxed);
		if(row >= fft->maxRows) {
			row = 0;
		}

		if(fft->cpxOut) {
			for(int i = 0; i < fft->freqCount; ++i) {
				fft->cpxvals[row * fft->freqCount + i] = fft->fbuf[i];
			}
		} else {
			for(int i = 0; i < fft->freqCount; ++i) {
				fft->vals[row * fft->freqCount + i] = fft->fbuf[i].r * fft->fbuf[i].r + fft->fbuf[i].i * fft->fbuf[i].i;
			}
		}
		atomic_store_explicit(&fft->rowCount, row + 1, memory_order_release);
		// if(tbufIndex == 0) {
		// 	fft->vals[(fft->rowCount - 1) * fft->freqCount + 256] = 1.0;
		// }

		// if(++fft->avgctr == fft->navg) {
		// 	fft->avgctr = 0;
		// 	++fft->rowCount;
		// 	//fft->vals = (float *)realloc(fft->vals, sizeof(float) * fft->rowCount * fft->freqCount);
		// 	float eps = 1;
		// 	for(int i = 0; i < fft->freqCount; ++i) {
		// 		fft->vals[(fft->rowCount - 1) * fft->freqCount + i] = 10 * log10(fft->mag2buf[i] / fft->navg + eps);
		// 	}
		// 	memset(fft->mag2buf, 0, sizeof(fft->mag2buf[0]) * fft->freqCount);
		// }
	}
}

void toggleFFTProcessing(Fft *fft) {
	fft->enabled = !fft->enabled;
	if(!fft->enabled) {
		fft->frameIndex = 0;
	}
}

bool fftPendingRows(Fft *fft, int *first, int *last) {
	int rowCount = atomic_load_explicit(&fft->rowCount, memory_order_acquire);
	int start = fft->prevRowCount;

	/* The producer wrapped the ring past the reader: the rows it had not
	 * drawn were overwritten, so start at the oldest live row. A negative
	 * cursor is equally impossible to index with; treat it the same way
	 * rather than trusting it. */
	if(start < 0 || start > rowCount) {
		start = 0;
	}
	if(start >= rowCount) {
		return false;
	}

	*first = start;
	*last = rowCount;
	return true;
}
