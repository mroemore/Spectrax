# Phase B — 2x type, mixed fonts, header-row dial grid

Phase B of the small-screen rework. Phase A (`2026-11-13-small-screen-type-phase-a.md`)
shipped 1.5x type (Pixel Times @14) with the layout still shaped like the 9px
UI: per-dial labels, ~17px gutters, proportional digits in value boxes.

Decisions locked by the user before this plan:

1. **Mixed fonts** — Pixel Times for labels, a monospace face for values, so
   digits are tabular and a changing value cannot shift horizontally.
2. **Header row + OP gutter** on the FM grid — the five column names appear
   once at the top, each op row is tagged in a left gutter, and the per-dial
   label is dropped from the op rows.
3. **Gutters ~10px** (from ~17px) on every screen.
4. **2x type scale** (18px labels/values) as the target for the grid layout.

## Global Constraints

- 640×480 fixed logical frame; 1 logical px = 1 device px on RG35XX SP and
  Pixel 2 (`SCREEN_W/H` in `src/settings.h:39-40`).
- Fonts ship in `bin/resources/fonts/`; `styleFont()` maps a style's `font`
  name to a loaded `Font` (`src/gui_core.c:259`). A new name needs a new
  global and a loader in `InitGUI`.
- The instrument_harness never seeds `FontConfig` (it renders with the
  `InitGUI` fallback) but it DOES load `bin/layout.json`
  (`instrument_harness.c:2107`), so geometry changes must keep existing
  fixture asserts honest — update pinned numbers, never weaken them.
- `meson install -C build` before any scripted run (memory #1312).
- Restore `bin/s1.sng` after scripted runs (memory #1291).
- Nothing is pushed without explicit go-ahead (memory #1078).
- Sprite fonts (`setback.png` = `text`, `iconzfin.png` = `symbol`) are fixed
  12px cell atlases; they do not scale with `gFontConfig.size`. Phase B
  scales them by an explicit factor rather than re-authoring the sheets.

## Review Focus

1. **Value overflow in the 2x box.** `%05.2f` (5 glyphs) in a box that must
   sit inside a 124px dial cell. Expected: mono @18 measures ≤ 56px for
   "88.88"; the box is 62px wide and the composition fits the cell.
2. **Row budget.** Meta 5 + preset 5 + controls 13 + pad 1 + mod line 2 of 26
   weights on the instrument screen; at 2x the control box must still hold
   four op rows plus a header, with the mods row reachable by scroll.
3. **Header/gutter alignment.** Header cells and op-row dials must share the
   same column geometry (same weights, same padding), or the labels point at
   the wrong dial.
4. **Label semantics.** Dropping the per-dial label must not drop the
   information: OP1..OP4 in the gutter and the column names in the header
   have to make "FEEDBACK of OP3" readable without hovering anything.
5. **Sprite-font scale** must not change glyph advances in the pattern grid
   (step cells are sized from the note font) — measure before/after.

---

### Task B1: Gutters to ~10px

- [ ] Instrument screen: main row weights 1:36:1 → 1:61:1 (640/63 ≈ 10.2px).
- [ ] Arranger: `pad1`/`margin2` weights on the root row, same ratio.
- [ ] Pattern screen: its own side weights, same ratio.
- [ ] Verify with harness REPORTs + screenshots; update any fixture that pins
      a rect derived from the old gutter.

### Task B2: 2x type scale

- [ ] `bin/clr.json` font size 14 → 18; `main.c` fallback seed matches.
- [ ] Style faces: dial value 18, dial label 18, btn 18/14, type-label 18,
      chip 15/12/11, step-cell 18, stage dials scaled by the same factor.
- [ ] Dial geometry: knob 28 → 37, value box 52×20 → 62×26, label offsetY
      26 → 34 (baked defaults in `gui_style.c` follow).
- [ ] Verify: 5 dials/row at 620px content = 124px cells; composition
      37 + 2 + 62 = 101px ≤ 124px.

### Task B3: Monospace values (mixed fonts)

- [ ] `InitGUI`: load `console.ttf` as `monoFont` at the value size.
- [ ] `styleFont()`: map `"mono"` to it.
- [ ] `dial` / `dial-discrete` value and stage-dial value fonts → `mono`.
- [ ] Verify: modulate a value and confirm the digits do not shift (measure
      the value box's left edge and the glyph cell width before/after).

### Task B4: FM header row + OP gutter

- [ ] New layout entries `fm-header` and `fm-row` (gutter weight 1, five
      equal dial weights, identical padding).
- [ ] Gutter cell per op row: `OP1..OP4` text nodes.
- [ ] Header row: `RATIO | FEEDBACK | LEVEL | OUTLV | PITCH` text nodes.
- [ ] Op dials resolve to a `dial-unlabelled` class (label fontSize 0).
- [ ] Verify: 4 op rows + header fit the control box at 2x; mods row scrolls;
      fixtures updated for the new row height.

### Task B5: Sprite fonts at 2x

- [ ] `initCustomFont` gains a scale factor; `textFont`/`symbolFont` load at
      2x by scaling each glyph's source and destination rect.
- [ ] Verify: pattern-screen note glyphs are the same size as 18px TTF text;
      no advance drift in step cells.

### Task B6: Verification

- [ ] `meson test` 0 failures; harness fixture sweep (17+ pass, only the two
      pre-existing failures allowed).
- [ ] New pinned fixture `ui_type_scale_2x.txt` recording META/preset/FMR
      rows, the header row and the gutter column.
- [ ] Before/after screenshots for all three screens + one instrument screen
      per voice type.
- [ ] Report: per-task numbers, config-migration caveat, device not tested.
