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
- [ ] **A3 — the control rate in samples.** The global modulation pass currently runs
      once per buffer with `deltaTime = 1/framesPerBuffer`, so the callback's buffer
      size is a synthesis input. The control tick must be a fixed sample count,
      scheduled independently of the buffer size, with a pinned interpolation.
- [ ] **A4 — the compiled modulation plan.** `processModulations` walks every
      parameter slot and every modulator linked list per sample; replace it with a
      flat, dirty-rebuilt array of `(dest, source, op, amount)` tuples and direct
      field access.
- [ ] **A5 — the per-voice PRNG.** The `MT_RND` modulator calls the global `rand()`
      per sample, so a voice's output depends on the order of unrelated work. A
      pinned xorshift32 seeded per entity, reused for the quantizer's dither.
- [ ] **A6 — the strict FP flags and the no-libm lint.** The cross files still set
      `-ffast-math`; drop it, add `-ffp-contract=off`, and extend the lint to the
      whole audio path (`powf` in the envelope curve, `sinf`/`cosf` in the filter
      coefficients, `sin` in the blit synthesiser).
- [ ] **A7 — the golden and scripted verification harness.** A golden SHA-256 over a
      canonical render, plus the corpus's scripted checks: a 4–8× sweep (aliases
      appear as reflected downward streaks), THD+N by the notch method, the impulse
      4-pole decay count, and the white-noise-through-envelope cutoff trajectory.

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

- [ ] **C1 — the device profile's measured layer and the auto-calibration.** Extend
      `devices/*.toml` with the measured per-voice-type costs and auto-calibrate on
      first run, writing to an SD-writable path (the ROCKNIX rootfs is a read-only
      squashfs).
- [ ] **C2 — the budget consumption.** Voices per channel, active channels, the
      permitted voice types, the sample-pool bytes (lazy), and the 128/256/512 buffer
      ladder chosen once at startup.
- [ ] **C3 — the runtime governor.** The callback-time utilisation drives the voice
      slots' grant and withdrawal, with a pinned stealing policy that cannot alter a
      surviving voice's output.
- [ ] **C4 — the voice-type gating.** GRAIN and SPECTRAL cannot make a sound; grey
      them out, and make the granular buffer's allocation lazy.

### Phase D — the C contract and the gate

- [ ] **D1 — the canonical synthesis spec.** The engine rate, the bake rate, the
      tables and interpolation, the control rate and its scheduling, the root-note
      tuning law, the quantizer laws, the filter maths, the PRNG and its seeding, the
      FP flag set, the forbidden-libm list, the buffer-size independence rule.
- [ ] **D2 — the per-voice isolation tests.** Compare the per-voice output buffers,
      **not** differenced mixes: the corpus's later per-channel bus saturation is
      non-linear, so `mix(all) − mix(all−V) ≠ solo(V)`.
- [ ] **D3 — the cross-architecture golden gate.** x86_64, aarch64 and armhf
      (qemu-user), scalar and NEON, wired into the project test gate.

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
currently stands at **51/51 meson tests**, **`meson/check.sh`: coverage + gate PASS**,
and **13/15 scripted fixtures** (the two failures, `chip_meta` and `save_overwrite`,
are pre-existing and confirmed on the pre-consolidation branch).
