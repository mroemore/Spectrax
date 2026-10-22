/* Theme config validation.
 *
 * Unlike test_cfg's targeted parse/round-trip cases, this test treats
 * the SHIPPED theme as an artefact that must be complete and canonical:
 * every ColourScheme field is covered, every key resolves, legacy flat
 * keys still load, and the writer emits the grouped schema it reads.
 * The shipped file path is compiled in by meson so this can never drift
 * onto a stale copy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "io/config_io.h"
#include "theme.h"

#ifndef SPECTRAX_THEME_JSON
#define SPECTRAX_THEME_JSON "clr.json"
#endif

#define TMP_DIR ".tmp_files"
#define TMP_THEME TMP_DIR "/theme_shipped_rt.json"
#define TMP_FLAT TMP_DIR "/theme_flat.json"
#define TMP_BOGUS TMP_DIR "/theme_bogus.json"

static int failures = 0;

#define CHECK(cond, msg) do { \
	if(!(cond)) { printf("FAIL %s\n", msg); failures = 1; } \
} while(0)

static void ensure_tmp(void) {
	mkdir(TMP_DIR, 0755);
}

static int write_text(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	if(!f) {
		return 0;
	}
	int ok = fputs(text, f) >= 0;
	fclose(f);
	return ok;
}

static Color *field_at(ColourScheme *cs, const char *path) {
	return themeFieldByName(cs, path);
}

/* Resolve a canonical dotted path inside a parsed `colors` object.
 * chip.paletteN is stored as an array; everything else as nested
 * objects. Returns NULL when the writer did not emit that key. */
static cJSON *lookup_written(cJSON *colors, const char *path) {
	if(strncmp(path, "chip.palette", 12) == 0 && path[12] >= '0' && path[12] <= '7') {
		cJSON *chip = cJSON_GetObjectItemCaseSensitive(colors, "chip");
		cJSON *arr = chip ? cJSON_GetObjectItemCaseSensitive(chip, "palette") : NULL;
		if(!cJSON_IsArray(arr)) {
			return NULL;
		}
		return cJSON_GetArrayItem(arr, path[12] - '0');
	}
	char group[32];
	const char *dot = strchr(path, '.');
	if(!dot) {
		return NULL;
	}
	size_t gl = (size_t)(dot - path);
	if(gl >= sizeof(group)) {
		return NULL;
	}
	memcpy(group, path, gl);
	group[gl] = '\0';
	cJSON *g = cJSON_GetObjectItemCaseSensitive(colors, group);
	if(!cJSON_IsObject(g)) {
		return NULL;
	}
	return cJSON_GetObjectItemCaseSensitive(g, dot + 1);
}

/* 1. The mapping table itself: unique names, unique fields. */
static void test_mapping_table(void) {
	int n = themePathCount();
	CHECK(n == 47, "theme path table has 47 entries");
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	for(int i = 0; i < n; i++) {
		const char *a = themePathName(i);
		CHECK(a != NULL && *a != '\0', "theme path name is non-empty");
		if(!a) {
			continue;
		}
		CHECK(strchr(a, '.') != NULL, "theme path is grouped (group.leaf)");
		Color *fa = field_at(&cs, a);
		CHECK(fa != NULL, "theme path resolves to a field");
		for(int j = i + 1; j < n; j++) {
			const char *b = themePathName(j);
			CHECK(b && strcmp(a, b) != 0, "theme path names are unique");
			if(fa && b) {
				CHECK(field_at(&cs, b) != fa, "theme paths map to distinct fields");
			}
		}
	}
}

/* 2. The shipped theme sets every field. A typo'd or missing key would
 * leave a zero-alpha field behind. */
static void test_shipped_is_complete(void) {
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	FontConfig font = { { 0 }, 0, 0 };
	loadThemeJson(SPECTRAX_THEME_JSON, &cs, &font);
	for(int i = 0; i < themePathCount(); i++) {
		const char *p = themePathName(i);
		Color *f = field_at(&cs, p);
		CHECK(f != NULL && f->a != 0, p);
	}
	CHECK(font.size > 0, "shipped theme supplies font.size");
	CHECK(font.path[0] != '\0', "shipped theme supplies font.path");
}

