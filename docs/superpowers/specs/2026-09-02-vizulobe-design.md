# Vizulobe: Audio-Reactive Visualisation Sandbox — Design

Date: 2026-09-02
Status: Approved design (awaiting spec review + implementation plan)

## Problem

Spectrax is a synthesizer/sequencer; its visuals are tied to the app (spectrogram,
time graph, graph GUI). There is no lightweight, standalone sandbox for developing
audio-reactive visualisations — something that captures whatever audio the user is
hearing and lets them iterate on small self-contained visual routines (GLSL shaders
and C snippets) that react to the live waveform and spectrum.

Vizulobe is that sandbox: a raylib game loop + PortAudio capture, where a scene is a
stack of freely-placed, drag-resizable rectangles — a full-window background layer
plus up to 32 foreground viz rectangles — each rendering either a GLSL fragment
shader or a small C program compiled at runtime with TCC and dlopen'd. Every viz
receives the same inputs: stereo waveform, stereo spectrum, per-rect backbuffer
(previous frame), time, and instantaneous audio level. Layouts are saved/loaded as
JSON project files.

## Confirmed decisions

- **Location**: new `executable('vizulobe')` under `src/tools/vizulobe/`, built by
  meson alongside the existing tools, reusing `src/fft.c` and the vendored raylib /
  system PortAudio deps. Not a standalone project.
- **Audio source**: system audio via PortAudio input capture — the PulseAudio /
  PipeWire "Monitor of ..." device, selected by the user. App does NOT own
  playback and does NOT reroute the system audio graph.
  - `-d <device>` / `--device` CLI flag, or `default_device` in the config file,
    falling back to `Pa_GetDefaultInputDevice()`. `--list-devices` prints inputs
    (same pattern as spectrogrammr).
  - Capture: stereo, `paFloat32`, `SAMPLE_RATE 44100` (matches Spectrax), block
    size 256 (matches Spectrax's portaudio block). Ring buffers with atomics,
    drained in the main loop (spectrogrammr `g_capture` pattern).
- **Viz loading**: both backends loaded at runtime.
  - GLSL `.frag`: `LoadShaderFromMemory(NULL, src)`.
  - C `.c`: `tcc -shared -fPIC -o <cache>.so <src> -Iinclude` then `dlopen`.
    Host binary built with `-rdynamic` so the snippet's undefined raylib symbols
    resolve against the running app (no duplicate raylib instance).
  - Failure (shader compile, tcc, dlopen, missing entry point) renders a red error
    panel inside that rect with the error text; the rect remains so the file can be
    fixed and reloaded.
- **Scene model**: two layers.
  - Background (`B` load prompt): exactly one viz, fills the whole window, not
    draggable/resizable. Rendered first.
  - Foreground (`L` load prompt): up to **32** viz rectangles, spawned centered at
    the cursor with a default size, click-select + drag to move, ctrl+drag to
    resize, `Del` to remove. Rendered on top of the background.
- **Rendering (Approach A)**: each rect owns a `RenderTexture2D`. Per frame:
  `BeginTextureMode(rect)` -> draw the rect's own previous-frame texture (feedback
  backbuffer) -> run the viz -> `EndTextureMode` -> blit at rect x/y/w/h. Because
  the C snippet body runs inside `BeginTextureMode`, all raylib draw calls it makes
  land in the rect with correct clipping. `gl_FragCoord` / UVs are rect-local for
  GLSL. Background is the same model with a full-window rect.
