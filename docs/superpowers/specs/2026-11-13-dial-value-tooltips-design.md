# Dial value tooltips — design

2026-11-13. Follows Phase A (1.5x type) and Phase B (2x type, mixed fonts,
header-row dial grid).

## Intent

At 2x the value plate is the largest thing in a dial cell after the knob,
and most of the time it says nothing the knob does not already say. What
it is *for* is a different job: knowing the exact number so a **related
parameter can be set against it**. That job happens at a specific moment
— when the control is selected and being adjusted — not continuously.

So the number leaves the cell and becomes an annotation of the selected
control, revealed on demand.

## Goals

1. **Default: no numbers.** A dial draws its knob (and its label, where
   the style has one) and nothing else.
2. **Selected control: exact value.** A small rounded box holding the
   real float, drawn near the knob it belongs to, in the monospace face.
3. **Motion.** The box drifts along a slow ∞ path — lobes left and right
   — so it reads as an annotation rather than printed furniture.
4. **Change feedback.** When the user changes the value the box changes
   colour instantly and fades back to normal over a slow fade. A nudge at
   a limit, which changes nothing, must not flash.
5. **Reveal and latch.** A chord shows every visible control's box; a
   second key latches them so the reveal survives letting go. Motion is
   **off while latched** (a screen of drifting boxes is noise).
6. **Permanent numbers stay.** Controls whose number is part of their
   identity (BPM, Swing) keep a drawn number always, by style.

## Non-goals

- No change to how a value is edited, to parameter ranges, or to
  modulation.
- Not a debug HUD: no parameter names, addresses, or units beyond the
  style's `format`.
- Not a replacement for the modulation routing overlay; the two overlays
  may be up at once and must not fight for input (both are passive).

## Anti-goals

- A box that covers the knob it describes.
- Motion that makes the digits hard to read at the moment they are
  needed (see Risks).
- A flash on every modulation block — values are rewritten continuously
  by modulation, and a per-value poll would strobe.

## Constraints

- **640×480 fixed**; 1 logical px = 1 device px.
- **Display-only.** No synthesis path may depend on this. Modulation
  values keep being written exactly as now.
- **Overlay precedent.** The modulation routing overlay is a full-screen
  passive layer whose single node draws over the graph and reads the live
  node rects (`syncRouteLinesOverlay`, `drawRouteLinesNode`,
  `setLayerPassive(l, true)` in `src/gui_inst_mod.c:1059-1095`). The
  tooltip layer follows that shape rather than becoming part of each
  dial's draw, because a tooltip must overhang its cell.
- **Inputs already exist, under other names.** There is no `KM_L1`/
  `KM_R1`: the pad's `LEFT_TRIGGER_1`/`RIGHT_TRIGGER_1` are
  `KM_NAV_LEFT`/`KM_NAV_RIGHT` (`src/input.h:31-32`, gamepad map
  `src/input.h:93-94`, keyboard `Q`/`W`). SELECT chords are detected as
  `isKeyHeld(KM_SELECT) && !instrumentLayerModalActive()` +
  `isKeyJustPressed(...)` per scene (`src/main.c:989-998`). SELECT
  currently pairs with LEFT/RIGHT (scene), EDIT (preset), UP/DOWN
  (channel) — so `SELECT+NAV_LEFT` and `SELECT+NAV_LEFT+NAV_RIGHT` are
  free, and no keymap entry is added. On the pattern screen
  `NAV_LEFT`/`NAV_RIGHT` already fine-step the velocity lane
  (`src/main.c:871-874`); the tooltip chords are instrument-only, so they
  do not collide.
- **Fonts**: values draw in the `mono` role (`styleFont("mono")`,
  `src/gui_core.c`) so digits stay tabular while the box moves.
- **Style system**: every visual knob is a JSON style field; a new
  element gets its own style class resolved through `StyleType`
  (`src/gui_style.c`), and the shipped `src/layout.json` must cover it.

## Decisions the user made

- **Two draws** — the tooltip is separate from the dial's `value` plate;
  the plate path stays for styles that keep a permanent number.
- **Motion off when latched.**
- **Chords**: `SELECT+L1` reveals, adding `R1` latches.
- Box orbits its own knob; the motion carries no meaning; the fade is on
  colour only; the flash is per-control (agreed, "mostly").

## The shape of the change

### 1. A style class for the box

`valtip` in `layout.json`, resolving to a new `ValueTipStyle`:
`bg`, `border`, `borderWidth`, `radius`, `padding`, `fontSize`,
`font` (default `mono`), `color`, `colorChanged`, `fadeMs`,
`driftRadiusX`, `driftRadiusY`, `driftPeriodMs`.

`dial.value` keeps its fields; a dial shows a permanent number only when
its style sets `value.always: true` (BPM/Swing styles gain it).

### 2. A passive overlay layer

A layer named `VALTIPS`, pushed by the graph build for every screen that
has dials, `setLayerPassive(l, true)`, whose node draws after the graph.
It resolves the visible dial nodes from the live graph the way
`findDialNodeForParam` does, so a dial scrolled out of a scroll container
draws nothing.

### 3. Flash bookkeeping

A single `Parameter *` + timestamp pair is not enough: several controls
can change in one frame (a preset load, a modulation edit). Keep a small
ring of `(Parameter *, uint32_t ms)` entries, capped, updated **from the
input/action path** (`incParameterBaseValue` and the other value-setting
callbacks), never by comparing values per frame.

### 4. Input

