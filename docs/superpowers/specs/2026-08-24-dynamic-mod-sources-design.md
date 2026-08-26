# SP-C: Dynamic Mod Sources — Design

Date: 2026-08-24
Status: Approved design (awaiting spec review + implementation plan)

## Problem

The modulation system supports creating mod sources (envelopes) and wiring them to
parameters at build time, but there is no support for *removing* or *rewiring*
them at runtime, and the UI has no way to add, delete, or route mod sources. The
instrument screen shows a fixed set of AD envelope controls; the mod system has no
`removeFromModList`, `removeModulation`, or teardown primitives; and deleting a mod
would leave dangling connections that `processModulations` would dereference.

This sub-project adds dynamic mod sources from the ground up: rigorous mod-system
tests first, then the removal/rewiring primitives, then instrument/voice integration
tests, then the UI (add/delete envelope + route dial), then an interactive harness
for the FM instrument screen that doubles as the automated UI test bed.

## Confirmed decisions

- Routing targets: **shared FM params only** (`ops[0..3].feedback/ratio/level`, owned
  by `instrument->paramList`). Affects all voices at once, single registry, single
  walk for removal/rewire. Per-voice outLevels are out of scope.
- Delete model: **core + runtime-deletable**. The instrument's startup envelopes
  (index < `envelopeCount` at startup; FM = 4) are aliased by voice envelopes
  (`voice.c:248-254`) and are non-deletable. Runtime-added envelopes (index >= startup
  count) are fully deletable.
- Routing UX: a **ROUTE dial** per runtime envelope, styled like the ALG/PAN dials
  (`drawDiscreteDialGuiNode`, `gui.c:522`). Cycles the 12 candidate FM params
  (op0-3 x feedback/ratio/level), starts at OFF.
- Keybinds: **`;` (KEY_SEMICOLON) = ADD envelope**, **`'` (KEY_APOSTROPHE) = REMOVE
  envelope** (new `KM_ADD` / `KM_REMOVE` entries in input.h).
- New envelopes start **un-routed** (dial at OFF).
- UI test layer: a **standalone harness that boots directly into the FM instrument
  screen** with an instrument and pattern pre-loaded (modvisual.c pattern). Run
  interactively it is the manual "kick the tires" tool; under Xvfb it is the
  automated UI test bed.

## Architecture (evidence)

- Mod sources live in `instrument->paramList` (params) and `instrument->modList`
  (structs): `init_instrument` (`voice.c:386`) sets `envelopeCount` (FM = 4) and
  creates `envelopes[i] = createAD(instrument->paramList, instrument->modList, ...)`
  (`voice.c:472-473`).
- Voice envelopes **alias** instrument envelope stage params
  (`voice.c:248-254`: `createParamPointerAD` uses `inst->envelopes[i]->stages[*].duration`).
- Routing to instrument params works for the sound with no extra plumbing:
  `processModulations(instrument->paramList, instrument->modList)` runs every buffer
  (`main.c:77`).
- `ModConnection` carries `amount` and `type` params that are created in the
  paramList passed to `createConnection` (`modsystem.c:188-216`) — removal must free
  those and remove them from the paramList.
- `updateMod`/`processModulations` do NOT use the `dependency_count`/`processed`/
  `visiting` fields — iteration is in list order. Dynamic add/remove only needs
  connection consistency, not ordering machinery.
- Envelope stages live in a fixed `stages[MAX_ENVELOPE_STAGES]` array; the AD helper
  only fills stage 0-1. Deleting an *envelope* does not touch stage arrays of other
  envelopes.

## Components

### 1. Mod-system primitives (modsystem.c / modsystem.h)

New API:

- `bool removeFromModList(ModList *list, Mod *mod)` — find + shift, return found.
- `bool removeFromParamList(ParamList *list, Parameter *param)` — find + shift.
- `bool removeModulation(ParamList *list, Parameter *destination, Mod *source)` —
  unlink the connection for (destination, source), free the connection struct and its
  amount/type params (removed from the paramList), decrement `modulator_count`.
- `int removeModulationsForSource(ParamList *list, Mod *source)` — walk every param in
  the list, remove all connections whose `conn->source == source`; return count.
- `bool removeMod(ModList *modList, ParamList *paramList, Mod *mod)` — full teardown:
  `removeModulationsForSource` (unwire), `removeFromModList`, free by type
  (LFO/Random/Envelope/plain), removing the mod's own params from the paramList
  (output, LFO rate/phase, envelope stage duration/curvature). Returns false if the
  mod is not in the modList.
- `bool rewireModulation(ParamList *list, Parameter *destination, Mod *oldSource,
  Mod *newSource)` — find the connection, reassign `conn->source`. (Used by the route
  dial; falls back to remove+add if the old source is absent.)
- `void wrapIncrementParameter(Parameter *p, float step)` — increment with wrap-around
  at min/max (route dial cycles 0..11 -> 0).

Bug fixes (documented in tests):

- `setParameterBaseValue` must also update `currentValue` when unmodulated (currently
  it only updates `baseValue`, so dials that read `currentValue` go stale).
- `clearModList` / `clearParamList` currently leak (free calls commented out). Fix to
  actually free their contents, keeping the app's ownership rules (paramList owns
  params; modList owns mod structs only when not shared with voices).

