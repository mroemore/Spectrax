# Config System (cfg.json + clr.json + --data-dir) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every colour, the font, and all settings config-driven via two JSON files (cfg.json settings, clr.json theme) with hex colour support, and remove the run-from-bin/ constraint via a `--data-dir` flag.

**Architecture:** cJSON (vendored into `third_party/cjson/`) parses `cfg.json` (settings) + `clr.json` (theme). A `src/theme.h` core header holds the expanded `ColourScheme` + `FontConfig`. New `src/io/config_io.c` (core) owns load/save of both files with per-key fallback to current defaults. `src/paths.c` (core) resolves the base dir (`--data-dir` → `~/.config/spectrax/` → cwd) and chdirs to it at startup. GUI draw sites replace `(Color){...}` literals with `cs.<key>` lookups.

**Tech Stack:** C (gnu99), cJSON v1.7.19, meson/ninja, raylib (for the app), existing tests/dsp harness.

## Global Constraints

- Build: `meson setup build && ninja -C build && meson test -C build` from the repo root.
- Tests live in `tests/dsp/*.c`, one executable per file, registered in `tests/meson.build`; run via `meson test -C build`. Temp files go under `.tmp_files/` (repo convention; see `TMP_DIR` in test_io.c:93).
- `settings.h` Settings struct (settings.h:33-39): `enabledChannels`, `defaultSequenceLength`, `voiceTypes[MAX_SEQUENCER_CHANNELS]`, `defaultVoiceCount`, `defaultBPM`.
- `ColourScheme` currently lives in `gui.h`; it is MOVED to the new core `src/theme.h` (gui.h includes theme.h) so core code + tests can use it.
- cJSON is vendored from `~/proj/anima.tty` (v1.7.19, MIT) into `third_party/cjson/` — do NOT fetch from the network.
- `cs` (global ColourScheme) is declared `static ColourScheme cs;` in gui.c and accessed via `getColourScheme()`.
- `pixelFont` is loaded in `InitGUI()` (gui.c:130): `LoadFontEx("resources/fonts/console.ttf", 9, 0, 255)`.
- App init order today: `main()` calls `InitGUI()` (gui.c:190) THEN `initApplication()` (gui.c:200). After this plan: `main()` resolves data-dir → loads cfg.json → loads clr.json → then `InitGUI()` (which must NOT reset cs if a theme was loaded) → then `initApplication()`.
- The spec: `docs/superpowers/specs/2026-08-29-config-system-design.md`.

---
## File Structure

- **Create** `src/theme.h` — `ColourScheme` (expanded, ~40 named `Color` fields) + `FontConfig { char path[256]; int size; int spacing; }` + default/fallback helpers.
- **Create** `src/paths.h` / `src/paths.c` — `resolveDataDir(int argc, char **argv, char *out, size_t outsz)` + `bool chdirToDataDir(const char *base)`.
- **Create** `src/io/config_io.h` / `src/io/config_io.c` — `parseHexColor`, `loadSettingsJson`, `saveSettingsJson`, `loadThemeJson`, `saveThemeJson`, `loadFontConfigJson` internals.
- **Create** `tests/dsp/test_cfg.c` — hex colour, paths, settings, theme, round-trip tests.
- **Modify** `src/gui.h` — remove ColourScheme typedef (now in theme.h), `#include "theme.h"`.
- **Modify** `src/gui.c` — all colour literals → `cs.<key>`; InitGUI font from theme; `initDefaultColourScheme` keeps filling defaults (fallback source).
- **Modify** `src/settings.c` — refactor `createSettings` to `void createSettings(Settings *s)` (single caller); defaults stay.
- **Modify** `src/main.c` — data-dir resolution + cfg/theme load before InitGUI; exit saves cfg + theme instead of `CLR.dat`.
- **Modify** `src/vizfx.c`, `src/dataviz.c`, `src/graph_gui.c`, `src/gui_layer.c`, `src/spectrogram.c` — colour literals → `cs.<key>`.
- **Modify** `src/meson.build` (cjson lib + dep), `tests/meson.build` (test_cfg), root `meson.build` (include third_party/cjson).
- **Create** `third_party/cjson/cJSON.h`, `third_party/cjson/cJSON.c`.
- **Create** `bin/cfg.json`, `bin/clr.json` (shipped defaults).

---

### Task 1: Vendor cJSON + meson wiring + smoke test

**Files:**
- Create: `third_party/cjson/cJSON.h`, `third_party/cjson/cJSON.c` (copy from `~/proj/anima.tty/include/cJSON.h` and `~/proj/anima.tty/src/cJSON.c`)
- Create: `tests/dsp/test_cfg.c` (skeleton: smoke test only in this task)
- Modify: `src/meson.build`, `tests/meson.build`

**Interfaces:**
- Produces: meson `cjson_dep` dependency available to all targets via `system_deps`; test binary `test_cfg` registered with meson.

- [ ] **Step 1: Vendor cJSON**

```bash
mkdir -p third_party/cjson
cp ~/proj/anima.tty/include/cJSON.h third_party/cjson/cJSON.h
cp ~/proj/anima.tty/src/cJSON.c third_party/cjson/cJSON.c
```

Verify version: `grep -m1 CJSON_VERSION third_party/cjson/cJSON.c` → expects the `1.7.19` guard comment.

- [ ] **Step 2: Wire meson**

In root `meson.build`, after the `inc` line:

```meson
cjson_inc = include_directories('third_party/cjson')
cjson_lib = static_library('spectrax_cjson', 'third_party/cjson/cJSON.c',
  include_directories: [inc, cjson_inc],
)
cjson_dep = declare_dependency(link_with: cjson_lib, include_directories: cjson_inc)
```

Then append to `system_deps`:

```meson
system_deps = [raylib, kissfft, portaudio, gl, x11, rt, dl, m, cjson_dep]
```

- [ ] **Step 3: Write the failing smoke test**

In `tests/dsp/test_cfg.c`:

```c
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static int test_cjson_parse_smoke(void) {
	cJSON *doc = cJSON_Parse("{\"a\": 1, \"b\": [true, \"x\"]}");
	if(!doc) {
		printf("FAIL cjson parse: %s\n", cJSON_GetErrorPtr());
		return 1;
	}
	int failed = 0;
	cJSON *a = cJSON_GetObjectItemCaseSensitive(doc, "a");
	if(!a || a->valueint != 1) {
		printf("FAIL cjson int\n");
		failed = 1;
	}
	cJSON *b = cJSON_GetObjectItemCaseSensitive(doc, "b");
	if(!b || !cJSON_IsArray(b) || cJSON_GetArraySize(b) != 2) {
		printf("FAIL cjson array\n");
		failed = 1;
	}
	cJSON_Delete(doc);
	return failed;
}

int main(void) {
	int failed = 0;
	failed |= test_cjson_parse_smoke();
	if(failed) {
		printf("test_cfg: FAILURES\n");
		return 1;
	}
	printf("test_cfg: all tests passed\n");
	return 0;
}
```

- [ ] **Step 4: Register the test**

In `tests/meson.build`, add `'test_cfg',` to the `test_names` list.

- [ ] **Step 5: Build + run — verify pass**

```bash
meson setup build --wipe && ninja -C build && meson test -C build
```
Expected: `test_cfg` listed in the `Ok:` summary.

- [ ] **Step 6: Commit**

```bash
git add third_party/cjson tests/dsp/test_cfg.c src/meson.build tests/meson.build meson.build
git commit -m "build: vendor cJSON 1.7.19 + register test_cfg smoke test"
```

---

### Task 2: parseHexColor

**Files:**
- Create: `src/io/config_io.h`
- Create: `src/io/config_io.c` (parseHexColor + helpers only in this task)
- Test: `tests/dsp/test_cfg.c`
- Modify: `src/meson.build` (add `io/config_io.c` to core_sources)

**Interfaces:**
- Produces: `bool parseHexColor(const char *s, Color *out)` — accepts `#RRGGBB`, `#RRGGBBAA`, bare `RRGGBB`/`RRGGBBAA`; case-insensitive; returns false on malformed input and leaves `*out` untouched.
- Note: `Color` (raylib, `typedef struct {unsigned char r,g,b,a;} Color`) is available in tests via the raylib include on `system_deps`. config_io.c must `#include "raylib.h"` for the Color type.

- [ ] **Step 1: Write the failing tests**

Append to `tests/dsp/test_cfg.c` (above `main`) and call from `main`:

```c
static int test_hex_rgb(void) {
	Color c;
	if(!parseHexColor("#ff0000", &c) || c.r != 255 || c.g != 0 || c.b != 0 || c.a != 255) {
		printf("FAIL hex #rrggbb\n");
		return 1;
	}
	if(!parseHexColor("#00ff00ff", &c) || c.g != 255 || c.a != 255) {
		printf("FAIL hex #rrggbbaa\n");
		return 1;
	}
	if(!parseHexColor("112233", &c) || c.r != 17 || c.g != 34 || c.b != 51) {
		printf("FAIL hex bare rrggbb\n");
		return 1;
	}
	if(!parseHexColor("#ABCDEF", &c) || c.r != 0xAB || c.g != 0xCD || c.b != 0xEF) {
		printf("FAIL hex uppercase\n");
		return 1;
	}
	return 0;
}

static int test_hex_invalid(void) {
	Color c = { 1, 2, 3, 4 };
	Color before = c;
	if(parseHexColor("#12345", &c) || parseHexColor("#gggggg", &c) ||
	   parseHexColor("123", &c) || parseHexColor("#123456789", &c) ||
	   parseHexColor("", &c) || parseHexColor(NULL, &c)) {
		printf("FAIL hex should reject malformed\n");
		return 1;
	}
	if(c.r != before.r || c.g != before.g || c.b != before.b || c.a != before.a) {
		printf("FAIL hex must not mutate on error\n");
		return 1;
	}
	return 0;
}
```

- [ ] **Step 2: Run — verify it fails (missing symbol)**

Run: `ninja -C build && meson test -C build`
Expected: build fails (parseHexColor undefined).

- [ ] **Step 3: Implement**

Create `src/io/config_io.h`:

```c
#ifndef CONFIG_IO_H
#define CONFIG_IO_H

#include <stdbool.h>
#include "raylib.h"

bool parseHexColor(const char *s, Color *out);

#endif
```

Create `src/io/config_io.c`:

```c
#include "config_io.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
```

- [ ] **Step 4: Wire into meson + test**

In `src/meson.build`, add `'io/config_io.c',` to `core_sources`. Add `#include "config_io.h"` to `tests/dsp/test_cfg.c` and register the two test functions in `main` (`failed |= test_hex_rgb(); failed |= test_hex_invalid();`).

- [ ] **Step 5: Build + run — verify pass**

```bash
ninja -C build && meson test -C build
```
Expected: `test_cfg` passes.

- [ ] **Step 6: Commit**

```bash
git add src/io/config_io.c src/io/config_io.h src/meson.build tests/dsp/test_cfg.c
git commit -m "feat(cfg): parseHexColor (#RRGGBB / #RRGGBBAA)"
```

---

### Task 3: Data-dir resolution

**Files:**
- Create: `src/paths.h`, `src/paths.c`
- Test: `tests/dsp/test_cfg.c`
- Modify: `src/meson.build` (add `paths.c` to core_sources)

**Interfaces:**
- Produces:
  - `void resolveDataDir(int argc, char **argv, char *out, size_t outsz)` — fills `out` with the base dir:
    1. `--data-dir <dir>` argument if present (argv contains the literal flag `--data-dir` followed by a value; if the value is missing, treat as absent).
    2. Else `$XDG_CONFIG_HOME/spectrax/` or `$HOME/.config/spectrax/` if a `cfg.json` exists in it.
    3. Else the current working directory (`"."`).
  - `bool chdirToDataDir(const char *base)` — returns true on success (including when base is `"."`).
