#ifndef CONFIG_IO_H
#define CONFIG_IO_H

#include <stdbool.h>
#include <stddef.h>
#include "raylib.h"
#include "settings.h"
#include "theme.h"

bool parseHexColor(const char *s, Color *out);

/* Load a theme JSON file from `path`. For each present colour key, the
 * matching ColourScheme field is overridden. For each present `font.*`
 * key (path/size/spacing), the FontConfig is overridden. Unknown keys
 * are ignored. On any parse/read failure `*cs` and `*font` are left
 * untouched (callers pre-fill them with defaults). */
void loadThemeJson(const char *path, ColourScheme *cs, FontConfig *font);

/* Write the full theme JSON (all colour keys as `#RRGGBBAA`, all font
 * keys) via cJSON. */
void saveThemeJson(const char *path, const ColourScheme *cs, const FontConfig *font);

/* Load user settings JSON from `path`. For each present top-level key,
 * the matching `Settings` field is overridden; `voiceTypes` is an
 * array of up to MAX_SEQUENCER_CHANNELS entries applied in order. The
 * `theme` key (a string filename) is copied into `themeOut` (NUL-
 * terminated, truncated to fit). When the `theme` key is absent
 * `themeOut` is left untouched so callers can pre-fill a default
 * (e.g. "clr.json"). On any parse/read failure `*s` and `themeOut`
 * are left untouched. */
void loadSettingsJson(const char *path, Settings *s, char *themeOut, size_t themeOutSz);

/* Write the full settings JSON (all scalar fields, full voiceTypes
 * array, and `theme` string) via cJSON. */
void saveSettingsJson(const char *path, const Settings *s, const char *themeFile);

#endif
