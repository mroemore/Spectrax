#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "scene.h"

int main(void) {
	Scene s;
	scene_init(&s);
	assert(s.fg_count == 0);
	assert(s.selected == -1);
	assert(s.bg_path[0] == '\0');

	/* add three fg rects; newest is selected */
	int i0 = scene_add_fg(&s, "viz/a.c", 0, 0, 100, 100);
	int i1 = scene_add_fg(&s, "viz/b.frag", 200, 50, 320, 240);
	int i2 = scene_add_fg(&s, "viz/c.c", 600, 300, 200, 150);
	assert(i0 == 0 && i1 == 1 && i2 == 2);
	assert(s.fg_count == 3);
	assert(s.selected == 2);

	/* hit test: inside rect 1 (topmost over rect 0's overlap), inside rect 0, miss */
	assert(scene_hit_test(&s, 210, 60) == 1);
	assert(scene_hit_test(&s, 10, 10) == 0);
	assert(scene_hit_test(&s, 1000, 700) == -1);

	/* select + drag moves only the selected rect */
	scene_select(&s, 0);
	scene_drag(&s, 30, -20);
	assert(s.fg[0].x == 30 && s.fg[0].y == -20);
	assert(s.fg[1].x == 200 && s.fg[1].y == 50);

	/* resize clamps at VIZ_MIN_RECT */
	scene_select(&s, 1);
	scene_resize(&s, -1000, -1000);
	assert(s.fg[1].w == VIZ_MIN_RECT && s.fg[1].h == VIZ_MIN_RECT);
	scene_resize(&s, 100, 50);
	assert(s.fg[1].w == 32 + 100 /* starts 320 -> clamped 32 -> +100 = 132 */ && s.fg[1].h == 32 + 50 /* 32 + 50 = 82 */);
	assert(s.fg[1].w == 132 && s.fg[1].h == 82);

	/* remove a middle rect; indices shift, selection clears if removed */
	scene_select(&s, 1);
	assert(scene_remove_fg(&s, 1));
	assert(s.fg_count == 2);
	assert(s.selected == -1);
	assert(strcmp(s.fg[1].path, "viz/c.c") == 0);

	/* bg */
	scene_set_bg(&s, "viz/plasma.frag");
	assert(strcmp(s.bg_path, "viz/plasma.frag") == 0);

	/* 32 cap */
	for(int i = 0; i < VIZ_MAX_FG; i++) {
		scene_add_fg(&s, "viz/x.c", 0, 0, 50, 50);
	}
	assert(s.fg_count == VIZ_MAX_FG);
	assert(scene_add_fg(&s, "viz/y.c", 0, 0, 50, 50) == -1);

	printf("test_scene OK\n");
	return 0;
}