- Consumes: `getenv`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/dsp/test_cfg.c`:

```c
#include <unistd.h>
#include "paths.h"

static int test_resolve_data_dir_flag(void) {
	char *argv[] = { "spectrax", "--data-dir", "/tmp/somewhere", NULL };
	char buf[512];
	resolveDataDir(3, argv, buf, sizeof(buf));
	if(strcmp(buf, "/tmp/somewhere") != 0) {
		printf("FAIL resolve --data-dir: got '%s'\n", buf);
		return 1;
	}
	/* Missing value: falls through to fallback logic */
	char *argv2[] = { "spectrax", "--data-dir", NULL };
	resolveDataDir(2, argv2, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL resolve missing value: got '%s'\n", buf);
		return 1;
	}
	return 0;
}

static int test_resolve_data_dir_home(void) {
	char old_home[512] = "";
	char old_xdg[512] = "";
	const char *h = getenv("HOME");
	const char *x = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(x) { strncpy(old_xdg, x, sizeof(old_xdg) - 1); }

	/* $HOME/.config/spectrax with no cfg.json -> cwd */
	setenv("HOME", ".tmp_files/nonexistent_home", 1);
	unsetenv("XDG_CONFIG_HOME");
	char buf[512];
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL home no cfg: got '%s'\n", buf);
		return 1;
	}

	/* $HOME/.config/spectrax WITH cfg.json -> that dir */
	ensure_tmp_dirs();
	FILE *f = fopen(".tmp_files/hometest/cfg.json", "wb");
	if(!f) { mkdir(".tmp_files/hometest", 0755); f = fopen(".tmp_files/hometest/cfg.json", "wb"); }
	if(f) { fputs("{}", f); fclose(f); }
	setenv("HOME", ".tmp_files", 1);
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".tmp_files/hometest") != 0) {
		printf("FAIL home with cfg: got '%s'\n", buf);
		return 1;
	}
	remove(".tmp_files/hometest/cfg.json");
	rmdir(".tmp_files/hometest");

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(x) setenv("XDG_CONFIG_HOME", old_xdg, 1); else unsetenv("XDG_CONFIG_HOME");
	return 0;
}
```

(`ensure_tmp_dirs` is already defined in test_io.c; replicate the same small helper in test_cfg.c: `mkdir(".tmp_files", 0755)` if missing — the pattern from test_io.c:96-102.)

- [ ] **Step 2: Run — verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: build fails (resolveDataDir undefined).

- [ ] **Step 3: Implement**

Create `src/paths.h`:

```c
#ifndef PATHS_H
#define PATHS_H

#include <stdbool.h>
#include <stddef.h>

void resolveDataDir(int argc, char **argv, char *out, size_t outsz);
bool chdirToDataDir(const char *base);

#endif
```

Create `src/paths.c`:

```c
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

void resolveDataDir(int argc, char **argv, char *out, size_t outsz) {
	/* 1. --data-dir <dir> */
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], "--data-dir") == 0) {
			snprintf(out, outsz, "%s", argv[i + 1]);
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
			snprintf(out, outsz, "%s", cand);
			return;
		}
	}
	if(home && *home) {
		snprintf(cand, sizeof(cand), "%s/.config/spectrax", home);
		if(dirHasFile(cand, "cfg.json")) {
			snprintf(out, outsz, "%s", cand);
			return;
		}
	}
	/* 3. cwd */
	snprintf(out, outsz, "%s", ".");
}

bool chdirToDataDir(const char *base) {
	if(!base || strcmp(base, ".") == 0) {
		return true;
	}
	return chdir(base) == 0;
}
```

- [ ] **Step 4: Wire meson + test registration**

In `src/meson.build`, add `'paths.c',` to `core_sources`. Add `#include "paths.h"` to test_cfg.c + register the two tests in `main`.

- [ ] **Step 5: Build + run — verify pass**

```bash
ninja -C build && meson test -C build
```
Expected: `test_cfg` passes.

- [ ] **Step 6: Commit**

```bash
git add src/paths.c src/paths.h src/meson.build tests/dsp/test_cfg.c
git commit -m "feat(cfg): resolveDataDir (--data-dir / ~/.config/spectrax / cwd)"
```

---

### Task 4: theme.h — expanded ColourScheme + FontConfig

**Files:**
- Create: `src/theme.h`
- Modify: `src/gui.h` (remove ColourScheme typedef, include theme.h)
- Modify: `src/gui.c` (`initDefaultColourScheme` fills ALL new fields)

