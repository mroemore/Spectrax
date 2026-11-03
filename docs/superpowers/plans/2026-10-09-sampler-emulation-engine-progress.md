# SDD ledger — plan: docs/superpowers/plans/2026-10-09-sampler-emulation-engine.md

Plan: the Sampler Character Engine (8 tasks + 4 waves).
Spec: the appik research corpus (article id 23, `samplers-emulation-and-mechanisms`),
reachable at https://appk.lynsand.tricky.click/articles/samplers-emulation-and-mechanisms/.
The plan inlines the load-bearing facts as "Grounded facts (inlined spec)", so rulings
made against them are not provisional.

## Session scope

This session ran the canonical-math task the plan's Task 6 needs, then the plan's
**DSP tasks 1-5 and Task 6's integration**. **Task 6's OTA filter and Task 7 (the
character panel) are the GUI phase** — the human partner wants that brainstormed with
mockups before it is built, so it stops here.

Commits (branch `pattern-mod`, base 7032ed5):
`0c2e075` canonical math · `66a2cd6` the engine's rate is a constant ·
`beb6d9b` quantizer laws · `802ecbc` bake + profiles · `1d19765` read path + intended
tuning · `bd69c5f` chain params + preset V4 · `efae3b0` the voice reads the bake ·
`afcca5b` the review's findings.

## Amendments from the canonical-core plan (approved by the human partner)

- **D13 (confirmed 2026-11-02): drop tuning parity; define intended tuning.**
  The plan's Task 4 Decision 3 ("tuning parity is an invariant") pins the legacy
  `2*PA_SR*bit/srcRate` ratio, whose value depends on the device's audio rate and
  on the sample's bit depth and channel count. That is incompatible with criterion C.
  Replaced by: the advance is `2^((note-root)/12) * tune * (bakedRate/engineRate)`,
  exactly 1.0 at unity, with the note and the root both counted from C4.
- **Canonical engine rate (2026-11-02):** `SPECTRAX_ENGINE_RATE` (48000) is a
  compile-time constant. `PA_SR` is that constant. No synthesis path may read a
  runtime rate. The bake rate is a *character* axis, not the engine rate.
- **No-libm invariant:** nothing on the audio path, nor in the bake (whose output
  the audio path reads), may call an inexact libm transcendental. Exact libm calls
  (floor, fabs, trunc, copysign, sqrt, min/max) are IEEE-754 exact and stay.

## Pre-flight: shared interfaces

| Producer | Consumer | What one produces vs what the other consumes | Finding |
|---|---|---|---|
| A2 (canonical math) | T1 (quantizer) | `canonicalLog` for mu-law | needed |
| A2 | T2 (bake) | `canonicalSin` for the sinc kernel, `canonicalCos` for the Hann window | needed |
| A2 | T6 (OTA) | `canonicalExp` for the one-pole coefficient | needed — **still unbuilt** |
| T1 (quantizer laws) | T2 (bake) | `samplerQuantize(x, bits, law, ditherState)`, `SamplerQuantLaw` | the dither's state was added here (see the ruling) |
| T2 (bake) | T4 (read path) | `BakedSample {data, length, rate, srcBit, bits, law, src, srcLength}` | matches |
| T3 (profiles) | T4, T5 | `SamplerInterp`, `SamplerProfile`, `samplerProfileConfig` | matches |
| T4 (read path) | T5 (params) | `samplerAdvance`, `getBakedSampleDrop/Linear` | **the advance's signature was amended by D13** |
| T4, T5 | T6 (integration) | the baked read path + the chain's parameters | matches, plus one parameter (root) |

## Rulings

- **Task 4: Ruling:** the plan's `test_advance_parity` asserts the legacy ratio and
  therefore contradicts D13. Replaced with the intended-tuning invariant. The legacy
  ratio is deleted from the sampler path. — *Why:* the human partner confirmed D13
  explicitly; parity can only be preserved by preserving an error. — *Cost if wrong:*
  existing sampler content shifts pitch; it already plays at 64x, so it is unusable
  either way.
- **Task 5: Ruling:** the plan adds 7 instrument parameters; D13 needs an 8th
  (`root`). The root is counted from C4, so zero means C4 — which is what a zeroed
  preset gives, and what a fresh instrument wants. — *Cost if wrong:* one parameter
  to remove, and the GUI's ROOT dial would need re-labelling.
