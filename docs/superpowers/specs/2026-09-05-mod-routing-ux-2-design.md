# Mod Routing UX Round 2 — attenuator node, picker polish, scroll fixes

Date: 2026-09-05. Branch: `mod-sources-ux` (continues the mod routing arc).

## Context

The route-picker layer, route-lines overlay, and scrollable mod container exist.
User review produced a new batch: a real attenuator modulation node, a picker
visual-language pass, a clear-all-routes prompt, scroll fixes (incl. a bug),
scroll coupling across layer/base/lines, and a scrollbar.

## A. Attenuator modulation node (MT_ATTEN)

### A1. Mod struct changes (src/modsystem.h/c)

- Add `MT_ATTEN` to `ModType` (before `MT_COUNT`).
- `Mod` gains:
  - `Mod *input;` — the upstream source mod this attenuator shapes.
  - `Parameter *attenAmount;`   — boost/attenuation (0.0 .. 2.0, default 1.0).
  - `Parameter *attenPolarity;` — discrete: 0 = bi, 1 = uni (default 0).
  - `Parameter *attenCurve;`    — discrete: 0 = linear, 1 = curved (default 0).
- `updateMod` for `MT_ATTEN` — no time-based state; it only shapes. Provide a
  case that does nothing beyond the shared bookkeeping (mirror how `MT_OFS`
  behaves as the "stateless" reference — check `updateMod`'s switch and follow
  the lightest existing path).
- `MT_ATTEN` generate (new `modGenerateAtten`):
  ```
  float v = getParameterValue(mod->input->output);
  float amt = getParameterValue(mod->attenAmount);
  v *= amt;
  if(polarity == uni) v = fmaxf(v, 0.0f);
  if(curve == curved) v = copysignf(powf(fabsf(v), 0.5f), v); // gentle square-root ease
  setParameterValue(mod->output, v);
  ```
  Curve choice: keep it minimal — 0.5 exponent is the only curve this pass.
  The curve param is a switch (linear/curved), not an exponent dial.
- `setModType`/`changeModType` (the type-cycle primitive): MT_ATTEN is NOT
  selectable as a source type (it is a connection-internal node, never a
  source the user routes from). The UI type-cycler must skip it (see C).
- `removeMod` (existing, frees a mod + its output param): must also free the
  atten params (amount/polarity/curve) when type is MT_ATTEN.

### A2. Lazy per-connection insertion + cleanup

- New `Mod *createAttenuatorMod(ParamList *paramList, ModList *modList, Mod *source, char *name);`
  — creates an MT_ATTEN mod, links `input = source`, creates the three params
  (registered in paramList, named `<source>_amt` / `<source>_pol` /
  `<source>_crv`), appends to modList, returns it.
- `addModulation(ParamList *, Mod *source, Parameter *destination, float amount, ModulationOperation type)`
  — when the connection is created AND the source is not already MT_ATTEN,
  insert an attenuator: create it, set `connection->source = atten`,
  `atten->input = source`. The connection's existing `amount` Parameter is
  REUSED as `atten->attenAmount` (the dial edits the connection amount, the
  attenuator reads it — no duplicate param). The route amount param is created
  as before; attenuator references it.
  - Do NOT insert an attenuator when the source is already an attenuator
    (re-routing an atten output into another dest should chain? NO — keep it
    simple: only insert when the source is a real source type; if a source is
    already MT_ATTEN, reuse it for the new connection).
  - Note: the picker currently routes via `addModulation(... amount, MO_ADD)`
    from `cbRouteToDest` and the probe — both get the attenuator for free.
- `removeModulation(ParamList *, Parameter *dest, Mod *source)` — after
  removing the connection, if `source` is MT_ATTEN and has no remaining
  connections anywhere (scan paramList: no connection whose source == it),
  `removeMod` it (frees atten + its params + modList slot). Cleanup handled.
- `removeModulationsForSource` — same cleanup per removed connection.

### A3. Impact on existing code/tests

- `processModulations` apply pass UNCHANGED (the attenuator shapes upstream;
  the connection then applies `getParameterValue(atten->output)` as today).
- The pinned test that asserts connection amount is ignored in the apply pass
  becomes a DIFFERENT assertion: the attenuator's amount shapes the value
  before the connection reads it. Update `tests/dsp/test_modsystem.c`:
  - New tests: attenuator linear-shape math (amount 0.5 halves the mod value
    into the destination), uni polarity clamps negatives, curve applies the
    sqrt ease, lazy insertion rewires connection.source to the attenuator and
    keeps input pointing at the real source, cleanup frees the attenuator when
    the last connection goes, and (integration) the route-line/UI matching
    follows `input` (see C/D).
