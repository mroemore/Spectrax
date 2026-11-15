/* test_ui_preview.c — the pure helpers behind --ui-preview.
 *
 * These live in src/ui_preview.c (core_lib) rather than in main.c, which is
 * app-only and is recompiled into the harness: a helper there cannot be
 * linked by a test. The argv scan is the part worth pinning — the flag has a
 * budget that means "run until closed" when it is absent, and a bad budget
 * must not become a negative or enormous frame count. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "ui_preview.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while(0)

#define ASSERT_INT(got, want, msg) do { \
	long g_ = (long)(got), w_ = (long)(want); \
	if(g_ != w_) { \
		fprintf(stderr, "FAIL %s:%d: %s (got %ld, want %ld)\n", \
		        __FILE__, __LINE__, msg, g_, w_); \
		return 1; \
	} \
} while(0)

static bool requested(char **argv) {
	int argc = 0;
	while(argv[argc]) argc++;
	return uiPreviewRequested(argc, argv);
}

static int frames(char **argv) {
	int argc = 0;
	while(argv[argc]) argc++;
	return uiPreviewFrames(argc, argv);
}

static int test_flags(void) {
	char *none[]        = { "spectrax", "--volume", "50", NULL };
	char *alone[]       = { "spectrax", "--ui-preview", NULL };
	char *budget[]      = { "spectrax", "--ui-preview", "--ui-preview-frames", "60", NULL };
	char *noFlag[]      = { "spectrax", "--ui-preview-frames", "60", NULL };
	char *missing[]     = { "spectrax", "--ui-preview", "--ui-preview-frames", NULL };
	char *garbage[]     = { "spectrax", "--ui-preview", "--ui-preview-frames", "abc", NULL };
	char *negative[]    = { "spectrax", "--ui-preview", "--ui-preview-frames", "-5", NULL };
	char *afterOthers[] = { "spectrax", "--config-dir", "/tmp/x", "--ui-preview",
	                        "--ui-preview-frames", "3", NULL };
	char *trailing[]    = { "spectrax", "--ui-preview", "--ui-preview-frames", "12x", NULL };

	ASSERT_TRUE(!requested(none), "no flag");
	ASSERT_INT(frames(none), 0, "no flag has no budget");

	ASSERT_TRUE(requested(alone), "the flag alone is a request");
	ASSERT_INT(frames(alone), 0, "no budget means run until closed");

	ASSERT_TRUE(requested(budget), "flag with a budget");
	ASSERT_INT(frames(budget), 60, "budget is read");

	ASSERT_TRUE(!requested(noFlag), "a budget alone is not a request");
	/* The two scans are independent: the budget is parsed wherever it
	 * appears, and main gates the mode on requested(). Coupling them would
	 * give a caller that already checked requested() a second, surprising
	 * zero. */
	ASSERT_INT(frames(noFlag), 60, "the budget parses independently of the flag");

	ASSERT_INT(frames(missing), 0, "missing value means run until closed");
	ASSERT_INT(frames(garbage), 0, "garbage means run until closed");
	ASSERT_INT(frames(negative), 0, "a negative budget means run until closed");
	ASSERT_INT(frames(trailing), 0, "trailing junk is not a budget");

	ASSERT_TRUE(requested(afterOthers), "flag after other options");
	ASSERT_INT(frames(afterOthers), 3, "budget after other options");
	return 0;
}

/* --- hot reload ------------------------------------------------------- */

static char g_dir[64];

static void writeFile(const char *name) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", g_dir, name);
	FILE *f = fopen(path, "w");
	if(f) {
		fputs("{}", f);
		fclose(f);
	}
}

/* Explicit mtimes: two touches in the same second are exactly the case a
 * second-resolution stamp misses, so the test cannot rely on wall time. */
static void stampFile(const char *name, long sec) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", g_dir, name);
	struct timespec ts[2] = { { sec, 0 }, { sec, 0 } };
	utimensat(AT_FDCWD, path, ts, 0);
}

static int test_file_newer_than(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s/watch.txt", g_dir);
	writeFile("watch.txt");
	stampFile("watch.txt", 1000);

	struct timespec stamp = { 0, 0 };
	ASSERT_TRUE(fileNewerThan(path, &stamp), "fresh file is newer than zero");
	ASSERT_TRUE(!fileNewerThan(path, &stamp), "same file twice is not newer");
	ASSERT_INT(stamp.tv_sec, 1000, "stamp is taken from the file");

	stampFile("watch.txt", 1001);
	ASSERT_TRUE(fileNewerThan(path, &stamp), "a later mtime is newer");

	ASSERT_TRUE(!fileNewerThan("/nonexistent/at/all", &stamp), "a missing file is not newer");
	return 0;
}

static int test_watch_masks(void) {
	char layout[256], theme[256], font[256];
	snprintf(layout, sizeof(layout), "%s/layout.json", g_dir);
	snprintf(theme, sizeof(theme), "%s/clr.json", g_dir);
	snprintf(font, sizeof(font), "%s/font.ttf", g_dir);
	writeFile("layout.json");
	writeFile("clr.json");
	writeFile("font.ttf");
	stampFile("layout.json", 1000);
	stampFile("clr.json", 1000);
	stampFile("font.ttf", 1000);

	PreviewWatch w = { 0 };
	ASSERT_INT(previewWatchChanges(&w, layout, theme, font), 0, "first call only primes");
	ASSERT_INT(previewWatchChanges(&w, layout, theme, font), 0, "nothing changed");

	stampFile("layout.json", 1001);
	int mask = previewWatchChanges(&w, layout, theme, font);
	ASSERT_TRUE((mask & PREVIEW_RELOAD_STYLE) != 0, "a layout edit recompiles styles");
	ASSERT_TRUE((mask & PREVIEW_RELOAD_LAYOUT) != 0, "a layout edit rebuilds graphs");
	ASSERT_TRUE((mask & PREVIEW_RELOAD_FONTS) == 0, "a layout edit leaves fonts alone");

	stampFile("clr.json", 1002);
	mask = previewWatchChanges(&w, layout, theme, font);
	ASSERT_TRUE((mask & PREVIEW_RELOAD_STYLE) != 0, "a theme edit recompiles styles");
	ASSERT_TRUE((mask & PREVIEW_RELOAD_FONTS) != 0, "a theme edit may change the font");
	ASSERT_TRUE((mask & PREVIEW_RELOAD_LAYOUT) == 0, "a theme edit leaves layouts alone");

	stampFile("font.ttf", 1003);
	mask = previewWatchChanges(&w, layout, theme, font);
	ASSERT_INT(mask, PREVIEW_RELOAD_FONTS, "a font file edit reloads the atlas only");

	mask = previewWatchChanges(&w, NULL, NULL, NULL);
	ASSERT_INT(mask, 0, "unwatched paths report nothing");
	return 0;
}

int main(void) {
	snprintf(g_dir, sizeof(g_dir), "./ui_preview_test.XXXXXX");
	if(!mkdtemp(g_dir)) {
		fprintf(stderr, "FAIL cannot create scratch dir\n");
		return 1;
	}
	int rc = test_flags();
	rc |= test_file_newer_than();
	rc |= test_watch_masks();
	static const char *files[] = { "watch.txt", "layout.json", "clr.json", "font.ttf" };
	for(int i = 0; i < 4; i++) {
		char path[256];
		snprintf(path, sizeof(path), "%s/%s", g_dir, files[i]);
		remove(path);
	}
	rmdir(g_dir);
	if(rc) {
		fprintf(stderr, "FAILED (ui preview)\n");
		return 1;
	}
	printf("PASSED (ui preview)\n");
	return 0;
}