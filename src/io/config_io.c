#include "config_io.h"
#include "cJSON.h"
#include "settings.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Canonical theme colour paths.
 *
 * The JSON surface groups keys by use case (`surface.panel`,
 * `text.primary`, ...) while the C side keeps the historical
 * ColourScheme field names. This table is the single mapping between the
 * two: `themeFieldByName`, `saveThemeJson` and the theme validator all
 * walk it, so adding a colour means adding one row here. */
typedef struct {
	const char *path;
	size_t offset;
} ThemePath;

static const ThemePath g_themePaths[] = {
	{ "surface.window",      offsetof(ColourScheme, backgroundColor) },
	{ "surface.panel",       offsetof(ColourScheme, panel) },
	{ "surface.inset",       offsetof(ColourScheme, valueDisplayBg) },
	{ "surface.dim",         offsetof(ColourScheme, layerDim) },

	{ "text.primary",        offsetof(ColourScheme, fontColour) },
	{ "text.secondary",      offsetof(ColourScheme, secondaryFontColour) },
	{ "text.muted",          offsetof(ColourScheme, label) },
	{ "text.mutedSelected",  offsetof(ColourScheme, labelSelected) },
	{ "text.value",          offsetof(ColourScheme, valueText) },
	{ "text.gridLabel",      offsetof(ColourScheme, arrangerCellText) },
	{ "text.disabled",       offsetof(ColourScheme, disabledText) },

	{ "line.panel",          offsetof(ColourScheme, panelBorder) },
	{ "line.wrapper",        offsetof(ColourScheme, wrapperBorder) },
	{ "line.strong",         offsetof(ColourScheme, outlineColour) },
	{ "line.grid",           offsetof(ColourScheme, vline) },
	{ "line.step",           offsetof(ColourScheme, stepBorder) },

	{ "cell.default",        offsetof(ColourScheme, defaultCell) },
	{ "cell.blank",          offsetof(ColourScheme, blankCell) },
	{ "cell.highlighted",    offsetof(ColourScheme, highlightedCell) },
	{ "cell.selected",       offsetof(ColourScheme, selectedCell) },

	{ "control.dial",        offsetof(ColourScheme, dial) },
	{ "control.accent",      offsetof(ColourScheme, reddish) },

	{ "waveform.bg",         offsetof(ColourScheme, waveformBg) },
	{ "waveform.primary",    offsetof(ColourScheme, waveform) },
	{ "waveform.alt",        offsetof(ColourScheme, waveformAlt) },
	{ "waveform.overlay",    offsetof(ColourScheme, poly) },
	{ "waveform.playhead",   offsetof(ColourScheme, spectrogramPlayhead) },

	{ "sample.bg",           offsetof(ColourScheme, sampleBg) },
	{ "sample.bgAlt",        offsetof(ColourScheme, sampleAltBg) },
	{ "sample.border",       offsetof(ColourScheme, sampleBorder) },
	{ "sample.closed",       offsetof(ColourScheme, stepClosed) },

	{ "arranger.playhead",   offsetof(ColourScheme, arrangerPlayhead) },

	{ "mod.lfo",             offsetof(ColourScheme, modStripLfo) },
	{ "mod.env",             offsetof(ColourScheme, modStripEnv) },
	{ "mod.random",          offsetof(ColourScheme, modStripRnd) },
	{ "mod.offset",          offsetof(ColourScheme, modStripOfs) },
	{ "mod.default",         offsetof(ColourScheme, modStripDefault) },
	{ "mod.routeAdd",        offsetof(ColourScheme, routeAdd) },
	{ "mod.routeMul",        offsetof(ColourScheme, routeMul) },

	{ "chip.palette0",       offsetof(ColourScheme, chipPalette0) },
	{ "chip.palette1",       offsetof(ColourScheme, chipPalette1) },
	{ "chip.palette2",       offsetof(ColourScheme, chipPalette2) },
	{ "chip.palette3",       offsetof(ColourScheme, chipPalette3) },
	{ "chip.palette4",       offsetof(ColourScheme, chipPalette4) },
	{ "chip.palette5",       offsetof(ColourScheme, chipPalette5) },
	{ "chip.palette6",       offsetof(ColourScheme, chipPalette6) },
	{ "chip.palette7",       offsetof(ColourScheme, chipPalette7) },
};

#define THEME_PATH_COUNT ((int)(sizeof(g_themePaths) / sizeof(g_themePaths[0])))

/* Pre-rename flat key -> canonical path. Kept so themes written by any
 * earlier build keep loading; the next save rewrites them canonically. */
