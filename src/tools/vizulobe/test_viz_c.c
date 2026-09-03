#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "viz.h"

static void write_file(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	assert(f);
	fprintf(f, "%s", text);
	fclose(f);
}

int main(void) {
	mkdir(".tmp_files", 0755);
	write_file(".tmp_files/good_viz.c",
		"#include \"vizulobe.h\"\n"
		"void viz_init(viz_t *ctx) { (void)ctx; }\n"
		"void viz_frame(viz_t *ctx) { (void)ctx; }\n");

	write_file(".tmp_files/bad_viz.c",
		"#include \"vizulobe.h\"\n"
		"void viz_frame(viz_t *ctx) { this is not valid C }\n");

	write_file(".tmp_files/noentry_viz.c",
		"#include \"vizulobe.h\"\n"
		"void some_other_fn(viz_t *ctx) { (void)ctx; }\n");

	/* valid snippet loads; entry points present */
	Viz *good = viz_load(".tmp_files/good_viz.c");
	assert(good);
	assert(viz_is_loaded(good));
	assert(good->kind == VIZ_KIND_C);
	assert(good->u.c.frame != NULL);
	assert(good->u.c.init != NULL);

	/* syntax error surfaces as error text */
	Viz *bad = viz_load(".tmp_files/bad_viz.c");
	assert(bad);
	assert(!viz_is_loaded(bad));
	assert(bad->kind == VIZ_KIND_ERR);
	assert(strlen(viz_error(bad)) > 0);

	/* missing viz_frame -> error */
	Viz *noentry = viz_load(".tmp_files/noentry_viz.c");
	assert(noentry);
	assert(!viz_is_loaded(noentry));
	assert(viz_error(noentry)[0] != '\0');

	/* unsupported extension */
	Viz *txt = viz_load(".tmp_files/foo.txt");
	assert(txt);
	assert(!viz_is_loaded(txt));
	assert(viz_error(txt)[0] != '\0');

	viz_free(good);
	viz_free(bad);
	viz_free(noentry);
	viz_free(txt);

	printf("test_viz_c OK\n");
	return 0;
}
