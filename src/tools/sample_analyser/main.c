#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "raylib.h"
#include "oscillator.h"
#include "portaudio.h"
#include "io.h"
#include "dataviz.h"
#include "sample.h"
#include "settings.h"

#include "sa_audio.h"
#include "sa_gui.h"

#define SA_SCREEN_W 640 // 640
#define SA_SCREEN_H 480 // 480
#define SA_PI 3.1415926535897932
#define SA_2PI 6.2831853071795864

#define SA_BUF_S 256
#define SA_SR 44100

typedef struct TestData TestData;
typedef struct WaveShaper WaveShaper;

typedef float (*ApplyWS)(WaveShaper *ws, float sample);

typedef struct PerfMeasure PerfMeasure;
#define SA_MAX_MEASUREMENTS 4096
#define SA_MAX_AVERAGE 256
struct PerfMeasure {
	clock_t current_start;
	clock_t current_end;

	double measurements[SA_MAX_MEASUREMENTS];
	double rolling_average[SA_MAX_MEASUREMENTS];
	unsigned int items_to_average;
	unsigned int index;
};

void init_perf_measure(PerfMeasure *pm, int avg) {
	pm->index = 0;
	pm->items_to_average = avg;
}

void push_measurement(PerfMeasure *pm, double measurement) {
	if(pm->index < SA_MAX_MEASUREMENTS) {
		pm->measurements[pm->index] = measurement;
		pm->index++;
	}
}

void perf_start(PerfMeasure *pm) {
	pm->current_start = clock();
}

void perf_end(PerfMeasure *pm) {
	pm->current_start = clock();
}

void process_average(PerfMeasure *pm) {
}

struct WaveShaper {
	float lookup_table[2048];
	ApplyWS apply;
	float amount;
};

float shape_wave(WaveShaper *s, float sample) {
	float scaled = 2048 * ((sample + 1.0f) / 2.0f);
	int index_a = (int)scaled;
	int index_b = (int)(scaled + 1.0f);
	index_b = index_b >= 2048 ? 2048 : index_b;
	float integral_a = 0;
	float frac_a = modff(scaled, &integral_a);
	float frac_b = 1.0f - frac_a;
	float wet = (s->lookup_table[index_a] * frac_a + s->lookup_table[index_b] * frac_b) * s->amount;
	return ((sample * (1.0f - s->amount) + wet)) / 2.0f;
}

float fold_wave(WaveShaper *s, float sample) {
	float abs_samp = fabsf(sample);
	float a = 1.0 - s->amount;
	float diff = abs_samp - a;
	float out = 0;
	if(abs_samp > a) {
		if(sample < 0) {
			out = -a + diff;
		} else {
			out = a - diff;
		}
	} else {
		out = sample;
	}
	return out;
}

void init_sine_shaper(WaveShaper *s) {
	for(int i = 0; i < 2048; i++) {
		float phase = (float)i * (1.0f / 2048.0f);
		s->lookup_table[i] = cos(SA_PI * phase) * -1;
	}
	s->amount = 0.2;
	s->apply = shape_wave;
}

void init_fold_shaper(WaveShaper *s) {
	for(int i = 0; i < 2048; i++) {
		s->lookup_table[i] = 0;
	}
	s->apply = fold_wave;
}

static float get_sin_samp(float phase) {
	return sin(phase * SA_2PI);
}

struct TestData {
	Ops fm;
	unsigned int samples_elapsed;
	DrawBufferCollection dbc;
	WaveShaper shaper;
};

void init_ops(Ops *fm) {
	fm->base_freq = 440.0f;
	fm->sample_rate = SA_SR;
	for(int i = 0; i < SA_OP_C; i++) {
		fm->phase[i] = 0.0f;
		fm->ratio[i] = 1.0f;
		fm->current_sample[i] = 0.0f;
	}
	for(int i = 0; i < SA_OP_C + 1; i++) {
		for(int j = 0; j < SA_OP_C; j++) {
			fm->mod_map[i][j] = 0.0f;
		}
	}

	fm->mod_map[5][6] = 0.8;
	fm->mod_map[4][5] = 0.8;
	fm->mod_map[3][4] = 0.8;
	fm->mod_map[2][3] = 0.8;
	// fm->mod_map[1][2] = 0.8;
	fm->mod_map[0][1] = 0.8;
}

