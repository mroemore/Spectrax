#ifndef CONFIG_IO_H
#define CONFIG_IO_H

#include <stdbool.h>
#include "raylib.h"
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

#endif
