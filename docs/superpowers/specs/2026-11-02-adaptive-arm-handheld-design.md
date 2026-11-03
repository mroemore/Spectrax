# Adaptive-quality Spectrax for low-power ARM Linux handhelds — design

**Date:** 2026-11-02
**Branch:** `pattern-mod`
**Status:** in progress — Phase A (the canonical core) partly landed; the sampler's
DSP tasks landed; the GUI phase not started.

## The three criteria

The work exists to meet three criteria on the target class (GKD Pixel 2, Miyoo Mini,
RG35XX SP — ~1–1.5 GHz ARM with NEON, 128 MB–1 GB RAM, usually no X server):

- **A** — efficient DSP, so a rich experience on most compatible devices.
- **B** — voice/effect/sample budgets chosen at runtime from on-device measurement.
- **C** — identical audio output per voice given identical settings.

C was resolved to its **strong form**: a voice's samples are a pure function of its
settings and the song; the voice count, the channel count and the device never alter
them.

That resolution collapses the problem. If no device may take a synthesis shortcut, the
cheap DSP has to be the *only* DSP — chosen once, used everywhere, desktop included.
So **A and C are one workstream** (a canonical core), and **B shrinks to resource
budgets**, which is safe precisely because changing a count cannot change a voice's
sound.

## The shape of the answer

### One canonical engine rate

`SPECTRAX_ENGINE_RATE` (48000) is a compile-time constant in `src/settings.h`. Every
synthesis timebase derives from it: the FM operator's phase, the voice's phase
increment, the modulation clock, the granular read, the sequencer's step cadence.

The device's own rate is a boundary fact (`g_deviceRate`, `src/devicerate.c`). The
stream open **requests** 48000 and **rejects** a candidate that negotiated anything
else, because PortAudio substitutes silently (asking the RG35XX SP's `hw:0,0` for
48000 gets 192000) and the engine's output would arrive at the speaker at the wrong
speed.

A device that cannot run at 48 kHz is currently **refused**, not resampled. The
boundary resampler is the remaining half of this decision.

### One canonical math module

The inexact libm transcendentals (`sinf`, `cosf`, `expf`, `logf`, `powf`) make a
voice's output depend on the platform's libm: glibc's and musl's differ by up to an
ULP, and glibc's own differ between its x86-64 and aarch64 implementations.
`src/canonical_math.{c,h}` replaces them with fixed sequences of IEEE-754 operations
(all correctly rounded), so the results are bit-identical everywhere.

The exact libm calls are **not** replaced: `floor`, `fabs`, `trunc`, `copysign`,
`fmin`, `fmax` and `sqrt` are exactly representable, so every correct implementation
returns the same bits.

The bake (below) is under the same invariant, because its output is read by the audio
path: a libc-dependent bake is a libc-dependent voice.

### One canonical sampler character chain

A sampler's character is a chain, not a bit depth: degrade **once** at record time
(rate → bits → coding law), then pitch at playback by reading the degraded buffer.
Degrading after the pitch shift is the standard way to get it wrong.

