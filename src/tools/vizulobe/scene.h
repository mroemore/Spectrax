#ifndef VIZ_SCENE_H
#define VIZ_SCENE_H

#include <stdbool.h>

#define VIZ_MAX_FG 32
#define VIZ_PATH_MAX 512
#define VIZ_MIN_RECT 32

typedef struct {
	char path[VIZ_PATH_MAX];
	int x, y, w, h;
	int selected;
} FgVizRect;

typedef struct {
	char bg_path[VIZ_PATH_MAX];
	FgVizRect fg[VIZ_MAX_FG];
	int fg_count;
	int selected;
} Scene;

void scene_init(Scene *s);
int scene_add_fg(Scene *s, const char *path, int x, int y, int w, int h);
bool scene_remove_fg(Scene *s, int idx);
void scene_set_bg(Scene *s, const char *path);
int scene_hit_test(Scene *s, int mx, int my);
void scene_select(Scene *s, int idx);
void scene_drag(Scene *s, int dx, int dy);
void scene_resize(Scene *s, int dw, int dh);

#endif