typedef struct {
	const char *legacy;
	const char *path;
} ThemeAlias;

static const ThemeAlias g_themeAliases[] = {
	{ "background",         "surface.window" },
	{ "panel",              "surface.panel" },
	{ "valueDisplayBg",     "surface.inset" },
	{ "layerDim",           "surface.dim" },

	{ "font",               "text.primary" },
	{ "secondaryFont",      "text.secondary" },
	{ "label",              "text.muted" },
	{ "labelSelected",      "text.mutedSelected" },
	{ "valueText",          "text.value" },
	{ "arrangerCellText",   "text.gridLabel" },

	{ "panelBorder",        "line.panel" },
	{ "wrapperBorder",      "line.wrapper" },
	{ "outline",            "line.strong" },
	{ "vline",              "line.grid" },
	{ "stepBorder",         "line.step" },

	{ "defaultCell",        "cell.default" },
	{ "blankCell",          "cell.blank" },
	{ "highlightedCell",    "cell.highlighted" },
	{ "selectedCell",       "cell.selected" },

	{ "dial",               "control.dial" },
	{ "reddish",            "control.accent" },

	{ "waveformBg",         "waveform.bg" },
	{ "waveform",           "waveform.primary" },
	{ "waveformAlt",        "waveform.alt" },
	{ "poly",               "waveform.overlay" },
	{ "spectrogramPlayhead", "waveform.playhead" },

	{ "sampleBg",           "sample.bg" },
	{ "sampleAltBg",        "sample.bgAlt" },
	{ "sampleBorder",       "sample.border" },
	{ "stepClosed",         "sample.closed" },

	{ "arrangerPlayhead",   "arranger.playhead" },

	{ "modStripLfo",        "mod.lfo" },
	{ "modStripEnv",        "mod.env" },
	{ "modStripRnd",        "mod.random" },
	{ "modStripOfs",        "mod.offset" },
	{ "modStripDefault",    "mod.default" },
	{ "routeAdd",           "mod.routeAdd" },
	{ "routeMul",           "mod.routeMul" },

	{ "chipPalette0",       "chip.palette0" },
	{ "chipPalette1",       "chip.palette1" },
	{ "chipPalette2",       "chip.palette2" },
	{ "chipPalette3",       "chip.palette3" },
	{ "chipPalette4",       "chip.palette4" },
	{ "chipPalette5",       "chip.palette5" },
	{ "chipPalette6",       "chip.palette6" },
	{ "chipPalette7",       "chip.palette7" },
};

static const char *canonicalThemePath(const char *name) {
	if(!name) {
		return NULL;
	}
	for(size_t i = 0; i < sizeof(g_themeAliases) / sizeof(g_themeAliases[0]); i++) {
		if(strcmp(name, g_themeAliases[i].legacy) == 0) {
			return g_themeAliases[i].path;
		}
	}
	return name;
}

int themePathCount(void) {
	return THEME_PATH_COUNT;
}

const char *themePathName(int index) {
	if(index < 0 || index >= THEME_PATH_COUNT) {
		return NULL;
	}
	return g_themePaths[index].path;
}

