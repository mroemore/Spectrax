# Spectrax pattern step-sequencer mod source — design

Date: 2026-08-31

## Problem

Modulation sources today are continuous (envelope, LFO, random). There is
no way to sequence a stepped modulation value against the song. The user
wants **pattern step-sequencers**: mod sources whose output is a sequence
of step values, clocked by the song, with controllable transition shapes.
These appear as ordinary sources in the unified mod-sources framework
(spec `2026-08-31-mod-sources-routing-design.md`) — type selector, route
button, delete button — and are assignable to any dialed destination.

## A. PatternMod data model

New `MT_PATTERN` mod type:

```c
#define MAX_PATTERN_STEPS 16

typedef struct {
    Mod base;
    Parameter *length;    // 1..16 (int param)
    Parameter *shape;     // SH_HOLD / SH_LINEAR / SH_SLEW / SH_CURVE
    Parameter *slew;      // glide time, 0..1 (SLEW mode)
    Parameter *polarity;  // PP_UNIPOLAR (0..1) / PP_BIPOLAR (-1..1)
    float steps[MAX_PATTERN_STEPS];   // stored 0..1
    int stepCount;
    int currentStep;
    float currentValue;   // the output value
    float stepProgress;   // 0..1 within the current song step
    const int *stepPtr;   // -> arranger->playhead_indices[channel]
    const float *stepDurationPtr; // -> current step duration in samples
} PatternMod;
```

- Steps are stored 0..1 internally; the output is scaled by polarity
  (unipolar: the raw step; bipolar: `step * 2 - 1`).
- The mod's base `output` Parameter is created like other sources.

### Clock — tempo-synced 1:1

- `stepPtr` points at the channel's `arranger->playhead_indices[channel]`
  (read-only; the audio thread already reads these). Wired at source
  creation.
- Advance (per-buffer, in the mod system's advance pass):
  - If `*stepPtr != lastStep` a step boundary was crossed: capture the new
    target (`steps[currentStep]`), reset `stepProgress = 0`, remember the
    previous output value as the shape's starting point.
  - `currentStep = (*stepPtr) % stepCount`; `stepProgress += deltaTime /
    *stepDurationPtr` (clamped to 1.0). When the song is stopped the
    playhead holds, so the pattern holds its output.
- The step duration pointer is the channel's current even/odd step duration
  (swing-aware) as the audio callback computes it; read-only.

### Shaping

The generate fn produces `currentValue` from the target + shape:
- **HOLD**: `currentValue = target`.
- **LINEAR**: `currentValue = mix(startValue, target, stepProgress)`.
- **CURVE**: `currentValue = mix(startValue, target, ease(stepProgress))`
  (smoothstep).
- **SLEW**: one-pole glide toward `target` with a time constant from the
  `slew` param; independent of `stepProgress` (the slew rate wins).

## B. Entry UI (in the unified mod-sources list)

The pattern entry row shows, after the type selector:
- The **step grid**: 16 cells (the active `length` are filled); the
  current song-step highlights as the playing step. Each cell's height
  maps its value (0..1) so the sequence is visible at a glance.
- **LENGTH** dial (1..16), **SHAPE** dial (HOLD/LINEAR/SLEW/CURVE),
  **SLEW** dial (0..1, only meaningful in SLEW mode), **POLARITY** dial
  (UNI/BIPOL).
- **Route button** (bottom right) + **Delete button** (top right) — the
  #3 machinery unchanged.
- **Step editing**: navigate to a step cell (the entry's nav moves across
  the 16 cells), KM_EDIT enters edit mode, arrows adjust the cell value
  (±0.1, clamped 0..1), KM_SELECT exits edit mode. (Same interaction
  grammar as the arcade name/preset inputs.)

## C. Lifecycle

- `addRuntimePattern(inst, channel)` creates a default pattern source
  (16 steps of 0.5, HOLD, unipolar, length 16), registered in
  `inst->modList` + `inst->paramList`, with the `rebuilding` guard.
- Delete: the #3 delete button + confirm modal → `removeMod` (the new type
  gets a cleanup case in `removeMod`/`cleanupModSystem` for the pattern
  params + no extra owned memory).
- The type selector (ENV→LFO→RND→PTN) via #3's `changeModType`; a pattern
  keeps its routes + output on a type change (its step data is discarded).
- **Persistence**: runtime-only for now — pattern sources are NOT stored in
  presets or the song file (consistent with runtime envelopes).

## D. Testing

- `tests/dsp/test_modsystem.c`: `MT_PATTERN` — boundary detect advances
  `currentStep` on a playhead change; hold/linear/curve/slew produce the
  expected outputs (drive `*stepPtr` + `*stepDurationPtr` directly);
  polarity scaling (unipolar keeps 0..1, bipolar maps to −1..1); length
  wraps (`step % length`); `changeModType` PTN→ENV→LFO keeps routes.
- `tests/dsp/test_mod_voice.c`: `addRuntimePattern` on a live instrument +
  delete round-trip.
- Harness scripted fixture: add a pattern source; set length/shape/polarity;
  edit step values; route it to a destination dial; verify the audio-path
  route + the grid highlight + the source entry rendering.
- Gate: `ninja` clean, `meson test` 8/8, both existing fixtures PASS, app
  boots.

## E. Out of scope

- Pattern persistence in presets / the song file.
- A pattern-source editor beyond the single row (e.g. full-screen step
  editor) — the row grid is the v1 UI.
- Step "division" or tempo multipliers (1:1 only for now).
- Chains / sequences of patterns (one pattern source = one sequence).