#ifndef VIZ_RECT_H
#define VIZ_RECT_H

#include "raylib.h"
#include "analysis.h"
#include "scene.h"
#include "viz.h"

typedef struct {
	Viz *viz;
	RenderTexture2D rt;
	int w, h;
	bool rt_valid;
	bool inited;
} VizRect;

typedef struct {
	VizRect fg[VIZ_MAX_FG];
	VizRect bg;
	Texture2D waveform_tex;
	Texture2D spectrum_tex;
	bool tex_valid;
	bool bg_inited;
} RectManager;

void rect_manager_init(RectManager *rm);
void rect_manager_sync(RectManager *rm, const Scene *s, const Analysis *a);
void rect_manager_render(RectManager *rm, const Scene *s, const Analysis *a, float time, float dt);
void rect_manager_shutdown(RectManager *rm);

#endif