In each scene's SELECT block: `KM_L1` just-pressed toggles *reveal*;
`KM_R1` while `KM_L1` is held toggles *latch*. Latch implies reveal and
survives release. Any modal layer on top keeps them from firing, as the
channel chord already does.

## Risks

1. **Legibility of a moving number.** A drifting 18px float is harder to
   read than a static one, exactly when the user wants to read it. Mitiga-
   tion: small amplitude, slow period, and the box comes to rest (tween
   out) for a moment after a change. Builder to pick amplitudes; if it
   still reads badly, the fallback is "drift only while untouched".
2. **Twenty boxes.** Latched with 20 dials at once must stay readable:
   motion off, and boxes must not overlap each other. Builder to verify
   on the instrument screen's FM grid, which is the densest case.
3. **Flash driven by modulation.** Must be impossible by construction —
   bookkeeping in the input path, not a value poll.

## Building blocks to reuse

- `drawPanelRect` (panel + shadow + border) for the box body, or
  `DrawRectangleRounded` for the corner radius if the style asks for it.
- `styleFont("mono")` and `MeasureTextEx` for sizing the box to the
  string, so a `%05.2f` and a `%i` box differ only by their text.
- `findScrollContainerByName` / `dialVisibleInViewport`
  (`src/gui_inst_mod.c:41-50`) for the visibility test.

## What is left to the builder

Box size, radius, padding and where it sits at drift phase zero; the
drift amplitudes and period; the fade duration and curve; the changed
colour; and the exact JSON field names.

## Resolved while reviewing the design

Answers that follow from the conversation or from the tree, so the plan
need not ask them again.

1. **When a box is visible.** The selected control's box is *always*
   drawn — not only during a change. The chord is for the rest of the
   screen. A dial with `value.always` draws its number in the cell and
   never a tooltip (it must not draw both).
2. **Keys.** `SELECT+NAV_LEFT` (L1) reveals while held;
   `SELECT+NAV_LEFT+NAV_RIGHT` (L1+R1) toggles latch. Unlatch is the same
   combo. Latch implies reveal, survives release, and stops the motion.
3. **The knob re-centres.** When the number leaves the cell, the
   `[knob, value, label]` composition drops the plate's width
   (`groupW` loses `value.offsetX + value.width`), so dials tighten and
   the knob centres in what remains. A style that keeps a permanent
   number keeps paying that width. The pinned geometry in
   `tests/dsp/test_layout.c` and the `ui_type_scale` fixture are updated
   with the numbers, not weakened.
4. **Permanent numbers.** `value.always: true` on a style. Shipped:
   a `dial-tempo` class for BPM and Swing (they have no class of their
   own today) and `stage-rate`. Everything else is tooltip-only.
5. **Scope is the instrument screen.** It is the only scene with an
   overlay-layer stack (`igui->overlayLayers`, `src/gui.h:62`); the
   arranger's only dials are the two tempo dials, which keep permanent
   numbers, so a tooltip there would add nothing.
6. **Modals.** The layer draws tooltips for the graph that is currently
   active — the modal's own dials when one is up, nothing for the dimmed
   base graph. A passive overlay draws above every layer
   (`src/gui_instrument.c:1629-1637`), so leaving the base graph's boxes
   on would float them over the modal.
7. **Colours reuse existing tokens** (`surface.inset`, `line.panel`,
   `text.value`, `control.accent`), so no theme-schema, saved-theme or
   theme-tool change.
8. **Verification** uses a test hook in the style of
   `guiRouteLinesSource()` (`src/gui_inst_mod.c:338`) rather than new
   harness key names: the harness asserts the tooltip layer's mode and
   the box rect for a known dial.

## Open

None. The one cross-cutting choice — base or effective value when a
modulator is sweeping the parameter — is settled below.

## Modulation, drawn twice

The same base/effective split applies to the dial's own indicator, not
just the number — and the base arc comes **back** for knobs that use a
texture.

Today the knob's arc is one sector from `knob.startAngle` through the
value's fraction (`src/gui_core.c:290-320` in `drawDialGuiNode`,
`:486-520` in `drawDiscreteDialGuiNode`): `angle` comes from
`p->baseValue`, the sector is drawn at `radius + 2` in `knob.color`, and
the needle texture rotates by the same angle. When the style names an
`asset`, the texture replaces the disc **and the sector is skipped
entirely** — the shipped `dial` style names `resources/images/dial2.png`,
so no arc is drawn on any screen. The only sign that a modulator is
moving a parameter is the routing overlay's lines, which needs the
routing layer open.

So:

- **The base arc is drawn for every knob**, asset or not: sector at
  `radius + 2` from `startAngle` through the base fraction, painted
  *before* the knob graphic so the graphic sits inside the ring. This is
  the "reenable" the user asked for; the procedural-disc path already
  drew it and this makes the two paths agree.
- **A second, dimmed arc** from `startAngle` to the effective value's
  angle (`getParameterValue(p)`, `src/modsystem.h:345`, same range and
  sweep) is drawn behind it, in `knob.modColor` — defaulting to
  `knob.color` at ~45% alpha so an unthemed style still shows it.
- What remains visible past the bright arc is exactly the modulator's
  excursion. Unmodulated, the two angles are equal and nothing extra
  shows, with no branch beyond the alpha.
- The needle keeps following the **base** value, so the knob still means
  "where you set it" and the dim arc means "where it is right now".

This is display-only and reads `getParameterValue`, which the audio
thread also reads; it must not mutate anything or take a lock the audio
callback holds.

The value box follows the same rule: base value in `text.value`, and when
`p->modulators` is non-empty, the effective value after it in
`text.secondary`, one step dimmer.