- **Task 6: Ruling:** the plan's OTA cascade computes its coefficient with `expf`
  twice per sample, which is an inexact libm call in the audio path. The coefficients
  come from A2's canonical exp instead. — *Why:* the no-libm invariant. — *Cost if
  wrong:* a slower coefficient path.
- **Task 2: Ruling:** the plan's bake uses `sinc`/`hann` from libm. The bake's output
  is read by the audio path, so a libc-dependent bake is a libc-dependent voice. The
  kernel and the window are built on A2's canonical sine. — *Cost if wrong:* a
  slightly different (still band-limited) bake.
- **Task 1: Ruling:** the plan's mu-law law needs `logf`; it uses A2's canonical log.
  — *Cost if wrong:* a slightly different companding curve.
- **Plan-wide: Ruling:** the plan's line references were written against
  `sampler-character-engine` (= main + 1). Every cited location was re-read against
  the consolidated tree before editing. — *Cost if wrong:* an edit lands in the wrong
  place; the tests catch it.
- **Task 1: Ruling:** the plan's absolute error bounds for the three non-linear laws
  (TPDF <= Δ, ULAW <= 1/128, COMPAND12 <= 1/2048) are written as if those laws were
  linear. A mu-law grid's top step is 4.4% of full scale, not 1/128, and the TPDF
  dither's peak-to-peak is two steps, so its error reaches 1.5 steps — a smaller
  dither could not decorrelate the error at all, since an input on a grid point would
  keep its zero error. Each law is bounded by its own grid's top step instead, and the
  companding property is asserted as it behaves: the absolute error at a low level is
  far below the one at a high level. The linear laws keep the plan's bounds exactly.
  — *Cost if wrong:* the bounds are looser or tighter than the plan asked for.
- **Task 2: Ruling:** the bake uses 32 taps, not the plan's 16. At 16 the transition
  band is wide enough that the 18 kHz component the band-limit test measures is only
  about -20 dB down, not the -40 dB the corpus's headroom discipline requires. The
  bake is a load-time operation, so the taps cost nothing per output sample. — *Cost
  if wrong:* a slower load.
- **Task 2: Ruling:** the plan's residual expectation for the truncating bake was
  Delta/sqrt(12), which is the *rounding* error's RMS. Truncation's error spans a
  whole step rather than half of one, so its RMS is Delta/sqrt(3) — twice as large.
  The plan's factor of 2.0 is kept. — *Cost if wrong:* the bound is twice as loose.
- **Task 2: Ruling:** the bake's edges mirror rather than replicate. A replicated edge
  is a step, and the filter rang off it hard enough to overshoot the source's own
  range by 5% on the first samples — a click at the sample's onset. Mirroring is
  continuous, so the ringing stays inside the signal (the worst overshoot is now 1%,
  on one sample). — *Cost if wrong:* a different edge transient.
- **Task 3: Ruling:** CLEAN's rate is 0, meaning "keep the source's own rate", rather
  than a fixed 44100. A fixed rate would resample every 48 kHz source for no reason,
  which is the opposite of what a pass-through profile is for. — *Cost if wrong:* the
  CLEAN profile's rate dial shows a number instead of "auto".
- **Task 6: Ruling:** the interpolator's enum has linear at zero, so a preset that
  predates the chain keeps the linear read it always had. The enum's order is part
  of the preset format, so it is fixed before the format ships. — *Cost if wrong:* the
  preset format's field values shift by one.
- **Task 6: Ruling:** a preset field outside its parameter's legal range means the
  preset never carried it — a zeroed boot preset, or a file from before the field
  existed. Clamping those to the range's edge gave a chain that was coherent but not
  neutral (a tuning trim of a quarter, and a loop that ended one frame in, which
  silenced the voice), so the apply path falls back to the parameter's own default.
  — *Cost if wrong:* an old preset's out-of-range value is silently replaced.
- **Task 1: Ruling (from the review):** the TPDF dither's state is the caller's, not
  the module's. A module-global state made the dither depend on how many
  quantizations had happened before: the same source baked twice came out different
  (230 of 512 frames differed), which is criterion C's "the output must not depend on
  the order of unrelated work" broken. The bake owns a word seeded with a fixed
  constant. — *Cost if wrong:* the dither is not reproducible across bakes.

## Review findings and their disposition

A whole-branch review (a fresh-context reviewer over 7032ed5..efae3b0) found 1
Critical, 8 Important and 11 Minor items. Fixed in `afcca5b`:

