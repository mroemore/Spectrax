---
plan_schema_version: 2.1
plan_id: 2026-11-02-adaptive-arm-handheld
title: Canonical DSP core, sampler rebuild, and adaptive quality for ARM handhelds
maturity_level: M2
status: in_progress
repo_snapshot_ref: pattern-mod @ afcca5b
generated_by: plan_builder
updated_at: 2026-11-03T00:00:00Z
recommended_plan_path: docs/superpowers/plans/2026-11-02-adaptive-arm-handheld.md
---

# Canonical DSP core, sampler rebuild, and adaptive quality

The design this plan implements: `docs/superpowers/specs/2026-11-02-adaptive-arm-handheld-design.md`.

The sampler's own plan and its ledger:
`docs/superpowers/plans/2026-10-09-sampler-emulation-engine.md` and
`docs/superpowers/plans/2026-10-09-sampler-emulation-engine-progress.md`.

## Base

`pattern-mod` @ `afcca5b`. The branch carries main's CTOL testing stack, the ARM
handheld work (`rg35xxsp-tooling`), the sampler's Phase 1, and this plan's landed
work. `pattern-mod` was 25 commits behind `main` before the consolidation; that is
done.

## Phases

### Phase A — the canonical core (A + C)

- [x] **A1 — the global canonical engine rate.** `SPECTRAX_ENGINE_RATE` (48000) is a
      compile-time constant; `SAMPLE_RATE`/`g_sampleRate` are gone; the device rate
      became `g_deviceRate` (a boundary fact); the stream open requests 48000 and
      rejects a substituted rate. `66a2cd6`. **Deferred: the boundary resampler**, so
      a device that cannot run at 48 kHz is refused rather than resampled.
- [x] **A2 — the canonical math module.** `src/canonical_math.{c,h}`: `canonicalSin`,
      `canonicalCos`, `canonicalExp`, `canonicalLog`, `canonicalPow`,
      `canonicalOnePoleCoef`, all fixed sequences of correctly-rounded IEEE-754
      operations; the sine table rebuilt from a double-precision Taylor series.
      `0c2e075`.
- [x] **A3 — the control rate in samples.** Two places made the buffer size a
      synthesis input: the global modulation pass ran once per buffer with
      `deltaTime = 1/framesPerBuffer`, and the sequencer's step advance was
      checked per buffer, so a note fired at a buffer boundary. Both now happen
      per sample. The proof is the golden render through the real callback at
      128, 256 and 512 frames producing one hash. `5d93b5f`.
- [x] **A4 — the modulation plan's cost.** The pass no longer re-derives the
      parameters that nothing modulates: the measured cost falls by 29-75%
      depending on the route count (a graph with no routes: 0.130 ms ->
      0.033 ms; 128 routes: 2.852 ms -> 2.017 ms). Two things had to be true
      for the skip to be sound, and the existing tests caught both: a source's
      output must still be refreshed, and a parameter that loses its last
      modulator must go back to its base value. `f506a22`.
      *The flat tuple array was not needed: the walk's cost was the
      re-derivation, not the data structure.*
- [x] **A5 — the per-voice PRNG.** The `MT_RND` modulator's global `rand()` is
      replaced by a per-entity xorshift32 (`CanonicalRng`): every modulator seeds
      its own, a voice's clone starts from the source's state, and the granular
      processor's grain positions have their own. `ca2e86c`.
- [x] **A6 — the strict FP flags and the no-libm lint.** The audio path's
      remaining inexact calls are gone (`powf` in the attenuator's square root, in
      `applyCurve` and in the curve table; `expf` in the envelope's slew;
      `sinf`/`cosf` in the biquad's coefficients; `sin` in the BLEP's sine and the
      granular window), and the lint covers the whole audio path plus the bake.
      `-ffast-math` is gone everywhere; `-ffp-contract=off` replaces it.
      `f17ece6`.
- [~] **A7 — the golden and scripted verification harness.** The golden half is
      done: `tests/dsp/test_golden_render.c` renders a canonical scene headlessly
      and hashes it, the hash is compared exactly against a recorded value, and
      the qemu smoke test runs the same test under qemu-aarch64 — **the host and
      the ARM build produce the same hash**. The corpus's scripted checks (the
      sweep alias, the THD+N notch, the impulse decay, the cutoff trajectory) are
      the sampler's *filter's* checks, so they belong with the OTA cascade in the
      GUI phase. `6798e1a`.

### Phase B — the sampler rebuild

The sampler's DSP tasks 1–5 and task 6's integration have landed; the OTA filter and
the GUI are the GUI phase. See the sampler's ledger for the per-task record.

- [x] The quantizer laws, the bake, the machine profiles, the read path with the
      intended tuning, the chain's parameters, the bake cache, and preset V4.
      `beb6d9b`, `802ecbc`, `1d19765`, `bd69c5f`.
- [x] The voice reads the bake; the sample plays at its written pitch. `efae3b0`.
- [x] The review's findings: the bake's lock, two leaks, the dither's global state,
      the note-on reset, the loop region's two disagreeing interpolators, the
      coverage floors, the contract tests' file lists. `afcca5b`.
- [ ] The OTA cascade filter behind the CUTOFF/RES dials — the GUI phase.

### Phase C — the adaptive quality (B)