static int hexNibble(char c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool parseHexColor(const char *s, Color *out) {
	if(!s) {
		return false;
	}
	if(*s == '#') {
		s++;
	}
	size_t len = strlen(s);
	if(len != 6 && len != 8) {
		return false;
	}
	int nib[8];
	for(size_t i = 0; i < len; i++) {
		nib[i] = hexNibble(s[i]);
		if(nib[i] < 0) {
			return false;
		}
	}
	Color c;
	c.r = (unsigned char)((nib[0] << 4) | nib[1]);
	c.g = (unsigned char)((nib[2] << 4) | nib[3]);
	c.b = (unsigned char)((nib[4] << 4) | nib[5]);
	c.a = (len == 8) ? (unsigned char)((nib[6] << 4) | nib[7]) : 255;
	*out = c;
	return true;
}

Color *themeFieldByName(ColourScheme *cs, const char *name) {
	if(!cs) return NULL;
	const char *path = canonicalThemePath(name);
	if(!path) return NULL;
	for(int i = 0; i < THEME_PATH_COUNT; i++) {
		if(strcmp(path, g_themePaths[i].path) == 0) {
			return (Color *)((char *)cs + g_themePaths[i].offset);
		}
	}
	return NULL;
}

static void colorToHex(const Color *c, char *out) {
	/* Format `#RRGGBBAA` = 9 chars + NUL = 10 bytes. */
	snprintf(out, 10, "#%02X%02X%02X%02X", c->r, c->g, c->b, c->a);
}

/* Apply every leaf of a `colors` object (nested groups allowed) to cs.
 * Unknown keys and zero-alpha values are ignored, matching the legacy
 * flat loader behaviour. */
static void applyThemeColors(ColourScheme *cs, cJSON *obj, const char *prefix) {
	cJSON *item = NULL;
	cJSON_ArrayForEach(item, obj) {
		char path[128];
		if(prefix[0]) {
			snprintf(path, sizeof(path), "%s.%s", prefix, item->string);
		} else {
			snprintf(path, sizeof(path), "%s", item->string);
		}
		if(cJSON_IsObject(item)) {
			applyThemeColors(cs, item, path);
			continue;
		}
		if(cJSON_IsArray(item)) {
			/* Compact form: chip.palette is chip.palette0..7. */
			if(strcmp(path, "chip.palette") == 0) {
				int n = cJSON_GetArraySize(item);
				if(n > 8) n = 8;
				for(int i = 0; i < n; i++) {
					cJSON *el = cJSON_GetArrayItem(item, i);
					Color v;
					if(!cJSON_IsString(el) || !parseHexColor(el->valuestring, &v) || v.a == 0) {
						continue;
					}
					char leaf[64];
					snprintf(leaf, sizeof(leaf), "chip.palette%d", i);
					Color *field = themeFieldByName(cs, leaf);
					if(field) {
						*field = v;
					}
				}
			}
			continue;
		}
		Color v;
		Color *field = themeFieldByName(cs, path);
		/* alpha == 0 means "unset" -- a key written back by an older
		 * build before the field existed (BSS-zero cs). Keep the
		 * default rather than rendering fully transparent. No
		 * legitimate theme value has zero alpha (surface.dim is 0xAA). */
		if(field && cJSON_IsString(item) && parseHexColor(item->valuestring, &v) && v.a != 0) {
			*field = v;
		}
	}
}

void loadThemeJson(const char *path, ColourScheme *cs, FontConfig *font) {
	if(!path || !cs || !font) return;
	FILE *f = fopen(path, "rb");
	if(!f) return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > 1 << 20) { fclose(f); return; }
	char *buf = malloc((size_t)sz + 1);
	if(!buf) { fclose(f); return; }
	if(fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return; }
	buf[sz] = '\0';
	fclose(f);

	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) return;

	cJSON *colors = cJSON_GetObjectItemCaseSensitive(doc, "colors");
	if(cJSON_IsObject(colors)) {
		applyThemeColors(cs, colors, "");
	}
	cJSON *fontObj = cJSON_GetObjectItemCaseSensitive(doc, "font");
	if(cJSON_IsObject(fontObj)) {
		cJSON *p = cJSON_GetObjectItemCaseSensitive(fontObj, "path");
		if(cJSON_IsString(p)) strncpy(font->path, p->valuestring, sizeof(font->path) - 1);
		cJSON *s = cJSON_GetObjectItemCaseSensitive(fontObj, "size");
		if(cJSON_IsNumber(s)) font->size = s->valueint;
		cJSON *sp = cJSON_GetObjectItemCaseSensitive(fontObj, "spacing");
		if(cJSON_IsNumber(sp)) font->spacing = sp->valueint;
	}
	cJSON_Delete(doc);
}

static cJSON *ensureThemeGroup(cJSON *colors, const char *group) {
	cJSON *g = cJSON_GetObjectItemCaseSensitive(colors, group);
	if(!g) {
		g = cJSON_CreateObject();
		if(!g) {
			return NULL;
		}
		cJSON_AddItemToObject(colors, group, g);
	}
	return g;
}

