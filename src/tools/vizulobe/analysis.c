#include <math.h>
#include <string.h>
#include "analysis.h"

void analysis_init(Analysis *a, int fft_bins) {
	memset(a, 0, sizeof(*a));
	a->fft_bins = fft_bins;
	initFFT(&a->fft[0], fft_bins * 2, 256, 1, true, false);
	initFFT(&a->fft[1], fft_bins * 2, 256, 1, true, false);
}

void analysis_push(Analysis *a, float l, float r) {
	a->waveform[0][a->waveform_head] = l;
	a->waveform[1][a->waveform_head] = r;
	a->waveform_head = (a->waveform_head + 1) % VIZ_WAVEFORM_LEN;
	pushFrameToFFT(&a->fft[0], l);
	pushFrameToFFT(&a->fft[1], r);
	a->sum_l += l * l;
	a->sum_r += r * r;
	a->block_len++;
}

void analysis_block_done(Analysis *a) {
	if(a->block_len > 0) {
		a->audio_l = sqrtf(a->sum_l / (float)a->block_len);
		a->audio_r = sqrtf(a->sum_r / (float)a->block_len);
		a->sum_l = 0.0f;
		a->sum_r = 0.0f;
		a->block_len = 0;
	}

	processFFTData(&a->fft[0]);
	processFFTData(&a->fft[1]);

	int freqCount = a->fft[0].freqCount;
	int row = a->fft[0].rowCount - 1;
	if(row < 0) {
		row = 0;
	}
	for(int ch = 0; ch < 2; ch++) {
		for(int bin = 0; bin < a->fft_bins; bin++) {
			int idx = bin + 1; /* skip DC */
			if(idx >= freqCount) {
				idx = freqCount - 1;
			}
			float p = a->fft[ch].vals[row * freqCount + idx];
			a->spectrum[ch][bin] = sqrtf(p);
		}
	}
}