- [x] **C1 — the device's budget, measured on the device.** `src/device_budget.{c,h}`:
      the derivation (voices from a measured cost per voice-sample and the
      callback's deadline; the pool's ceiling from the available memory; the
      buffer size from the two) and the profile (a versioned JSON file in the
      user's config directory). The app measures itself on its first run -- the
      app's own first instrument, one voice, through the voice-level path, timed
      -- and writes the profile. `9308852`.
- [x] **C2 — the budget consumption.** The channel count and the voice count are
      capped before the voice manager and the arranger are built; the buffer
      size is the stream open's first choice. The permitted voice types are
      enforced by the engine (`setInstrumentVoiceType` refuses a type the
      device does not offer), and the sample pool's ceiling is the budget's
      number -- the pool no longer mallocs a fixed 32 MB arena up front, it
      grows as samples arrive. `015c36f`.
- [ ] **C3 — the runtime governor.** The callback-time utilisation drives the voice
      slots' grant and withdrawal, with a pinned stealing policy that cannot alter a
      surviving voice's output.
- [ ] **C4 — the voice-type gating.** GRAIN and SPECTRAL cannot make a sound; grey
      them out, and make the granular buffer's allocation lazy.

### The review's findings

A whole-branch review found two Critical and four Important problems, all
now fixed (`ff562e9`): the stream was opened with the buffer size in
PortAudio's *rate* slot (both are numbers, so it compiled — and the app
then exited, because every candidate failed the rate check); the
governor's ceiling was pinned at boot, so the VOICES dial did nothing; the
pattern's velocities were never initialised, so an older file loaded over a
session with velocities left them in place (a criterion-C violation); the
device profile was loaded without clamping, so a profile saying zero voices
left the pool empty and the allocator handed out a voice slot that was never
allocated; the tempo's step durations were a data race between the audio
thread's per-sample read and the GUI's write (now atomic — the tsan rung of
the matrix fails without the fix); and the sample-pool test never actually
forced the arena to grow.

The stream's opening now lives in `src/audio_device.{c,h}`, where a test can
ask a real device what it negotiated.

### Phase D — the C contract and the gate

- [x] **D1 — the canonical synthesis spec.** `docs/superpowers/specs/2026-11-03-canonical-synthesis-spec.md`:
      the engine rate, the canonical maths, the FP rules, the random source, the
      modulation pass, the control rate and the buffer size, the sampler's chain
      and its tuning law, the isolation of the voices -- each with the test that
      checks it. `10416b2`.
- [x] **D2 — the per-voice isolation tests.** `tests/dsp/test_voice_isolation.c`
      compares the voice's own stream, **not** differenced mixes: (a + b) - a is
      not b in floating point. Each render gets its own scene, because a voice's
      state advances as it is rendered. `62c919a`.
- [x] **D3 — the cross-architecture golden gate.** The golden render runs under
      qemu-aarch64 in the test suite and matches the host's; and the gate now
      also builds the target *without the vectorizer* and requires the same
      hash -- the vectorized build has 6619 vector instructions in the golden
      test and the scalar one has none, and they agree bit for bit. The gate
      was verified by breaking it. The armhf leg stays deferred (the Miyoo
      Mini is best-effort, much later, by the human partner's decision).
      `cc93944`.

### Phase E — the sequencer velocity

- [ ] **E1 — the note velocity into the voice.** Currently absent (only
      `grainVelocity` exists), and it is what the MPC60 profile's
      velocity-to-start-address needs.

## Acceptance criteria

- AC1. The golden hashes are equal across x86_64 / aarch64 / armhf, scalar / NEON.
- AC2. The per-voice isolation holds for every voice type.
- AC3. The sampler's pitch is correct and transposable, and identical across devices.
- AC4. On each target class a maximal song runs without xruns and degrades only by
      voice allocation.
- AC5. The sequencer's timing is within ±1% of real time on an unpaced ALSA device.
- AC6. No forbidden libm symbol is reachable from the audio path.
- AC7. The output is bit-identical across the 128 / 256 / 512 buffer ladder.
- AC8. The corpus's scripted checks (the sweep alias, the THD+N notch, the impulse
      decay, the cutoff trajectory) pass and are assertable.

## Rulings made so far

See the sampler's ledger for the sampler's own rulings. The ones that bind the whole
plan:

1. **C = device-invariant per-voice math**, in its strong form.
2. **A and C are one workstream**; B reduces to resource budgets.
3. **The S2 core is deterministic float + wavetables**, not fixed-point (parked as
   session note #133, with escalation if the cross-arch gate cannot be pinned).
4. **The intended tuning replaces the tuning parity** (D13) — the legacy ratio is
   deleted from the sampler path.
5. **The interpolator is a character axis**, not a quality upgrade: drop-sample and
   linear are dialable, and a fixed cubic default is rejected.
6. **The filter is an OTA cascade**, not a biquad; the ladder is a droppable tier.
7. **The engine rate is global**, 48 kHz; the device rate is reconciled at the
   boundary.
8. **The control rate is pinned in samples** and scheduled independently of the
   callback size.

## The evidence for each phase

Each phase is evidenced by harness numbers, not narrative: the meson suite's count,
the coverage gate's verdict, and the scripted fixtures' results. The landed work
currently stands at **59/59 meson tests**, **`meson/check.sh`: coverage + gate PASS**,
and **13/15 scripted fixtures** (the two failures, `chip_meta` and `save_overwrite`,
are pre-existing and confirmed on the pre-consolidation branch). The golden
render's hash is `efb6e5e8a5e51c85`, identical on the x86-64 host and under
qemu-aarch64.