### 2. Test layer 1 — tests/dsp/test_modsystem.c (pure, no raylib)

Coverage:

- Creation/processing: addToModList/ParamList, createAD/ADSR stage counts, LFO phase
  wrap, random phase advance, processModulations advances envelopes once triggered,
  outputs clamped to param min/max.
- addModulation: connection created, amount/type params registered, modulator_count
  increments, prepend order, MO_ADD/MUL/SUB/DIV arithmetic, MO_DIV by zero is skipped,
  multiple modulators apply in list order.
- New primitives: removeFromModList (first/mid/last/absent), removeFromParamList,
  removeModulation (single/mid-list/none), removeModulationsForSource (one/many/none),
  removeMod teardown (mod gone from both lists, connections gone, count decremented,
  no dangling pointers — verify by re-running processModulations), rewireModulation
  (present/absent), wrapIncrementParameter (both ends).
- Limits: modList at MAX_MODS, paramList at MAX_PARAMS, envelope stage overflow
  (addEnvelopeStage beyond MAX_ENVELOPE_STAGES ignored).

### 3. Test layer 2 — tests/dsp/test_mod_voice.c

Integration:

- init_instrument FM: envelopeCount = 4, envelopes in modList, op feedback/ratio/level
  params in paramList.
- initialize_voice: voice->envelope[] aliases inst envelope stage params; FM operators
  share inst feedback/ratio/level; outLevels owned by voice->paramList.
- Route a runtime envelope to an op level: processModulations changes the op param
  value; removeModulation reverts it to base.
- Runtime add: envelopeCount++ (cap MAX_ENVELOPES); new envelope in modList/paramList;
  existing voices unaffected (no aliasing).
- Runtime delete: removeMod teardown, no dangle, later envelopes compacted,
  paramList/modList consistent.
- Core delete rejected: removeMod on a core (startup) envelope returns false (guarded
  at the caller level).
- Voice render smoke: after route and after delete, renderVoice produces valid output
  (no crash).

### 4. UI (input.h, main.c, gui.c)

- input.h: add `KM_ADD` (KEY_SEMICOLON), `KM_REMOVE` (KEY_APOSTROPHE) to the enum,
  KEYBOARD_MAP, GAMEPAD_MAP (best-effort mapping), KEY_NAMES.
- gui.c: 
  - `appendRuntimeEnvControlNode(...)` — the AD control wrapper for a runtime envelope,
    plus a ROUTE dial (drawDiscreteDialGuiNode) bound to the envelope's routeIndex
    Parameter (0..11, or 12 = OFF).
  - `routeIndexParameter(Instrument*, int envIndex)` — createParameterPro with an
    onChange callback that rewires: removeModulation(oldTarget, env->base) +
    addModulation(newTarget, env->base). oldTarget tracked per envelope.
  - `addRuntimeEnvelope(Instrument*)` — createAD, envelopeCount++, rebuild the selected
    instrument's graph.
  - `removeRuntimeEnvelope(Instrument*, int envIndex)` — only for index >= coreCount;
    unwire via removeMod, compact envelopes[] (memmove), envelopeCount--, rebuild.
  - `rebuildInstrumentGraph(Instrument*)` — rebuild the selected instrument's graph
    (rebuildPatternGraph pattern).
- main.c SCENE_INSTRUMENT input: handle KM_ADD (just-pressed) and KM_REMOVE
  (just-pressed) before/after the plain arrow block. Only on the FM instrument screen.
  A runtime envelope is "selected for delete" when its ROUTE dial (or any node in its
  wrapper) is the graph's selected node — resolve wrapper -> envIndex.
- Core envelopes (index < startup envelopeCount) are not deletable: KM_REMOVE on a core
  envelope's section is a no-op.

### 5. Test layer 3 — instrument-screen harness (src/tools/instrument_harness/)

A standalone raylib program (modvisual.c pattern) that:

- Initializes a VoiceManager with an FM instrument + SamplePool, a PatternList with a
  pattern already associated with the instrument, and an AppState whose currentScene
  is SCENE_INSTRUMENT.
- Runs the same graph navigation + the same KM_ADD/KM_REMOVE/route handling as main.c
  (either by reusing gui.c functions or a thin shared input handler).
- Interactive: the user's manual "kick the tires" tool for add/delete/route.
- Automated: run under Xvfb with scripted inputs + ground-truth asserts (envelopeCount,
  graph structure, wiring, navigation) — the automated UI test layer. Uses the
  action-sequencer pattern from the automated-ui-testing skill.

## Out of scope

- Per-voice outLevel routing (deferred).
- Scrollable modwrap container (SP-B, separate sub-project).
- Nav algorithm refinement (SP-A, separate sub-project).
- LFO/random add/delete in the UI (envelopes only this round; primitives are generic).
- Preset save/load of runtime envelope routing.

## Verification

- `make -f Makefile.test` in tests/: test_modsystem.c + test_mod_voice.c pass.
- `make` clean, app launches, FM instrument screen: `;` adds an envelope with a ROUTE
  dial, `'` deletes it, dial cycles and rewires (sound responds), core envelopes
  undeletable.
- instrument_harness builds; interactive session works; Xvfb scripted run asserts pass.