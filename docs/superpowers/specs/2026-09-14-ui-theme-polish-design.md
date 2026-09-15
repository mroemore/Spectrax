# UI / Theme Polish — Design

**Date:** 2026-09-14
**Status:** draft (awaiting user review)
**Scope:** five workstreams (A–E) of UI/theming/visual work on top of the
pattern-seq tracks feature. Each workstream is independently
implementable and testable.

## Goal

Make the pattern-sequence mod row a first-class citizen of the mod-source
list, then push on general style/theme polish: a generic reusable text
element, an envelope-stage control rework, cursor-retention fixes, a
multi-screen render mode, and font/icon updates.

## Conventions & constraints

- **Use the theme/layout system** (`bin/layout.json` `styles` + `layouts`,
  `src/gui_style.h/c`, `bin/clr.json`) wherever possible. Every new visual
  knob should be a style field resolved from JSON, with a baked default in
  `gui_style.c` equal to today's hardcoded literal.
- **Flag every theme-system extension** explicitly (the user may veto a
  plan change). Extensions are collected in §Theme extensions.
- No comments in code unless asked. Follow existing naming and lifecycle
  conventions (create/destroy, `initGuiNode` zero-inits every readable
  field — rule #1154).
- Existing unit suite must stay green; UI behaviour is verified with the
  instrument harness fixtures (rule #1063 ordering: mod-system → voice →
  UI).

---

## WS-A — Generic reusable text element

### Problem
There is no way to place arbitrary text at an arbitrary position with its
own font/size/spacing/colour. `STYLE_BTN`/labels always render the node's
`name`, so a button cannot have a unique identity and different visible
text; and labels like `ATK`/`DEC`/`CRV` need arbitrary placement.

### Design
1. **Per-instance display text, separate from identity.** Add
   `char *text` to `GuiNode` (graph_gui.h). `initGuiNode` zero-inits it
   (rule #1154); `freeGuiNode` frees it; add `guiNodeSetText(GuiNode *,
   const char *)` (strdup). Every label-drawing path (`drawActionBtnGuiNode`,
   label draws, chip labels, etc.) renders `gn->text ? gn->text :
   gn->name`. This makes unique internal names + visible text possible
   without duplicate-name nav/assert hazards.
2. **`TextStyle`** (gui_style.h): `char fontName[16]; int fontSize;
   int spacing; int offsetX, offsetY; int hAlign; int vAlign; Color color;
   Color colorSelected;` with `hAlign ∈ {LEFT,CENTER,RIGHT}` and
   `vAlign ∈ {TOP,MIDDLE,BOTTOM}`; `offsetX/Y` nudge from the anchor.
   Position is the node's x/y rect plus alignment/offset.
3. **`STYLE_TEXT`** added to `StyleType`; `resolveTextStyle()`;
   `g_defaultText` (pixelFont, size 9, spacing 0, left/top, `fontColour`);
   JSON overlay + a `"text"` entry in `layout.json` `styles`.
4. **`createTextGuiNode(int x,int y,int w,int h,const char *text,const char *className)`**
   (gui_core.c): non-selectable drawable node, `draw = drawTextGuiNode`,
   which resolves the style, picks `styleFont(st->fontName)`, computes the
   aligned origin and calls `DrawTextEx` with `color` (or `colorSelected`
   when `gn->selected`).

### WS-A.2 — PTN row parity (first consumer)
- Replace the current grouped-PTN row markup with the standard
  `mod-source-row` conventions (same `padding/gap/weights`, same
  `drawWrapperNode`, same row height as ENV/LFO rows).
- One `STYLE_TYPE_LABEL` `PTN` cell, then four `STYLE_BTN` route buttons.
- Each route button: unique `name` (`PTN_ROUTE_1..4`), `text = "ROUTE"`,
  and a **sublabel** carrying the track stats `L<n> <shape-abbrev>` (e.g.
  `L16 LIN`). ⚠️ Requires `BtnStyle.sublabel` (a `LabelStyle`) — see §Theme
  extensions. The separate `PTN_READ` readout node is removed.
- The route callback (`cbOpenRouteLayer`) receives `&g_sourceCtx[firstPat+t]`
  as today, so routing is unchanged.

### Testing
- Unit: `createTextGuiNode` zero-inits, sets text, resolves style, is
  non-selectable; `guiNodeSetText` frees the previous value (no leak).
- Harness: PTN row assertions in `pattern_screen.txt`/`pagenav` (4 buttons,
  each opens its own track's picker); update `pagenav` bottom-of-group name.

---

## WS-B — Envelope stage rework (user list item 2)

### Current
Each LFO/AD-style envelope stage renders as `[ATTACK dial][CURVE
dial][DECAY dial][CURVE dial]` (gui_inst_mod.c ~1919) inside an `env-row`
layout; each dial shows a numeric value.

### Design
- **One visual box per stage**, two independently selectable children:
  - **Rate dial** (left): keeps its numeric readout but moved **below**
    the dial and slightly smaller.
  - **Curve dial** (right): ~2/3 the rate knob size, **top-aligned**,
    **no numeric display**; a small `CRV` text element above it; the
    numeric is replaced by an 8×8 **curve icon**.
- **Curve icon atlas:** pre-render **32 frames of 8×8**, one per curvature
  step `i/31`, generated at init from the existing `generateCurve`
  (0.0 = logarithmic → 0.5 = linear → 1.0 = exponential). Store as a
  single texture; the curve dial draws the frame for
  `round(curvature * 31)`.
- **Stage labels:** `ATK` / `DEC` text elements pinned top-right of the
  box (replacing the full `ATTACK`/`DECAY` dial labels).
- The box border uses `BorderStyle`; the two dials remain normal graph
  children so existing navigation selects each independently, and the
  `changeGraphSelection`/`KM_EDIT` dial-edit paths are unchanged.
- Applies to **envelope stage curvature only** (user directive) — not to
  LFO/RND shape or the mod-row interpolation type.

### Testing
- Unit: curvature→icon index mapping; `generateCurve` frame generation
  produces distinct frames; stage geometry (two selectable children) via
  `test_graph_nav`.
- Harness: nav into a stage selects rate then curve; EDIT on curve changes
  the icon index; `ATK`/`DEC`/`CRV` text nodes present.

---

## WS-C — Cursor retention on rebuild (items 6,7)

### Problem
Cycling a modulator's type and adding a modulator both rebuild the
instrument graph; the cursor warps to the top of the screen (`RATIO1`).

### Design
- Add graph-search helpers next to the existing route-reselect machinery:
  `GuiNode *findSourceTypeNode(GuiNode *root, Instrument *inst, int idx)`
  and reuse `findSelectableByName` for the add button.
- In `cbCycleSourceType`: after `rebuildInstrumentGraph()`, re-select the
  same source's TYPE button (`findSourceTypeNode`). The source index is
  unchanged by `changeModType`, so `sc->idx` is stable.
- In `cbAddModSource` / `addRuntimeSource`: after the rebuild, re-select
  `MODS_ADD` so repeated additions don't require re-navigating from the
  top.
- Mirror the guarded pattern already used for route reselect (only
  `changeGraphSelection` when the found node is non-NULL).

### Testing
- Harness: a fixture that cycles a type and asserts `selected==` the type
  button (not `RATIO1`), and one that ADDs and asserts `selected==MODS_ADD`.
  Names are stable (`ENV`/`LFO`/`RND` type buttons; `MODS_ADD`).

---

## WS-D — Multi-screen render mode (item 3)

### Design (hot-reload window, per the user's preference)
- New CLI flag `--ui-panels` that boots the app in a **panel mode**
  window instead of the normal single-screen loop.
- The window holds a grid of panels. Each panel renders a representative
  screen into its own `RenderTexture2D` (the existing `createPresentTarget`
  pattern) and is blitted scaled into its cell. A key (e.g. SPACE) cycles
  which panel is zoomed full-window.
- **Representative panels:**
  1. Arranger.
  2. Pattern screen — note page.
  3. Pattern screen — a modulation-track page.
  4. Instrument screen, one panel per **distinct voice type** in
     `settings.voiceTypes` (FM, BLEP, sample, …).
  5. Modals: route picker, delete-confirm, preset load list.
  6. Routing overlay with a few routes wired (scripted setup, no live
     input).
- **Hot reload:** a per-frame mtime check on `layout.json`, `clr.json`
  and the active font file; on change, re-run the equivalent of
  `compileLayoutConfig` + colour-scheme load + `InitGUI` font reload, then
  rebuild every panel's graph. The app keeps running.
- **No audio** in panel mode (device-independent, deterministic under
  Xvfb).
- Fallback: if hot-reload proves materially heavier than a static PNG
  dump, ship the PNG approach and say so (user's stated fallback).

### Testing
- Boot under Xvfb with `--ui-panels`, confirm the window runs N frames
  without crash, then exit. Add `--ui-panels-frames N` for scriptable
  auto-exit.
- This mode is also the primary visual-verification aid for WS-A/B/E.

---

## WS-E — Fonts & icons

### E.1 Primary font swap (item 1)
- Primary font is `pixelFont = LoadFontEx(gFontConfig.path,
  gFontConfig.size, …)`, default `resources/fonts/console.ttf` size 9
  (fallback in `InitGUI`), configured via `cfg.json`.
- Swap the default to a similarly-sized resource. Proposed default:
  `resources/fonts/04B_03__.TTF` at the same size; `cfg.json` keeps it
  switchable. Final choice confirmed visually in WS-D panels (one cfg
  value to retune).
- Verify no text overflow in arranger, instrument, and pattern screens
  (font metrics differ).

### E.2 Instrument name field size (item 5)
- `drawPresetNameGuiNode` (gui_instrument.c:1097-1099) draws at size 30.
  Reduce to ~20 (2/3) and scale the per-cell width/min (`cellW`) so the
  32-char field still fits. Consider sourcing the size from a style so it
  is themeable (flagged extension if so).

### E.3 Pixel icon font / plug glyph (item 4)
- **Ignore the existing `iconzfin.png`/`iconz.png` placeholders** (user
  directive).
- Evaluate freely-usable pixel icon assets. Preferred: a CC0/OFL pixel
  **icon font** with a plug glyph; if no suitable font is found, fall back
  to a CC0 **icon atlas** rendered by an icon element (same machinery as
  the WS-B curve icons). Candidates to evaluate during planning:
  VerzatileDev "Pixel UI Icons" (CC0), Nikoichu "1-bit Pixel Icons"
  (CC0), pixelarticons (license to confirm).
- Replace the `ROUTE` text on route buttons with the plug glyph; keep the
  accessible name/text for fixtures.
- ⚠️ Exact asset + mechanism (font glyph vs atlas) confirmed with the user
  once the license and glyph coverage are checked.

---

## Theme extensions (consolidated, for sign-off)

1. `GuiNode.text` (display text separate from `name`) + `guiNodeSetText`.
2. `TextStyle` + `STYLE_TEXT` + `"text"` style/class + JSON overlay.
3. `BtnStyle.sublabel` (`LabelStyle`) + button draws `text`/sublabel.
4. `ValueStyle.fontSize` (so the rate readout can shrink).
5. `env-stage` layout + `stage-rate`/`stage-curve` dial classes.
6. `CurveIconStyle` (+ generated 32×8×8 atlas).
7. (Conditional) an `IconStyle`/icon element if the plug ships as an
   atlas rather than a font glyph.

## Testing strategy (overall)

- Unit tests for all pure logic (style resolution, icon-index mapping,
  geometry, text lifecycle), keeping the suite green.
- Instrument-harness fixtures for every interactive change.
- WS-D panel mode is the visual regression aid; boot smoke under Xvfb
  after each workstream.
- App boot smoke (`spectrax` under Xvfb) after each workstream.

## Out of scope

- A full theme editor / runtime colour picker.
- Replacing the interpolation model or audio behaviour.
- Multiple alternative fonts beyond the one swap (configurable, but only
  one default chosen).
