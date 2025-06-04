#include "sa_gui.h"
#include "raylib.h"
#include "tools/sample_analyser/sa_audio.h"

#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

GuiElementList clickable_list;
GuiElementList drawable_list;

void gui_setup() {
	init_gui_element_list(&clickable_list);
	init_gui_element_list(&drawable_list);
}

void add_click_element(GuiElement *ge) {
	add_gui_element_to_list(&clickable_list, (GuiElement *)ge);
}
void add_draw_element(GuiElement *ge) {
	add_gui_element_to_list(&drawable_list, (GuiElement *)ge);
}

void add_drawclick_element(GuiElement *ge) {
	add_gui_element_to_list(&drawable_list, (GuiElement *)ge);
	add_gui_element_to_list(&clickable_list, (GuiElement *)ge);
}

void handle_input(Vector2 *mouse_xy) {
	*mouse_xy = GetMousePosition();
	if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
		for(int i = 0; i < clickable_list.active_count; i++) {
			if(CheckCollisionPointRec(*mouse_xy, clickable_list.list[i]->hitbox)) {
				clickable_list.list[i]->on_click(clickable_list.list[i], *mouse_xy);
			}
		}
	}
}

void draw_elements() {
	for(int i = 0; i < drawable_list.active_count; i++) {
		if(drawable_list.list[i]->visible) {
			drawable_list.list[i]->draw(drawable_list.list[i]);
		}
	}
}

void init_ge_defaults(GuiElement *ge) {
	ge->clickable = false;
	ge->visible = true;
	ge->click_held = false;
	ge->on_click = NULL;
	ge->on_release = NULL;
	ge->draw = NULL;
	ge->hitbox = (Rectangle){ 10, 10, 100, 100 };
}

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
		return;
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
	if(!l || !e) {
		printf("Error: NULL arg to %s\n", __func__);
		return;
	}
	bool item_found = false;
	for(int i = 0; i < l->active_count; i++) {
		if(l->list[i] == e) {
			item_found = true;
			l->list[i] = NULL;
		}
	}
}
void i_remove_gui_element_from_list(GuiElementList *l, unsigned int index) {
	if(!l) {
		printf("Error: NULL arg to %s\n", __func__);
		return;
	}
	if(index < 0 || index >= SA_MAX_GUI_ELEMENTS) {
		printf("Error: Invalid ElementList state [active: %i, removed: %i] in ``%s``\n", l->active_count, l->removed_item_count, __func__);
		return;
	}
	if(index >= l->active_count) {
		printf("Error: Invalid ElementList state [active: %i, index: %i] in ``%s``\n", l->active_count, index, __func__);
		return;
	}
	l->list[index] = NULL;
	l->removed_item_indicies[l->removed_item_count] = index;
	l->removed_item_count++;
}

void push_frame_to_history(float frame, DrawBufferCollection *dbc, int buffer_id) {
	dbc->buffer[buffer_id * SA_HIST_LEN + dbc->buffer_write_indx[buffer_id]] = frame;
	dbc->buffer_write_indx[buffer_id]++;
	if(dbc->buffer_write_indx[buffer_id] >= SA_HIST_LEN) {
		dbc->buffer_write_indx[buffer_id] -= SA_HIST_LEN;
	}
}

void draw_buffer(DrawBufferCollection *dbc, int buffer_id, Rectangle bounds, Color c) {
	DrawRectangleLinesEx(bounds, 1, c);
	float x_scale = bounds.width / SA_HIST_LEN;
	float y_scale = bounds.height / 2.0;
	for(int i = 0; i < SA_HIST_LEN - 1; i++) {
		DrawLineEx(
		  (Vector2){ bounds.x + x_scale * i, bounds.y + y_scale + (dbc->buffer[buffer_id * SA_HIST_LEN + i] * y_scale) },
		  (Vector2){ bounds.x + x_scale * (i + 1), bounds.y + y_scale + (dbc->buffer[buffer_id * SA_HIST_LEN + i + 1] * y_scale) },
		  1,
		  c);
	}
}

void init_mod_matrix(GE_ModMatrix *d_mm, Ops *fm, Rectangle bounds, Color c_grid1, Color c_grid2, Color c_txt) {
	bool draw_legend = true;
	d_mm->fm = fm;
	d_mm->base.hitbox = bounds;
	int cell_count = draw_legend ? SA_OP_C + 1 : SA_OP_C;
	d_mm->cell_bounds = (Rectangle){ 0, 0, bounds.width / cell_count, bounds.height / cell_count };
	d_mm->c_grid1 = c_grid1;
	d_mm->c_grid2 = c_grid2;
	d_mm->c_diff = (Color){
		c_grid1.r - c_grid2.r,
		c_grid1.g - c_grid2.g,
		c_grid1.b - c_grid2.b,
		255,
	};
	d_mm->c_txt = c_txt;
	d_mm->base.hitbox = bounds;
	d_mm->base.visible = true;
	d_mm->base.draw = draw_mod_matrix;
	d_mm->base.clickable = true;
	d_mm->base.on_click = on_click_mod_matrix;
}

