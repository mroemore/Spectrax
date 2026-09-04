#ifndef VIZ_VIZ_H
#define VIZ_VIZ_H

#include <stdbool.h>
#include "raylib.h"
#include "scene.h"
#include "vizulobe.h"

typedef enum { VIZ_KIND_GLSL, VIZ_KIND_C, VIZ_KIND_ERR } VizKind;

typedef struct Viz {
	VizKind kind;
	char path[VIZ_PATH_MAX];
	char error[1024];
	union {
		struct {
			Shader shader;
			int loc_time, loc_dt, loc_resolution, loc_waveform, loc_spectrum, loc_audio, loc_backbuffer;
		} gl;
		struct {
			void *dl;
			viz_init_fn init;
			viz_frame_fn frame;
			viz_t ctx;
		} c;
	} u;
} Viz;

Viz *viz_load(const char *path);
const char *viz_error(const Viz *v);
bool viz_is_loaded(const Viz *v);
void viz_free(Viz *v);
const char *viz_cache_dir(void);

#endif
