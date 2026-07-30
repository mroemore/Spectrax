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
| 3 | **planned:** 5 parallel subagents (oscillator, wavetable, blit_synth, distortion, filters) | — | not yet | — |
| 4 | — | — | — | — |
| 5 | — | — | — | — |

(Each subagent gets: source files to read, API summary, expected outputs
to verify against, build/test commands, return format.)

## Per-section test counts

| Section | Tests added | Pass | Fail | Skipped |
|---------|-------------|------|------|---------|
| 1 | 2 (test_empty_wav, test_with_samples) | 2 | 0 | 0 |
| 2 | 10 (notes: 4, fft: 6) | 10 | 0 | 0 |
| 3 | — | — | — | — |
| 4 | — | — | — | — |
| 5 | — | — | — | — |

## Coverage gaps

> Anything that should have a test but doesn't (yet). Capture as we go.

(Empty.)

## Test execution

All DSP tests live in `bin/dsp_tests/` after build. Run with:

```sh
./bin/dsp_tests/test_<name>
```

Each test exits 0 on success, non-zero on failure. Fil-c panics exit 133.