#ifndef VIZ_AUDIO_H
#define VIZ_AUDIO_H

#include <stdbool.h>
#include "portaudio.h"

#define VIZ_SAMPLE_RATE 44100
#define VIZ_FRAMES_PER_BLOCK 256
#define VIZ_RING_CAP 8192

typedef struct {
	PaStream *stream;
	int device_index;
	float ring_l[VIZ_RING_CAP];
	float ring_r[VIZ_RING_CAP];
	volatile int write_pos;
	int read_pos;
	bool running;
} AudioCapture;

int audio_init(AudioCapture *ac, const char *device_name);
int audio_drain(AudioCapture *ac, float *l, float *r, int max);
void audio_shutdown(AudioCapture *ac);
void audio_list_devices(void);

#endif
