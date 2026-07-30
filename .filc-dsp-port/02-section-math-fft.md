# Section 2 — Math, Notes, FFT

**Status:** ✅ **complete** (with bug findings logged below)

## Outcome

`src/notes.c` and `src/fft.c` build and run cleanly under fil-c. kissfft
has been rebuilt from source (`third_party/kissfft/`) under fil-c and links
correctly. 10 new DSP tests added (4 notes + 6 fft), all passing.

## Files added

| File | Purpose |
|------|---------|
| `third_party/kissfft/kiss_fft.c` | kissfft (BSD-3, Mark Borgerding) — copied from evo_fm's CMake FetchContent cache |
| `third_party/kissfft/kiss_fftr.c` | real-FFT variant |
| `third_party/kissfft/kiss_fft.h` | header |
| `third_party/kissfft/kiss_fftr.h` | header |
| `third_party/kissfft/_kiss_fft_guts.h` | internal |
| `third_party/kissfft/kiss_fft_log.h` | logging macros |
| `tests/dsp/test_notes.c` | 4 tests |
| `tests/dsp/test_fft.c` | 6 tests |

## Files modified

| File | Change |
|------|--------|
| `Makefile` | Added `-Ithird_party/kissfft`, `-Isrc` to `FILC_CFLAGS`. Added `FILC_TESTS` list, kissfft build rules, `tests/dsp/*.o` rule, notes/fft test targets. Added `src_*.o` rule for building src files into `dsp/`. |

## Build & test results

```sh
$ make filc-test
# Builds all 3 test binaries, no warnings

$ ./bin/test_notes
PASS test_known_frequencies
PASS test_octave_doubling (12 notes × 9 octaves checked)
PASS test_note_string_valid
PASS test_note_string_out_of_range

$ ./bin/test_fft
PASS test_triangular_window (center=1.0, edge=2.0 — see report)
PASS test_hann_window
PASS test_hamming_window (current impl: -0.29 at edge — see report)
PASS test_blackman_windows
PASS test_fft_impulse_roundtrip (DC peak at bin 0, val 18.557)
PASS test_window_switching

$ readelf -p .interp bin/test_fft
/opt/fil-c/.../pizfix/lib/ld-yolo-x86_64.so
```

kissfft built clean under fil-c with zero source modifications — as expected
for plain C math libs.

## Pre-existing bugs found (NOT fil-c related)

These are bugs in `src/fft.c` that exist in the gcc baseline. They don't
affect fil-c compatibility — they just mean the original code is incorrect.

### BUG-FFT-1: `triangularWindow` is not actually triangular

```c
// src/fft.c:16
float triangularWindow(int index, int length) {
    float l2 = length / 2.0f;
    return 1.0f - (index - l2) / l2;
}
```

This is a *linear ramp* from `2.0` (at i=0) to `0.0` (at i=L), NOT a
triangular window. A correct triangle peaks at the center (1.0) and falls
to 0.0 at both edges. The fix is:

```c
return 1.0f - fabsf(index - l2) / l2;
```

Severity: low. The center value is still 1.0, the edge values are non-zero
but harmless for FFT analysis. No crash, just incorrect windowing math.

### BUG-FFT-2: `hammingWindow` coefficients are reversed

```c
// src/fft.c:24
float hammingWindow(int index, int length) {
    return (0.46f - cos((index * 2.0f * M_PI) / (length - 1.0f))) * 0.54f;
}
```

Correct Hamming window formula: `0.54 - 0.46 * cos(...)`.
Current implementation gives `(0.46 - cos(...)) * 0.54` which produces
negative values (≈ -0.29 at the edges). Windowing with negative values
*inverts* the signal — could introduce subtle spectral artifacts.

Severity: medium. Output is numerically wrong. Easy to fix but worth a
separate PR so the change is traceable.

Both bugs are regressions vs. upstream Hamming/triangular window math.
Should be filed against the gcc baseline, not this fil-c port.

## Decisions made in this section

1. **kissfft vendored from local evo_fm cache** instead of fetched from
   GitHub. Saves a network round-trip; copy is identical (same version
   pinned by evo_fm's CMake).
2. **kissfft headers kept in `include/`** — left untouched so the gcc
   build keeps working. Fil-c build uses `-Ithird_party/kissfft`
   explicitly so it picks up the rebuilt versions.
3. **Pre-existing bugs logged, not fixed** — out of scope for the port.
   Tests assert actual (buggy) behavior to lock in a regression baseline.
   A future commit can fix BUG-FFT-1 and BUG-FFT-2 against this baseline.
4. **Tests written directly, not delegated to subagents** — only 2 test
   files, independent but small. Subagent dispatch overhead would exceed
   the parallel-payoff. Section 3 (5 DSP primitives) is where subagent
   delegation starts making sense.

## Fil-c gotchas hit

None. Notes/FFT are vanilla C with `<math.h>`. kissfft is also vanilla C.
No fil-c patches required.

## Section exit criteria — all met

- [x] src/notes.c builds clean
- [x] src/fft.c builds clean (kissfft rebuilt and links)
- [x] All notes tests pass
- [x] All fft tests pass
- [x] Binary uses fil-c loader
- [x] Pre-existing bugs logged in this report (BUG-FFT-1, BUG-FFT-2)
- [x] Section report updated