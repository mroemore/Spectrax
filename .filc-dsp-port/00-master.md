# Fil-C DSP Port — Master Plan & Change Log

**Project:** Port Spectrax's DSP/audio engine to fil-c (memory-safe C/C++).
**Scope:** DSP-only — no GUI, no realtime audio. Render-to-WAV POC.
**Branch:** `fil-c-dsp-port` in `~/proj/Spectrax`
**Toolchain:** fil-c 0.681 (pizfix slice, installed system-wide at `/opt/fil-c/`)
**Date opened:** 2026-07-29

## Goal

Prove the audio engine (`oscillator`, `wavetable`, `filters`, `voice`, `modsystem`,
`blit_synth`, `distortion`, `sequencer`, `fft`, `kissfft`, `notes`) compiles and
runs end-to-end under fil-c, producing byte-identical WAV output to the gcc
baseline. Massively expand test coverage along the way.

## Sections (sequenced for minimal coupling)

| # | Section | Status | Notes |
|---|---------|--------|-------|
| 1 | **Foundation & build pipeline** | ✅ done | fil-c-friendly build system, stub main, render-to-WAV skeleton |
| 2 | **Math, notes, FFT** | ✅ done | Pure math layer — notes.c, fft.c, kissfft rebuild. 2 pre-existing bugs found in fft.c window functions (logged). |
| 3 | **Core DSP primitives** | ✅ done | oscillator, wavetable, blit_synth, distortion, filters. 60 new tests, 8 pre-existing bugs logged. |
| 4 | **Voice & modulation** | ⬜ pending | voice.c (envelope), modsystem (LFO routing) |
| 5 | **Sequencing & full render** | ⬜ pending | sequencer.c, preset_io, full pipeline → WAV |

## Plan-change log

> Append-only. Every time the order is reshuffled, a section is split, or a
> discovered issue forces revisiting a previous section, record it here.

(Empty — no pivots yet.)

## Cross-section issues discovered late

> When Section N finds a problem whose root cause was planted in Section M < N,
> note it here with the (M → N) relationship. Used to spot architectural debt
> early.

- **(2 → TBD):** `src/fft.c` has 2 pre-existing bugs (BUG-FFT-1, BUG-FFT-2
  in `02-section-math-fft.md`) in `triangularWindow` and `hammingWindow`.
  Not fil-c related — they're upstream issues. If Section 4/5 ends up
  relying on the visualizer path, fix them then.
- **(3 → 4):** BUG-BLIT-2 and BUG-OSC-1 (unimplemented functions) may
  block Section 4 if `voice.c` references `init_blit`, `blit_synth`,
  `band_limited_sawtooth`, or `band_limited_square`. Need to verify at
  start of Section 4.
- **(3 → 4):** BUG-DIST-1 (fold asymmetry) only matters if voice path
  uses distortion. Verify at Section 4 start.
- **(3 → 4):** BUG-FILT-2 (memory leak in `createFilter` error path)
  only matters if voice code triggers invalid FilterType. Likely fine.
- **(3 → 5):** BUG-WT-1 (wavetable overflow guard off-by-one) — if
  Section 5 loads >128 wavetables during a sequence render, fil-c will
  panic. Verify the test sequence stays under the cap.

## How to read this folder

- `00-master.md` (this file) — overall plan, pivots, cross-section debt
- `01-section-foundation.md` … `05-section-sequencing-render.md` — per-section
  work logs (blockers hit, how they were overcome, files touched, test
  results, fil-c-specific gotchas)
- `tests.md` — test coverage tracker. New tests written per section, subagent
  dispatches, results, gaps remaining.

## Build infrastructure

Native file: `/home/krang/proj/fil-c/filc-native.ini` (reusable for any C project)

```ini
[binaries]
c      = '/usr/local/bin/clang'      # fil-c clang (NOT system clang)
cpp    = '/usr/local/bin/clang++'
ar     = 'ar'                        # fil-c clang can't act as archiver
ranlib = 'ranlib'
strip  = 'strip'

[built-in options]
c_args     = ['-O2', '-g']
cpp_args   = ['-O2', '-g']

[host_machine]
system      = 'linux'
cpu_family  = 'x86_64'
endian      = 'little'
```

## Open questions

- (none yet)