#include <stdbool.h>
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

#define SA_SCREEN_W 640 // 640
#define SA_SCREEN_H 480 // 480
#define SA_PI 3.1415926535897932
#define SA_2PI 6.2831853071795864

#define SA_BUF_S 256
#define SA_SR 44100

#define SA_OP_C 6

#define SA_HIST_LEN 512
#define SA_HIST_C 16

typedef struct DrawBufferCollection DrawBufferCollection;
typedef struct Clickable Clickable;
typedef struct TestData TestData;
typedef struct Ops Ops;

struct DrawBufferCollection {
	unsigned int buffer_write_indx[SA_HIST_C];
	float buffer[SA_HIST_LEN * SA_HIST_C];
};

struct Ops {
	// 0 represents audio out, 1 to SA_OP_C are ops
	float phase[SA_OP_C + 1];
	float ratio[SA_OP_C + 1];
	float current_sample[SA_OP_C + 1];
	float mod_map[SA_OP_C + 1][SA_OP_C + 1];
	float base_freq;
};

static float get_sin_samp(float phase) {
	return sin(phase * SA_2PI);
}

struct TestData {
	Ops fm;
	unsigned int samples_elapsed;
	DrawBufferCollection dbc;
};

void init_ops(Ops *fm) {
	fm->base_freq = 440.0f;
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
	fm->mod_map[1][2] = 0.8;
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

	init_ops(&td->fm);
}

void push_frame_to_history(float frame, DrawBufferCollection *dbc, int buffer_id) {
	dbc->buffer[dbc->buffer_write_indx[buffer_id]] = frame;
	dbc->buffer_write_indx[buffer_id]++;
	if(dbc->buffer_write_indx[buffer_id] >= SA_HIST_LEN) {
		dbc->buffer_write_indx[buffer_id] %= SA_HIST_LEN;
	}
}

void draw_buffer(DrawBufferCollection *dbc, int buffer_id, Rectangle bounds, Color c) {
	DrawRectangleLinesEx(bounds, 1, c);
	float x_scale = bounds.width / SA_HIST_LEN;
	float y_scale = bounds.height / 2.0;
	for(int i = 0; i < SA_HIST_LEN - 1; i++) {
		DrawLineEx(
		  (Vector2){ bounds.x + x_scale * i, bounds.y + y_scale + (dbc->buffer[buffer_id * SA_HIST_C + i] * y_scale) },
		  (Vector2){ bounds.x + x_scale * (i + 1), bounds.y + y_scale + (dbc->buffer[buffer_id * SA_HIST_C + i + 1] * y_scale) },
		  1,
		  c);
	}
}

void draw_mod_matrix(Ops *fm, Rectangle bounds, Color c_grid1, Color c_grid2, Color c_txt) {
	float grid_cell_w = bounds.width / SA_OP_C;
	float grid_cell_h = bounds.height / SA_OP_C;
	for(int grid_x = SA_OP_C; grid_x >= 0; grid_x--) {
		for(int grid_y = SA_OP_C; grid_y >= 0; grid_y--) {
			Color shade = (Color){
				c_grid1.r + (int)((c_grid1.r - c_grid2.r) * (log10(1 - fm->mod_map[grid_x][grid_y]))),
				c_grid1.g + (int)((c_grid1.g - c_grid2.g) * (log10(1 - fm->mod_map[grid_x][grid_y]))),
				c_grid1.b + (int)((c_grid1.b - c_grid2.b) * (log10(1 - fm->mod_map[grid_x][grid_y]))),
				255
			};
			DrawRectangle(bounds.x + (SA_OP_C - grid_x) * grid_cell_w, bounds.y + (SA_OP_C - grid_y) * grid_cell_h, grid_cell_w, grid_cell_h, shade);
			char mod_amount_txt[10];
			snprintf(mod_amount_txt, 10, "%0.2f", fm->mod_map[grid_x][grid_y]);
			DrawText(mod_amount_txt,
			         bounds.x + (int)(grid_cell_w * 0.125 + (SA_OP_C - grid_x) * grid_cell_w),
			         bounds.y + (int)(grid_cell_w * 0.125 + (SA_OP_C - grid_y) * grid_cell_h),
			         (int)grid_cell_w * 0.125,
			         c_txt);
			DrawRectangleLines(
			  bounds.x + (SA_OP_C - grid_x) * grid_cell_w,
			  bounds.y + (SA_OP_C - grid_y) * grid_cell_h,
			  grid_cell_w,
			  grid_cell_h,
			  c_txt);
		}
	}
}

static int patestCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
	TestData *data = (TestData *)userData;
	(void)inputBuffer;
	float *out = (float *)outputBuffer;

	for(int i = 0; i < framesPerBuffer; i++) {
		float left_output = 0.0f;
		float right_output = 0.0f;

		left_output = get_fm_sample(&data->fm, false);
		push_frame_to_history(left_output, &data->dbc, 0);
		left_output *= 0.2;
		right_output = left_output;
		*out++ = left_output;
		*out++ = right_output;
	}

	return 0;
}

void handle_input(Vector2 *mouse_xy) {
	*mouse_xy = GetMousePosition();
}

int main(void) {
	const int screenWidth = SA_SCREEN_W;
	const int screenHeight = SA_SCREEN_H;
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

			draw_buffer(&data.dbc, 0, (Rectangle){ 25, 25, 600, 60 }, RED);
			draw_mod_matrix(&data.fm, (Rectangle){ 25, 95, 150, 150 }, BLACK, RED, WHITE);
			DrawFPS(5, 5);

			EndDrawing();
		}
	}
}
