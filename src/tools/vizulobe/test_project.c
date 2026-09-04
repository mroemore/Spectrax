#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "project.h"

int main(void) {
	mkdir(".tmp_files", 0755);
	const char *tmp = ".tmp_files/vizulobe_proj.json";

	Scene s;
	scene_init(&s);
	scene_set_bg(&s, "viz/plasma.frag");
	scene_add_fg(&s, "viz/osc.c", 40, 60, 320, 240);
	scene_add_fg(&s, "viz/bars.frag", 500, 300, 200, 120);

	assert(project_save(tmp, &s, 512) == 0);

	ProjectFile p;
	assert(project_load(tmp, &p) == 0);
	/* paths stored relative to ".tmp_files/" -> resolved on load */
	assert(strcmp(p.bg_path, ".tmp_files/viz/plasma.frag") == 0);
	assert(p.fg_count == 2);
	assert(strcmp(p.fg[0].path, ".tmp_files/viz/osc.c") == 0);
	assert(p.fg[0].x == 40 && p.fg[0].y == 60 && p.fg[0].w == 320 && p.fg[0].h == 240);
	assert(strcmp(p.fg[1].path, ".tmp_files/viz/bars.frag") == 0);
	assert(p.fg[1].x == 500 && p.fg[1].y == 300 && p.fg[1].w == 200 && p.fg[1].h == 120);
	assert(p.fft_bins == 512);

	/* round-trip scene equality on resolved paths */
	Scene s2;
	scene_init(&s2);
	scene_set_bg(&s2, p.bg_path);
	for(int i = 0; i < p.fg_count; i++) {
		scene_add_fg(&s2, p.fg[i].path, p.fg[i].x, p.fg[i].y, p.fg[i].w, p.fg[i].h);
	}
	assert(strcmp(s2.bg_path, ".tmp_files/viz/plasma.frag") == 0);
	assert(s2.fg_count == 2);

	/* missing file -> nonzero */
	assert(project_load(".tmp_files/does_not_exist.json", &p) != 0);

	/* minimal JSON defaults */
	FILE *f = fopen(".tmp_files/vizulobe_min.json", "w");
	fprintf(f, "{ \"bg\": \"viz/x.frag\" }");
	fclose(f);
	assert(project_load(".tmp_files/vizulobe_min.json", &p) == 0);
	assert(p.fg_count == 0);
	assert(p.fft_bins == 512);
	assert(strcmp(p.bg_path, ".tmp_files/viz/x.frag") == 0);

	printf("test_project OK\n");
	return 0;
}
