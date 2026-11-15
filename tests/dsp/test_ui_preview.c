/* test_ui_preview.c — the pure helpers behind --ui-preview.
 *
 * These live in src/ui_preview.c (core_lib) rather than in main.c, which is
 * app-only and is recompiled into the harness: a helper there cannot be
 * linked by a test. The argv scan is the part worth pinning — the flag has a
 * budget that means "run until closed" when it is absent, and a bad budget
 * must not become a negative or enormous frame count. */

#include <stdio.h>
#include <string.h>

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

int main(void) {
	if(test_flags()) {
		fprintf(stderr, "FAILED (ui preview args)\n");
		return 1;
	}
	printf("PASSED (ui preview args)\n");
	return 0;
}