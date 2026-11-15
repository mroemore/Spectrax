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

const char *uiPreviewModeArg(int argc, char **argv) {
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--ui-preview-mode") == 0 && argv[i + 1][0]) {
			return argv[i + 1];
		}
	}
	return NULL;
}

const char *uiPreviewShotPrefix(int argc, char **argv) {
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--ui-preview-shot") == 0 && argv[i + 1][0]) {
			return argv[i + 1];
		}
	}
	return NULL;
}

PreviewMode previewNextMode(PreviewMode mode) {
	int next = (int)mode + 1;
	if(next < 0 || next >= PREVIEW_MODE_COUNT) {
		next = 0;
	}
	return (PreviewMode)next;
}

const char *previewModeName(PreviewMode mode) {
	switch(mode) {
		case PREVIEW_MODE_PAGED:  return "paged";
		case PREVIEW_MODE_SINGLE: return "single";
		default:                  return "all";
	}
}

PreviewMode previewModeFromName(const char *name) {
	if(!name) {
		return PREVIEW_MODE_ALL;
	}
	for(int i = 0; i < PREVIEW_MODE_COUNT; i++) {
		if(strcmp(name, previewModeName((PreviewMode)i)) == 0) {
			return (PreviewMode)i;
		}
	}
	return PREVIEW_MODE_ALL;
}

int previewPageCount(int panels) {
	if(panels <= 0) {
		return 0;
	}
	return (panels + PREVIEW_PER_PAGE - 1) / PREVIEW_PER_PAGE;
}

int previewWrapIndex(int v, int n) {
	if(n <= 0) {
		return 0;
	}
	int w = v % n;
	return w < 0 ? w + n : w;
}

int previewVisiblePanels(PreviewMode mode, int page, int index, int panels,
                         int *out, int max) {
	if(!out || panels <= 0 || max <= 0) {
		return 0;
	}
	int n = 0;
	switch(mode) {
		case PREVIEW_MODE_ALL:
			for(int i = 0; i < panels && n < max; i++) {
				out[n++] = i;
			}
			break;
		case PREVIEW_MODE_PAGED: {
			int pages = previewPageCount(panels);
			int first = previewWrapIndex(page, pages) * PREVIEW_PER_PAGE;
			for(int i = first; i < panels && i < first + PREVIEW_PER_PAGE && n < max; i++) {
				out[n++] = i;
			}
			break;
		}
		case PREVIEW_MODE_SINGLE:
			out[n++] = previewWrapIndex(index, panels);
			break;
		default:
			break;
	}
	return n;
}

int previewInstrumentChannels(const int *voiceTypes, int channelCount,
                              int *out, int max) {
	if(!voiceTypes || !out || channelCount <= 0 || max <= 0) {
		return 0;
	}
	int found = 0;
	for(int ch = 0; ch < channelCount && found < max; ch++) {
		bool dup = false;
		for(int k = 0; k < found; k++) {
			if(voiceTypes[out[k]] == voiceTypes[ch]) {
				dup = true;
			}
		}
		if(!dup) {
			out[found++] = ch;
		}
	}
	return found;
}

/* Compares without storing: the caller decides when a change counts as
 * applied (see previewWatchChanges). */
static bool stampNewerThan(const char *path, const struct timespec *stamp) {
	if(!path || !stamp) {
		return false;
	}
	struct stat sb;
	if(stat(path, &sb) != 0) {
		return false;
	}
	return sb.st_mtim.tv_sec > stamp->tv_sec
	       || (sb.st_mtim.tv_sec == stamp->tv_sec && sb.st_mtim.tv_nsec > stamp->tv_nsec);
}

int previewWatchChanges(PreviewWatch *w, const char *layoutPath,
                        const char *themePath, const char *fontPath) {
	if(!w) {
		return 0;
	}
	if(!w->primed) {
		/* First call stamps the files as they are: the app has just loaded
		 * them, so nothing is "changed" yet. */
		previewWatchAck(w, layoutPath, themePath, fontPath);
		w->primed = 1;
		return 0;
	}
	int mask = 0;
	if(stampNewerThan(layoutPath, &w->layout)) {
		mask |= PREVIEW_RELOAD_STYLE | PREVIEW_RELOAD_LAYOUT;
	}
	if(stampNewerThan(themePath, &w->theme)) {
		mask |= PREVIEW_RELOAD_STYLE | PREVIEW_RELOAD_FONTS;
	}
	if(stampNewerThan(fontPath, &w->font)) {
		mask |= PREVIEW_RELOAD_FONTS;
	}
	return mask;
}

void previewWatchAck(PreviewWatch *w, const char *layoutPath,
                     const char *themePath, const char *fontPath) {
	if(!w) {
		return;
	}
	fileNewerThan(layoutPath, &w->layout);
	fileNewerThan(themePath, &w->theme);
	fileNewerThan(fontPath, &w->font);
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