#ifndef VIZ_PROJECT_H
#define VIZ_PROJECT_H

#include "scene.h"

typedef struct {
	char bg_path[VIZ_PATH_MAX];
	struct {
		char path[VIZ_PATH_MAX];
		int x, y, w, h;
	} fg[VIZ_MAX_FG];
	int fg_count;
	int fft_bins;
} ProjectFile;

int project_save(const char *filename, const Scene *s, int fft_bins);
int project_load(const char *filename, ProjectFile *out);

#endif