The chain is: **bake** → **read** (drop-sample or linear) → **filter** → (bus
saturation → room, in the corpus's later waves).

- **The bake** is a band-limited resample plus the coding law, at load time. The
  crudeness comes from the truncation and the drop-sample reads, not from a sloppy
  bake — which is why the filter is band-limited properly.
- **The read** is either drop-sample (the SP-1200's actual tuning algorithm: an
  integer-address accumulator whose fractional increment is truncated when it
  indexes, so a sample may repeat on a pitch-down and never stalls) or linear
  (the varispeed class).
- **The filter** is a CEM3320/SSM2040-class OTA cascade — four one-poles in a global
  feedback loop, no `tanh`, so no oversampling is needed. The transistor ladder
  (Wave 3) does need 2× oversampling, so it is a droppable tier rather than the
  default.

### The intended tuning (D13)

The advance is in baked frames per output frame:

```
advance = 2^((note - root)/12) · tune · (bakedRate / engineRate)
```

At unity — the note played is the root note, tune 1, and the baked rate equal to the
engine's — the advance is exactly **1.0**, so the sample sounds at its recorded
pitch. The note and the root are both counted from C4 (C4 is 0, A4 is 9).

This replaced the plan's "tuning parity is an invariant" rule, which pinned the
legacy `2·PA_SR·bit/srcRate` ratio. That ratio made the pitch depend on the device's
audio rate *and* on the sample's bit depth and channel count — at 16-bit stereo it
put a sample 64× too fast. Parity could only be preserved by preserving an error, so
it is gone from the sampler path.

### Budgets, not tiers

B's levers are all output-neutral:

| Lever | Notes |
|---|---|
| Voice slots per channel | refuse allocation / steal |
| Simultaneously active channels | |
| Permitted voice types | the spectral (FFT) type is droppable, and is in fact non-functional today |
| Sample-pool byte cap | the eager 32 MB `malloc` becomes lazy |
| Callback buffer size (128/256/512) | **output-neutral** once the control rate is scheduled in samples |
| Effects per voice | |
| GUI frame rate, spectrogram | not audio |

Excluded, because they would break C: the internal sample rate, the modulation rate,
the wavetable size, the FFT size, the filter topology.

## The invariants

- **C-invariant** — no synthesis operation may depend on the device, the
  architecture, the voice count, the channel count or the load. Budgets may add or
  remove whole voices, channels and effects only.
- **Canonical-rate invariant** — all synthesis runs at one global engine rate
  (48000). The device rate is a boundary concern; a device profile may only *request*
  it.
- **Control-rate invariant** — the modulation control tick is a fixed number of
  samples, scheduled independently of the callback buffer size. The buffer size is
  never a synthesis input.
- **No-libm invariant** — nothing on the audio path, nor in the bake, may call an
  inexact libm transcendental.
- **Determinism invariant** — all audio-path RNG is per-entity, with a pinned
  algorithm and seed.
- **Pinned-constant invariant** — the engine rate, `MOD_BLOCK`, the table sizes, the
  interpolation, the PRNG and the tuning law are compile-time constants, never
  runtime-polled.
- **Bake/character separation** — the bake rate, the word, the coding law and the
  interpolator are *character* parameters, pinned per profile and recorded in the
  canonical spec, never selected by device capability.
- **Lock invariant** — the audio lock discipline (instrument values are constant
  within a locked buffer) is what makes the per-buffer graph sync legal.

## Where the work stands

Landed (see `docs/superpowers/plans/2026-11-02-adaptive-arm-handheld.md` for the
subtask list and the evidence):

- The engine rate as a constant; the device rate demoted to a boundary fact; the
  stream open rejecting substituted rates.
- The canonical math module, and the sine table rebuilt from a fixed Taylor series.
- The sampler's quantizer laws, bake, machine profiles, read path with the intended
  tuning, the chain's eight instrument parameters, the bake cache, and the preset
  format's V4 with a V3 migration.
- The sampler voice reading the bake — the sample now plays at its written pitch.

Not yet landed:

- The control rate scheduled in samples (so the buffer size is output-neutral).
- The compiled modulation plan (the per-sample walk over every parameter).
- The per-voice PRNG (the `MT_RND` modulator still calls the global `rand()`).
- The audio path's remaining libm calls (`powf` in the envelope curve, `sinf`/`cosf`
  in the filter coefficients, `sin` in the blit synthesiser).
- The cross-architecture golden gate, and the scripted corpus checks (sweep alias,
  THD+N notch, impulse decay, cutoff trajectory).
- The boundary resampler.
- The adaptive budgets and the runtime governor.
- The pattern-track editor's unit tests (166 untested lines in `src/gui_pattern.c`).

## The GUI phase

Not started, and by agreement it gets a brainstorm and mockups before any code. It
covers: the character panel (PROFILE / LAW / INTERP / BITS / RATE / FILTER / CUTOFF /
RES / TUNE / ROOT), the waveform preview using the intended advance, the loop points
and playback modes made live, the voice-type gating for the non-functional GRAIN and
SPECTRAL types, and the OTA filter behind its dials.

## Sources

- The sampler's research corpus: the appik article `samplers-emulation-and-mechanisms`
  (id 23), and its inlined form in
  `docs/superpowers/plans/2026-10-09-sampler-emulation-engine.md`.
- The measured device facts: `docs/superpowers/plans/2026-10-22-rg35xxsp-tooling.md`
  and `devices/*.toml`.
