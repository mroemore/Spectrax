# Unified Mod-Voice Architecture

Date: 2026-09-06
Branch: `mod-sources-ux`

## Problem

Three coupled deficiencies in the mod/voice architecture:

1. **Mod types are not interchangeable.** `LFO`, `Random`, `Envelope` are separate
   structs that each *embed* `Mod base`. A mod is identified by its struct, not its
   `type` field, and `changeModType` frees the old struct and mallocs a fresh one of
   the target type. Nothing about this is realtime-safe or space-efficient.

2. **Voices point raw pointers back into instrument modulators.** `initialize_voice`
   aliases instrument envelope stage params into per-voice envelopes
   (`createParamPointerAD`, voice.c:263-272) and instrument FM operator
   feedback/ratio/level into the voice's operators (`createParamPointerOperator`,
   voice.c:293-296). Any source retype/delete frees those params while the voice still
   derefs them — a latent UAF whose only existing protection is "every freeing path
   must remember to rebuild voices."

3. **Voice gain/pitch routing is invisible and hard-coded.** The per-voice envelope
   wiring (`env0 → volume`, `env0 → outLevel[0..3]`, BLEP `env1 → frequency`) lives
   only inside `initialize_voice`. It cannot be seen, rerouted, re-amounted, or
   re-typed, which is exactly what the default modulators must be able to do.

## Goals

- Mod types become **realtime interchangeable**: `Mod` carries all data capacity for
  every mod type in a named union; `changeModType` swaps the active payload in place.
- **All modulators are per-voice.** The instrument graph becomes a control-plane
  blueprint; each voice runs a full copy of the graph with per-voice state, so every
  modulation is polyphonic (each note sweeps its own envelope, its own LFO phase,
  etc.). Voice envelope progress is per-voice (explicitly confirmed).
- **No raw parameter pointers from voices into instrument mods.** Live modulation is
  preserved via a per-buffer base-value sync, not aliasing.
- **Voice gain/pitch/outLevel routing becomes first-class, visible, reroutable
  ModConnections**, seeded with sensible defaults per instrument type.
- **LFO (and RND) get a play mode**: `FREERUN` (default, no response to trigger) or
  `RETRIGGER` (phase resets on note-on). Envelopes always retrigger per note.

## Part 1 — `Mod` as a tagged union

### Struct shape

```c
typedef struct EnvState {
    EnvelopeStage stages[MAX_ENVELOPE_STAGES];
    int currentStageIndex;
    int stageCount;
    float currentTime;
    float totalElapsedTime;
    float currentLevel;
    bool isTriggered;
    bool isSustaining;
    bool loop;
} EnvState;

typedef struct LfoState {
    Parameter *rate;
    Parameter *phase;
    Parameter *shape;     /* routable: onChange syncs shapeValue + base.generate */
    int shapeValue;
    int playMode;         /* 0 = FREERUN, 1 = RETRIGGER (Part 4) */
} LfoState;

typedef struct RndState {
    Parameter *rate;
    Parameter *phase;
    float lastPhase;
    float lastRandom;
    Parameter *shape;
    int shapeValue;
    int playMode;
} RndState;

typedef struct AttenState {
    struct Mod *input;          /* real upstream mod feeding this node */
    Parameter *attenAmount;
    Parameter *attenPolarity;
    Parameter *attenCurve;
} AttenState;

typedef struct Mod {
    ModType type;
    Parameter *output;          /* stable across retype */
    char name[MAX_NAME_LEN];
    int dependency_count;
    bool processed;
    bool visiting;
    ModGenerate generate;
    union {
        EnvState env;
        LfoState lfo;
        RndState rnd;
        AttenState atten;
    } data;                     /* named union; size = largest payload */
} Mod;
```

- `LFO`, `Random`, `Envelope` struct types are removed; their bodies become the named
  union members. All `LFO*`/`Random*`/`Envelope*` returns and casts migrate to `Mod*`
  with `mod->data.lfo` / `mod->data.env` / `mod->data.rnd` access.
- `ModPreset` already uses the same union shape (`md.env`/`md.lfo`/`md.rand`); the
  data-level precedent exists.
