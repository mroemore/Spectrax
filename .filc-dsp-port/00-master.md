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
| 2 | **Math, notes, FFT** | ⬜ pending | Pure math layer — notes.c, fft.c, kissfft rebuild |
| 3 | **Core DSP primitives** | ⬜ pending | oscillator, wavetable, blit_synth, distortion, filters |
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

(Empty.)

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