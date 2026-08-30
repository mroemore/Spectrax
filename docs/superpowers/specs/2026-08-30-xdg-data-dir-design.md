# Spectrax XDG data-dir split — resources from an XDG data directory

Date: 2026-08-30

## Problem

Today the app resolves ONE data dir and `chdir()`s into it; every path
(`cfg.json`, `clr.json`, `resources/`, `data/`, `s1.sng`, instrument
presets) is then cwd-relative. The resolver picks `--data-dir` → XDG
**config** home (`~/.config/spectrax`, gated on cfg.json) → cwd.

This mixes config and data in one directory. It bit us directly: a stale
`~/.config/spectrax/s1.sng` (written by the old binary, incompatible
format) silently failed to load, so the app booted with an empty song and
the pattern screen had nothing to edit. Config data was shadowing song
data.

Per the XDG Base Directory spec, **resources are data**, not config:
- Config (`cfg.json`, `clr.json`) → `$XDG_CONFIG_HOME/spectrax`
  (default `~/.config/spectrax`).
- Data (assets `resources/`, samples `data/`, song `s1.sng`, instrument
  presets) → `$XDG_DATA_HOME/spectrax` (default `~/.local/share/spectrax`).

Decision (user-approved): **split** — resources resolve from an XDG data
directory (first port of call), config resolves from the config directory.

## A. Resolution

`src/paths.c` gains a second resolver; both honor CLI flags.

```
resolveConfigDir(argc, argv, out, outsz):
  1. --config-dir <dir>
  2. $XDG_CONFIG_HOME/spectrax        if it contains cfg.json
  3. $HOME/.config/spectrax           if it contains cfg.json
  4. "."

resolveDataDir(argc, argv, out, outsz):
  1. --data-dir <dir>
  2. $XDG_DATA_HOME/spectrax          if the directory exists
  3. $HOME/.local/share/spectrax      if the directory exists
  4. "."
```

The config-dir candidates keep the `cfg.json` existence gate (a config dir
with no cfg.json is useless — fall to cwd). The data-dir candidates gate on
**directory existence** only (a data dir may legitimately have no resources
yet; the app already handles missing assets).

`--config-dir` is new (mirrors `--data-dir`, gives tests and portable
setups a way to pin the config dir). `--data-dir` keeps its current
meaning but now targets the DATA dir only.

## B. main() flow (`src/main.c`)

```
resolveConfigDir(argc, argv, cfgDir, sizeof(cfgDir));
resolveDataDir(argc, argv, dataDir, sizeof(dataDir));

// config from the CONFIG dir (absolute paths)
snprintf(cfgPath, "%s/cfg.json", cfgDir);
loadSettingsJson(cfgPath, &settings, settings.themeFile, ...);
if (themeFile empty) themeFile = "clr.json";
snprintf(clrPath, "%s/%s", cfgDir, settings.themeFile);
loadThemeJson(clrPath, getColourScheme(), getFontConfig());
markThemeLoaded() only if clrPath exists (current behavior preserved);

if(!chdirToDataDir(dataDir)) { stderr; return 1; }

InitGUI();            // font path "resources/..." resolves in dataDir
initApplication(&data, &appState, NULL);
```

`paTestData` (src/main.h) gains `char configDir[1024];`. `main()` sets it
from `resolveConfigDir` so the exit path can save cfg/clr back to the
right place.

`initApplication`:
```
resolve/use data->configDir; if empty, resolveConfigDir() internally
  (covers the harness path).
snprintf(cfgPath, "%s/cfg.json", cfgDir);
loadSettingsJson(cfgPath, settings, settings->themeFile, ...);
apply the "clr.json" fallback to settings->themeFile;
data->settings = settings;
```
Loading cfg.json twice (once in main() for the theme name, once in
initApplication for the real Settings) is harmless — same file, same
result, and it is what fixed the earlier settings-discard bug.

## C. Exit path

```
saveSequencerState("s1.sng", ...)            // cwd = dataDir (unchanged)
saveSettingsJson(cfgPath, data.settings, ...) // cfgDir absolute path
saveThemeJson(clrPath, getColourScheme(), ...) // cfgDir absolute path
```

cfgPath/clrPath are the same buffers main() built, stored via
`data.configDir`.

## D. Harness

The instrument harness calls `InitGUI()` + `initApplication()` directly
(no main(), no chdir). Its `data.configDir` starts empty, so
`initApplication` resolves the config dir itself — the harness boots from
`bin/`, so it resolves to `~/.config/spectrax` (if present) or `.`.
Resources (assets, s1.sng) stay cwd-relative in `bin/` as today.

Risk: the harness fixtures assert against default settings; if the user's
`~/.config/spectrax/cfg.json` ever differs from the defaults the fixtures
could break. Mitigation: verify both scripted fixtures still PASS after
the change; document the coupling in the harness header.

## E. Cleanup of the legacy single-dir setup

`~/.config/spectrax/` currently holds cfg.json + clr.json + a stale
s1.sng + `data`/`resources` symlinks (artifacts of the everything-dir
model). After the split:
- Keep: cfg.json, clr.json (the config dir).
- Remove: s1.sng (song belongs in the data dir), the data/resources
  symlinks (resources now resolve from the data dir or cwd).
- Optionally create `~/.local/share/spectrax/` with `data`/`resources`
  symlinks to `bin/` so a fully-hermetic XDG data dir exists. This is the
  user's call — the app falls back to cwd (`bin/`) for data when
  `~/.local/share/spectrax` does not exist, which is the shipped default.

## F. Testing

- `tests/dsp/test_cfg.c`: update `resolveDataDir` tests — the HOME
  fallback is now `$HOME/.local/share/spectrax` (XDG **data** home), gated
  on directory existence (not cfg.json). Add `resolveConfigDir` tests
  (flag, XDG config home gate, HOME fallback, cwd).
- App boot checks: with `~/.local/share/spectrax` absent → data from cwd,
  config from `~/.config/spectrax`; with it present + resources symlinked
  → data from it. `--data-dir bin` pins data to bin.
- Gate: `ninja` clean, `meson test` 8/8, both scripted fixtures PASS, app
  boots with PRESETS LOADED.

## G. Out of scope

- Per-widget `DrawTextEx` font sizes remain hardcoded (unchanged).
- `$XDG_CACHE_HOME` — not used by the app today; no cache concept exists.
- A `--config-dir` flag is included; no further CLI additions.