void draw_mod_matrix(void *self) {
	GE_ModMatrix *d_mm = (GE_ModMatrix *)self;
	int scaled_fontsize = (int)d_mm->cell_bounds.width * 0.125;

	for(int grid_x = SA_OP_C - 1; grid_x >= 0; grid_x--) {
		char legend_text[MAX_CELL_TEXT_LENGTH];

		for(int grid_y = SA_OP_C - 1; grid_y >= 0; grid_y--) {
			Color shade = (Color){
				d_mm->c_grid1.r + (int)((d_mm->c_diff.r) * (log10(1 - d_mm->fm->mod_map[grid_x][grid_y]))),
				d_mm->c_grid1.g + (int)((d_mm->c_diff.g) * (log10(1 - d_mm->fm->mod_map[grid_x][grid_y]))),
				d_mm->c_grid1.b + (int)((d_mm->c_diff.b) * (log10(1 - d_mm->fm->mod_map[grid_x][grid_y]))),
				255
			};

			DrawRectangle(
			  d_mm->base.hitbox.x + (SA_OP_C - grid_x) * d_mm->cell_bounds.width,
			  d_mm->base.hitbox.y + (SA_OP_C - grid_y) * d_mm->cell_bounds.height,
			  d_mm->cell_bounds.width,
			  d_mm->cell_bounds.height,
			  shade);

			char mod_amount_txt[MAX_CELL_TEXT_LENGTH];

			snprintf(mod_amount_txt, MAX_CELL_TEXT_LENGTH, "%0.2f", d_mm->fm->mod_map[grid_x][grid_y]);
			DrawText(mod_amount_txt,
			         d_mm->base.hitbox.x + (int)(d_mm->cell_bounds.width * 0.125 + (SA_OP_C - grid_x) * d_mm->cell_bounds.width),
			         d_mm->base.hitbox.y + (int)(d_mm->cell_bounds.width * 0.125 + (SA_OP_C - grid_y) * d_mm->cell_bounds.height),
			         scaled_fontsize,
			         d_mm->c_txt);

			DrawRectangleLines(
			  d_mm->base.hitbox.x + (SA_OP_C - grid_x) * d_mm->cell_bounds.width,
			  d_mm->base.hitbox.y + (SA_OP_C - grid_y) * d_mm->cell_bounds.height,
			  d_mm->cell_bounds.width,
			  d_mm->cell_bounds.height,
			  d_mm->c_txt);

			snprintf(legend_text, MAX_CELL_TEXT_LENGTH, "%d", grid_y + 1);
			DrawText(
			  legend_text,
			  d_mm->base.hitbox.x,
			  d_mm->base.hitbox.y + (SA_OP_C - grid_y) * d_mm->cell_bounds.width,
			  scaled_fontsize,
			  RED);
		}

		snprintf(legend_text, MAX_CELL_TEXT_LENGTH, "%d", grid_x + 1);
		DrawText(
		  legend_text,
		  d_mm->base.hitbox.x + (SA_OP_C - grid_x) * d_mm->cell_bounds.width,
		  d_mm->base.hitbox.y,
		  scaled_fontsize,
		  RED);
	}
}

void on_click_mod_matrix(void *self, Vector2 mouse_xy) {
	GE_ModMatrix *d_mm = (GE_ModMatrix *)self;
	int x_index = SA_OP_C - floor((mouse_xy.x - d_mm->base.hitbox.x) / d_mm->cell_bounds.width) + 1;
	int y_index = SA_OP_C - floor((mouse_xy.y - d_mm->base.hitbox.y) / d_mm->cell_bounds.height) + 1;
	printf("ON CLICK: matrix cell [%i, %i] has value %0.4f\n", x_index, y_index, d_mm->fm->mod_map[x_index][y_index]);
}

void init_fader_control(GE_FaderControl *fc, Rectangle r, char label[], float *data_ref, float min, float max, GE_FaderOpts options) {
	init_ge_defaults((GuiElement *)fc);
	strncpy(fc->label, label, 128);
	fc->base.hitbox = r;
	fc->data_ref = data_ref;
	fc->min_value = min;
	fc->max_value = max;
	snprintf(fc->fader_value_string, 16, "%0.4f", *data_ref);
	fc->base.on_click = on_click_fader_control;
	fc->base.draw = draw_fader_control;
	fc->base.clickable = true;
	fc->options = options;
}

void on_click_fader_control(void *self, Vector2 mouse_xy) {
	GE_FaderControl *fc = (GE_FaderControl *)self;
	float offset = mouse_xy.y - fc->base.hitbox.y;
	float scaled = offset / fc->base.hitbox.height;
	*fc->data_ref = fc->min_value + scaled * (fc->max_value - fc->min_value);
	snprintf(fc->fader_value_string, 16, "%0.4f", *fc->data_ref);
	printf("%s\n", fc->fader_value_string);
}

void draw_fader_control(void *self) {
	GE_FaderControl *fc = (GE_FaderControl *)self;
	Rectangle r = fc->base.hitbox;
	DrawRectangle(r.x, r.y, r.width, r.height, GRAY);
	float current_offet = *fc->data_ref - fc->min_value / (fc->max_value - fc->min_value);
	DrawRectangle(r.x, r.y + r.height * current_offet, r.width, r.height - r.height * current_offet, GREEN);
	DrawRectangleLines(r.x, r.y, r.width, r.height, BLACK);
	if(fc->options & GEF_OPT_SHOW_LABEL) {
		DrawText(fc->label, r.x, r.y + r.height, 10, GREEN);
		DrawText(fc->fader_value_string, r.x, r.y + r.height + 12, 10, GREEN);
	}
}
