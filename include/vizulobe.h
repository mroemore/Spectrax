#ifndef VIZULOBE_H
#define VIZULOBE_H

#include "raylib.h"

#define VIZ_WAVEFORM_LEN 1024
#define VIZ_SPECTRUM_MAX 1024
#define VIZ_SCREEN_W 1280
#define VIZ_SCREEN_H 800

typedef struct viz_t {
	float time;
	float dt;
	float waveform[2][VIZ_WAVEFORM_LEN];
	float spectrum[2][VIZ_SPECTRUM_MAX];
	int fft_bins;
	float audio_l;
	float audio_r;
	int rect_w;
	int rect_h;
	Texture2D backbuffer;
} viz_t;

typedef void (*viz_init_fn)(viz_t *ctx);
typedef void (*viz_frame_fn)(viz_t *ctx);

#endif
