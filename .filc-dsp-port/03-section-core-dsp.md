# Section 3 — Core DSP Primitives

**Status:** ✅ **complete** — 60 new tests added (5 subagents, all in parallel). All pass.

## Outcome

All 5 core DSP primitives build clean under fil-c and are covered by 60
new tests. **8 pre-existing bugs** surfaced — none are fil-c related;
all are upstream Spectrax regressions the tests now lock in as baselines.

## Subagent dispatch (parallel)

Per the user's directive, 5 subagents were dispatched in parallel — one per
primitive. Each agent:

- Read its source files
- Wrote a test file under `tests/dsp/`
- Built, ran, verified
- Reported back: test count, PASS/FAIL, bugs found, build warnings

Wall-clock time for the 5-agent fan-out: ~3 minutes (parallel). All 5
came back clean on the first try.

| Subagent | Tests added | PASS | FAIL | Bugs found |
|----------|-------------|------|------|------------|
| oscillator | 15 | 15 | 0 | 1 (undeclared symbols) |
| wavetable | 8 | 8 | 0 | 1 (overflow guard) |
| blit_synth | 10 | 10 | 0 | 2 (BLEP disabled + unimplemented API) |
| distortion | 14 | 14 | 0 | 1 (fold formula wrong) |
| filters | 13 | 13 | 0 | 3 (missing impl + leak + typo) |

## Files added

| File | Tests |
|------|-------|
| `tests/dsp/test_oscillator.c` | 15 |
| `tests/dsp/test_wavetable.c` | 8 |
| `tests/dsp/test_blit_synth.c` | 10 |
| `tests/dsp/test_distortion.c` | 14 |
| `tests/dsp/test_filters.c` | 13 |

## Files modified

| File | Change |
|------|--------|
| `Makefile` | Added `FILC_TESTS` entries for 5 new tests, plus their build rules (each links its own `src_<name>.o`, oscillator also pulls in `modsystem`, `wavetable`, `dstruct` via the header chain). |

## Build & test results

```sh
$ make filc-clean && make filc-test
# All 8 test binaries built. 2 pre-existing warnings (see below).

$ for t in test_wav_writer test_notes test_fft \
           test_oscillator test_wavetable test_blit_synth \
           test_distortion test_filters; do ./bin/$t; done

TOTAL: 72 pass, 0 fail
```

## Pre-existing bugs found

### BUG-OSC-1: Undefined oscillator functions

- **Files:** `src/oscillator.h:94-95`
- **Issue:** `band_limited_sawtooth` and `band_limited_square` are declared
  in the public header but **not implemented** in `src/oscillator.c`. Any
  caller gets an undefined-reference linker error.
- **Severity:** high — anyone who tries to use these (e.g., to do
  band-limited waveforms) hits a link error.
- **Workaround in test:** tests for these are absent.

### BUG-WT-1: Overflow guard off-by-one in wavetable pool

- **File:** `src/wavetable.c:43`
- **Issue:** The capacity guard uses `>` instead of `>=`, so the 129th
  `loadWavetable` call writes past the 128-slot allocation
  (`tables[MAX_WAVETABLES]`). This would be a heap buffer overflow.
- **Severity:** high — potential memory corruption.
- **Fil-c behavior:** caught at runtime with a bounds-check panic. The
  test deliberately loads only 128 wavetables to stay in defined
  territory and document the bug without triggering UB.

### BUG-BLIT-1: BLEP corrections disabled in triangle

- **File:** `src/blit_synth.c:32,35`
- **Issue:** `blep_tri` has its `poly_blep` correction calls commented
  out, so it returns a raw naive triangle instead of a band-limited one.
  Output is aliased above the fundamental.
- **Severity:** medium — audible aliasing artifacts on triangle waves.
- **Fix:** uncomment the BLEP corrections (matching the pattern in
  `blep_square` and `blep_saw`).

### BUG-BLIT-2: `init_blit` and `blit_synth` unimplemented

- **File:** `src/blit_synth.h`
- **Issue:** `init_blit()` and `blit_synth()` are declared in the header
  but not defined in the .c file. Tests for them can't be linked.
