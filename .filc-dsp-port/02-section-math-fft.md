# Section 2 — Math, Notes, FFT

**Goal:** Build and validate pure-math layer (no synthesis). `notes.c`,
`fft.c`, and the vendored `kissfft` static lib (rebuilt under fil-c).

## Scope

- `src/notes.c|h` — MIDI note ↔ frequency conversions, interval math.
- `src/fft.c|h` — FFT wrapper around kissfft.
- `include/kiss_fft.h`, `include/kiss_fftr.h` — vendored headers.
- `lib/linux/libkissfft-float.a` — REPLACE with fil-c build of kissfft
  (need to fetch upstream kissfft source and rebuild).
- `tests/test_notes.c` — note/frequency roundtrip tests.
- `tests/test_fft.c` — known-spectrum FFT roundtrip tests.

## Pre-work

1. Find kissfft source (BSD-3-Clause, Mark Borgerding). Latest release.
2. Drop it into a vendored dir, e.g. `third_party/kissfft/`.
3. Set up a tiny CMake or Makefile fragment to build `libkissfft-float.a`
   with fil-c.

## Expected fil-c gotchas

- kissfft uses `float` (single-precision). Fil-c supports floats fully.
- kissfft is plain C, no asm. Should build clean out of the box per
  fil-c's track record with similar C math libs.
- `notes.c` uses `powf()`, `logf()`, `expf()` from libm — should work.

## Tests added

- `test_notes`: MIDI 69 = 440 Hz, MIDI 60 = middle C ≈ 261.63 Hz, etc.
- `test_fft`: roundtrip impulse → constant spectrum, sine → single bin.

## Status

- [ ] Source vendored
- [ ] Build clean
- [ ] Tests pass
- [ ] Report file updated

## Blocker log

(Empty.)