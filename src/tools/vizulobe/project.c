#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "project.h"

static void path_dirname(const char *path, char *out, size_t n) {
	snprintf(out, n, "%s", path);
	char *slash = strrchr(out, '/');
	if(slash && slash != out) {
		*slash = '\0';
	} else if(slash == out) {
		out[1] = '\0';
	} else {
		out[0] = '\0';
	}
}

static void path_join(const char *dir, const char *name, char *out, size_t n) {
	if(!dir[0]) {
		snprintf(out, n, "%s", name);
	} else {
		snprintf(out, n, "%s/%s", dir, name);
	}
}

/* Store path relative to the project dir if it's under it, else as-is. */
static void path_relativize(const char *dir, const char *path, char *out, size_t n) {
	size_t dl = strlen(dir);
	if(dir[0] && strncmp(path, dir, dl) == 0 && path[dl] == '/') {
		snprintf(out, n, "%s", path + dl + 1);
	} else {
		snprintf(out, n, "%s", path);
	}
}

/* Resolve a stored path: relative paths hang off the project dir. */
static void path_resolve(const char *dir, const char *path, char *out, size_t n) {
	if(path[0] == '/') {
		snprintf(out, n, "%s", path);
	} else {
		path_join(dir, path, out, n);
	}
}

int project_save(const char *filename, const Scene *s, int fft_bins) {
	char dir[VIZ_PATH_MAX];
	path_dirname(filename, dir, sizeof(dir));

	cJSON *root = cJSON_CreateObject();
	if(s->bg_path[0]) {
		char rel[VIZ_PATH_MAX];
		path_relativize(dir, s->bg_path, rel, sizeof(rel));
		cJSON_AddStringToObject(root, "bg", rel);
	}
	cJSON *fg = cJSON_AddArrayToObject(root, "fg");
	for(int i = 0; i < s->fg_count; i++) {
		cJSON *item = cJSON_CreateObject();
		char rel[VIZ_PATH_MAX];
		path_relativize(dir, s->fg[i].path, rel, sizeof(rel));
		cJSON_AddStringToObject(item, "path", rel);
		cJSON_AddNumberToObject(item, "x", s->fg[i].x);
		cJSON_AddNumberToObject(item, "y", s->fg[i].y);
		cJSON_AddNumberToObject(item, "w", s->fg[i].w);
		cJSON_AddNumberToObject(item, "h", s->fg[i].h);
		cJSON_AddItemToArray(fg, item);
	}
	cJSON_AddNumberToObject(root, "fft_bins", fft_bins);

	char *text = cJSON_Print(root);
	FILE *file = fopen(filename, "wb");
	int ok = 0;
	if(file && text) {
		ok = (fwrite(text, 1, strlen(text), file) == strlen(text));
		fclose(file);
	}
	if(text) {
		cJSON_free(text);
	}
	cJSON_Delete(root);
	return ok ? 0 : 1;
}

int project_load(const char *filename, ProjectFile *out) {
	char dir[VIZ_PATH_MAX];
	path_dirname(filename, dir, sizeof(dir));

	FILE *file = fopen(filename, "rb");
	if(!file) {
		return 1;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)size + 1);
	if(!buf) {
		fclose(file);
		return 1;
	}
	if(fread(buf, 1, (size_t)size, file) != (size_t)size) {
		free(buf);
		fclose(file);
		return 1;
	}
	buf[size] = '\0';
	fclose(file);

	memset(out, 0, sizeof(*out));
	out->fft_bins = 512;

	cJSON *root = cJSON_Parse(buf);
	free(buf);
	if(!root) {
		return 1;
	}

	cJSON *bg = cJSON_GetObjectItemCaseSensitive(root, "bg");
	if(bg && cJSON_IsString(bg)) {
		path_resolve(dir, bg->valuestring, out->bg_path, sizeof(out->bg_path));
	}

	cJSON *fft = cJSON_GetObjectItemCaseSensitive(root, "fft_bins");
	if(fft && cJSON_IsNumber(fft)) {
		out->fft_bins = fft->valueint;
	}

	cJSON *fg = cJSON_GetObjectItemCaseSensitive(root, "fg");
	if(fg && cJSON_IsArray(fg)) {
		cJSON *item;
		cJSON_ArrayForEach(item, fg) {
			if(out->fg_count >= VIZ_MAX_FG) {
				break;
			}
			cJSON *path = cJSON_GetObjectItemCaseSensitive(item, "path");
			cJSON *x = cJSON_GetObjectItemCaseSensitive(item, "x");
			cJSON *y = cJSON_GetObjectItemCaseSensitive(item, "y");
			cJSON *w = cJSON_GetObjectItemCaseSensitive(item, "w");
			cJSON *h = cJSON_GetObjectItemCaseSensitive(item, "h");
			if(!path || !cJSON_IsString(path)) {
				continue;
			}
			path_resolve(dir, path->valuestring, out->fg[out->fg_count].path, sizeof(out->fg[out->fg_count].path));
			out->fg[out->fg_count].x = cJSON_IsNumber(x) ? x->valueint : 0;
			out->fg[out->fg_count].y = cJSON_IsNumber(y) ? y->valueint : 0;
			out->fg[out->fg_count].w = cJSON_IsNumber(w) ? w->valueint : VIZ_MIN_RECT;
			out->fg[out->fg_count].h = cJSON_IsNumber(h) ? h->valueint : VIZ_MIN_RECT;
			out->fg_count++;
		}
	}

	cJSON_Delete(root);
	return 0;
}
