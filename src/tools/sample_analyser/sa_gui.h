#ifndef SA_GUI_H
#define SA_GUI_H

#include "raylib.h"
#include "sa_audio.h"
#include <stdint.h>

#define SA_MAX_GUI_ELEMENTS 1024

#define SA_HIST_LEN 512
#define SA_HIST_C 16

typedef void (*ClickCB)(void *self, Vector2 mouse_xy);
typedef void (*DrawCB)(void *self);

typedef struct DrawBufferCollection DrawBufferCollection;
struct DrawBufferCollection {
	unsigned int buffer_write_indx[SA_HIST_C];
	float buffer[SA_HIST_LEN * SA_HIST_C];
};

typedef struct GuiElement GuiElement;
struct GuiElement {
	bool visible;
	bool clickable;
	DrawCB draw;
	ClickCB on_click;
	ClickCB on_release;
	bool click_held;
	Rectangle hitbox;
};

typedef struct GuiElementList GuiElementList;
struct GuiElementList {
	GuiElement *list[SA_MAX_GUI_ELEMENTS];
	unsigned int active_count;
	unsigned int removed_item_indicies[SA_MAX_GUI_ELEMENTS];
	unsigned int removed_item_count;
};

#define MAX_CELL_TEXT_LENGTH 5
typedef struct GE_ModMatrix GE_ModMatrix;
struct GE_ModMatrix {
	GuiElement base;
	Ops *fm;
	int font_size;
	Rectangle grid_bounds;
	Rectangle cell_bounds;
	Color c_grid1;
	Color c_grid2;
	Color c_diff;
	Color c_txt;
};

typedef enum GE_FaderOpts GE_FaderOpts;
enum GE_FaderOpts {
	GEF_OPT_SHOW_LABEL = 1,
	GEF_OPT_SHOW_VALUE = 2,
	GEF_OPT_HIDE_LABEL_EXCEPT_HOVER = 4,
	GEF_OPT_HIDE_VALUE_EXCEPT_HOVER = 8,
	GEF_OPT_HORIZONTAL_MODE = 16
};

typedef struct GE_FaderControl GE_FaderControl;
struct GE_FaderControl {
	GuiElement base;
	char label[128];
	char fader_value_string[16];
	float min_value;
	float max_value;
	float *data_ref;
	uint64_t options;
	int font_size;
};

void gui_setup();
void draw_text_cf(const char *text, int posX, int posY, int fontSize, Color color);
void add_click_element(GuiElement *ge);
void add_draw_element(GuiElement *ge);
void add_drawclick_element(GuiElement *ge);

void handle_input(Vector2 *mouse_xy);
void draw_elements();

void init_ge_defaults(GuiElement *ge);

void init_gui_element_list(GuiElementList *l);
void add_gui_element_to_list(GuiElementList *l, GuiElement *e);
void p_remove_gui_element_from_list(GuiElementList *l, GuiElement *e);
void i_remove_gui_element_from_list(GuiElementList *l, unsigned int index);

void push_frame_to_history(float frame, DrawBufferCollection *dbc, int buffer_id);
void draw_buffer(DrawBufferCollection *dbc, int buffer_id, Rectangle bounds, Color c);

void init_mod_matrix(GE_ModMatrix *d_mm, Ops *fm, Rectangle bounds, Color c_grid1, Color c_grid2, Color c_txt);
void draw_mod_matrix(void *self);
void on_click_mod_matrix(void *self, Vector2 mouse_xy);

void init_fader_control(GE_FaderControl *fc, Rectangle r, char label[], float *data_ref, float min, float max, GE_FaderOpts options);
void on_click_fader_control(void *self, Vector2 mouse_xy);
void draw_fader_control(void *self);
#endif
