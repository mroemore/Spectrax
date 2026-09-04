#include <assert.h>
#include <math.h>
#include <string.h>
#include "analysis.h"

#define SR 44100.0f

int main(void) {
	Analysis a;
	analysis_init(&a, 512);
	assert(a.fft_bins == 512);
	assert(a.fft[0].fftSize == 1024);

	/* waveform ring: push > 1024 samples; head wraps, latest 1024 retained */
	for(int i = 0; i < 1024 + 100; i++) {
		analysis_push(&a, (float)i, (float)(-i));
	}
	assert(a.waveform_head == 100 % VIZ_WAVEFORM_LEN);
	assert(a.waveform[0][0] == 1024.0f);
	assert(a.waveform[1][0] == -1024.0f);
	assert(a.waveform[0][VIZ_WAVEFORM_LEN - 1] == 1023.0f);

	/* RMS + spectrum: push 9 blocks of 256 (2304 samples) so the Fft's
	   staggered sub-buffers are fully populated with valid data (a single
	   block leaves 75% of the window as uninitialized malloc garbage that
	   MALLOC_PERTURB_ turns into inf, breaking the peak assert). */
	Analysis b;
	analysis_init(&b, 512);
	for(int blk = 0; blk < 9; blk++) {
		for(int i = 0; i < 256; i++) {
			float v = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * i / SR);
			analysis_push(&b, v, v);
		}
		analysis_block_done(&b);
	}
	assert(fabsf(b.audio_l - 0.5f * 0.70710678f) < 0.02f);
	assert(fabsf(b.audio_r - 0.5f * 0.70710678f) < 0.02f);

	/* spectrum: peak bin near 440 Hz. bin = freq * fftSize / SR = 440*1024/44100 ~ 10.2 */
	int peak = 0;
	for(int i = 1; i < 512; i++) {
		if(b.spectrum[0][i] > b.spectrum[0][peak]) {
			peak = i;
		}
	}
	assert(peak >= 8 && peak <= 12);

	printf("test_analysis OK\n");
	return 0;
}
