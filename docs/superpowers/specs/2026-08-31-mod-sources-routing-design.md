# Spectrax unified mod sources + routing — design

Date: 2026-08-31

## Problem

The instrument page exposes envelopes as the only first-class mod sources
(the `modwrap` container from SP-C). LFOs and random mods exist in the mod
system (`createLFO`, `createRandom`) but have no UI path to be created on
an instrument. Routing is a single per-envelope dial that cycles a hard
list of 12 FM targets, and deleting a source is only possible for runtime
envelopes via a keybind. The user wants a **unified mod-sources model**:
every source (envelope, LFO, random, and later pattern sequencers) lives
in one list, each with a type selector, type-specific controls, a **route
button** and a **delete button**, with a real destination-picking layer and
a connection-line overlay. This is the framework item — item #2 (pattern
step-sequencing) rides on it later as a new source type.

## A. Mod-system primitives

### A1. `changeModType`

New primitive: swap a mod's type in place while keeping the `Mod` entry,
its output parameter, and its existing routes.

```c
bool changeModType(Mod *mod, ModType newType, ParamList *paramList);
```

Behaviour:
- Validate: the mod must be registered in `modList` (caller guarantees)
  and `newType` must be a supported source type (ENV, LFO, RND).
- Remove the mod's current type-specific params from the paramList
  (rate/phase for LFO/RND; stage params for env — mirror `removeMod`'s
  per-type cleanup) WITHOUT touching the base output param or the routes.
- Re-init the type-specific data: allocate the type struct fields, create
  the type params, set the `generate` fn (`generateSine`/`generateRamp`/
  `generateSquare` for LFO, `generateRandom` for RND, the env generator for
  ENV).
- Routes survive: `processModulations` reads the source via `mod->output`,
  which is untouched.

Testable against `modList`/`paramList` without a GUI (like the SP-C tests).

### A2. Runtime source lifecycle

Reuse the SP-C primitives:
- `removeMod(modList, paramList, mod)` — removes routes + type params +
  the mod struct (caller-frees the type fields per `removeMod`'s switch).
- `addToModList` / `removeFromModList`.
- Core-envelope constraint stays: the first `coreEnvelopeCount` sources
  (the startup envelopes, voice-aliased) have NO delete button and their
  type selector is disabled (or hidden). Runtime sources (index >=
  coreEnvelopeCount) are fully deletable.

### A3. ADD behaviour

`addRuntimeSource(inst)` creates a default **envelope** source (matching
the current `addRuntimeEnvelope`), registered in `inst->modList` +
`inst->paramList` + `inst->envelopes`-style storage, with the `rebuilding`
guard around the modList mutation (audio-thread race, the established
pattern).

## B. Mod sources UI

A vertical **mod sources container** (replaces the envelope-only `modwrap`)
inside the instrument graph, holding one entry per source:

- **Header**: the container name + an **ADD** action button
  (`cbAddModSource` → `addRuntimeSource`).
- **Entry row** (one per source):
  - **Type selector** (leftmost): a discrete control cycling
    ENV → LFO → RND. Pressing it fires `changeModType` on the source.
    Shows the current type's tag (`ENV` / `LFO` / `RND`).
  - **Type-specific controls**: env → ATTACK/CURVE/DECAY/CURVE dials (as
    today); LFO → RATE + SHAPE (LS_SIN/LS_SQU/LS_RMP) dials; RND → RATE +
    RND-type dials.
  - **Route button** (bottom right of the entry): `createActionBtnGuiNode`
    with the route behaviour (Section C).
  - **Delete button** (top right of the entry): `createActionBtnGuiNode`
    with the delete behaviour (Section D). Only present on runtime sources.
- The mod strip (vizfx `drawModStrip`) stays as the live-output readout.

## C. Routing

### C1. Destination set

Routable destinations = parameters that have a **visible dial** in the
base instrument graph (FM op feedback/ratio/level dials, envelope stage
dials, preset-row dials). Params without dials (panning/detune/volume) are
excluded for now. The destination set is computed from the live graph on
each routing-layer open.

### C2. Route button — focused (bright-line overlay)

When the route button is the graph's selected node but not activated, a
**draw-only overlay layer** draws bright lines from the source entry's
route-button position to every destination dial that source currently
influences (the params with an active modulation from that source). Lines
are colour-coded by operation (ADD vs MUL). The overlay captures no input;
navigation continues in the base graph.

### C3. Route button — pressed (destination-picking layer)

KM_EDIT on the route button pushes a **destination-picking layer** (a
`Layer` with its own graph over the base):
- The base graph is dimmed; the routable dials are highlighted.
- Navigation moves among the highlighted dials (the layer's graph contains
  the routable dial nodes, so nav is constrained to them).
- KM_EDIT on a highlighted dial **routes** the source to it (adds the
  modulation, amount 1.0, MO_ADD) and closes the layer. If the source is
  already routed to that dial, it **un-routes** (removes the modulation)
  and closes.
- KM_SELECT (back) cancels the layer without changing anything.

Route amount stays fixed at 1.0/MO_ADD for now; amount/operation controls
are a future item.

## D. Delete modal

The delete button opens a **confirm layer** (the existing modal-layer
pattern — a `Layer` overlay with YES/NO). YES calls `removeMod` for the
source wrapped in the `rebuilding` guard, then rebuilds the instrument
graph; NO cancels. Core sources never show the delete button.

## E. Pattern-sequencer readiness (item #2)

The framework is a new `MT_PATTERN` ModType later: it gets a generate fn,
type-specific controls (steps + slew), and appears as an entry row with
the same type selector / route / delete controls. No framework rework
should be needed — the unified list + routing + delete all operate on
`Mod *`.

## F. Testing

- `tests/dsp/test_modsystem.c`: `changeModType` — ENV→LFO→RND keeps the
  output param + existing routes; type params are swapped; invalid types
  rejected; a change on a core source is rejected.
- `tests/dsp/test_mod_voice.c`: `addRuntimeSource` + delete via `removeMod`
  round-trip on a live instrument; routes survive a type change.
- Harness scripted fixture: navigate to a source entry; press the type
  selector (ENV→LFO); press the route button → layer opens; nav to a
  destination dial; KM_EDIT routes; verify `processModulations` sees the
  new route; focus the route button → lines overlay active (assert the
  overlay layer is present); delete a runtime source → confirm modal →
  YES → source gone + graph rebuilt.
- Gate: `ninja` clean, `meson test` 8/8, both existing fixtures PASS, app
  boots.

## G. Out of scope

- Route amount/operation controls (fixed 1.0/ADD).
- Non-dial destinations (panning/detune/volume) — need dials first.
- Core-envelope deletion (voice-aliasing constraint).
- Item #2 pattern sequencers (this spec only lays the framework).
- Item #1 chips/meta row (separate spec).