/* 3. The writer emits the grouped canonical schema and round-trips. */
static void test_writer_roundtrip(void) {
	ensure_tmp();
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	FontConfig font = { { 0 }, 0, 0 };
	loadThemeJson(SPECTRAX_THEME_JSON, &cs, &font);
	saveThemeJson(TMP_THEME, &cs, &font);

	FILE *f = fopen(TMP_THEME, "rb");
	CHECK(f != NULL, "saved theme is readable");
	if(!f) {
		return;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)sz + 1);
	CHECK(buf != NULL, "saved theme buffer allocated");
	if(!buf) {
		fclose(f);
		return;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	buf[rd] = '\0';
	fclose(f);

	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	CHECK(doc != NULL, "saved theme parses");
	if(!doc) {
		return;
	}
	cJSON *schema = cJSON_GetObjectItemCaseSensitive(doc, "schema");
	CHECK(cJSON_IsNumber(schema) && schema->valueint == 2, "saved theme declares schema 2");
	cJSON *colors = cJSON_GetObjectItemCaseSensitive(doc, "colors");
	CHECK(cJSON_IsObject(colors), "saved theme has colors object");
	if(cJSON_IsObject(colors)) {
		for(int i = 0; i < themePathCount(); i++) {
			const char *p = themePathName(i);
			cJSON *v = lookup_written(colors, p);
			CHECK(cJSON_IsString(v), p);
		}
		/* No legacy flat keys may leak into the canonical writer. */
		CHECK(cJSON_GetObjectItemCaseSensitive(colors, "background") == NULL,
		      "saved theme is not flat");
	}
	cJSON_Delete(doc);

	ColourScheme cs2;
	memset(&cs2, 0, sizeof(cs2));
	FontConfig font2 = { { 0 }, 0, 0 };
	loadThemeJson(TMP_THEME, &cs2, &font2);
	for(int i = 0; i < themePathCount(); i++) {
		const char *p = themePathName(i);
		Color *a = field_at(&cs, p);
		Color *b = field_at(&cs2, p);
		CHECK(a && b && memcmp(a, b, sizeof(Color)) == 0, p);
	}
	unlink(TMP_THEME);
}

/* 4. Themes written by any earlier build (flat keys) still load. */
static void test_legacy_flat_keys(void) {
	ensure_tmp();
	CHECK(write_text(TMP_FLAT,
		"{\"colors\":{"
		"\"background\":\"#010203FF\",\"panel\":\"#040506FF\","
		"\"font\":\"#070809FF\",\"outline\":\"#0A0B0CFF\","
		"\"labelSelected\":\"#0D0E0FFF\",\"chipPalette3\":\"#101112FF\"}}"),
		"wrote legacy fixture");
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	FontConfig font = { { 0 }, 0, 0 };
	loadThemeJson(TMP_FLAT, &cs, &font);
	CHECK(cs.backgroundColor.r == 1 && cs.backgroundColor.g == 2 && cs.backgroundColor.b == 3,
	      "legacy background loads");
	CHECK(cs.panel.r == 4 && cs.panel.g == 5, "legacy panel loads");
	CHECK(cs.fontColour.r == 7, "legacy font loads");
	CHECK(cs.outlineColour.r == 0x0A, "legacy outline loads");
	CHECK(cs.labelSelected.r == 0x0D, "legacy labelSelected loads");
	CHECK(cs.chipPalette3.r == 0x10 && cs.chipPalette3.b == 0x12, "legacy chipPalette3 loads");
	unlink(TMP_FLAT);
}

/* 5. Unknown keys and zero-alpha values never write into the scheme. */
static void test_unknown_and_zero_alpha_ignored(void) {
	ensure_tmp();
	CHECK(write_text(TMP_BOGUS,
		"{\"colors\":{"
		"\"bogus\":\"#FF0000FF\","
		"\"text\":{\"nope\":\"#00FF00FF\",\"primary\":\"#00000000\"},"
		"\"chip\":{\"palette\":[\"#00000000\"]}}}"),
		"wrote bogus fixture");
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	FontConfig font = { { 0 }, 0, 0 };
	loadThemeJson(TMP_BOGUS, &cs, &font);
	CHECK(themeFieldByName(&cs, "text.primary")->a == 0,
	      "zero-alpha value leaves the field untouched");
	CHECK(cs.chipPalette0.a == 0, "zero-alpha palette entry is ignored");
	for(int i = 0; i < themePathCount(); i++) {
		Color *f = field_at(&cs, themePathName(i));
		CHECK(f && f->a == 0, "unknown keys write nothing");
	}
	unlink(TMP_BOGUS);
}

int main(void) {
	test_mapping_table();
	test_shipped_is_complete();
	test_writer_roundtrip();
	test_legacy_flat_keys();
	test_unknown_and_zero_alpha_ignored();
	if(failures) {
		printf("test_theme: FAILURES\n");
		return 1;
	}
	printf("test_theme: all tests passed\n");
	return 0;
}