- **Ripple (must be handled in the plan, not silently):** because insertion is
  inside `addModulation`, every existing connection in the modsystem tests
  gains an attenuator in the modList. The looped-feedback fixed-point tests
  (`test_two_cycle_feedback`, `test_three_cycle_feedback`,
  `test_self_modulation`) assert values derived from the exact modList cascade
  order — those expected values WILL change. The plan must recompute the new
  fixed points (deterministic — the cascade order stays well-defined) and
  re-pin them, PLUS add an assertion that an attenuator at default settings
  (amount 1, bi, linear) is an identity transform on the modulated value. The
  same review applies to `test_mod_voice.c`'s routing tests if any inspect
  `connection->source`.
- `modStripColor` (vizfx): add an MT_ATTEN case (reuse `modStripDefault`).

## B. Scroll fixes + scrollbar + coupling

### B1. Scroll bug (real, confirmed)

`scrollToVisible` updates `scrollOffset` but never re-runs `reflowCoordinates`,
so the container's children keep their last-baked positions — navigating to an
off-screen row sets the offset with no visible effect.

Fix: at the end of `scrollToVisible` (when the offset actually changed), call
`reflowCoordinates(&sc->base)` so the rows re-position with the new offset.
Draw-time scissor already clips.

### B2. Scrollbar

In `drawNode`'s scrollable-container branch (or the container's own draw),
when `contentH > viewportH`, draw a right-edge scrollbar:
- thumb height = viewportH * viewportH / contentH (min 8px);
- thumb y = container.y + (contentH - viewportH > 0 ? scrollOffset * (viewportH - thumbH) / (contentH - viewportH) : 0);
- track = dim outline, thumb = cs.dial (or a fixed grey). Drawn after the rows,
  inside the scissor. Non-interactive (nav drives scrolling).

### B3. Scroll coupling across layer/base/lines

The picker layer's dest buttons are pinned to base dial rects; route lines use
`findDialRectForParam` on the base graph. When the mod list scrolls (base
selection moves, or picker nav moves to an off-screen dest), everything must
track:

- Base: `syncModWrapScroll` (gui_instrument.c draw) already runs every frame
  before drawNode; with B1 fixed it now visibly scrolls. The base's own
  `scrollOffset` is the source of truth.
- Picker: nav inside the picker layer that lands on an off-viewport dest must
  scroll the BASE container (the dest buttons live at the base dial rects, so
  scrolling the base moves them). Implement: after picker nav changes the
  selected dest, if the selected dest's rect (pinned base position) is outside
  the base mod container's viewport, adjust `scrollOffset` (same clamp) — i.e.
  call a `scrollToVisible` on the base mod container using a synthetic node
  whose y = the dest's base rect. The picker layer's dest buttons then need
  their pinned positions updated to the scrolled base positions: simplest is
  to re-run the picker's Loop-2 pin each frame in a sync step
  (`syncPickerDestRects()` called from `gui_instrument_draw` while the ROUTE
  layer is up), re-reading each dest param's base dial rect via
  `findDialRectForParam`. The layer graph reflows after repinning (the buttons
  are children of the layer graph; reflow would stamp their rects from the
  layer layout — so repin AFTER the layer graph's own reflow, or pin in the
  draw path). Keep it deterministic: repin immediately before drawing.
- Lines: `drawRouteLinesNode` already reads live base rects each frame via
  `findDialRectForParam` — since base rects move with the scroll offset
  (children positions are reflowed), lines follow automatically once B1 lands.
- The route button anchor (`findRouteButtonRect`) likewise reads live rects.

## C. Picker layer visual language (gui_inst_mod.c)

### C1. Ghost (preview) lines

In the ROUTE picker layer, add a full-screen draw node (or extend the existing
label-overlay node) that draws a desaturated, semi-transparent line from the
source's ROUTE button to EVERY destination dial rect:
- colour = desaturate(routeAdd): mix toward grey (~50%), alpha ~60;
- 2px, drawn beneath the real route lines (which stay full gradient + circles).
Constant indicator of where a line will land if routed. Real lines for
existing routes draw over the ghost.

### C2. Selection border oscillation

`drawRouteDestNode` selection outline: oscillate between the current green and
a brighter green (lerp factor `0.5 + 0.5*sinf(GetTime()*4)`), UNLESS one of the
KM_FUNCTION-held conditions below applies (then the special colour wins).

### C3. KM_FUNCTION-held destination states

The picker input path (layerStackInputLayer → actionCb) must communicate
KM_FUNCTION-held state to the DRAW path. Add a file-scope
`static bool g_pickerFunctionHeld;` set in the picker's input dispatch each
frame (from `isKeyHeld(is, KM_FUNCTION)`), reset when no picker is up.

- Routed dest (source → this dest has a connection): outline oscillates
  bright↔dark RED (lerp routeAdd toward (180,0,0)); inside the button draw the
  text `tap <<km_edit>> to clear modulations for <<ENV_xyz_attack>>` — the
  source name + the destination param name, wrapped to fit the button width
  (small 7-8px font, clipped by the scissor).
