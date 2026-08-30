#include "config_io.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

static Color *themeFieldByName(ColourScheme *cs, const char *name) {
	if(!cs || !name) return NULL;
	if(!strcmp(name, "background")) return &cs->backgroundColor;
	if(!strcmp(name, "secondaryFont")) return &cs->secondaryFontColour;
	if(!strcmp(name, "font")) return &cs->fontColour;
	if(!strcmp(name, "outline")) return &cs->outlineColour;
	if(!strcmp(name, "defaultCell")) return &cs->defaultCell;
	if(!strcmp(name, "blankCell")) return &cs->blankCell;
	if(!strcmp(name, "highlightedCell")) return &cs->highlightedCell;
	if(!strcmp(name, "selectedCell")) return &cs->selectedCell;
	if(!strcmp(name, "reddish")) return &cs->reddish;
	if(!strcmp(name, "panel")) return &cs->panel;
	if(!strcmp(name, "panelBorder")) return &cs->panelBorder;
	if(!strcmp(name, "valueDisplayBg")) return &cs->valueDisplayBg;
	if(!strcmp(name, "label")) return &cs->label;
	if(!strcmp(name, "labelSelected")) return &cs->labelSelected;
	if(!strcmp(name, "dial")) return &cs->dial;
	if(!strcmp(name, "valueText")) return &cs->valueText;
	if(!strcmp(name, "vline")) return &cs->vline;
	if(!strcmp(name, "poly")) return &cs->poly;
	if(!strcmp(name, "waveformBg")) return &cs->waveformBg;
	if(!strcmp(name, "waveform")) return &cs->waveform;
	if(!strcmp(name, "waveformAlt")) return &cs->waveformAlt;
	if(!strcmp(name, "sampleBg")) return &cs->sampleBg;
	if(!strcmp(name, "sampleAltBg")) return &cs->sampleAltBg;
	if(!strcmp(name, "sampleBorder")) return &cs->sampleBorder;
	if(!strcmp(name, "stepBorder")) return &cs->stepBorder;
	if(!strcmp(name, "stepClosed")) return &cs->stepClosed;
	if(!strcmp(name, "arrangerPlayhead")) return &cs->arrangerPlayhead;
	if(!strcmp(name, "arrangerCellText")) return &cs->arrangerCellText;
	return NULL;
}

static void colorToHex(const Color *c, char *out) {
	/* Format `#RRGGBBAA` = 9 chars + NUL = 10 bytes. */
	snprintf(out, 10, "#%02X%02X%02X%02X", c->r, c->g, c->b, c->a);
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
		cJSON *item = NULL;
		cJSON_ArrayForEach(item, colors) {
			Color *field = themeFieldByName(cs, item->string);
			Color v;
			if(field && cJSON_IsString(item) && parseHexColor(item->valuestring, &v)) {
				*field = v;
			}
		}
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

void saveThemeJson(const char *path, const ColourScheme *cs, const FontConfig *font) {
	if(!path || !cs || !font) return;
	cJSON *doc = cJSON_CreateObject();
	if(!doc) return;
	cJSON *fontObj = cJSON_CreateObject();
	cJSON_AddStringToObject(fontObj, "path", font->path);
	cJSON_AddNumberToObject(fontObj, "size", font->size);
	cJSON_AddNumberToObject(fontObj, "spacing", font->spacing);
	cJSON_AddItemToObject(doc, "font", fontObj);

	cJSON *colors = cJSON_CreateObject();
	/* Every field the loader understands (must stay in sync with themeFieldByName). */
	const char *names[] = { "background", "secondaryFont", "font", "outline",
		"defaultCell", "blankCell", "highlightedCell", "selectedCell", "reddish",
		"panel", "panelBorder", "valueDisplayBg", "label", "labelSelected", "dial",
		"valueText", "vline", "poly", "waveformBg", "waveform", "waveformAlt",
		"sampleBg", "sampleAltBg", "sampleBorder", "stepBorder", "stepClosed",
		"arrangerPlayhead", "arrangerCellText" };
	size_t n = sizeof(names) / sizeof(names[0]);
	for(size_t i = 0; i < n; i++) {
		/* Cast away const for the lookup helper (it only dereferences for read,
		 * but we copy out the value rather than writing through it). */
		Color *field = themeFieldByName((ColourScheme *)cs, names[i]);
		if(!field) continue;
		char hex[10];
		colorToHex(field, hex);
		cJSON_AddStringToObject(colors, names[i], hex);
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
