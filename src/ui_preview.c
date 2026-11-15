#include "ui_preview.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool uiPreviewRequested(int argc, char **argv) {
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--ui-preview") == 0) {
			return true;
		}
	}
	return false;
}

int uiPreviewFrames(int argc, char **argv) {
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--ui-preview-frames") == 0 && i + 1 < argc) {
			char *end = NULL;
			long v = strtol(argv[i + 1], &end, 10);
			/* A budget is a positive frame count; anything else means
			 * "run until closed", which is also the default. */
			if(end == argv[i + 1] || (end && *end) || v <= 0) {
				return 0;
			}
			return (int)(v > 1000000 ? 1000000 : v);
		}
	}
	return 0;
}

const char *uiPreviewShotPrefix(int argc, char **argv) {
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--ui-preview-shot") == 0 && argv[i + 1][0]) {
			return argv[i + 1];
		}
	}
	return NULL;
}

bool fileNewerThan(const char *path, struct timespec *stamp) {
	if(!path || !stamp) {
		return false;
	}
	struct stat sb;
	if(stat(path, &sb) != 0) {
		return false;
	}
	if(sb.st_mtim.tv_sec > stamp->tv_sec
	   || (sb.st_mtim.tv_sec == stamp->tv_sec && sb.st_mtim.tv_nsec > stamp->tv_nsec)) {
		*stamp = sb.st_mtim;
		return true;
	}
	return false;
}