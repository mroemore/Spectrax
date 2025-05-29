#include <stdio.h>
#include <math.h>
#include <time.h>

#include "raylib.h"
#include "portaudio.h"
#include "io.h"
#include "dataviz.h"
#include "sample.h"

typedef struct
{
	int samples_elapsed;
} paTestData;

static int patestCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
	paTestData *data = (paTestData *)userData;
	(void)inputBuffer;
	float *out = (float *)outputBuffer;

	for(int i = 0; i < framesPerBuffer; i++) {
		float left_output = 0.0f;
		float right_output = 0.0f;

		*out++ = left_output;
		*out++ = right_output;
	}

	return 0;
}
int main(void) {
	const int screenWidth = SCREEN_W;
	const int screenHeight = SCREEN_H;
	SamplePool *sp = createSamplePool();
	loadSamplesfromDirectory("./", sp);
	if(sp->sampleCount <= 0) {
		printf("NO WAVS!\n");
		return -1;
	}
	Spectrogram *spectrogram;
	int sampleIndex = 0;

	PaStream *stream;
	PaError err;
	paTestData data;

	err = Pa_Initialize();
	if(err == paNoError) {
		err = Pa_OpenDefaultStream(&stream,
		                           0,
		                           2,
		                           paFloat32,
		                           SAMPLE_RATE,
		                           256,
		                           patestCallback,
		                           &data);
		if(err == paNoError) {
			err = Pa_StartStream(stream);
		}
	}

	InitWindow(screenWidth, screenHeight, "sample_analyser");
	SetTargetFPS(60);

	if(err == paNoError) {
		while(!WindowShouldClose()) {
			BeginDrawing();
			ClearBackground(BLACK);
			DrawFPS(5, 5);

			EndDrawing();
		}
	}
}
