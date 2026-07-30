# Section 3 — Core DSP Primitives

**Goal:** Build all single-buffer DSP generators and shapers. This is the
most numerically dense section.

## Scope

- `src/oscillator.c|h` — sine, saw, square, triangle, noise generators.
- `src/wavetable.c|h` — table lookup with interpolation.
- `src/blit_synth.c|h` — band-limited impulse train.
- `src/distortion.c|h` — waveshaping / wavefolding.
- `src/filters.c|h` — filter implementations (state-variable, etc.).
- Tests for each.

## Approach

1. Read each source file's public API (header).
2. Write a test that drives each function with known inputs and checks
   output within tolerance against a reference (computed via python or
   by hand for a small buffer).
3. Use the test framework already in the project (looks like
   `tests/third_party/utest.h` from Section 1).
4. Build under fil-c, run tests.

## Expected fil-c gotchas

- `oscillator.c` likely uses `<math.h>` — `sinf`, `cosf`, `powf`. Should work.
- `blit_synth` might use `expf` heavily — same.
- `filters.c` does state mutation — pointer aliasing rules apply, but fil-c
  allows that.
- Potential: NaN/Inf handling — fil-c's floating point is IEEE-754,
  no special semantics for these.

## Tests added

- `test_oscillator` — sine at sample 0 = 0, period matches expected.
- `test_wavetable` — known table, known index → known interpolated value.
- `test_blit_synth` — first sample non-zero, energy in band-limited range.
- `test_distortion` — identity range (small input → small output).
- `test_filters` — impulse response length matches pole count.

## Subagent plan

This section is a natural place to fan out tests via subagents — each DSP
primitive is independent. Plan:
- Dispatch one subagent per primitive (5 in parallel)
- Each subagent: read source, write test, build, report back

## Status

- [ ] API surface mapped
- [ ] Subagents dispatched for tests
- [ ] All tests pass under fil-c
- [ ] Reference outputs cross-checked
- [ ] Report file updated

## Blocker log

(Empty.)