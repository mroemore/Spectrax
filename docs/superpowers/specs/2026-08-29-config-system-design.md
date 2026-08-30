# Spectrax Config System: cfg.json + clr.json + --data-dir

Date: 2026-08-29
Status: Approved design

## Problem

- ~48 hardcoded `Color{...}` literals across 6 source files (gui.c 31, vizfx.c 10,
  dataviz.c 4, graph_gui.c 1, gui_layer.c 1, spectrogram.c 1) are not themeable.
- The existing `ColourScheme` (9 named colours) is loaded from `colourscheme2.txt`
  (`r,g,b` lines) and saved to `CLR.dat` (binary `CSC1`) — but only some widgets
  use it, and neither format is human-friendly.
- Settings (`defaultBPM`, `enabledChannels`, `voiceTypes`, `defaultSequenceLength`,
  `defaultVoiceCount`) are hardcoded in `createSettings()` (settings.c:1-25).
- The font (`pixelFont`, gui.c:130: `LoadFontEx("resources/fonts/console.ttf", 9, 0, 255)`)
  path/size/spacing are hardcoded.
- All assets load cwd-relative, so the app must be launched from `bin/`.

## Goals

1. Every colour literal becomes a named, config-driven theme key.
2. Font path/size/spacing become config-driven (in the theme).
3. Settings move to JSON.
4. Hex colour support (`#RRGGBB`, `#RRGGBBAA`).
5. A `--data-dir` CLI arg (plus `~/.config/spectrax/` fallback) removes the
   "must run from bin/" constraint.

## Design

### A. Data-directory resolution

New CLI flag: `--data-dir <dir>`.

At startup (before `InitGUI` / any asset load), resolve ONE base directory:

1. `--data-dir <dir>` if provided.
2. `$HOME/.config/spectrax/` if it exists AND contains `cfg.json`.
3. The current working directory.

Then `chdir(base)`. Because every asset path in the app is cwd-relative
(`resources/`, `data/`, `s1.sng`, `cfg.json`, `clr.json`), a single `chdir`
makes all of them resolve under the resolved base — one line, uniform effect.

Implementation: `const char *resolveDataDir(int argc, char **argv)` in main.c
(or a small `src/paths.c` if it grows), called at the top of `main()` before
`InitGUI()`. Path resolution for `$HOME` uses `getenv("HOME")` (or `$XDG_CONFIG_HOME`
if set, falling back to `~/.config`).

Errors: if `--data-dir` is given and `chdir` fails, print a clear error and exit 1.

### B. cfg.json — settings

File name: `cfg.json` in the base dir.

```json
{
  "defaultBPM": 120,
  "enabledChannels": 8,
  "defaultSequenceLength": 16,
  "defaultVoiceCount": 1,
  "voiceTypes": [4, 1, 2, 1, 2, 1, 2, 1],
  "theme": "clr.json"
}
```

Semantics:
- Each key maps onto the existing `Settings` struct (settings.h:33-39).
- **Per-key fallback**: a missing key keeps the current hardcoded default
  (`createSettings` values). A malformed value for one key falls back to that
  key's default; a malformed file entirely falls back to all defaults.
