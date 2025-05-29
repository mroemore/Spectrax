#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#include "oscillator.h"
#include "raylib.h"
#include "portaudio.h"
#include "io.h"
#include "dataviz.h"
#include "sample.h"
#include "settings.h"

#define SA_PI 3.1415926535897932
#define SA_2PI 6.2831853071795864

#define SA_BUF_S 256
#define SA_SR 44100

#define SA_OP_C 6

#define SA_HIST_LEN 512
#define SA_HIST_C 16

typedef struct DrawBufferCollection DrawBufferCollection;
typedef struct TestData TestData;

struct DrawBufferCollection {
	unsigned int buffer_write_indx[SA_HIST_C];
	float buffer[SA_HIST_LEN * SA_HIST_C];
};

struct TestData {
	float ratios[SA_OP_C];
	float phases[SA_OP_C];
	float phase_inc[SA_OP_C];
	float mod_accumulator[SA_OP_C];
	unsigned int generated_flags;
	float mod_map[SA_OP_C * SA_OP_C];
	unsigned int samples_elapsed;
	DrawBufferCollection dbc;
	float carrier_freq;
};

static void pa_data_init(TestData *td) {
	td->samples_elapsed = 0;
	td->carrier_freq = 440.0f;
	td->generated_flags = 0;
	for(int i = 0; i < SA_OP_C; i++) {
		td->ratios[i] = 1.0f;
		td->phases[i] = 0.0f;
		td->mod_accumulator[i] = 0.0;
		td->phase_inc[i] = (td->ratios[i] * td->carrier_freq) / SA_SR;
		for(int j = 0; j < SA_OP_C; j++) {
			td->mod_map[i * SA_OP_C + j] = 0.0f;
		}
	}

	// basic algo
	td->mod_map[(5 * SA_OP_C) + 4] = 0.8;
	td->mod_map[(4 * SA_OP_C) + 3] = 0.8;
	td->mod_map[(3 * SA_OP_C) + 2] = 0.8;
	td->mod_map[(2 * SA_OP_C) + 1] = 0.8;
	td->mod_map[(1 * SA_OP_C) + 0] = 0.8;

	for(int i = 0; i < SA_HIST_C; i++) {
		td->dbc.buffer_write_indx[i] = 0;
		for(int j = 0; j < SA_HIST_LEN; j++) {
			td->dbc.buffer[i * SA_HIST_C + j] = 0.0f;
		}
	}
}

void push_frame_to_history(float frame, DrawBufferCollection *dbc, int buffer_id) {
	dbc->buffer[dbc->buffer_write_indx[buffer_id]] = frame;
	dbc->buffer_write_indx[buffer_id]++;
	if(dbc->buffer_write_indx[buffer_id] >= SA_HIST_LEN) {
		dbc->buffer_write_indx[buffer_id] %= SA_HIST_LEN;
	}
}

static float get_sin_samp(float phase) {
	return sin(phase * SA_2PI);
}

static int patestCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
	TestData *data = (TestData *)userData;
	(void)inputBuffer;
	float *out = (float *)outputBuffer;

	for(int i = 0; i < framesPerBuffer; i++) {
		float left_output = 0.0f;
		float right_output = 0.0f;

		for(int m_src = SA_OP_C - 1; m_src > 0; m_src--) {
			unsigned int mask = (1 << m_src);
			if((data->generated_flags & mask) == 0) {
				data->generated_flags &= mask;
			}
			for(int m_dest = m_src - 1; m_dest >= 0; m_dest--) {
				if(data->mod_map[(m_dest * SA_OP_C) + m_src] > 0.0) {
					data->mod_accumulator[m_dest] +=
				}
			}
		}

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
	TestData data;

	err = Pa_Initialize();
	if(err == paNoError) {
		err = Pa_OpenDefaultStream(&stream,
		                           0,
		                           2,
		                           paFloat32,
		                           SA_SR,
		                           SA_BUF_S,
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
