#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
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
#define SA_MAX_GUI_ELEMENTS 1024

#define SA_BUF_S 256
#define SA_SR 44100

#define SA_OP_C 6

#define SA_HIST_LEN 512
#define SA_HIST_C 16

typedef void (*ClickCB)(void *self);
typedef void (*DrawCB)(void *self);

typedef struct DrawBufferCollection DrawBufferCollection;
typedef struct TestData TestData;
typedef struct Ops Ops;

typedef struct GuiElement GuiElement;
typedef struct GuiElementList GuiElementList;

typedef struct GE_ModMatrix GE_ModMatrix;

struct GuiElement {
	bool visible;
	bool clickable;
	DrawCB draw;
	ClickCB on_click;
	ClickCB on_release;
	bool click_held;
	Rectangle hitbox;
};

struct GuiElementList {
	GuiElement *list[SA_MAX_GUI_ELEMENTS];
	unsigned int active_count;
	unsigned int removed_item_indicies[SA_MAX_GUI_ELEMENTS];
	unsigned int removed_item_count;
};

void init_gui_element_list(GuiElementList *l) {
	for(int i = 0; i < SA_MAX_GUI_ELEMENTS; i++) {
		l->list[i] = NULL;
		l->removed_item_indicies[i] = -1;
	}
	l->active_count = 0;
	l->removed_item_count = 0;
}

void add_gui_element_to_list(GuiElementList *l, GuiElement *e) {
	if(!l || !e) {
		printf("Error: NULL arg to %s\n", __func__);
	}
	if(l->removed_item_count > 0) {
		l->removed_item_count--;
		l->list[l->removed_item_indicies[l->removed_item_count]] = e;
	} else if(l->active_count < SA_MAX_GUI_ELEMENTS) {
		l->list[l->active_count] = e;
		l->active_count++;
	} else {
		printf("Error: Invalid ElementList state [active: %i, removed: %i] in %s\n", l->active_count, l->removed_item_count, __func__);
	}
}

void p_remove_gui_element_from_list(GuiElementList *l, GuiElement *e) {
	printf("Error: Invalid index in %s\n", __func__);
}
void i_remove_gui_element_from_list(GuiElementList *l, unsigned int index) {
	if(index < 0 || index >= SA_MAX_GUI_ELEMENTS) {
		printf("Error: Invalid ElementList state [active: %i, removed: %i] in ``%s``\n", l->active_count, l->removed_item_count, __func__);
	}
	l->list[index] = NULL;
	l->removed_item_indicies[l->removed_item_count] = index;
	l->removed_item_count++;
}

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

struct GE_ModMatrix {
	GuiElement base;
	Ops *fm;
	Rectangle grid_bounds;
	Rectangle cell_bounds;
	Color c_grid1;
	Color c_grid2;
	Color c_diff;
	Color c_txt;
};

void init_mod_matrix(GE_ModMatrix *d_mm, Ops *fm, Rectangle bounds, Color c_grid1, Color c_grid2, Color c_txt) {
	d_mm->grid_bounds = bounds;
	d_mm->cell_bounds = (Rectangle){ 0, 0, bounds.width / SA_OP_C, bounds.height / SA_OP_C };
	d_mm->c_grid1 = c_grid1;
	d_mm->c_grid2 = c_grid2;
	d_mm->c_diff = (Color){
		c_grid1.r - c_grid2.r,
		c_grid1.g - c_grid2.g,
		c_grid1.b - c_grid2.b,
		255,
	};
	d_mm->c_txt = c_txt;
}

void mod_matrix_draw(void *self) {
	GE_ModMatrix *d_mm = (GE_ModMatrix *)self;

	for(int grid_x = SA_OP_C; grid_x >= 0; grid_x--) {
		for(int grid_y = SA_OP_C; grid_y >= 0; grid_y--) {
			Color shade = (Color){
				d_mm->c_grid1.r + (int)((d_mm->c_diff.r) * (log10(1 - d_mm->fm->mod_map[grid_x][grid_y]))),
				d_mm->c_grid1.g + (int)((d_mm->c_diff.g) * (log10(1 - d_mm->fm->mod_map[grid_x][grid_y]))),
				d_mm->c_grid1.b + (int)((d_mm->c_diff.b) * (log10(1 - d_mm->fm->mod_map[grid_x][grid_y]))),
				255
			};

			DrawRectangle(
			  d_mm->grid_bounds.x + (SA_OP_C - grid_x) * d_mm->cell_bounds.width,
			  d_mm->grid_bounds.y + (SA_OP_C - grid_y) * d_mm->cell_bounds.height,
			  d_mm->cell_bounds.width,
			  d_mm->cell_bounds.height,
			  shade);

			char mod_amount_txt[10];

			snprintf(mod_amount_txt, 10, "%0.2f", d_mm->fm->mod_map[grid_x][grid_y]);
			DrawText(mod_amount_txt,
			         d_mm->grid_bounds.x + (int)(d_mm->cell_bounds.width * 0.125 + (SA_OP_C - grid_x) * d_mm->cell_bounds.width),
			         d_mm->grid_bounds.y + (int)(d_mm->cell_bounds.width * 0.125 + (SA_OP_C - grid_y) * d_mm->cell_bounds.height),
			         (int)d_mm->cell_bounds.width * 0.125,
			         d_mm->c_txt);

			DrawRectangleLines(
			  d_mm->grid_bounds.x + (SA_OP_C - grid_x) * d_mm->cell_bounds.width,
			  d_mm->grid_bounds.y + (SA_OP_C - grid_y) * d_mm->cell_bounds.height,
			  d_mm->cell_bounds.width,
			  d_mm->cell_bounds.height,
			  d_mm->c_txt);
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

	for(int i = 0; i < clickable_count; i++) {
		if(CheckCollisionPointRec(*mouse_xy, clickable_list[i]->hitbox)) {
			clickable_list[i]->on_click(clickable_list[i]);
		}
	}
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
