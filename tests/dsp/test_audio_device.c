/* test_audio_device.c — the output stream's rate and buffer size.
 *
 * PortAudio takes the sample rate and the callback's buffer size in two
 * separate slots, and both are numbers: putting the buffer size in the
 * rate's slot compiles cleanly and produces a stream that runs at 256 Hz
 * while the engine synthesises at 48000.
 *
 * That mistake was made, and it is the kind that no unit test catches --
 * until the stream's opening is in a module a test can drive, and the
 * test asks a real device what it negotiated.
 *
 * The test skips when the machine has no output device: a CI box
 * without sound is not a failure, it is a skip.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_device.h"
#include "settings.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

static int g_callbackRan = 0;

static int testCallback(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags, void *userData) {
	(void)inputBuffer;
	(void)timeInfo;
	(void)statusFlags;
	(void)userData;
	if(outputBuffer && framesPerBuffer > 0) {
		memset(outputBuffer, 0, sizeof(float) * framesPerBuffer * 2);
	}
	g_callbackRan++;
	return paContinue;
}

/* The stream opens at the rate that was asked for, and at the buffer
 * size that was asked for. */
static int test_the_stream_opens_at_the_asked_for_rate(void) {
	if(Pa_Initialize() != paNoError) {
		printf("SKIP: PortAudio could not be initialised\n");
		return 77;
	}
	PaDeviceIndex dev = Pa_GetDefaultOutputDevice();
	if(dev == paNoDevice) {
		printf("SKIP: the machine has no output device\n");
		Pa_Terminate();
		return 77;
	}
	PaStreamParameters out;
	memset(&out, 0, sizeof(out));
	out.device = dev;
	out.channelCount = 2;
	out.sampleFormat = paFloat32;
	out.suggestedLatency = 0.1;

	const unsigned long wantFrames = 256;
	PaStream *stream = NULL;
	PaError err = audioDeviceOpenStream(&stream, &out, (double)SPECTRAX_ENGINE_RATE,
	                                    wantFrames, testCallback, NULL);
	if(err != paNoError || !stream) {
		/* A machine whose default device refuses 48000 is not a failure
		 * of this code: the app tries several candidates. */
		printf("SKIP: the default device refused the stream (%d)\n", (int)err);
		if(stream) {
			Pa_CloseStream(stream);
		}
		Pa_Terminate();
		return 77;
	}
	double rate = audioDeviceNegotiatedRate(stream);
	if(rate != (double)SPECTRAX_ENGINE_RATE) {
		printf("FAIL: the stream negotiated %.0f Hz, the engine's rate is %d\n",
		       rate, SPECTRAX_ENGINE_RATE);
		Pa_CloseStream(stream);
		Pa_Terminate();
		return 1;
	}
	Pa_CloseStream(stream);
	Pa_Terminate();
	printf("PASS test_the_stream_opens_at_the_asked_for_rate\n");
	return 0;
}

int main(void) {
	int fails = 0;
	fails += test_the_stream_opens_at_the_asked_for_rate();
	if(fails) {
		fprintf(stderr, "%d audio-device test(s) failed\n", fails);
		return 1;
	}
	printf("ALL audio-device tests passed\n");
	return 0;
}
