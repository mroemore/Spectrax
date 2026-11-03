#include "audio_device.h"

#include <stddef.h>

PaError audioDeviceOpenStream(PaStream **stream, const PaStreamParameters *out,
                               double rate, unsigned long framesPerBuffer,
                               PaStreamCallback *callback, void *userData) {
	if(!stream || !out) {
		return paNotInitialized;
	}
	/* The rate goes in the rate's slot and the buffer size in the
	 * buffer's slot. They are both numbers, so the compiler will not
	 * catch a swap -- which is why this call lives in one place and a
	 * test checks it against a device. */
	return Pa_OpenStream(stream, NULL, out, rate, framesPerBuffer, paNoFlag, callback, userData);
}

double audioDeviceNegotiatedRate(const PaStream *stream) {
	if(!stream) {
		return 0.0;
	}
	const PaStreamInfo *si = Pa_GetStreamInfo(stream);
	if(!si) {
		return 0.0;
	}
	return si->sampleRate;
}
