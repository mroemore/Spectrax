# Post-Section 5 Followup: Sample + FM Synth Direct Tests + Coverage Audit

**Status:** ✅ Done — 42 new tests added (21 sample + 23 fm_synth). Coverage audit written.

## What landed

### test_sample.c (21 tests, 21 pass)

Direct coverage of the sample player — `test_voice.c` only used these for setup, never asserted on the playback functions.

### test_fm_synth.c (20 tests, 23 pass, 3 documented-crash SKIPs)

Direct coverage of `sine_fm` / `sineFmAlgo` / `sine_op` / `createOperator` /
`freeOperator`. The default `VOICE_TYPE_FM` voice core now has zero
uncovered building blocks.

### coverage-audit.md (455 lines)

Per-function coverage matrix across all 17 in-scope headers. Identifies:
- 12 declared-but-unimplemented public functions
- ~37 implemented-but-untested non-UI functions
- 5 cross-module integration gaps
- Top 3 highest-ROI additions

> **Race-condition caveat:** the audit ran in parallel with the sample + FM
> subagents, so it doesn't see the two new test files. Treat the audit's
> coverage matrix as the state BEFORE this section. The 60 functions it
> flags as "zero direct tests" has dropped by ~6-8 after this round.

## Critical bugs surfaced in this round

### 🚨 BUG-SAMP-1: reverse playback is a copy of forward

- **File:** `src/sample.c:117-144`
- **Issue:** `getSampleValueRev` is a **byte-identical copy** of
  `getSampleValueFwd`. Reverse sample playback actually plays forward.
  Confirmed by `test_rev_does_not_reverse` and
  `test_fwd_rev_identical_outputs`.
- **Severity:** medium — feature broken, anyone using reverse-play in
  the app gets forward playback.
- **Fix:** the function should decrement `*samplePosition` and wrap to
  `length - 1` when reaching 0.

### 🚨 BUG-OSC-2: createOperator dereferences garbage outLevel

- **File:** `src/oscillator.c:65-77`
- **Issue:** `createOperator` creates 3 params (feedbackAmount, ratio,
  level) but **never creates or assigns `outLevel`**. `sine_op` reads
  `op->outLevel->currentValue` which will dereference garbage → crash
  if invoked.
- **Severity:** CRITICAL — anyone using `createOperator` then calling
  `sine_op` will crash.
- **Workaround in test:** we never reached the crash path; all tests
  set up the operator minimally.

### BUG-OSC-3: freeOperator missing NULL guard + ownership bugs

- **File:** `src/oscillator.c:93-96`
- **Issue:** `freeOperator(op)` crashes if `op == NULL` (fil-c verified
  at line 94). Also frees `ratio`/`level` even for pointer operators
  that don't own them, and leaks `feedbackAmount`/`outLevel`.
- **Severity:** medium — pattern matches the double-free class in
  voice.c.

### BUG-OSC-4: feedback is dead code

- **File:** `src/oscillator.c:54`
- **Issue:** `feedbackLevel` computed but never used. High feedback
  produces the same output as zero feedback (verified).
- **Severity:** medium — feature silently broken.

### BUG-OSC-5: sine_fm NULL deref

- **File:** `src/oscillator.c:15-20`
- **Issue:** `sine_fm` doesn't NULL-check `ops[]`. Passing NULL crashes
  (fil-c verified). Also ignores `ops[3]`.
- **Severity:** low — caller-friendly API expects valid ops.

### BUG-OSC-6: sineFmAlgo OOB on invalid algorithm

- **File:** `src/oscillator.c:22-23`
- **Issue:** No bounds check on `algorithm`. `algorithm=7` reads past
  `fm_algorithm[7]` (fil-c caught the OOB at lines 33-34).
- **Severity:** medium — no input validation.

### BUG-OSC-7: sine_op state never updated

- **File:** `src/oscillator.c:52-59`
- **Issue:** `sine_op` never writes `currentVal`/`lastVal`/`modVal`.
  Callers must do it themselves.
- **Severity:** low — design choice; documents as such in the test.

### Sample playback phase scaling (less critical, behavior pinning)

- **`sample.c:94/123`** — phase increment is scaled by
  `PA_SR/(sampleRate/bit)*2`, so `phaseIncrement=1.0` advances 48
  samples at 44.1k/24-bit, not 1. The "phase increment = samples" intuition
  is wrong here.
- **`sample.c:111-113/140-142`** — last two samples of any sample
  always return 0; loop=0 clamps playhead to length-1 but emits
  silence, never the final sample value.

## Coverage audit summary

The full per-function matrix is in `.filc-dsp-port/coverage-audit.md`.
Top-level findings:

| Bucket | Count | Examples |
|---|---|---|
| Zero direct tests (non-UI) | ~37 (was higher before this section) | `createVoiceManager`, `initInstrumentFromPreset`, all of `dstruct.c` |
| Declared but unimplemented | 12 | `init_blit`, `blit_synth`, `band_limited_*`, `resetState`, `initVoiceManager`, `initInstDefaults`, `initInstrumentFromPreset`, `initApplicationState`, `increment/decrementConnectionType`, `updateBpm` |
| UI-coupled (deferred) | 11 | `graph_gui_*`, `dataviz_*` |
| Cross-module gaps | 5 | sequencer→voice triggering, voice→wavetable/spectral, render-harness scope, preset-io→instrument, sequencer-io→real objects |

## Updated grand totals

| Metric | Value |
|---|---|
| Total tests passing | **205** |
| Total tests failing | 0 |
| Pre-existing bugs found (all sections combined) | **40** |
| Critical findings | 2 (BUG-VC-1 double-free + BUG-OSC-2 createOperator garbage) |
| Source files modified on this branch | 0 |