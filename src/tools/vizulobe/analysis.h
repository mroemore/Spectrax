#ifndef VIZ_ANALYSIS_H
#define VIZ_ANALYSIS_H

#include "fft.h"
#include "vizulobe.h"

typedef struct {
	Fft fft[2];
	float waveform[2][VIZ_WAVEFORM_LEN];
	float spectrum[2][VIZ_SPECTRUM_MAX];
	int fft_bins;
	int waveform_head;
	int block_len;
	float sum_l, sum_r;
	float audio_l, audio_r;
} Analysis;

void analysis_init(Analysis *a, int fft_bins);
void analysis_push(Analysis *a, float l, float r);
void analysis_block_done(Analysis *a);

#endif