- `voiceTypes`: array of `MAX_SEQUENCER_CHANNELS` ints. If shorter, pad with the
  default pattern (`i % 3`, then index 0 = 4, matching today's `createSettings`).
- `theme`: file name of the active theme in the base dir. Default `"clr.json"`.
  Relative to base (no subdirectory needed for now).
- On exit the app saves settings back to `cfg.json` (new `saveSettingsJson`).

Wiring: `createSettings()` (settings.c) gains a `const char *cfgPath` parameter
(or a `loadSettingsJson(Settings*, const char *path)` called from
`initApplication`). Missing file → defaults, no error noise.

### C. clr.json — theme

File name: named by `cfg.json`'s `theme` key (default `clr.json`) in the base dir.

```json
{
  "font": {
    "path": "resources/fonts/console.ttf",
    "size": 9,
    "spacing": 1
  },
  "colors": {
    "background": "#cf6e3a",
    "font": "#631100",
    "secondaryFont": "#b03500",
    "outline": "#db9467",
    "defaultCell": "#944410",
    "highlightedCell": "#d63c11",
    "selectedCell": "#eba14b",
    "blankCell": "#5e171d",
    "reddish": "#aa2631",
    "panel": "#503c3c",
    "panelBorder": "#0a0000",
    "valueDisplayBg": "#322828",
    "label": "#c8b4b4",
    "labelSelected": "#ffb4b4",
    "dial": "#ff0000",
    "valueText": "#ff0000",
    "vline": "#3cff96",
    "poly": "#ff5050",
    "waveformBg": "#000000",
    "waveform": "#ff0000",
    "waveformAlt": "#00ff00",
    "sampleBg": "#3c0a0a",
    "sampleAltBg": "#0a320a",
    "sampleBorder": "#c8503c",
    "stepBorder": "#501414",
    "stepClosed": "#501e1e",
    "arrangerPlayhead": "#ff0000",
    "arrangerCellText": "#c8b4b4"
  }
}
```

The key list above is the initial enumeration of today's literals; the full list
is derived mechanically during implementation by replacing every `(Color){...}`
literal with a named lookup. Names follow the widget/role they colour.

Semantics:
- `font.path` is relative to the base dir (the resolved data dir).
- `font.size` + `font.spacing` feed `LoadFontEx(path, size, NULL, 255)` for
  `pixelFont`. Missing/zero → current defaults (size 9, spacing 1).
  Note: changing the base size changes every `DrawTextEx` scale by design — layout
  may need manual tweaks, which is expected when a user switches fonts.
- `colors` are hex (`#RRGGBB` or `#RRGGBBAA`). Missing key → current hardcoded
  literal (partial themes OK).
- The existing 9 `ColourScheme` fields (gui.c:82-90) map onto the same-named
  keys; the struct gains the extra named fields for the ~40 literals.
  Decision: **extend the `ColourScheme` struct with one named `Color` field per
  theme key** (compile-time safety, no string lookups at draw time). The struct
  grows to ~40 fields; each draw site references `cs.<key>`.
- On exit the theme saves back to `clr.json` (new `saveThemeJson`).

Wiring: `loadThemeJson(ColourScheme*, FontConfig*, const char *themePath)` called
from `initApplication` after cfg.json loads (so the theme name is known) and
before `LoadFontEx`. `initDefaultColourScheme` remains the fallback source.

### D. Hex colour parsing

New helper `bool parseHexColor(const char *s, Color *out)`:

- Accepts `#RRGGBB` (alpha 255), `#RRGGBBAA`, and bare `RRGGBB` / `RRGGBBAA`
  (no `#`). Case-insensitive hex digits.
- Returns false on malformed input (wrong length, non-hex digit) — caller falls
  back to the default.

Placed in `src/io/gui_io.c` (or `src/colour.c` if it grows) + declared in the
matching header.

### E. Migration

- `loadColourSchemeTxt` (`colourscheme2.txt`) and `saveColourScheme`/`loadColourScheme`
  (`CLR.dat`) loaders/savers are removed (or left unused + removed in a follow-up
  dead-code pass — prefer removing now).
- `main.c` exit path: `saveColourScheme("CLR.dat", ...)` → `saveThemeJson(...)`;
  the `s1.sng` save stays.
- Ship default `cfg.json` + `clr.json` in `bin/` (the default base dir), generated
  from the current defaults so the app looks identical on first boot.
- `initDefaultColourScheme` stays as the in-memory fallback.

### F. cJSON vendoring

- Vendor `cJSON.h` + `cJSON.c` (MIT) into `third_party/cjson/`.
- meson: new target/library `cjson` (static lib from `third_party/cjson/cJSON.c`,
  include dir `third_party/cjson`). Linked into the app, the harnesses, and the
  DSP tests that need it.
- `.gitignore` already covers `third_party/` (check); the vendored sources are
  committed.

### G. Testing

New `tests/dsp/test_cfg.c` (build + register in tests/meson.build):
- Settings: full cfg.json parse → Settings fields; missing key → default; malformed
  file → all defaults; short voiceTypes → padded.
- Theme: full clr.json parse → ColourScheme + FontConfig; missing colour key →
  hardcoded default; malformed hex value → default.
- Hex: `#RRGGBB`, `#RRGGBBAA`, bare `RRGGBB`, uppercase/lowercase, malformed
  (`#12345`, `#gggggg`, `123` ) → false.
- Save round-trip: write cfg.json + clr.json → parse back → identical fields.

Manual verification gate:
- App boots identically from `bin/` (cwd), from repo root with `--data-dir bin`,
  and from a `~/.config/spectrax/` copy.
- A theme tweak (e.g. `dial` → `#00ff00`) visibly recolours the UI.
- Existing scripted fixture (`run_scripted.sh`) still passes (it boots the harness
  which now loads the config — the fixture's assertions are state-based so they
  are unaffected by colours).

## Out of scope

- Per-widget font-size override in JSON (each `DrawTextEx` size stays as-is; only
  the font's loaded base size is configurable).
- Multiple simultaneous themes / theme hot-swap at runtime (theme is chosen by
  cfg.json and loaded at boot).
- Porting the `vizfx.c` mod-strip colours is in scope (they are part of the ~48
  literals); porting vizfx layout is not.