- **Critical — the bake freed without the audio lock** (the sample dial's onChange
  path). Fixed: the rebuild takes the lock, like every other rebuild path.
- **Important — two leaks** (applyInstrumentPreset orphaned the old bake;
  freeInstrument never freed it). Fixed.
- **Important — the TPDF dither's global state.** Fixed (see the ruling above).
- **Important — the drop read's state was not reset on note-on.** Fixed, and only for
  a sampler voice: the voice's type data is a union, and writing the sampler's
  fields for an FM voice lands on the operator array.
- **Important — the two interpolators disagreed about the loop region** (the linear
  read skipped the head). Fixed: both play the head and wrap into the region.
- **Important — the coverage ratchet was looser** (the new modules had no floors).
  Fixed: `sampler_character.c` 66, `canonical_math.c` 51, `sintab.c` 85.
- **Minor — the contract tests did not scan the branch's own new files; the bake
  cache's key lacked the source's length; the bake rate's ceiling was 48000 while a
  source may be at 96000; the V3 migration did not re-save the file; the note
  numbering was documented three ways; canonicalExp's overflow clamp and the bake's
  window were documented wrongly; the branch's diff was line-ending churn in two
  headers.** All fixed.

Deferred, with the reason:

- **Important — Task 6's OTA filter is absent**, so `filter`, `cutoff` and `res` are
  inert and `canonicalPow`/`canonicalOnePoleCoEF` have no product caller. This is
  the GUI phase's own task (the panel binds those dials), and it needs the GUI
  design first.
- **Important — the no-libm invariant is not yet met on the audio path**: `powf` in
  modsystem.c's envelope curve, `sinf`/`cosf` in filters.c's coefficient maths, and
  `sin` in blit_synth.c. These are pre-existing and are the canonical-core phase's
  audio-path rewiring, not the sampler's.
- **Important — `rand()` on the audio path** (modsystem.c's MT_RND modulator). Also
  pre-existing; the canonical-core phase's per-voice PRNG task.
- **Minor — the bench's rate sweep is cosmetic** (the engine renders at the constant
  rate; the sweep now only moves the deadline). Partly fixed: the misleading comment
  is corrected. The sweep's semantics are the benchmark's own follow-up.
- **Minor — `bakedMatches` is dead API.** It now compares the source's length; it is
  still only used by the test. It stays until something calls it.

## Remaining work (not in this session)

1. **The GUI phase** — brainstorm with mockups first, then: the character panel
   (PROFILE/LAW/INTERP/BITS/RATE/FILTER/CUTOFF/RES/TUNE/ROOT), the waveform
   preview using the intended advance, the loop points and playback modes made live,
   the voice-type gating for the non-functional GRAIN and SPECTRAL, and the OTA
   filter behind the filter dials.
2. **The audio-path canonicalization** — modsystem.c's `powf`, filters.c's
   `sinf`/`cosf`, blit_synth.c's `sin`, and the MT_RND modulator's `rand()`.
3. **The cross-architecture golden gate** — the hash of a canonical render across
   x86_64 / aarch64 / armhf and scalar / NEON. The pieces it needs (the canonical
   math, the engine-rate contract test) are in place; the golden render itself is
   not.
4. **The boundary resampler** — a device that cannot run at 48000 is currently
   refused by the stream open rather than resampled.
5. **The pattern-track editor's unit tests** — 166 untested lines in
   src/gui_pattern.c, which is why its coverage floor was re-baselined to 40.

## Task log

- Task A2 (canonical math): complete (`0c2e075`).
- Task 1 (quantizer laws): complete (`beb6d9b`).
- Task 2 (bake): complete (`802ecbc`).
- Task 3 (machine profiles): complete (`802ecbc`).
- Task 4 (read path + intended tuning): complete (`1d19765`).
- Task 5 (chain params + preset V4): complete (`bd69c5f`).
- Task 6 (OTA cascade + voice integration): **integration complete** (`efae3b0`); the
  OTA cascade is deferred to the GUI phase.
- Task 7 (character panel) and Task 8 (coverage + fixtures): the GUI phase — not
  started, by the human partner's instruction.

---

## Continuation (2026-11-03): the canonical core's remaining tasks

The plan's own task log continues above; this is the second session's
record, on the canonical-core plan (docs/superpowers/plans/2026-11-02-adaptive-arm-handheld.md).