- Unrouted dest: outline static dim green + text `no active modulations.` in
  the same dim green.

### C4. KM_EDIT-held on a routed dest → semi-transparent amount dial

When the picker's selected dest has a connection and KM_EDIT is HELD, draw a
semi-transparent dial (existing `drawDialGuiNode`-style arc) centered on the
dest showing the connection amount (`getParameterValue(conn->amount)`, 0..2
display). Draw in the picker layer (post-dest-buttons), alpha ~150.

### C5. Double-tap EDIT → atten editor panel

Double-tap EDIT on a ROUTED dest opens an attenuation editor panel (a small
overlay drawn near the dest, within the picker layer):
- controls (navigable):
  - amount dial (0..2, default 1) — edits `conn->amount` (== atten->attenAmount).
  - curve button (LINEAR / CURVED) — cycles atten->attenCurve.
  - polarity button (BI / UNI) — cycles atten->attenPolarity.
  - RESET button — sets amount=1, curve=linear, polarity=bi.
- KM_FUNCTION exits back to the routing layer (closes the editor).
- Navigation: the editor controls are selectable nodes appended into the
  picker layer graph (a small sub-row near the selected dest) OR drawn +
  handled inline (a mini state machine in the picker input path). Prefer the
  mini state machine (fewer graph mechanics): while the editor is open, the
  picker input path handles arrows/EDIT/START/SELECT/FUNCTION directly against
  the 4 controls, and KM_EDIT on amount adjusts (dial nav), etc. Lock the
  exact interaction in the plan; keep it consistent with existing dial/button
  gestures (KM_EDIT to fire, arrows to adjust).

### C6. Clear-all prompt

On the BASE graph: KM_FUNCTION held + KM_EDIT on a source's ROUTE button →
push a confirm layer (reuse the overwrite-confirm pattern from gui_instrument.c
modal machinery or a small dedicated layer):
- text: `clear all modulations for ENV_xyz?` (source name from
  `modList->mods[idx]->name`).
- YES / NO buttons, NO SELECTED BY DEFAULT. KM_EDIT fires the selected;
  KM_SELECT cancels (== NO).
- YES → `removeModulationsForSource(paramList, modList, source)` (A2 cleanup
  applies) + rebuild the instrument graph + close the layer.
- This is distinct from the erase-mode (KM_FUNCTION+EDIT in the picker clears
  ONE dest) — it clears ALL of the source's routes.

## D. ADD button position

`createInstGraph`'s mod header row currently is `[MODS label][ADD]`. Move the
ADD action button to the LEFT of the row (`[ADD][MODS label]`), keeping the
same weights/visuals.

## E. Route-line matching follows attenuators

`drawRouteLinesNode` matches `c->source == src`. With attenuators inserted,
connections' `source` is the attenuator, not the real source. Update the match
to `(c->source == src) || (c->source && c->source->type == MT_ATTEN && c->source->input == src)`.
Same for `cbRouteToDest`'s erase-mode detection and `findRouteButtonRect`
(unchanged — it keys on actionCb, not source).

## F. Verification

- Unit: new test_modsystem attenuator tests (A3) + existing suites green
  (15/15 after the amount-ignore test update).
- Scripted fixtures: existing 6 PASS; add/extend a fixture for the clear-all
  prompt (navigate to ROUTE, KM_FUNCTION+EDIT, assert confirm layer, NO
  default, YES clears all routes for the source) and one for scroll: ADD
  several sources, nav to a row beyond the viewport, assert the selected row's
  y is inside the container viewport (harness can assert node geometry via a
  new assert verb or reuse --probe-row-geom-style output).
- Probe (`--probe-route`): unchanged, still warmpx >= 100.
- Boot clean. Orphan sweep after every xvfb-run (#1137). No long timeout
  runs — prefer fixtures (they self-exit) run asynchronously (#1147).

## G. Out of scope

- Response-curve exponent dial (single 0.5 curve only).
- Attenuator chaining UI (routing an attenuator's output) — reuse-if-atten
  rule in A2 keeps it simple.
- Scrollbar interactivity (nav-driven only).
- Preset persistence of atten params (runtime-only, like runtime envelopes).

## Decisions locked

- Attenuator = real MT_ATTEN node, lazy per-connection, hidden from source UI,
  reuses connection amount param, freed on last-connection cleanup.
- processModulations apply pass unchanged; attenuator shapes upstream. Default
  attenuator (amount 1 / bi / linear) is an identity.
- The modsystem feedback-test fixed points WILL shift because insertion lives
  in addModulation; they are recomputed + re-pinned, not papered over.
- NO default on the clear-all confirm.
- Scroll reflow-after-offset is the core scroll fix; coupling via live-rect
  reads + repin-before-draw; lines follow automatically.
- Picker states keyed on g_pickerFunctionHeld + per-dest routed-ness.