void saveThemeJson(const char *path, const ColourScheme *cs, const FontConfig *font) {
	if(!path || !cs || !font) return;
	cJSON *doc = cJSON_CreateObject();
	if(!doc) return;
	cJSON_AddNumberToObject(doc, "schema", 2);
	cJSON *fontObj = cJSON_CreateObject();
	cJSON_AddStringToObject(fontObj, "path", font->path);
	cJSON_AddNumberToObject(fontObj, "size", font->size);
	cJSON_AddNumberToObject(fontObj, "spacing", font->spacing);
	cJSON_AddItemToObject(doc, "font", fontObj);

	cJSON *colors = cJSON_CreateObject();
	cJSON *chipPalette = NULL;
	/* Emit the canonical grouped form straight from the mapping table so
	 * the writer can never drift from the loader. */
	for(int i = 0; i < THEME_PATH_COUNT; i++) {
		const char *p = g_themePaths[i].path;
		const Color *field = (const Color *)((const char *)cs + g_themePaths[i].offset);
		char hex[10];
		colorToHex(field, hex);
		if(strncmp(p, "chip.palette", 12) == 0 && isdigit((unsigned char)p[12])) {
			if(!chipPalette) {
				cJSON *chip = ensureThemeGroup(colors, "chip");
				if(!chip) {
					continue;
				}
				chipPalette = cJSON_CreateArray();
				if(!chipPalette) {
					continue;
				}
				cJSON_AddItemToObject(chip, "palette", chipPalette);
			}
			cJSON_AddItemToArray(chipPalette, cJSON_CreateString(hex));
			continue;
		}
		const char *dot = strchr(p, '.');
		if(!dot) {
			continue;
		}
		char group[32];
		size_t gl = (size_t)(dot - p);
		if(gl >= sizeof(group)) {
			continue;
		}
		memcpy(group, p, gl);
		group[gl] = '\0';
		cJSON *g = ensureThemeGroup(colors, group);
		if(g) {
			cJSON_AddStringToObject(g, dot + 1, hex);
		}
	}
	cJSON_AddItemToObject(doc, "colors", colors);

	char *text = cJSON_Print(doc);
	cJSON_Delete(doc);
	if(text) {
		FILE *f = fopen(path, "wb");
		if(f) { fputs(text, f); fclose(f); }
		free(text);
	}
}

void loadSettingsJson(const char *path, Settings *s, char *themeOut, size_t themeOutSz) {
	if(!path || !s || !themeOut || themeOutSz == 0) return;
	FILE *f = fopen(path, "rb");
	if(!f) return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > 1 << 20) { fclose(f); return; }
	char *buf = malloc((size_t)sz + 1);
	if(!buf) { fclose(f); return; }
	if(fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return; }
	buf[sz] = '\0';
	fclose(f);

	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) return;

	cJSON *v;
	if((v = cJSON_GetObjectItemCaseSensitive(doc, "defaultBPM")) && cJSON_IsNumber(v)) s->defaultBPM = v->valueint;
	if((v = cJSON_GetObjectItemCaseSensitive(doc, "enabledChannels")) && cJSON_IsNumber(v)) s->enabledChannels = v->valueint;
	if((v = cJSON_GetObjectItemCaseSensitive(doc, "defaultSequenceLength")) && cJSON_IsNumber(v)) s->defaultSequenceLength = v->valueint;
	if((v = cJSON_GetObjectItemCaseSensitive(doc, "defaultVoiceCount")) && cJSON_IsNumber(v)) s->defaultVoiceCount = v->valueint;
	if((v = cJSON_GetObjectItemCaseSensitive(doc, "voiceTypes")) && cJSON_IsArray(v)) {
		int n = cJSON_GetArraySize(v);
		if(n > MAX_SEQUENCER_CHANNELS) n = MAX_SEQUENCER_CHANNELS;
		for(int i = 0; i < n; i++) {
			cJSON *el = cJSON_GetArrayItem(v, i);
			if(cJSON_IsNumber(el)) s->voiceTypes[i] = el->valueint;
		}
	}
	if((v = cJSON_GetObjectItemCaseSensitive(doc, "theme")) && cJSON_IsString(v)) {
		strncpy(themeOut, v->valuestring, themeOutSz - 1);
		themeOut[themeOutSz - 1] = '\0';
	}
	cJSON_Delete(doc);
}

void saveSettingsJson(const char *path, const Settings *s, const char *themeFile) {
	if(!path || !s) return;
	cJSON *doc = cJSON_CreateObject();
	if(!doc) return;
	cJSON_AddNumberToObject(doc, "defaultBPM", s->defaultBPM);
	cJSON_AddNumberToObject(doc, "enabledChannels", s->enabledChannels);
	cJSON_AddNumberToObject(doc, "defaultSequenceLength", s->defaultSequenceLength);
	cJSON_AddNumberToObject(doc, "defaultVoiceCount", s->defaultVoiceCount);
	cJSON *vt = cJSON_CreateArray();
	if(vt) {
		for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
			cJSON_AddItemToArray(vt, cJSON_CreateNumber(s->voiceTypes[i]));
		}
		cJSON_AddItemToObject(doc, "voiceTypes", vt);
	}
	cJSON_AddStringToObject(doc, "theme", themeFile ? themeFile : "");

	char *text = cJSON_Print(doc);
	cJSON_Delete(doc);
	if(text) {
		FILE *f = fopen(path, "wb");
		if(f) { fputs(text, f); fclose(f); }
		free(text);
	}
}
