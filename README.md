# Spectrax

A 16-channel step sequencer and synthesiser: FM, sample, BLEP, granular and
spectral voices, a modulation matrix, and a JSON-driven UI that runs on a
desktop and on 640×480 handhelds (RG35XX SP). It plays on Linux through
PortAudio, or directly through ALSA on hardware where PortAudio cannot pace
the stream.

Everything is one C project built with Meson. The UI, theme and layout are
data (`clr.json`, `layout.json`), so most visual work needs no rebuild.

## Requirements

- Linux (X11 on the desktop, KMS/DRM on the handheld). Other platforms are
  untested; see [Windows](#windows).
- A C compiler, `meson` >= 0.60 and `ninja`.
- raylib and kissfft are **vendored** in `include/` and `lib/<arch>/` — there
  is nothing to download.
- System libraries: PortAudio, ALSA, OpenGL and (host builds) X11.

### Void Linux

```sh
sudo xbps-install -S base-devel meson ninja pkgconf \
    portaudio-devel alsa-lib-devel MesaLib-devel glu-devel \
    libX11-devel libXrandr-devel libXi-devel libXcursor-devel \
    libXinerama-devel wayland-devel libxkbcommon-devel
```

### Debian / Ubuntu

```sh
sudo apt install build-essential meson ninja-build pkg-config \
    portaudio19-dev libasound2-dev libgl1-mesa-dev libglu1-mesa-dev \
    libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev \
    libwayland-dev libxkbcommon-dev
```

### Arch / Fedora

Arch: `meson ninja base-devel portaudio alsa-lib mesa libx11 libxrandr libxi
libxcursor libxinerama wayland libxkbcommon`.
Fedora: `meson ninja-build gcc portaudio-devel alsa-lib-devel
mesa-libGL-devel libX11-devel libXrandr-devel libXi-devel libXcursor-devel
libXinerama-devel wayland-devel libxkbcommon-devel`.

Optional, for the parts of the test stack they gate: `xvfb-run` (the headless
UI tests), `qemu-user-static` and an aarch64 cross toolchain (the cross-build
smoke test), `ImageMagick` (the screenshot tooling).

## Build and run

```sh
git clone https://github.com/mroemore/Spectrax
cd Spectrax

meson setup build
meson compile -C build
meson install -C build     # copies the binaries into bin/ and provisions XDG dirs
cd bin && ./spectrax
```

`meson install` is not optional: the binaries, the app and the harnesses all
run from `bin/`, which is the runtime root holding `resources/`, `layout.json`
and `clr.json`. A stale binary in `bin/` is the usual explanation for a change
that "did nothing".

A release build:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release && meson install -C build-release
```

`meson setup` warns and carries on when a library is missing; the compile then
fails at link time. If that happens, check the development package for the
library named in the error.

## Where the app keeps its files

| What | Path |
|------|------|
| Config | `~/.config/spectrax/` — `cfg.json` (settings), `clr.json` (theme + font), `layout.json` (styles + layouts) |
| Data | `~/.local/share/spectrax/` — a real copy of `resources/`, `data/` and the shipped song, refreshed by every `meson install` |
| Overrides | `--config-dir DIR`, `--data-dir DIR` |

`meson install` creates both directories. Resources and the shipped presets are
copied (not symlinked) so the install is self-contained and survives moving the
checkout; `resources/` is refreshed on every install, so a fixed font or theme
asset reaches an existing install. Config files and a saved song are never
overwritten. Editing the theme or layout in the config dir takes effect on the
next start; the app itself does not hot-reload, but `--ui-preview` does (below).

## Command line

```
spectrax [--config-dir DIR] [--data-dir DIR] [--volume N]
         [--alsa-pcm PCM] [--ui-preview [--ui-preview-frames N]
                          [--ui-preview-shot BASE] [--ui-preview-mode MODE]]
         [--probe-route] [--probe-tempo]
```

- `--alsa-pcm hw:0,0` — take the PCM directly instead of going through
  PortAudio. Required on the RG35XX SP, where PortAudio's ALSA path either
  never delivers callbacks or free-runs; the app falls back to PortAudio with a
  warning if the PCM will not open.
- `--ui-preview` — the panel window, see below.
- `--probe-route`, `--probe-tempo` — diagnostics for the fixture scripts: the
  app runs normally and prints (and, for `--probe-route`, exports
  `probe_route.png`) once the routing overlay has settled. The fixtures kill
  the process; the flags do not exit on their own.
- `--volume N` / `-V N` — master volume at start-up, 0-100.

The engine runs at a fixed 48 kHz (`SPECTRAX_ENGINE_RATE`). A device that
cannot run at that rate is rejected rather than resampled, because a silently
substituted rate puts every note at the wrong pitch.

## UI preview

`--ui-preview` renders the app's screens in one window — arranger, both
pattern pages, one instrument panel per voice type, and the current modal —
so a styling or layout change can be judged across every screen instead of
navigating to each.

```sh
cd bin && ./spectrax --ui-preview                       # until closed
cd bin && ./spectrax --ui-preview --ui-preview-frames 60  # then exit
cd bin && ./spectrax --ui-preview --ui-preview-shot /tmp/pv   # /tmp/pv-arranger.png, ...
cd bin && ./spectrax --ui-preview --ui-preview-mode single
```

Three display modes, cycled with **TAB** as you watch:

| Mode | Shows | Keys |
|------|-------|------|
| `all` (default) | every panel at once, in a grid | — |
| `paged` | four panels per page | **left/right** step pages |
| `single` | one panel filling the window | **left/right** step panels |

Left/right wrap at the ends. Start in a mode with
`--ui-preview-mode {all|paged|single}`; the window resizes to fit the mode.

It runs with no audio and no save: an exporter, not a session; the only input
it takes is the mode keys above. While
it is open it watches the live `layout.json`, the active theme file and the
font, and recompiles on change — edit `~/.config/spectrax/layout.json` and the
panels follow. A theme or layout edit that fails to parse is reported as
`UI_PREVIEW: reload failed (...)` and retried on the next write.

## Tests

```sh
meson test -C build          # the whole suite (66 tests)
meson test -C build test_layout
```

The suite needs no display; the windowed UI tests are skipped when `xvfb-run`
is absent, and the cross-build smoke is skipped when `qemu-user-static` is
missing.

Coverage, sanitizers, fuzzing, mutation and the tiered gate live in `meson/`:
`./meson/coverage.sh`, `./meson/check.sh` (fast tier) and `./meson/check.sh
--full`. `TESTING.md` documents the stack.

Offline UI fixtures drive the instrument harness without a human:

```sh
src/tools/instrument_harness/run_scripted.sh fixtures/add_route_delete.txt
```

## Tools

Built alongside the app; most are copied into `bin/` by the install step.

- `spectrax_bench --bench={audio|gfx|fileio|mod|voicetype|kernels|replay|census|all}`
  — benchmarks, written as reports into `runs/<UTC>/`. `--help` lists the
  options; `-R`, `-F`, `--duration`, `--voices`, `--channels`, `--seed`.
- `instrument_harness --script fixtures/<name>.txt` — scripted UI assertions and
  screenshots; fixtures live in `src/tools/instrument_harness/fixtures/`.
- `nav_harness --layouts N --keys l,r,u,d --out DIR` — layout navigation checks.
- `inspectro_wavget` — sample analyser.
- `vizulobe` — the visualisation sandbox, see below.
- `styling_tutorial` and `theme_tool` — the styling-system demo and the theme
  auditor. Neither is copied into `bin/`; run them from the build tree, e.g.
  `./build/src/tools/theme_tool/theme_tool check bin/clr.json`.

## vizulobe

Audio-reactive visualisation sandbox. Captures system audio (point it at your
`Monitor of ...` device) and runs a scene of GLSL `.frag` shaders and C
snippets compiled at runtime with TCC.

- `cd bin && ./vizulobe -p sample_project.json` (the sample project and its
  example vizzes are copied into `bin/` by the install step; sources live in
  `src/tools/vizulobe/`)
- `./vizulobe --list-devices`, then `./vizulobe -d <device-or-index>`
- `L` load a fg viz, `B` load the bg viz, drag to move, ctrl+drag to resize,
  `S` save project, `R` load project, `Del` remove selected.
- C snippets: `#include "vizulobe.h"`, define `viz_frame(viz_t *ctx)`. The full
  raylib draw API is available. Needs `tcc` on `PATH`
  (`sudo xbps-install tcc` on Void, `sudo apt install tcc` on Debian).
- GLSL: see `src/tools/vizulobe/smoke_viz.frag` for the uniform set.

## Handheld builds

Cross-compiling and deploying to a RG35XX SP is scripted end to end. The
profile (`devices/rg35xxsp.toml`) carries the panel, GPU, audio and path
facts.

```sh
scripts/gen-cross-file.sh rg35xxsp
meson setup build-rg35xxsp --cross-file build-rg35xxsp/cross.ini --buildtype=release
meson compile -C build-rg35xxsp spectrax spectrax_bench instrument_harness

scripts/setup-device.sh --device rg35xxsp          # first time: keys + address
scripts/deploy-spectrax.sh --device rg35xxsp       # rsync over wifi or USB gadget
SPECTRAX_ALLOW_PENDING_PROBES=1 scripts/deploy-spectrax.sh --device rg35xxsp
```

Do **not** run `meson install` for a cross build: `bin/` holds the host build,
and the install script refuses to touch it for a cross target. The deploy
copies from the build directory instead.

The aarch64 library bundle in `lib/aarch64/` is committed; to rebuild it,
`scripts/build-target-libs.sh aarch64`.

## Windows

Not supported today. The build assumes a POSIX shell (`meson/install.sh`), a
cross-capable toolchain and the vendored Linux raylib. An MSYS2 build would
need a Windows raylib in `lib/`, a `meson/install.ps1`-equivalent for the
`bin/` copy step, and a PortAudio that can see WASAPI; contributions welcome.

## Documentation

- `manual/` — user- and author-facing guides: the [styling
  system](manual/ui-styling.md) and tutorials for [building a
  component](manual/tutorials/building-a-component.md) and [shipping a
  theme](manual/tutorials/shipping-a-theme.md).
- `TESTING.md` — the testing stack: coverage, contracts, fault injection,
  fuzzing, the sanitizer matrix, mutation and the release gates.
- `RELEASE-CHECKLIST.md` — what to run before a release.

## Licence

MIT (see `project()` in `meson.build`). The bundled raylib is zlib-licensed and
kissfft BSD-3; their upstream terms apply to the copies in `include/` and
`lib/`.