**Interfaces:**
- Produces: `typedef struct { Color backgroundColor, fontColour, secondaryFontColour, outlineColour, defaultCell, blankCell, highlightedCell, selectedCell, reddish, panel, panelBorder, valueDisplayBg, label, labelSelected, dial, valueText, vline, poly, waveformBg, waveform, waveformAlt, sampleBg, sampleAltBg, sampleBorder, stepBorder, stepClosed, arrangerPlayhead, arrangerCellText; } ColourScheme;` and `typedef struct { char path[256]; int size; int spacing; } FontConfig;` — both in `src/theme.h`, which `#include "raylib.h"`.
- `initDefaultColourScheme(ColourScheme *cs)` keeps its current signature but now also fills the new fields with the literal values that currently appear at the draw sites (each new field's default = the literal it replaces, e.g. `label = (Color){ 200, 180, 180, 255 }`).

- [ ] **Step 1: Create theme.h with the expanded struct**

```c
#ifndef THEME_H
#define THEME_H

#include "raylib.h"

typedef struct {
	Color backgroundColor;
	Color secondaryFontColour;
	Color fontColour;
	Color outlineColour;
	Color defaultCell;
	Color blankCell;
	Color highlightedCell;
	Color selectedCell;
	Color reddish;
	Color panel;
	Color panelBorder;
	Color valueDisplayBg;
	Color label;
	Color labelSelected;
	Color dial;
	Color valueText;
	Color vline;
	Color poly;
	Color waveformBg;
	Color waveform;
	Color waveformAlt;
	Color sampleBg;
	Color sampleAltBg;
	Color sampleBorder;
	Color stepBorder;
	Color stepClosed;
	Color arrangerPlayhead;
	Color arrangerCellText;
} ColourScheme;

typedef struct {
	char path[256];
	int size;
	int spacing;
} FontConfig;

#endif
```

- [ ] **Step 2: Update gui.h + gui.c includes**

In `gui.h`: delete the `ColourScheme` typedef block (the 9-field struct) and add `#include "theme.h"` at the top. In `gui.c` the existing `#include "gui.h"` then pulls theme.h.

- [ ] **Step 3: Extend initDefaultColourScheme**

In `src/gui.c`, extend `initDefaultColourScheme` (gui.c:81-90) to also set every new field to the literal it will replace (copy the exact `(Color){...}` values from the draw sites in Task 7/8 — the values are listed in the spec's seed key list; the implementer greps `(Color){` in gui.c/vizfx.c/dataviz.c to get the exact bytes). Example additions:

```c
	cs->panel = (Color){ 80, 60, 60, 255 };
	cs->panelBorder = (Color){ 10, 0, 0, 255 };
	cs->valueDisplayBg = (Color){ 50, 40, 40, 255 };
	cs->label = (Color){ 200, 180, 180, 255 };
	cs->labelSelected = (Color){ 255, 180, 180, 255 };
	cs->dial = (Color){ 255, 0, 0, 255 };
	cs->valueText = (Color){ 255, 0, 0, 255 };
	cs->vline = (Color){ 60, 255, 150, 255 };
	cs->poly = (Color){ 255, 80, 80, 255 };
	cs->waveformBg = (Color){ 0, 0, 0, 255 };
	cs->waveform = (Color){ 255, 0, 0, 255 };
	cs->waveformAlt = (Color){ 0, 255, 0, 255 };
	cs->sampleBg = (Color){ 60, 10, 10, 255 };
	cs->sampleAltBg = (Color){ 10, 50, 10, 255 };
	cs->sampleBorder = (Color){ 200, 80, 60, 255 };
	cs->stepBorder = (Color){ 80, 20, 20, 255 };
	cs->stepClosed = (Color){ 80, 30, 30, 255 };
	cs->arrangerPlayhead = (Color){ 255, 0, 0, 255 };
	cs->arrangerCellText = (Color){ 200, 180, 180, 255 };
```

If Task 7/8 turns up literals not covered by these names, add a field + default for each (the struct must have exactly one field per distinct literal).

- [ ] **Step 4: Build**

```bash
ninja -C build
```
Expected: 0 errors (gui.c still uses the same 9 fields; new fields unused but valid).

- [ ] **Step 5: Commit**

```bash
git add src/theme.h src/gui.h src/gui.c
git commit -m "feat(theme): expanded ColourScheme + FontConfig in core theme.h"
```

---

### Task 5: loadThemeJson / saveThemeJson

**Files:**
- Modify: `src/io/config_io.h`, `src/io/config_io.c`
- Test: `tests/dsp/test_cfg.c`

**Interfaces:**
- Consumes: `parseHexColor`, `ColourScheme`, `FontConfig`, `initDefaultColourScheme`.
- Produces:
  - `void loadThemeJson(const char *path, ColourScheme *cs, FontConfig *font)` — parses `path`; on any failure keeps `*cs`/`*font` untouched (caller pre-fills with defaults). For each present colour key, overrides the matching field. For each present `font.*` key, overrides `*font` (path/size/spacing). Unknown keys ignored.
  - `void saveThemeJson(const char *path, const ColourScheme *cs, const FontConfig *font)` — writes the full theme JSON (all colour keys as `#RRGGBBAA`, all font keys) via cJSON.
  - Internal `static Color *themeFieldByName(ColourScheme *cs, const char *name)` mapping string keys → struct fields via `strcmp` chain (used by both load and the tests' expectations).
  - `static void colorToHex(const Color *c, char out[9])` — `#RRGGBBAA`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/dsp/test_cfg.c`:

```c
#include "config_io.h"
#include "theme.h"

static int test_theme_parse_full(void) {
	ColourScheme cs = { 0 };
	FontConfig font = { { 0 }, 0, 0 };
	FontConfig fallback = { { 0 }, 9, 1 };
	loadThemeJson(".tmp_files/theme_full.json", &cs, &font);
	if(cs.label.r != 200 || cs.label.g != 180 || cs.dial.r != 255 ||
	   cs.backgroundColor.r != 207 || cs.arrangerCellText.b != 180) {
		printf("FAIL theme colours\n");
		return 1;
	}
	if(strcmp(font.path, "resources/fonts/console.ttf") != 0 || font.size != 9 || font.spacing != 1) {
		printf("FAIL theme font\n");
		return 1;
	}
	return 0;
}

static int test_theme_parse_partial(void) {
	/* Pre-fill with defaults; only the dial key present -> everything else stays default. */
	ColourScheme cs = { 0 };
	cs.label = (Color){ 200, 180, 180, 255 };
	cs.dial = (Color){ 255, 0, 0, 255 };
	FontConfig font = { { 0 }, 9, 1 };
	FontConfig fallback = { { 0 }, 9, 1 };
	loadThemeJson(".tmp_files/theme_partial.json", &cs, &font);
	if(cs.dial.r != 0 || cs.dial.g != 255 || cs.label.r != 200) {
		printf("FAIL theme partial override\n");
		return 1;
	}
	if(font.size != 9) {
		printf("FAIL theme font unchanged when absent\n");
		return 1;
	}
	return 0;
}

static int test_theme_missing_file(void) {
	ColourScheme cs = { 0 };
	cs.label = (Color){ 1, 2, 3, 4 };
	Color before = cs.label;
	FontConfig font = { { 0 }, 9, 1 };
	FontConfig fallback = { { 0 }, 9, 1 };
	loadThemeJson(".tmp_files/no_such_theme.json", &cs, &font);
	if(cs.label.r != before.r || cs.label.g != before.g) {
		printf("FAIL theme missing file must not mutate\n");
		return 1;
	}
	return 0;
}

static int test_theme_roundtrip(void) {
	ColourScheme cs = { 0 };
	cs.backgroundColor = (Color){ 207, 110, 58, 255 };
	cs.label = (Color){ 200, 180, 180, 255 };
	cs.dial = (Color){ 12, 34, 56, 78 };
	FontConfig font = { "myfont.ttf", 12, 2 };
	saveThemeJson(".tmp_files/theme_rt.json", &cs, &font);
	ColourScheme cs2 = { 0 };
	FontConfig font2 = { { 0 }, 0, 0 };
	FontConfig fallback = { { 0 }, 9, 1 };
	loadThemeJson(".tmp_files/theme_rt.json", &cs2, &font2);
	if(cs2.label.r != 200 || cs2.label.g != 180 || cs2.dial.a != 78 ||
	   cs2.backgroundColor.r != 207 || strcmp(font2.path, "myfont.ttf") != 0 ||
	   font2.size != 12 || font2.spacing != 2) {
		printf("FAIL theme roundtrip\n");
		return 1;
	}
	return 0;
}
```

Fixtures (write these in the test before calling, or ship as files — write them in the test):

```c
/* .tmp_files/theme_full.json */
{
  "font": { "path": "resources/fonts/console.ttf", "size": 9, "spacing": 1 },
  "colors": {
    "background": "#cf6e3a", "label": "#c8b4b4", "dial": "#ff0000",
    "arrangerCellText": "#c8b4b4"
  }
}
/* .tmp_files/theme_partial.json */
{ "colors": { "dial": "#00ff00" } }
```

- [ ] **Step 2: Run — verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: build fails (loadThemeJson undefined).

- [ ] **Step 3: Implement in config_io.c**

```c
#include "config_io.h"
#include "theme.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

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

static void colorToHex(const Color *c, char out[9]) {
	snprintf(out, 9, "#%02X%02X%02X%02X", c->r, c->g, c->b, c->a);
}

void loadThemeJson(const char *path, ColourScheme *cs, FontConfig *font) {
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
	cJSON *doc = cJSON_CreateObject();
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
	ColourScheme tmp = *cs;
	for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		Color *field = themeFieldByName(&tmp, names[i]);
		if(!field) continue;
		char hex[9];
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
```

- [ ] **Step 4: Build + run — verify pass**

```bash
ninja -C build && meson test -C build
```
Expected: `test_cfg` passes.

- [ ] **Step 5: Commit**

```bash
git add src/io/config_io.c src/io/config_io.h tests/dsp/test_cfg.c
git commit -m "feat(cfg): loadThemeJson/saveThemeJson with per-key fallback"
```

---

### Task 6: loadSettingsJson / saveSettingsJson

**Files:**
- Modify: `src/io/config_io.h`, `src/io/config_io.c`
- Test: `tests/dsp/test_cfg.c`
- Modify: `src/settings.h` (add `char themeFile[256];` to the Settings struct)

**Interfaces:**
- Consumes: `Settings` (settings.h:33-39), `MAX_SEQUENCER_CHANNELS`.
- Produces:
  - `void loadSettingsJson(const char *path, Settings *s, char *themeOut, size_t themeOutSz)` — parses `path`; on any failure leaves `*s` untouched. For each present key overrides the matching field; `voiceTypes` array (up to MAX_SEQUENCER_CHANNELS entries) overrides in order. `theme` key copied into `themeOut` (default `"clr.json"` when absent).
  - `void saveSettingsJson(const char *path, const Settings *s, const char *themeFile)`.

- [ ] **Step 1: Add themeFile to Settings**

In `src/settings.h`, add `char themeFile[256];` as the last field of the Settings struct (before the closing brace).

- [ ] **Step 2: Write the failing tests**

Append to `tests/dsp/test_cfg.c`:

```c
#include "settings.h"

static int test_settings_parse_full(void) {
	Settings s = { 0 };
	char theme[256];
	loadSettingsJson(".tmp_files/cfg_full.json", &s, theme, sizeof(theme));
	if(s.defaultBPM != 120 || s.enabledChannels != 8 || s.defaultSequenceLength != 16 ||
	   s.defaultVoiceCount != 1 || s.voiceTypes[0] != 4 || s.voiceTypes[1] != 1 ||
	   s.voiceTypes[7] != 2 || strcmp(theme, "clr.json") != 0) {
		printf("FAIL settings full\n");
		return 1;
	}
	return 0;
}

static int test_settings_partial(void) {
	Settings s = { 0 };
	s.defaultBPM = 77;
	s.enabledChannels = 3;
	char theme[256];
	loadSettingsJson(".tmp_files/cfg_partial.json", &s, theme, sizeof(theme));
	if(s.defaultBPM != 77 || s.enabledChannels != 3) {
		printf("FAIL settings partial must keep defaults\n");
		return 1;
	}
	if(strcmp(theme, "clr.json") != 0) {
		printf("FAIL settings theme default\n");
		return 1;
	}
	return 0;
}

static int test_settings_missing(void) {
	Settings s = { 0 };
	s.defaultBPM = 99;
	char theme[256];
	loadSettingsJson(".tmp_files/no_such_cfg.json", &s, theme, sizeof(theme));
	if(s.defaultBPM != 99) {
		printf("FAIL settings missing file must not mutate\n");
		return 1;
	}
	return 0;
}

static int test_settings_roundtrip(void) {
	Settings s = { 0 };
	s.defaultBPM = 130;
	s.enabledChannels = 4;
	s.defaultSequenceLength = 32;
	s.defaultVoiceCount = 2;
	s.voiceTypes[0] = 4; s.voiceTypes[1] = 1; s.voiceTypes[2] = 2; s.voiceTypes[3] = 1;
	saveSettingsJson(".tmp_files/cfg_rt.json", &s, "clr.json");
	Settings s2 = { 0 };
	char theme[256];
	loadSettingsJson(".tmp_files/cfg_rt.json", &s2, theme, sizeof(theme));
	if(s2.defaultBPM != 130 || s2.enabledChannels != 4 || s2.defaultSequenceLength != 32 ||
	   s2.defaultVoiceCount != 2 || s2.voiceTypes[2] != 2 || strcmp(theme, "clr.json") != 0) {
		printf("FAIL settings roundtrip\n");
		return 1;
	}
	return 0;
}
```

Fixtures (written by the test):

```c
/* .tmp_files/cfg_full.json */
{ "defaultBPM": 120, "enabledChannels": 8, "defaultSequenceLength": 16,
  "defaultVoiceCount": 1, "voiceTypes": [4,1,2,1,2,1,2,2], "theme": "clr.json" }
/* .tmp_files/cfg_partial.json */
{ "theme": "clr.json" }
```

- [ ] **Step 3: Run — verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: build fails (loadSettingsJson undefined).

- [ ] **Step 4: Implement in config_io.c**

```c
#include "settings.h"

void loadSettingsJson(const char *path, Settings *s, char *themeOut, size_t themeOutSz) {
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
	cJSON *doc = cJSON_CreateObject();
	cJSON_AddNumberToObject(doc, "defaultBPM", s->defaultBPM);
	cJSON_AddNumberToObject(doc, "enabledChannels", s->enabledChannels);
	cJSON_AddNumberToObject(doc, "defaultSequenceLength", s->defaultSequenceLength);
	cJSON_AddNumberToObject(doc, "defaultVoiceCount", s->defaultVoiceCount);
	cJSON *vt = cJSON_CreateArray();
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		cJSON_AddItemToArray(vt, cJSON_CreateNumber(s->voiceTypes[i]));
	}
	cJSON_AddItemToObject(doc, "voiceTypes", vt);
	cJSON_AddStringToObject(doc, "theme", themeFile);

	char *text = cJSON_Print(doc);
	cJSON_Delete(doc);
	if(text) {
		FILE *f = fopen(path, "wb");
		if(f) { fputs(text, f); fclose(f); }
		free(text);
	}
}
```

- [ ] **Step 5: Build + run — verify pass**

```bash
ninja -C build && meson test -C build
```
Expected: `test_cfg` passes.

- [ ] **Step 6: Commit**

```bash
git add src/io/config_io.c src/io/config_io.h src/settings.h tests/dsp/test_cfg.c
git commit -m "feat(cfg): loadSettingsJson/saveSettingsJson"
```

---

### Task 7: Migrate gui.c colour literals to cs.<key>

**Files:**
- Modify: `src/gui.c` (all `(Color){...}` literals → `cs.<field>`)

**Constraints:**
- Every `(Color){...}` literal in gui.c becomes a `cs.<field>` reference. Field names come from Task 4's theme.h. If a literal has no matching field, add a field to theme.h + a default in `initDefaultColourScheme` (guaranteeing the "one field per distinct literal" invariant).
- The 9 existing `cs.backgroundColour`-style references (gui.c:82-90 + drawColourRectangle's `cs.highlightedCell`) stay as-is.
- Do NOT touch the numeric constants that are NOT colours (paddings, font sizes, `line_w`, etc.).

- [ ] **Step 1: Replace the literals**

For each `(Color){...}` in gui.c, replace with `cs.<name>`:

```c
/* line 271 */ (Color){ 60, 255, 150, 255 }   -> cs.vline
/* line 280 */ (Color){ 255, 80, 80, 255 }    -> cs.poly
/* line 448 */ (Color){ 0, 0, 0, 255 }        -> cs.waveformBg
/* line 449 */ (Color){ 255, 0, 0, 255 }      -> cs.waveform
/* line 450 */ (Color){ 0, 255, 0, 255 }      -> cs.waveformAlt
/* line 533 */ (Color){ 50, 40, 40, 255 }     -> cs.valueDisplayBg
/* line 538 */ (Color){ 80, 60, 60, 255 }     -> cs.panel
/* line 542 */ (Color){ 10, 0, 0, 255 }       -> cs.panelBorder
/* line 575 */ (Color){ 200, 180, 180, 255 }  -> cs.label
/* line 607 */ (Color){ 255, 180, 180, 255 }  -> cs.labelSelected
/* line 607 */ (Color){ 200, 180, 180, 255 }  -> cs.label
/* line 656 */ (Color){ 200, 180, 180, 255 }  -> cs.label
/* line 661 */ (Color){ 80, 60, 60, 255 }     -> cs.panel
/* line 662 */ (Color){ 10, 5, 5, 255 }       -> cs.panelBorder
/* line 1458 */ (Color){ 60, 10, 10, 255 }    -> cs.sampleBg
/* line 1460 */ (Color){ 10, 50, 10, 255 }    -> cs.sampleAltBg
/* line 1467 */ (Color){ 200, 80, 60, 255 }   -> cs.sampleBorder
/* line 1599 */ (Color){ 80, 20, 20, 255 }    -> cs.stepBorder
/* line 1603 */ (Color){ 80, 30, 30, 255 }    -> cs.stepClosed
/* line 2156 */ (Color){ 255, 0, 0, 255 }     -> cs.arrangerPlayhead
/* line 2160/2163 */ (Color){ 200, 180, 180, 255 } -> cs.arrangerCellText
```

Then re-grep: `grep -nE "\(Color\)\{[0-9]" src/gui.c` — if any literal remains, either map it to an existing field or add a new field + default.

- [ ] **Step 2: Build**

```bash
ninja -C build
```
Expected: 0 errors. The visual result is IDENTICAL (the defaults match the literals).

- [ ] **Step 3: Verify boot**

```bash
cd bin && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax 2>&1 | grep -iE "fpe|segfault"
```
Expected: no crash.

- [ ] **Step 4: Commit**

```bash
git add src/gui.c
git commit -m "refactor(gui): colour literals -> cs.<key> (theme-driven)"
```

---

### Task 8: Migrate colour literals in the remaining files

**Files:**
- Modify: `src/vizfx.c`, `src/dataviz.c`, `src/graph_gui.c`, `src/gui_layer.c`, `src/spectrogram.c`

**Constraints:**
- Same rule as Task 7: every `(Color){...}` literal becomes `cs.<field>` via `getColourScheme()` (these files may not have a `cs` static — use `Color *sch = getColourScheme();` or `getColourScheme()-><field>`).
- `vizfx.c` mod-strip colour literals become fields; add new fields to theme.h + defaults if not covered (e.g. `modStripEnv`, `modStripLfo`).
- `spectrogram.c:38` region colours (the RGB-triple literals in the FFT colour ramp) are DATA colours, not theme — leave those (they are per-pixel spectrum rendering, not widgets). Only the fixed widget colours migrate.
- `dataviz.c` fixed widget colours migrate; per-signal data colours stay.

- [ ] **Step 1: Replace the literals**

For each file: `grep -nE "\(?Color\)\{[0-9]" <file>` → replace widget/static colours with `getColourScheme()-><field>` (add fields to theme.h + defaults in `initDefaultColourScheme` as needed). Re-grep to confirm zero remaining widget colours (data colours may remain, documented per constraint above).

- [ ] **Step 2: Build**

```bash
ninja -C build
```
Expected: 0 errors.

- [ ] **Step 3: Verify boot + fixtures**

```bash
cd bin && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax 2>&1 | grep -iE "fpe|segfault"
bash ../src/tools/instrument_harness/run_scripted.sh 2>&1 | grep -E "PASS|FAIL"
meson test -C build
```
Expected: no crash, fixture PASS, tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/vizfx.c src/dataviz.c src/graph_gui.c src/gui_layer.c src/spectrogram.c src/theme.h src/gui.c
git commit -m "refactor(ui): migrate remaining colour literals to theme cs.<key>"
```

---

### Task 9: Wire init + exit (data-dir, cfg, theme, font)

**Files:**
- Modify: `src/main.c` (main() + initApplication + exit path)
- Modify: `src/gui.c` (`InitGUI` uses theme font; does not reset cs when a theme was loaded)

**Interfaces:**
- Consumes: `resolveDataDir`, `chdirToDataDir`, `loadSettingsJson`, `saveSettingsJson`, `loadThemeJson`, `saveThemeJson`, `Settings.themeFile`, `FontConfig`.
- Produces: a working end-to-end flow:
  - `main(int argc, char **argv)` (currently `main(void)`, main.c:183) — add args; resolve data dir + chdir; load cfg.json into a Settings (via `loadSettingsJson` on a default-filled struct) + theme name; load clr.json into `cs` + a FontConfig; THEN `InitGUI()`; then `initApplication(&data, &appState, NULL)`.
  - `InitGUI()` (gui.c:122) — `initDefaultColourScheme(&cs)` only when no theme was loaded (a `bool themeLoaded` flag set by `loadThemeJson`); load `pixelFont` from the theme's FontConfig (fall back to `resources/fonts/console.ttf`, 9, 1).
  - `initApplication` (main.c:482) — replace the hardcoded `createSettings()` + `loadColourSchemeTxt(...)` with: create settings from the already-loaded default-filled struct (or call `createSettings()` then `loadSettingsJson("cfg.json", settings, themeName, ...)`), set `settings->themeFile`, drop the `loadColourSchemeTxt` call.
  - Exit path (main.c:452) — replace `saveColourScheme("CLR.dat", getColourScheme())` with `saveThemeJson(themePath, getColourScheme(), &gFontConfig)` + `saveSettingsJson("cfg.json", settings, settings->themeFile)`.

- [ ] **Step 1: Make the flow compile**

Add a file-scope `static FontConfig gFontConfig;` + `static bool gThemeLoaded;` in gui.c. `InitGUI()`:

```c
void InitGUI(void) {
	const int screenWidth = SCREEN_W;
	const int screenHeight = SCREEN_H;

	if(!gThemeLoaded) {
		initDefaultColourScheme(&cs);
	}

	InitWindow(screenWidth, screenHeight, "Spectrax");
	textFont = LoadFont("resources/fonts/setback.png");
	/* theme font (base size + spacing from clr.json) */
	{
		const char *fp = gFontConfig.path[0] ? gFontConfig.path : "resources/fonts/console.ttf";
		int fsz = gFontConfig.size > 0 ? gFontConfig.size : 9;
		pixelFont = LoadFontEx(fp, fsz, NULL, 255);
	}
	...
```

In `main.c` main():

```c
int main(int argc, char **argv) {
	char baseDir[1024];
	resolveDataDir(argc, argv, baseDir, sizeof(baseDir));
	if(!chdirToDataDir(baseDir)) {
		fprintf(stderr, "spectrax: cannot use data dir '%s'\n", baseDir);
		return 1;
	}
	InitGUI();
	...
```

- [ ] **Step 2: Load cfg + theme before InitGUI**

In `main()` before `InitGUI()`, add:

```c
	Settings bootSettings;
	createSettings(&bootSettings);          /* refactor createSettings to fill a caller struct */
	char themeName[256] = "clr.json";
	loadSettingsJson("cfg.json", &bootSettings, themeName, sizeof(themeName));
	loadThemeJson(themeName, &cs, &gFontConfig, NULL);
	gThemeLoaded = true;                    /* set even if the file was missing — cs already has defaults */
```

Note: `createSettings` has a SINGLE caller (main.c:483, initApplication) and no test references it — refactor directly to `void createSettings(Settings *s)` filling a caller struct, and drop the `Settings *createSettings(void)` malloc wrapper entirely. `cs` is `static` in gui.c — expose it via the existing `getColourScheme()` (returns the static) so main.c can pass it. Add `bool themeWasLoaded(void)` (returns `gThemeLoaded`) OR set `gThemeLoaded` via a small setter `markThemeLoaded(void)` called from main.c.

- [ ] **Step 3: initApplication uses the loaded settings**

In `initApplication`, replace:

```c
	Settings *settings = createSettings();
	loadColourSchemeTxt("colourscheme2.txt", getColorSchemeAsPointerArray(), 9);
```

with (keeping the rest unchanged — `settings` is used throughout):

```c
	Settings *settings = createSettings();
	loadSettingsJson("cfg.json", settings, settings->themeFile, sizeof(settings->themeFile));
	if(!settings->themeFile[0]) {
		strncpy(settings->themeFile, "clr.json", sizeof(settings->themeFile) - 1);
	}
```

- [ ] **Step 4: Exit path saves cfg + theme**

Add a `Settings *settings;` field to `paTestData` (main.h), set it in `initApplication` (`data->settings = settings;` right after creation). Then in main()'s exit path (main.c:452), replace `saveColourScheme("CLR.dat", getColourScheme());` with:

```c
	saveSettingsJson("cfg.json", data.settings, data.settings->themeFile);
	saveThemeJson(data.settings->themeFile, getColourScheme(), &gFontConfig);
```

(`gFontConfig` is the gui.c static from Step 1 — expose it via `FontConfig *getFontConfig(void)`.)

- [ ] **Step 5: Remove dead loaders**

Remove (or leave unused) `loadColourSchemeTxt`/`saveColourScheme`/`loadColourScheme` in `src/io/gui_io.c` and `getColorSchemeAsPointerArray()` (gui.c:97-112) if nothing else uses them. Prefer removing; if `getColorSchemeAsPointerArray` is used elsewhere, keep it until Task 10's audit.

- [ ] **Step 6: Build + verify**

```bash
ninja -C build && meson test -C build
cd bin && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax 2>&1 | grep -iE "fpe|segfault|presets loaded"
```
Expected: 0 errors, all tests pass, app boots.

- [ ] **Step 7: Commit**

```bash
git add src/main.c src/gui.c src/gui.h src/settings.c src/io/gui_io.c src/io/gui_io.h
git commit -m "feat(cfg): wire data-dir + cfg.json + clr.json into init/exit; theme font"
```

---

### Task 10: Ship defaults + full boot verification

**Files:**
- Create: `bin/cfg.json`, `bin/clr.json`

- [ ] **Step 1: Generate the default files**

Run the app once from `bin/` (it now saves cfg.json + clr.json on exit via Task 9), then commit the generated files:

```bash
cd bin && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax >/dev/null 2>&1
cat bin/cfg.json
cat bin/clr.json
```
Expected: cfg.json contains the settings (defaultBPM 120, enabledChannels 8, ...) + `"theme": "clr.json"`; clr.json contains all 28 colour keys as `#RRGGBBAA` + the font block.

If the files don't appear (no clean exit under the timeout kill), write them by hand from the defaults instead.

- [ ] **Step 2: Verify all three base-dir paths**

```bash
# (1) cwd = bin (no flag)
cd bin && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax 2>&1 | grep -c "PRESETS LOADED"
# (2) --data-dir
cd repo-root && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./bin/spectrax --data-dir bin 2>&1 | grep -c "PRESETS LOADED"
# (3) ~/.config/spectrax: mkdir -p ~/.config/spectrax; cp bin/cfg.json bin/clr.json + resources/data symlinks or copies there; run from repo root
```
Expected: all three print `PRESETS LOADED: 5` and boot without FPE/segfault.

- [ ] **Step 3: Verify a theme tweak recolours the UI**

Edit `bin/clr.json`: change `"dial": "#ff0000"` to `"dial": "#00ff00"`. Run + confirm the instrument screen dial needles render green via the SHOT harness verb. Create the fixture `src/tools/instrument_harness/fixtures/shot_instrument.txt`:

```
FRAMES 2
SHOT /tmp/shot_inst.png
QUIT
```

Run: `cd bin && bash ../src/tools/instrument_harness/run_scripted.sh fixtures/shot_instrument.txt` and OCR the shot (`ocrq submit /tmp/shot_inst.png --profile describe`) to confirm dials are green-ish and labels readable. Then revert clr.json.

- [ ] **Step 4: Full gate**

```bash
meson test -C build
cd bin && bash ../src/tools/instrument_harness/run_scripted.sh 2>&1 | grep -E "PASS|FAIL"
bash ../src/tools/instrument_harness/run_scripted.sh fixtures/preset_save_load.txt 2>&1 | grep -E "PASS|FAIL"
```
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add bin/cfg.json bin/clr.json
git commit -m "chore(cfg): ship default cfg.json + clr.json"
```

---

## Self-Review

**Spec coverage:**
- A (data-dir) → Task 3 + Task 9 (wiring). ✓
- B (cfg.json settings) → Task 6 + Task 9. ✓
- C (clr.json theme) → Tasks 4-5 + 9. ✓
- D (hex colours) → Task 2. ✓
- E (migration: remove colourscheme2.txt/CLR.dat) → Task 9 Step 5. ✓
- F (cJSON vendored) → Task 1. ✓
- G (testing) → Tasks 2-6 unit tests + Task 10 verification. ✓
- Spec's out-of-scope items respected (per-widget font sizes stay; no hot-swap). ✓

**Dependency order:** T1 (cjson) → T2 (hex) → T3 (paths) → T4 (theme.h) → T5 (theme json) → T6 (settings json) → T7/T8 (migrations, depend on T4) → T9 (wiring, depends on T5+T6+T3) → T10 (defaults, depends on T9).

**Note on Task 9 createSettings:** `createSettings` has a single caller (main.c:483) and no test references — refactor directly to `void createSettings(Settings *s)`, drop the malloc wrapper, store the pointer in `data.settings`.