- **Viz contract** — both backends receive identical inputs.
  - GLSL uniforms: `uTime`, `uDt`, `uResolution` (rect size), `uWaveform`
    (sampler2D 1024x1, L packed in R channel, R in G channel), `uSpectrum`
    (sampler2D 1024x1, same packing), `uAudio` (vec2, instantaneous L/R level),
    `uBackbuffer` (sampler2D, this rect's previous frame).
  - C: `void viz_init(viz_t *ctx)` (optional) and `void viz_frame(viz_t *ctx)`
    (required), run inside the rect's RenderTexture. `viz_t` exposes `time`, `dt`,
    `waveform[2][1024]`, `spectrum[2][1024]` (both sized for the max FFT; the valid
    length is `fft_bins`, so only the first `fft_bins` spectrum entries are filled),
    `audio_l`, `audio_r`, `rect_w`, `rect_h`, and `Texture2D backbuffer`. Full
    raylib draw API available.
  - `vizulobe.h` installed under `include/` so snippets can include it.
- **Analysis**: rolling waveform ring of 1024 samples/channel; FFT size default
  1024 -> **512 spectrum bins/channel**, overridable via `--fft <bins>` (FFT size
  = 2*bins). RMS of the current 256-sample block for `uAudio`. Reuses `src/fft.c`
  via `initFFT(fft, fftSize, 256, 1, true, false)`.
- **Project file** (JSON, mirrors `preset_io` style), saved with `S`, loaded with
  `R`:
  ```json
  {
    "bg": "viz/plasma.frag",
    "fg": [
      { "path": "viz/osc.c", "x": 40, "y": 60, "w": 320, "h": 240 }
    ],
    "fft_bins": 512
  }
  ```
  Paths relative to the project file location for portability.
- **Input / UX**:
  - `L` text-prompt to load a fg viz (spawns at cursor), `B` to replace bg,
    `S` save project, `R` load project, `Del` remove selected fg rect.
  - Same small text prompt reused for viz paths (and device).
  - A terse helper/notification box (keybinds + CLI reminder) is drawn at the top
    of the window on startup and dismissed on the first click.
- **CLI**:
  ```
  vizulobe [-d device] [--fft bins] [-p project.json] [--list-devices]
  ```
- **Error handling**: missing audio device -> app runs, visuals idle on silence,
  one message to stderr. Project load failures -> non-fatal, stderr, keep current
  scene. Bad viz file -> red error panel in that rect.

## Architecture (evidence)

- `SAMPLE_RATE = 44100` at `src/oscillator.h:8`; Spectrax opens PortAudio at 44100
  with block 256 (`src/main.c:175`). Vizulobe captures at the same rate/block so
  any shared assumptions line up.
- `fft.c` is parameterizable: `initFFT(Fft*, int fftSize, int framesPerBuffer, int
  toAverage, bool removeDC, bool cpxOut)` (`src/fft.h:65`); `freqCount =
  fftSize/2`. Spectrax uses 4096/256 for its spectrogram (`src/main.c:415`); the
  tool defaults to 1024 -> 512 bins.
- spectrogrammr (`~/proj/spectrogrammr/src/main.c`) establishes the proven
  PortAudio capture pattern: `--list-devices`/`--device N`, fallback to
  `Pa_GetDefaultInputDevice()`, atomic ring buffer drained by the main thread, and
  a config-file `default_device`. "Monitor of ..." devices capture system audio.
- `preset_io.c` precedent for JSON persistence.

## File layout

```
src/tools/vizulobe/
├── meson.build          # executable 'vizulobe', links raylib+portaudio+kissfft
├── main.c               # init, main loop, input dispatch, helper box
├── audio.c/h            # portaudio capture (stereo monitor), ring buffers
├── analysis.c/h         # waveform ring + FFT spectrum + RMS (uses src/fft.c)
├── scene.c/h            # layer model: bg viz + up to 32 fg viz rects
├── viz.c/h              # viz loaders: GLSL (.frag) + C (.c via tcc->dlopen)
├── rect.c/h             # per-rect RenderTexture, feedback draw, drag/resize
└── project.c/h          # JSON save/load of the scene layout
include/vizulobe.h       # snippet-facing API (viz_t + entry point decls)
```

## Testing

Ground-up order per repo convention (tests/dsp/ pattern; `meson test`):

1. `test_scene.c` — fg rect add/remove/select, drag + ctrl-resize math, 32-cap
   enforcement. No window.
2. `test_analysis.c` — waveform ring push/rollover, spectrum magnitude from
   `fft.c`, RMS level. No window.
3. `test_project.c` — JSON round-trip: save a scene, load it back, assert
   identical rects + bg + fft_bins. No window.
4. Viz loader tests — feed a valid `.c` and a broken one through the tcc->dlopen
   path; assert successful load exposes the entry point and a broken one surfaces
   the error (may need Xvfb since dlopen needs raylib loaded for symbol
   resolution).
5. Xvfb app smoke — boot under `xvfb-run`, auto-load a shader + a C viz via a temp
   auto-drive hook (keyboard unreachable under Xvfb), screenshot, confirm both
   rects render non-black; reuse the `SPECTRAX_HARNESS`-style hook pattern.

## Out of scope

- App-side audio playback or audio graph rerouting (capture only).
- Hot-reload / directory watching of viz files (explicit `L`/`B` loads only).
- Global composited backbuffer (per-rect backbuffer only).
- More than one background layer, layering/reordering of fg rects, or z-depth.
- Per-rect arbitrary C pre-processing feeding GLSL (single self-contained contract).
- fzf or any external picker dependency.