#include "paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool dirHasFile(const char *dir, const char *name) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return access(path, F_OK) == 0;
}

/* Helper: copy src into out, leaving out untouched if src (incl. NUL)
 * would not fit. Returns true on success. */
static bool safeCopy(char *out, size_t outsz, const char *src) {
	if(strlen(src) >= outsz) {
		return false;
	}
	strcpy(out, src);
	return true;
}

/* On truncation, out is left untouched (caller falls back). */
void resolveConfigDir(int argc, char **argv, char *out, size_t outsz) {
	/* 1. --config-dir <dir> */
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--config-dir") == 0) {
			if(!safeCopy(out, outsz, argv[i + 1])) { return; }
			return;
		}
	}
	/* 2. $XDG_CONFIG_HOME/spectrax if it has cfg.json */
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char cand[1024];
	if(xdg && *xdg) {
		snprintf(cand, sizeof(cand), "%s/spectrax", xdg);
		if(dirHasFile(cand, "cfg.json")) {
			if(!safeCopy(out, outsz, cand)) { return; }
			return;
		}
	}
	/* 3. $HOME/.config/spectrax if it has cfg.json */
	if(home && *home) {
		snprintf(cand, sizeof(cand), "%s/.config/spectrax", home);
		if(dirHasFile(cand, "cfg.json")) {
			if(!safeCopy(out, outsz, cand)) { return; }
			return;
		}
	}
	/* 4. cwd */
	if(!safeCopy(out, outsz, ".")) { return; }
}

/* On truncation, out is left untouched (caller falls back). */
void resolveDataDir(int argc, char **argv, char *out, size_t outsz) {
	/* 1. --data-dir <dir> */
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--data-dir") == 0) {
			if(!safeCopy(out, outsz, argv[i + 1])) { return; }
			return;
		}
	}
	/* 2. $XDG_DATA_HOME/spectrax (if set) or $HOME/.local/share/spectrax,
	 *    gated on directory existence (not cfg.json). */
	const char *xdg_data = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	char cand[1024];
	if(xdg_data && *xdg_data) {
		snprintf(cand, sizeof(cand), "%s/spectrax", xdg_data);
		if(access(cand, F_OK) == 0) {
			if(!safeCopy(out, outsz, cand)) { return; }
			return;
		}
	}
	if(home && *home) {
		snprintf(cand, sizeof(cand), "%s/.local/share/spectrax", home);
		if(access(cand, F_OK) == 0) {
			if(!safeCopy(out, outsz, cand)) { return; }
			return;
		}
	}
	/* 3. cwd */
	if(!safeCopy(out, outsz, ".")) { return; }
}

bool chdirToDataDir(const char *base) {
	if(!base || strcmp(base, ".") == 0) {
		return true;
	}
	return chdir(base) == 0;
}