Commits (branch `pattern-mod`):
`b15d703` the plan and design on the repo's convention ·
`f17ece6` the last libm calls off the audio path, and strict FP everywhere ·
`ca2e86c` the random modulator gets its own generator ·
`6798e1a` the golden render.

### Done

- **A6 — the strict FP flags and the no-libm lint.** The audio path's remaining
  inexact calls are gone: `powf` in the attenuator's square root, in `applyCurve`
  and in the curve table; `expf` in the envelope's slew; `sinf`/`cosf` in the
  biquad's coefficients; `sin` in the BLEP's sine and the granular window. The
  lint's file list now covers the whole audio path plus the bake.
  `-ffast-math` is gone from the host build, both cross templates, both device
  profiles and the vendored kissfft build; `-ffp-contract=off` replaces it.
- **A5 — the per-voice PRNG.** The `MT_RND` modulator's global `rand()` is
  replaced by a per-entity xorshift32 (`CanonicalRng`): every modulator seeds its
  own, a voice's clone starts from the source's state, and the granular
  processor's grain positions have their own.
- **A7 (the golden half) — the golden render.** `tests/dsp/test_golden_render.c`
  renders a canonical scene headlessly and hashes the samples; the hash is
  compared exactly against `tests/golden/voice.render.hash`. The qemu smoke test
  cross-compiles it and runs it under qemu-aarch64, so the host and the ARM
  build are compared by the suite: **they match** (`efb6e5e8a5e51c85`). The test
  renders twice and requires equal hashes, so a render that depended on
  uninitialised memory would fail (meson runs the suite with a random
  MALLOC_PERTURB_).

### Rulings

- **A6: Ruling:** the strict-FP flag is added to the *whole project*, not to the
  audio path's files. The audio path's determinism cannot depend on which
  translation unit a function landed in, and a file-scoped flag would let a
  change in a header silently relax it. — *Why:* the invariant is about the
  output, not about the files. — *Cost if wrong:* a couple of files lose an
  FMA contraction they could have had.
- **A7: Ruling:** the golden comparison is exact, and the hash is FNV-1a rather
  than SHA-256. The comparison's purpose is to catch a difference, not to
  withstand an adversary, and a strong hash would be a dependency the tree does
  not otherwise have. — *Why:* the gate's value is in being *run*, and a
  dependency is a thing that can be missing on a device. — *Cost if wrong:* a
  collision could hide a difference; a 64-bit hash over 64 kB makes that
  vanishingly unlikely for an accidental change.

### What A3 (the control rate) actually needs — the analysis, for the next session

The buffer size is currently a synthesis input in three places, not one:

1. **The global modulation pass** runs once per buffer with
   `deltaTime = 1.0f / framesPerBuffer` (audio.c). The song-level parameters it
   writes are the instruments' base values, which the voices then sync — so the
   value a global mod produces depends on the buffer size.
2. **The sequencer's step advance** happens when
   `tempoSettings.samplesElapsed >= stepSamples`, checked once per buffer. The
   note therefore fires at a *buffer boundary*, up to a whole buffer late, so the
   onset moves with the buffer size.
3. **The voice's modulation pass** is already per sample, with the canonical
   rate — so it is already buffer-size neutral.

So A3 is two changes, not one:
- the global mod pass must run on a fixed control tick, scheduled from an
  *absolute* sample position (a per-buffer position would make the tick's phase
  depend on the buffer size, which is the same bug one level down);
- the render loop must check for a step boundary *per sample*, not per buffer, so
  the note's onset lands on the sample it belongs to.

The test that proves it: the golden render, driven through
`renderAudioBuffer` at 128, 256 and 512 frames, must produce the same hash. That
needs the full `paTestData`, so the golden test grows a second entry point
rather than a new binary.

### Still outstanding

- A3 (above), A4 (the compiled modulation plan).
- A7's second half: the corpus's scripted checks — the sweep alias, the THD+N
  notch, the impulse 4-pole decay, the white-noise-through-envelope cutoff
  trajectory — all of which are the *sampler's filter*'s checks, so they belong
  with the OTA cascade in the GUI phase.
- C1-C4 (the device profile's measured layer, the auto-calibration, the budgets,
  the governor, the voice-type gating), D1-D3 (the canonical spec, the isolation
  tests, the gate — the gate's hash half is done), E1 (the sequencer velocity).
- The boundary resampler: a device that cannot run at 48000 is refused by the
  stream open rather than resampled.
- The pattern-track editor's unit tests (166 untested lines in gui_pattern.c).
