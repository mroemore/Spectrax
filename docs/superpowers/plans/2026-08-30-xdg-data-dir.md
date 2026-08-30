# Spectrax XDG data-dir split — implementation plan

Spec: `docs/superpowers/specs/2026-08-30-xdg-data-dir-design.md`

Branch: `config-system` (on top of the config work).

## Preconditions

- `src/paths.c` has `resolveDataDir(argc, argv, out, outsz)` (currently
  `--data-dir` → XDG config home → HOME config → `.`) + `chdirToDataDir`.
- `src/main.c` `main()` resolves + chdirs ONE data dir, loads cfg/clr
  cwd-relative, calls `initApplication`. `initApplication` loads
  `"cfg.json"` cwd-relative into a malloc'd Settings.
- `paTestData` (src/main.h) has NO configDir field. Both mains declare
  `paTestData data;` WITHOUT zero-init.
- test_cfg.c tests `resolveDataDir` (flag, XDG-config-home, HOME,
  missing-value, env isolation).

## Task 1 — paths: resolveConfigDir + resolveDataDir rework (TDD)

**RED first.** Update `test_resolve_data_dir_*` in `tests/dsp/test_cfg.c`:
- HOME fallback is now `$HOME/.local/share/spectrax` (XDG **data** home),
  gated on **directory existence** (not cfg.json).
- `$XDG_DATA_HOME/spectrax` candidate gated on dir-exists.
- Keep: `--data-dir` flag, missing-value → `.`, env isolation, truncation.

Add `test_resolve_config_dir_*`:
- `--config-dir <dir>` flag.
- `$XDG_CONFIG_HOME/spectrax` gated on cfg.json presence.
- `$HOME/.config/spectrax` gated on cfg.json presence.
- missing flag value → falls through; env isolation (HOME/XDG_CONFIG_HOME);
  truncation guard leaves out untouched.

**GREEN.** `src/paths.c`:
```c
void resolveConfigDir(int argc, char **argv, char *out, size_t outsz) {
    /* 1. --config-dir <dir> */
    for(int i = 1; i < argc - 1; i++)
        if(strcmp(argv[i], "--config-dir") == 0) { guard+write; return; }
    /* 2. $XDG_CONFIG_HOME/spectrax if it has cfg.json */
    /* 3. $HOME/.config/spectrax if it has cfg.json */
    /* 4. "." */
}

void resolveDataDir(int argc, char **argv, char *out, size_t outsz) {
    /* 1. --data-dir <dir> */
    for(int i = 1; i < argc - 1; i++)
        if(strcmp(argv[i], "--data-dir") == 0) { guard+write; return; }
    /* 2. $XDG_DATA_HOME/spectrax or $HOME/.local/share/spectrax,
     *    gated on directory existence (access(dir, F_OK|X_OK)) */
    /* 3. "." */
}
```
Mirror the existing truncation-guard idiom (leave `out` untouched on
overflow). `src/paths.h` declares `resolveConfigDir`.

Gate: `ninja -C build` clean, `meson test -C build` 8/8.

## Task 2 — main wiring: configDir threading (main.c/main.h + harness)

- `src/main.h`: add `char configDir[1024];` to `paTestData`.
- `src/main.c` `main()`:
  ```
  char cfgDir[1024], dataDir[1024];
  resolveConfigDir(argc, argv, cfgDir, sizeof(cfgDir));
  resolveDataDir(argc, argv, dataDir, sizeof(dataDir));
  snprintf(data.configDir, sizeof(data.configDir), "%s", cfgDir);
  // load cfg.json from cfgDir (absolute)
  snprintf(cfgPath, sizeof(cfgPath), "%s/cfg.json", cfgDir);
  loadSettingsJson(cfgPath, &settings, settings.themeFile, ...);
  // theme fallback "clr.json" -> snprintf(clrPath, "%s/%s", cfgDir, themeFile)
  // loadThemeJson(clrPath, ...) + markThemeLoaded only if clrPath exists
  if(!chdirToDataDir(dataDir)) { fprintf(stderr,...); return 1; }
  InitGUI();
  initApplication(&data, &appState, NULL);
  ```
- `initApplication`: use `data->configDir`; if empty, `strcpy(..., ".")`.
  Load cfg from `configDir/cfg.json` (absolute) + apply the "clr.json"
  theme fallback to `settings->themeFile`. (Preserves the settings-discard
  fix from the config work.)
- Exit path: `saveSettingsJson(cfgPath, data.settings, ...)` +
  `saveThemeJson(clrPath, ...)` where cfgPath/clrPath are rebuilt from
  `data.configDir`. `saveSequencerState("s1.sng", ...)` stays cwd-relative
  (dataDir).
- Harness (`src/tools/instrument_harness/instrument_harness.c`): zero-init
  `paTestData data;` → `paTestData data = { 0 };` so configDir is "". Its
  `initApplication` then loads `./cfg.json` from cwd (bin) — hermetic.
- `test_vizfx.c` / any other file that constructs paTestData: unaffected
  (initApplication is the only constructor).

Gate: `ninja` clean, `meson test` 8/8, both scripted fixtures PASS, app
boots (PRESETS LOADED) from `bin/`.

## Task 3 — legacy cleanup + XDG data-dir setup + verification

- Clean `~/.config/spectrax/`: remove `s1.sng` (stale) + the
  `data`/`resources` symlinks (everything-dir artifacts). Keep cfg.json +
  clr.json.
- Set up `~/.local/share/spectrax/` with `data`/`resources` symlinks to
  `bin/` so an XDG data dir exists (the app then resolves data there first
  when present). This is the optional part of the spec — confirm with the
  user before creating it; without it the app falls back to cwd (bin/)
  for data, which is the shipped default.
- Verify all resolution paths:
  1. `cd bin && ./spectrax` (no flags): config from ~/.config/spectrax,
     data from cwd (bin) [or from ~/.local/share/spectrax if created].
  2. `./spectrax --data-dir bin` from repo root: data pinned to bin.
  3. `./spectrax --config-dir bin` from repo root: config pinned to bin.
  Each: PRESETS LOADED, no FPE/segfault, clean exit saves cfg/clr to the
  right place + s1.sng to the data dir.
- Gate: `meson test` 8/8, both scripted fixtures PASS, boot clean.

## Task 4 — final verification + commit

- Full gate: `ninja -C build` clean, `meson test -C build` 8/8, both
  scripted fixtures PASS, app boots under Xvfb + on the real display.
- Working tree tidy (no temp hooks, no stray files), binaries reinstalled.
- Commit.

## Global constraints

- Run app-level verification under Xvfb when possible
  (`xvfb-run -a -s "-screen 0 1280x800x24"`).
- Preserve the settings-discard fix: `initApplication` must load cfg.json
  into the Settings it stores in `data->settings`, theme fallback included.
- Do NOT touch `src/vizfx.c` (user's restored WIP) or commit the
  uncommitted WIP.
- Do not push; merge to local main + user reviews + user handles remote.