float get_fm_sample(Ops *fm, bool dbg) {
	float base_phase_inc = fm->base_freq / SA_SR;
	// final operator is processed outside loop to avoid redundant mod checks.
	fm->phase[SA_OP_C] += base_phase_inc * fm->ratio[SA_OP_C];
	if(fm->phase[SA_OP_C] > 1.0f) {
		fm->phase[SA_OP_C] -= 1.0f;
	}
	fm->current_sample[SA_OP_C] = get_sin_samp(fm->phase[SA_OP_C]);

	for(int dest_idx = SA_OP_C - 1; dest_idx >= 1; dest_idx--) {
		float mod = 0;
		for(int src_idx = SA_OP_C; src_idx > dest_idx; src_idx--) {
			mod += fm->current_sample[src_idx] * fm->mod_map[dest_idx][src_idx];
		}
		fm->phase[dest_idx] += base_phase_inc * fm->ratio[dest_idx];
		if(fm->phase[dest_idx] > 1.0f) {
			fm->phase[dest_idx] -= 1.0f;
		}
		fm->current_sample[dest_idx] = get_sin_samp(fm->phase[dest_idx] + mod);
	}

	float final_sample = 0;
	for(int j = SA_OP_C; j > 0; j--) {
		final_sample += fm->current_sample[j] * fm->mod_map[0][j];
	}

	return final_sample;
}

static void pa_data_init(TestData *td) {
	td->samples_elapsed = 0;

	for(int i = 0; i < SA_HIST_C; i++) {
		td->dbc.buffer_write_indx[i] = 0;
		for(int j = 0; j < SA_HIST_LEN; j++) {
			td->dbc.buffer[i * SA_HIST_C + j] = 0.0f;
		}
	}
	init_sine_shaper(&td->shaper);
	init_ops(&td->fm);
}

static int patestCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
	TestData *data = (TestData *)userData;
	(void)inputBuffer;
	float *out = (float *)outputBuffer;

	for(int i = 0; i < framesPerBuffer; i++) {
		float left_output = 0.0f;
		float right_output = 0.0f;

		left_output = get_fm_sample(&data->fm, false);
		left_output = data->shaper.apply(&data->shaper, left_output);
		push_frame_to_history(left_output, &data->dbc, 0);
		left_output *= 0.2;
		right_output = left_output;
		*out++ = left_output;
		*out++ = right_output;
	}
	return 0;
}

int main(void) {
	const int screenWidth = SA_SCREEN_W;
	const int screenHeight = SA_SCREEN_H;

	Vector2 mouse_position = (Vector2){ 0, 0 };
	gui_setup();

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
	pa_data_init(&data);
	GE_ModMatrix ge_mm;
	init_mod_matrix(&ge_mm, &data.fm, (Rectangle){ 25, 95, 150, 150 }, BLACK, RED, WHITE);
	add_drawclick_element((GuiElement *)&ge_mm);

	GE_FaderControl fader;
	init_fader_control(&fader, (Rectangle){ 400, 300, 25, 95 }, &data.shaper.amount, 0.0f, 1.0f);
	add_drawclick_element((GuiElement *)&fader);

	// struct timespec start, end;
	//  double elapsed;
	//  double sum = 0;
	//  clock_gettime(CLOCK_MONOTONIC, &start);
	//  for(int i = 0; i < 200000; i++) {
	//  	sum += get_fm_sample(&data.fm, false);
	//  }
	//  clock_gettime(CLOCK_MONOTONIC, &end);

	// elapsed = (end.tv_sec - start.tv_sec) +
	//           (end.tv_nsec - start.tv_nsec) / 1e9;
	// printf("\n\n\n\n");
	// printf("Elapsed time: %.9f seconds\n", elapsed);
	// printf("\n\n\n\n");

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
	SetTraceLogLevel(LOG_WARNING);
	InitWindow(screenWidth, screenHeight, "sample_analyser");
	SetTargetFPS(60);

	if(err == paNoError) {
		while(!WindowShouldClose()) {
			handle_input(&mouse_position);
			Ops fmcpy = data.fm;
			// fmcpy.sample_rate = SA_HIST_LEN;
			fmcpy.base_freq = 86.0f;
			for(int i = 0; i < SA_OP_C; i++) {
				fmcpy.phase[i] = 0;
			}
			for(int i = 0; i < SA_HIST_LEN; i++) {
				float s = get_fm_sample(&fmcpy, false);
				push_frame_to_history(s, &data.dbc, 2);
				s = data.shaper.apply(&data.shaper, s);
				push_frame_to_history(s, &data.dbc, 1);
			}
			BeginDrawing();
			ClearBackground(BLACK);

			draw_buffer(&data.dbc, 0, (Rectangle){ 25, 25, 600, 60 }, RED);
			draw_buffer(&data.dbc, 2, (Rectangle){ 25, 225, 600, 100 }, RAYWHITE);
			draw_buffer(&data.dbc, 1, (Rectangle){ 25, 225, 600, 100 }, GREEN);

			draw_elements();
			DrawFPS(5, 5);

			EndDrawing();
		}
	}
}
