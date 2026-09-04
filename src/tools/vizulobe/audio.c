#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

static int pa_capture_callback(const void *input, void *output,
	unsigned long frame_count, const PaStreamCallbackTimeInfo *time_info,
	PaStreamCallbackFlags status_flags, void *user_data) {
	(void)output;
	(void)time_info;
	AudioCapture *ac = (AudioCapture *)user_data;
	const float *in = (const float *)input;
	if(!in) {
		return paContinue;
	}
	int wp = ac->write_pos;
	for(unsigned long i = 0; i < frame_count; i++) {
		ac->ring_l[wp] = in[i * 2];
		ac->ring_r[wp] = in[i * 2 + 1];
		wp = (wp + 1) % VIZ_RING_CAP;
	}
	ac->write_pos = wp;
	return paContinue;
}

int audio_drain(AudioCapture *ac, float *l, float *r, int max) {
	if(!ac->stream) {
		return 0;
	}
	int wp = ac->write_pos;
	int available = (wp - ac->read_pos + VIZ_RING_CAP) % VIZ_RING_CAP;
	int n = available < max ? available : max;
	for(int i = 0; i < n; i++) {
		int idx = ac->read_pos;
		l[i] = ac->ring_l[idx];
		r[i] = ac->ring_r[idx];
		ac->read_pos = (ac->read_pos + 1) % VIZ_RING_CAP;
	}
	return n;
}

static int audio_resolve_device(const char *device_name) {
	if(!device_name || !device_name[0]) {
		return Pa_GetDefaultInputDevice();
	}
	char *end = NULL;
	long idx = strtol(device_name, &end, 10);
	if(end && *end == '\0') {
		if(idx >= 0 && idx < Pa_GetDeviceCount()) {
			const PaDeviceInfo *di = Pa_GetDeviceInfo((PaDeviceIndex)idx);
			if(di && di->maxInputChannels > 0) {
				return (int)idx;
			}
		}
		return paNoDevice;
	}
	int count = Pa_GetDeviceCount();
	for(int i = 0; i < count; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if(di && di->maxInputChannels > 0 && strstr(di->name, device_name)) {
			return i;
		}
	}
	return paNoDevice;
}

void audio_list_devices(void) {
	if(Pa_Initialize() != paNoError) {
		return;
	}
	int count = Pa_GetDeviceCount();
	int def = Pa_GetDefaultInputDevice();
	for(int i = 0; i < count; i++) {
		const PaDeviceInfo *di = Pa_GetDeviceInfo(i);
		if(di && di->maxInputChannels > 0) {
			const PaHostApiInfo *api = Pa_GetHostApiInfo(di->hostApi);
			const char *marker = (i == def) ? "  <-- default" : "";
			printf("  %-4d  %-6d  %-14s  %s%s\n", i, di->maxInputChannels,
				api ? api->name : "?", di->name, marker);
		}
	}
	printf("\nTip: pick the 'Monitor of ...' device to capture system audio.\n");
	Pa_Terminate();
}

int audio_init(AudioCapture *ac, const char *device_name) {
	memset(ac, 0, sizeof(*ac));
	ac->device_index = -1;

	PaError err = Pa_Initialize();
	if(err != paNoError) {
		fprintf(stderr, "[vizulobe] Pa_Initialize: %s\n", Pa_GetErrorText(err));
		return 1;
	}

	int dev = audio_resolve_device(device_name);
	if(dev == paNoDevice) {
		fprintf(stderr, "[vizulobe] no input device found (use -d or --list-devices)\n");
		Pa_Terminate();
		return 1;
	}
	const PaDeviceInfo *di = Pa_GetDeviceInfo(dev);
	PaStreamParameters params = {0};
	params.device = (PaDeviceIndex)dev;
	params.channelCount = 2;
	params.sampleFormat = paFloat32;
	params.suggestedLatency = di->defaultHighInputLatency;

	err = Pa_OpenStream(&ac->stream, &params, NULL, VIZ_SAMPLE_RATE,
		VIZ_FRAMES_PER_BLOCK, paClipOff, pa_capture_callback, ac);
	if(err != paNoError) {
		fprintf(stderr, "[vizulobe] Pa_OpenStream: %s\n", Pa_GetErrorText(err));
		Pa_Terminate();
		return 1;
	}

	err = Pa_StartStream(ac->stream);
	if(err != paNoError) {
		fprintf(stderr, "[vizulobe] Pa_StartStream: %s\n", Pa_GetErrorText(err));
		Pa_CloseStream(ac->stream);
		Pa_Terminate();
		return 1;
	}

	ac->device_index = dev;
	ac->running = true;
	printf("[vizulobe] capturing %s (%s)\n", di->name,
		Pa_GetHostApiInfo(di->hostApi) ? Pa_GetHostApiInfo(di->hostApi)->name : "?");
	return 0;
}

void audio_shutdown(AudioCapture *ac) {
	if(!ac || !ac->stream) {
		return;
	}
	Pa_StopStream(ac->stream);
	Pa_CloseStream(ac->stream);
	Pa_Terminate();
	ac->stream = NULL;
	ac->running = false;
}
