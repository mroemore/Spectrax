# The canonical synthesis spec

**Date:** 2026-11-03
**Branch:** `pattern-mod`
**Status:** current. Every rule here is enforced by a test; the file names
the test where one exists.

This is the reference the engine must match. It exists because criterion C —
*a voice's output is a pure function of its settings, never of the device,
the architecture, the voice count or the load* — is not a property that
falls out of writing careful code. It has to be pinned down, and then
checked.

The golden render (`tests/dsp/test_golden_render.c`) and the voice
isolation test (`tests/dsp/test_voice_isolation.c`) are the checks; this
document is what they check against.

## 1. The engine's rate

`SPECTRAX_ENGINE_RATE` = **48000**, a compile-time constant in
`src/settings.h`. `PA_SR` is that constant.

Every synthesis timebase derives from it and nothing else:

| What | Where |
|---|---|
| the FM operator's phase increment | `oscillator.c` (`sine_op`) |
| the voice's phase increment | `audio.c` |
| the modulation pass's `deltaTime` | `audio.c` |
| the granular read's rate | `voice.c` |
| the sequencer's step cadence | `sequencer.c` (`intBpmToSamplesPerStep`) |

The device's own rate is `g_deviceRate` (`src/devicerate.c`) and is a
**boundary fact only**. The stream open requests 48000 and rejects a
candidate that negotiated anything else, because PortAudio substitutes
silently (asking the RG35XX SP's `hw:0,0` for 48000 gets 192000) and the
engine's output would then arrive at the speaker at the wrong speed.

No file on the synthesis path may name `g_deviceRate`.
*Checked by* `tests/dsp/test_engine_rate.c`, which also greps the
synthesis sources for the name.

## 2. The floating-point rules

- `-ffp-contract=off` everywhere (the host build and both cross
  templates). FMA contraction is the one thing that would break equality
  silently: the compiler may fuse `a*b+c` on a machine that has an FMA and
  not on one that does not.
- `-ffast-math` nowhere. It permits reassociation, which breaks the
  invariant far worse.
- Elementwise auto-vectorisation is allowed: under strict FP, reassociation
  is forbidden, so an elementwise vectorised expression produces the same
  bits as the scalar one.

## 3. The canonical math

`src/canonical_math.{c,h}` replaces the inexact libm transcendentals.
Each is a fixed sequence of IEEE-754 operations (all correctly rounded) or
a table built by a fixed double-precision series, so the results are
bit-identical on every platform.

| Function | How |
|---|---|
| `canonicalSin(phase)` | `sinTab`: a 1024-entry table with linear interpolation, built by a 12-term Taylor series. The phase is in **cycles**, and the wrap is exact. |
| `canonicalCos(phase)` | `canonicalSin(phase + 0.25)` — the same table, so the two agree bit for bit. |
| `canonicalExp(x)` | `2^k · exp(r)` with `k = round(x/ln2)`, a 12-term series for `r`. |
| `canonicalLog(x)` | `x = m·2^e`, then `ln(m) = 2·atanh((m-1)/(m+1))`. |
| `canonicalPow(x, y)` | `exp(y·log(x))`, defined for `x ≥ 0`. |
| `canonicalOnePoleCoef(fc, sr)` | `1 - exp(-2π·fc/sr)`, computed in double. |

Two documented differences from libm: `canonicalExp` flushes to zero below
`exp(-87)` instead of returning a subnormal (a subnormal is where NEON's
flush-to-zero and x86's denormals-are-zero disagree), and it saturates at
`FLT_MAX` above `exp(88)`, which overflows a float.

The **exact** libm functions are not replaced: `floor`, `fabs`, `trunc`,
`copysign`, `fmin`, `fmax` and `sqrt` are exactly representable, so every
correct implementation returns the same bits for them.

Nothing on the audio path — nor in the bake, whose output the audio path
reads — may call an inexact transcendental.
*Checked by* `tests/dsp/test_canonical_math.c`, whose lint covers
`audio.c`, `voice.c`, `oscillator.c`, `modsystem.c`, `filters.c`,
`blit_synth.c`, `sample.c`, `sequencer.c`, `wavetable.c`, `notes.c`,
`canonical_math.c`, `sintab.c` and `sampler_character.c`.

## 4. The random source

`CanonicalRng` — a xorshift32 with a **per-entity** state. Three shifts
and three xors, all exact.

The global `rand()` is not usable on the audio path: its state is
process-global, so a voice's random values would shift with every other
`rand()` call anywhere in the program, and glibc and musl give different
sequences.

- Every modulation source seeds its own generator with a fixed constant
  (`0x9E3779B9`), so two identically built sources produce the same
  sequence.
- A voice's clone of a source starts from the source's generator state, so
  every voice of an instrument draws the same sequence — independent of how
  many voices there are.
- The granular processor's grain positions have their own seeded generator.
- The bake's TPDF dither has its own state, local to the bake and seeded
  with a fixed constant, so the same source always bakes the same way and
  two bakes never share a pattern.

*Checked by* `tests/dsp/test_canonical_math.c`: two identically built
modulators produce the same sequence with the global RNG hammered in
between.

## 5. The modulation pass

`processModulations(paramList, modList, deltaTime)` runs once per sample
per voice, with `deltaTime = 1/48000`.

It does two things:

1. **The sources.** Each source in the mod list advances its state and
   generates its value.
2. **The destinations.** Each parameter that is *modulated* has its
   modulator chain applied to its base value, and the result written to it.
   Each parameter that is a **source's output** is refreshed from its base
   value — a source that stops generating (an envelope that has run out)
   leaves its last value behind, and the refresh is what clears it.

A parameter that is neither is skipped: its value is its base value, and
`setParameterBaseValue` keeps the two in step. When a parameter loses its
last modulator, the disconnection resets it to its base value — the
modulation pass no longer re-derives it.

*Checked by* `tests/dsp/test_modsystem.c` and `tests/dsp/test_mod_voice.c`.

## 6. The control rate and the buffer size

The callback's buffer size is a **latency knob**, not a parameter of the
sound.

- The sequencer's step boundary is checked **per sample**, so a note fires
  on the sample it belongs to. (Checking it once per buffer made the note
  fire at the next buffer's start, up to a whole buffer late.)
- The song-level modulation pass runs **per sample**, like the voices' own.
  (It used to run once per buffer with `deltaTime = 1/framesPerBuffer`,
  which made the song-level sources advance by a buffer's worth of time at
  a time.)

*Checked by* the golden render, which drives the real callback at 128, 256
and 512 frames and requires one hash.

## 7. The sampler's chain

A sampler's character is a chain, not a bit depth: degrade **once** at
record time, then pitch at playback.

**The bake** (`bakeSamplerCharacter`): a band-limited resample (a
windowed sinc with the cutoff just below the target's Nyquist, 32 taps,
mirrored edges) followed by the coding law. It happens at load time, so
neither stage costs anything per output sample.

**The coding laws**: `SQL_OFF` (exact pass-through), `SQL_TRUNC`
(truncation toward negative infinity — the vintage error profile, with its
signal-correlated harmonics and its −Δ/2 bias), `SQL_ROUND` (half away
from zero), `SQL_TPDF` (triangular-PDF dither), `SQL_ULAW` (8-bit mu-law,
G.711), `SQL_COMPAND12` (12-bit companded — an approximation of the
MPC60's storage, whose curve has no public definition).

**The read path**: drop-sample (the SP-1200's actual tuning algorithm — an
integer-address accumulator whose fractional increment is truncated when it
indexes, so a sample may repeat on a pitch-down and never stalls) or
linear (the varispeed class). Both honour the loop region and fall silent
at the end when they are not looping.

**The intended tuning** — the advance, in baked frames per output frame:

```
advance = 2^((note - root)/12) · tune · (bakedRate / engineRate)
```

The note and the root are both counted from C4 (C4 is 0, A4 is 9). At
unity — the note played is the root note, tune 1, and the baked rate equal
to the engine's — the advance is exactly **1.0**: one baked frame per
output frame, and the sample sounds at its recorded pitch.

The advance it replaced (`phaseIncrement · PA_SR · 2 · srcBit / srcRate`)
made the pitch depend on the device's audio rate *and* on the sample's bit
depth and channel count: at 16-bit stereo it put a sample 64× too fast.
Parity with that formula was refused — it can only be preserved by
preserving an error.

**The filter**: a CEM3320/SSM2040-class OTA cascade — four one-poles in a
global feedback loop, no `tanh`, so no oversampling is needed. Its
coefficients come from `canonicalOnePoleCoef`, not `expf`.

*Checked by* `tests/dsp/test_sampler_character.c` (the laws, the bake, the
profiles, the read path, the tuning law, and an end-to-end proof that a
440 Hz recording played at A4 with the root at A4 comes out at 440 Hz).

## 8. The isolation of the voices

A voice's output does not depend on the other voices. The voices are
summed linearly, and nothing feeds back.

*Checked by* `tests/dsp/test_voice_isolation.c`: the same scene rendered
with four voices and with one, and the lone voice's samples compared bit
for bit.

Note that the check must compare the voice's own stream, not a difference
of mixes: `(a + b) - a` is not `b` in floating point.

## 9. What is checked, and where

| Property | Test |
|---|---|
| the engine's rate is a constant, and nothing on the audio path reads the device's | `test_engine_rate` |
| no inexact libm on the audio path | `test_canonical_math` |
| the canonical maths are accurate and reproducible | `test_canonical_math` |
| the random source is per-entity | `test_canonical_math` |
| the golden render, and the cross-architecture equality | `test_golden_render` (run under qemu by `test_qemu_smoke`) |
| the buffer size does not change the output | `test_golden_render` |
| a voice does not hear the others | `test_voice_isolation` |
| the sampler's chain and its tuning law | `test_sampler_character` |
| the modulation pass's semantics | `test_modsystem`, `test_mod_voice` |

## 10. Open

- The boundary resampler: a device that cannot run at 48000 is currently
  refused by the stream open rather than resampled.
- The sampler's OTA filter: the `filter`, `cutoff` and `res` parameters
  exist and are wired to a profile, but the cascade itself is the GUI
  phase's task.
- The pattern-track editor's 166 untested lines in `src/gui_pattern.c`,
  which is why its coverage floor sits at 40.
