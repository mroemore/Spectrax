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

/* On truncation, out is left untouched (caller falls back). */
void resolveDataDir(int argc, char **argv, char *out, size_t outsz) {
	/* 1. --data-dir <dir> */
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--data-dir") == 0) {
			int n = snprintf(out, outsz, "%s", argv[i + 1]);
			if(n < 0 || (size_t)n >= outsz) { return; }
			return;
		}
	}
	/* 2. $XDG_CONFIG_HOME/spectrax or $HOME/.config/spectrax, if it has cfg.json */
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char cand[1024];
	if(xdg && *xdg) {
		snprintf(cand, sizeof(cand), "%s/spectrax", xdg);
		if(dirHasFile(cand, "cfg.json")) {
			int n = snprintf(out, outsz, "%s", cand);
			if(n < 0 || (size_t)n >= outsz) { return; }
			return;
		}
	}
	if(home && *home) {
		snprintf(cand, sizeof(cand), "%s/.config/spectrax", home);
		if(dirHasFile(cand, "cfg.json")) {
			int n = snprintf(out, outsz, "%s", cand);
			if(n < 0 || (size_t)n >= outsz) { return; }
			return;
		}
	}
	/* 3. cwd */
	int n = snprintf(out, outsz, "%s", ".");
	if(n < 0 || (size_t)n >= outsz) { return; }
}

bool chdirToDataDir(const char *base) {
	if(!base || strcmp(base, ".") == 0) {
		return true;
	}
	return chdir(base) == 0;
}