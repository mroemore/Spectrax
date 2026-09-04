# font_preview — pixel-font profiling & preview for Spectrax

Tooling for choosing replacement pixel fonts that render pixel-perfect in
Spectrax (and sibling raylib apps). Two parts:

- **`rank_fonts.sh` + `fontprobe.c`** — measure every font in the tree with
  raylib's own rasterizer (stb_truetype ground truth; fontTools math diverges)
  and rank them against a profile: max glyph box `10 x 10` px at a comparable
  size of 10 px, derived from raylib's bundled default font
  (`GetFontDefault()`), which is the fallback behind every font-less
  `DrawText()` in `gui.c`, `modvisual.c`, `spectrogram.c`, `dataviz.c`,
  `graph_gui.c`.
- **`preview_fonts.c`** — interactive grid rendering 17 strings sampled from
  Spectrax's UI source side by side across fonts (current `console.ttf @9` +
  `setback.png @12`, plus candidates). Also exports full sheets headlessly.

## Build

Both use the vendored raylib (no system dep):

```sh
gcc src/tools/font_preview/fontprobe.c -Iinclude -Llib/linux -lraylib -lm -o fontprobe
gcc src/tools/font_preview/preview_fonts.c -Iinclude -Llib/linux -lraylib -lm -o preview_fonts
```

## Preview usage

```sh
./preview_fonts                            # interactive: current + top-5 candidates
./preview_fonts --screenshot out.png .     # curated grid -> out.png, exit
./preview_fonts --all every.png .          # EVERY font in the tree -> every.png, exit
./preview_fonts --all big.png . --size 12  # TTFs rendered at 12 px
```

`--screenshot` / `--all` are headless: if `DISPLAY` is unset a private Xvfb
(`:97`) is spawned automatically and torn down on exit. PNG sprite fonts
render at their native cell; TTFs at the size given (`--size`, default 10).
`--all` auto-discovers all `.ttf/.TTF/.otf` anywhere plus `.png` under
`fonts/` dirs, sorted current-fonts-first then alphabetical.

Controls: mouse wheel = vertical scroll, shift+wheel = horizontal, drag = pan.

## Ranking usage

```sh
sh src/tools/font_preview/rank_fonts.sh --spectrax . --max-w 10 --max-h 10 --target 10
```

Writes `spectrax-font-profile-10x10.md`. Requires gcc + vendored raylib and an
X display (pass `--display :99` with Xvfb, or run under your session).
Tune `--max-w/--max-h/--target` to the target app's real pixel box. The probe
binary is cached in `~/.cache/spectrax-font-profile/` and rebuilt when
`fontprobe.c` changes. (Formerly `rank_fonts.py`; ported to POSIX sh — the
`--no-raylib`/fontTools APPROXIMATE fallback is gone, the raylib probe is the
only measurement engine.)

## Current ranking (profile 10x10, comparable size 10)

| # | font | best size | box | vs profile |
|---|------|-----------|-----|------------|
| 1 | Daydream.ttf | 9 | 10x10 | perfect |
| 2 | DigitalDisco.ttf | 10 | 9x10 | dW=1 |
| 3 | DigitalDisco-Thin.ttf / themevck-text.ttf / KiwiSoda.ttf | 10/10/9 | 8x10 | dW=2 |
| 4 | 04B_03 / console.ttf (current default) | 10/9 | 7x10 | dW=3 |
| 5 | Nereus.ttf | 10 | 5x10 | dW=5 |

All 128x128 PNG sheets (setback, alpha_beta, romulus, pixantiqua, mecha,
alagard, pixelplay, jupiter_crash) are 10x12+ and exceed the 10x10 profile.