- The `MT_ATTEN` fields move from scattered Mod fields into `data.atten`.

### In-place `changeModType`

- Finds the mod's slot in `modList` (unchanged).
- Frees the OLD payload's params from `paramList` (envelope stage durations/curvatures,
  LFO/RND rate/phase/shape) exactly as today.
- Inits the NEW payload onto the **same** `Mod` (no malloc/free of the Mod itself),
  reusing `output` (Parameter*) and `name`. Connections survive for free because
  `ModConnection.source` is a `Mod*` that never changes.
- The UI's route overlay, picker routed-state, and any connection reads stay valid
  across the swap.

### Migration surface

Every `(Envelope*)mod` / `LFO *l = (LFO*)mod` / `Random *r = (Random*)mod` site moves
to `mod->data.*`: modsystem generators (`generateEnvelope`, `generateLFO`,
`generateRandom`, attenuator), the creator/init functions, voice.c, gui_inst_mod.c
(AD dial builders read `inst->envelopes[i]->stages[...]`), vizfx, preset io, and all
tests. Mechanical but wide — this is why Part 1 is its own gated phase.

## Part 2 — Voice-driver destinations and default connections

### New / promoted destination params (all in `inst->paramList`, all shared FM targets)

- `gain` — **new** instrument param (base 1.0, MUL semantics) representing the
  per-voice gain path (stands in for the current `env0 → volume` wiring).
- `outLevel` — the param already exists per operator (oscillator.h); **promoted** into
  the picker's routable dest list.
- `pitch` — **new** per-op param: continuous additive frequency offset in Hz, range
  ±1000, fine steps (0.1), coarse 1.0. Wired into `sine_op`: `phase_inc =
  (fundamental × ratio + pitch) / SAMPLE_RATE`.
- `feedback`, `ratio`, `level` — already per-op destinations; unchanged.

### Seeded default connections (created at instrument init, under the audio lock)

- All types: `source[0] → gain` (MUL, amount 1.0).
- FM: additionally `source[0] → outLevel[0..3]` (MUL, amount 1.0).
- BLEP: `source[1] → pitch` (ADD, amount 400.5 — matching today's hard-coded
  `env1 → frequency` value).
- Per-op `pitch`: routable destination, **no** default seed.
- SAMPLE / GRAIN / SPECTRAL: `source[0] → gain` only.

These are real `ModConnection`s: the ROUTE overlay draws them, the picker lists
`gain`/`outLevel`/`pitch` as destinations, and they can be cleared/rerouted/re-amounted
exactly like user-made routes. "By default" means only the initial seeded state — once
visible and editable they behave like any other connection. Deleting the gain source
removes the connection (flat gain) unless the user reroutes; there is **no** hidden
positional fallback after seeding.

## Part 3 — Per-voice graph copy (the voice side)

### Control-plane vs audio

- The **instrument** mod graph is the control-plane blueprint: the sources the user
  edits, the connections they route, the op params that hold preset/base values. It is
  what gets serialized to presets.
- The **audio** side is entirely per-voice. Each voice owns a full copy of the graph
  and runs it itself.

### What each voice owns (all created in `initialize_voice`)

1. **Destination params** — `frequency`, `volume`, and per-op `feedback/ratio/level/
   outLevel` become the voice's OWN params (no `createParamPointerOperator` aliasing).
2. **Source mods** — one per-voice clone of every instrument source. A clone is the
   same union'd `Mod` with its own payload params + per-voice progress state (envelope
   stage/time, LFO phase, RND state).
3. **Connections** — one per instrument connection, source-clone → voice destination,
   carrying the instrument connection's op + amount. The amount param is the voice's
   own, value-synced.

### Per-buffer base sync (live modulation without aliasing)

Before the voice's `processModulations` runs, a helper syncs the voice's copy from the
instrument blueprint: copy baseValues of (a) every instrument source param → the
voice's clone params, and (b) every instrument op/voice param → the voice's destination
params. Source dial edits (AD durations, LFO rate) and op dial edits reach sounding
notes on the next buffer — the same audible behavior as the alias, but the voice owns
its storage, so nothing dangles when a source is retyped or deleted between syncs.