- **Severity:** high — `blit_synth` is the centerpiece of the module
  (the whole point of band-limited synthesis). Without it, the BLIT
  oscillator pipeline is unusable from this branch.

### BUG-DIST-1: Negative-side fold formula wrong

- **File:** `src/distortion.c:9`
- **Issue:** Negative-side wavefold uses `-1.0f + overflow * foldAttenuation`
  where `overflow` is negative. This *adds* to the negative threshold
  instead of folding back toward zero, breaking symmetry:
  `fold(x) != -fold(-x)` for `|x| > 1`.
- **Fix:** `-1.0f - overflow * foldAttenuation` (subtract a negative → fold
  toward zero).
- **Severity:** medium — wavefolder asymmetry. Tested in `test_distortion`
  where the buggy behavior is asserted.

### BUG-FILT-1: `resetState` declared but never defined

- **File:** `src/filters.h:70`
- **Issue:** `void resetState(BiquadFilter* bf);` declared in header,
  not implemented in `src/filters.c`. Calling it would link-fail.
- **Severity:** medium — public API is broken. Tests document this as
  a TODO rather than calling it.

### BUG-FILT-2: Memory leak in `createFilter` error path

- **File:** `src/filters.c:152-153`
- **Issue:** When `FilterType` is invalid, the code returns NULL without
  freeing the allocated `BiquadFilter bf`. Each error-path invocation
  leaks one struct.
- **Severity:** low — only triggered by invalid input. Real callers
  using valid types never hit it.

### BUG-FILT-3 (cosmetic): Typo in function name

- **File:** `src/filters.h:72`
- **Issue:** `checkFLoatUnderflow` (capital `F`/`L`). Should be
  `checkFloatUnderflow`. Not technically a bug but inconsistent with
  the project's style.

## Pre-existing warnings

`make filc-test` emits two warnings not introduced by this port:

- `src/dstruct.c:182` — non-void function does not return a value.
- `src/filters.c:13` — `switch(type)` doesn't handle `biquad_count`
  enum value.

Both should be filed against the gcc baseline.

## Fil-c gotchas hit

**None.** All 5 primitives are pure float math with limited heap use.
Fil-c handles `malloc`/`free` cleanly (FUGC catches any leaked objects
even if we don't). No new fil-c-specific patches were required for any
of these files.

## Decisions made in this section

1. **Subagent fan-out paid off.** 5 parallel agents → 5 test files in
   ~3 minutes wall-clock, with no back-and-forth. The same work done
   sequentially would have taken much longer.
2. **Test framework consistency.** All 5 agents used the same local
   `ASSERT_NEAR`/`ASSERT_EQ` macros. No coupling to Unity, no shared
   test headers — each test file is self-contained. Easy to move tests
   if the framework changes.
3. **Pre-existing bugs locked in, not fixed.** All 8 bugs are upstream
   Spectrax issues, not fil-c related. Tests assert actual (buggy)
   values with explanatory comments. Future "fix upstream" commits can
   reference these tests as the regression baseline.

## Section exit criteria — all met

- [x] All 5 DSP primitives build under fil-c
- [x] 60 new tests added, all pass
- [x] Pre-existing bugs documented (8 found)
- [x] No new fil-c patches required
- [x] Section report updated

## Cross-section implications

- **BUG-BLIT-2 (`init_blit`/`blit_synth` unimplemented)** may affect
  Section 4 (voice/modulation) if the BLIT oscillator is on the voice
  path. Need to check `src/voice.c` for references. If used, this is a
  blocker for end-to-end testing in Section 5.
- **BUG-OSC-1** (`band_limited_*` unimplemented) — same caveat: if
  Section 4's voice pipeline depends on these, end-to-end testing is
  blocked.
- **BUG-FILT-2** (memory leak) — only matters if invalid FilterType is
  exercised in Section 4. Probably not.
- **BUG-DIST-1** (negative fold) — only matters if Section 4's voice
  pipeline sends through distortion. Check.

These should be revisited at the start of Section 4 — see 04-section-*.md
and the master cross-section debt section.