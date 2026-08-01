# Test Coverage Tracker

**Project:** Spectrax DSP port under fil-c
**Started:** 2026-07-29

## Existing coverage

Project has a vendored test framework `tests/third_party/utest.h`
(macro-based, like Criterion-lite). Existing test files exist in `tests/`
but they test GUI/interactive code we won't be touching. We'll add new
tests under `tests/dsp/` for the fil-c build.

## Subagent dispatches

| Section | Agent task | Test file(s) | Status | Result |
|---------|-----------|--------------|--------|--------|
| 1 | none — wrote directly | — | n/a | n/a |
| 2 | none — wrote directly (2 small test files; subagent overhead > payoff) | — | n/a | n/a |
| 3 | **dispatched:** 5 parallel subagents (oscillator, wavetable, blit_synth, distortion, filters) | tests/dsp/test_{oscillator,wavetable,blit_synth,distortion,filters}.c | done | 60 tests, 60 pass, 8 pre-existing bugs found |
| 4 | **dispatched:** 2 parallel subagents (modsystem, voice) | tests/dsp/test_{modsystem,voice}.c | done | 53 tests, 53 pass, ~17 pre-existing bugs (incl. CRITICAL double-free) |
| 5 | — | — | — | — |

(Each subagent gets: source files to read, API summary, expected outputs
to verify against, build/test commands, return format.)

## Per-section test counts

| Section | Tests added | Pass | Fail | Skipped |
|---------|-------------|------|------|---------|
| 1 | 2 (test_empty_wav, test_with_samples) | 2 | 0 | 0 |
| 2 | 10 (notes: 4, fft: 6) | 10 | 0 | 0 |
| 3 | 60 (osc: 15, wt: 8, blit: 10, dist: 14, filt: 13) | 60 | 0 | 0 |
| 4 | 53 (mod: 42, voice: 11) | 53 | 0 | 0 |
| 5 | 37 (sequencer: 23, io: 14) | 37 | 0 | 0 |
| **Total** | **162 unit + 1 integration render** | **163** | **0** | **0** |

## Coverage gaps

> Anything that should have a test but doesn't (yet). Capture as we go.

- **Section 3:** `init_blit()` and `blit_synth()` from blit_synth.h are
  unimplemented in `src/blit_synth.c` — tests can't be written until
  someone implements them. Same for `band_limited_sawtooth` /
  `band_limited_square` from oscillator.h. These are pre-existing bugs,
  not a coverage choice.

## Test execution

All DSP tests live in `bin/dsp_tests/` after build. Run with:

```sh
./bin/dsp_tests/test_<name>
```

Each test exits 0 on success, non-zero on failure. Fil-c panics exit 133.