### Trigger semantics

- Per note-on, the voice retriggers its **envelope** clones (stage reset, per-voice
  AD progress).
- **LFO/RND** clones follow their **play mode** (Part 4): FREERUN continues, RETRIGGER
  resets phase on note-on.

### Rebuild policy

- **Structural edits** (route/unroute, source retype/delete, connection
  amount-param creation) → `rebuildVoicesForInstrument` re-creates the voice graphs
  under `g_audioLock`; the audio thread skips that channel's window (established
  pattern). Explicit edits may reset sounding notes.
- **Value edits** (dial tweaks to any param) → handled by the per-buffer base sync; no
  rebuild, no note reset.

### Audio callback changes

- The instrument-level `processModulations` in the audio callback (main.c:325) is
  **dropped** from the audio path: no audio consumer needs the instrument graph's
  currentValues (dials show baseValue; strip/picker/overlay read the voice-side graph
  or connection metadata). Confirmed during implementation that no UI readout depends
  on it; if one is found it is retained only for that readout.
- The voice per-sample `processModulations` (main.c:345) remains the audio driver,
  now processing the full per-voice graph.

## Part 4 — LFO / RND play mode

- Each LFO and RND source gains a **play-mode** parameter: `FREERUN` (default —
  phase continues across notes, no response to trigger) vs `RETRIGGER` (phase resets on
  note-on).
- Stored on the payload (`LfoState.playMode` / `RndState.playMode`), backed by a
  routable parameter so it lives naturally in the union, in presets, and in the UI.
- Envelopes always retrigger; play mode does not apply to them.
- This is the hook point for the user's future "define what behaviour a mod source has
  on receiving a trigger" option.

## Part 5 — UI surface

- **Source container:** type-cycle (ENV/LFO/RND), ROUTE, and DEL work on **all** rows
  including the defaults (safe once the voice decouples). LFO/RND-typed rows gain a
  **play-mode** control.
- **FM section layout:** operator rows grow from 3 to **5 dials per op**
  (RATIO/FEEDBACK/LEVEL/OUTLV/PITCH) + ALG + PAN + a GAIN control. 23 controls total —
  a real layout pass (5-across or a second row per op group; row height target stays
  the AD-env dial standard the user established). The exact arrangement is a plan
  detail.
- **Picker:** `gain`, per-op `outLevel`, per-op `pitch` join the dest list (~40 pins
  on FM). Dest collection stays rect-pinned to the (now existing) dials; picker density
  is handled in the plan.
- **Route overlay:** the seeded default connections render as real route lines and are
  clearable/reroutable.

## Part 6 — Build order (gated phases)

1. **Union refactor** — `Mod` carries the named union; `changeModType` in-place; all
   call sites migrate; no behavior change. Tests: swap preserves output/name/
   connections; payload init; atten fields move to `data.atten`.
2. **Per-voice graph copy** — voices own full copies + base-sync each buffer +
   per-note trigger; `createParamPointer*` aliasing deleted. Tests: live sync,
   polyphony, no dangling after retype/delete.
3. **New params + defaults** — per-op `pitch` (Hz offset + oscillator wiring), `gain`,
   `outLevel` in the picker, seeded default connections, preset round-trip. Tests: io
   round-trip with the new fields, seeding, op pitch math.
4. **LFO play mode** — param + FREERUN/RETRIGGER behavior + tests.
5. **UI** — type-cycle/DEL on core rows, play-mode control, FM 5-dial layout, picker
   dests, route overlay. Fixtures.
6. **Full gate** — all suites, all fixtures, boot.

## Out of scope

- Future trigger-behavior options beyond the LFO/RND play mode.
- Any change to the pattern-step-sequencer, sampler, or config systems.

## Risks / notes

- The union refactor is wide mechanical churn; gating it separately keeps behavior
  verifiable.
- Per-voice graph copies multiply mod processing by voice count. Processing already
  runs per active voice per sample; the increase is bounded and only on active voices.
- Dropping the instrument-level processModulations from audio must be verified against
  every UI readout before the gate.