# Grounded Component/Layout Wiring — Chip, Mod Rows, Step Cells, Route-Dest

Date: 2026-09-09 · Branch: `layout-config`
Follows: `docs/superpowers/research/2026-09-08-layout-system-css-parallels.md`

## 1. Goal

Develop the layout/style system by **defining and wiring the representations of
components the app already renders but which are currently bespoke**. The
feature gaps identified in the research (composed-node model, intrinsic sizing,
margin/gap, state colours, theme-name style wiring) are solved *as a necessity*
of wiring those representations — not as standalone refactors.

Four targets, each grounded in a real node:

| target | current state | representation defined |
|---|---|---|
| instrument chip | `drawInstChipGuiNode` hand-places 6 regions inline + hardcoded `cs.*`/`chipPalette` | `ChipStyle` struct + `chip` class + geometry helper + intrinsic height |
| mod source rows | `appendModSourceEntry` builds rows with weight/blank hacks | `mod-source-row` named layout with `gap` |
| step cells | `drawStepGuiNode` inline `cs.*` if/else for empty/playhead/selected | `StepCellStyle` struct with state colours |
| route-dest | `drawRouteDestGuiNode` hardcoded `cs.routeAdd`/`cs.labelSelected` | `DestStyle` struct |

Representation model: **per-component structs** (the dial's pattern — each
component is a `StyleType` with fixed sub-element structs; code defines structs
+ baked defaults; `layout.json` overrides per class via `extends`). Colours in
styles are **theme names**, never hex.

## 2. Component representations

### 2.1 Chip — `ChipStyle`

```c
typedef struct { int size; int gap; Color colour; Color colourActive; } DotsStyle;
typedef struct {
    BorderStyle border;        /* gains colorSelected (below) */
    Color palette[8];          /* labelColourIdx → bg; theme names in JSON */
    LabelStyle voiceCount;     /* "V:N" top-left */
    LabelStyle typeTag;        /* FM/SMP/BLP top-right */
    LabelStyle label;          /* 8-char channel label, centred */
    LabelStyle patchName;      /* bottom-left */
    DotsStyle dots;            /* bottom-right active lights */
} ChipStyle;
```

- `computeChipGeometry(const GuiNode *gn, const ChipStyle *st, ChipGeometry *out)`
  positions every region from the style (replacing the inline hand-placement).
  `ChipGeometry` carries the rects for voiceCount/typeTag/label/patchName/dots.
- `chipComponentHeight(const ChipStyle *st)` — intrinsic content height
  (`max(voiceCount offset+font, label offset+font, patchName offset+font, dots
  extent)`), the chip-row analogue of `dialComponentHeight`.
- The **expanded strip** (swatches + label input) keeps its bespoke interactive
  body but reads `palette` + `border` from the style (the report's "contain a
  custom body in a styleable frame" rule).
- The 8 palette colours are **theme names**: `ColourScheme` gains
  `chipPalette0..chipPalette7` (baked defaults = the current `chipPalette[8]`
  values from gui_arranger.c; present in clr.json). The chip class references
  them: `"palette": ["chipPalette0", ..., "chipPalette7"]`.
- `BorderStyle` gains `colorSelected` (default = `color`) so the chip border
  resolves `selected ? colorSelected : color`. Existing border users (dial,
  btn, type-label) are unaffected (their `colorSelected` stays defaulted).

### 2.2 Mod source rows — `mod-source-row` layout

A named layout in the `layouts` registry:

```json
"mod-source-row": { "orientation": "horizontal", "padding": 1, "gap": 2,
                    "weights": [3, 4, 4, 4, 4, 3, 2, 1] }
```

`weights[i]` mirrors the current row proportions (type selector 3, param dials
4, ROUTE 3, DEL 2, trailing blank 1), sized for the ENV row's 8 children;
shorter rows (LFO/RND) use the prefix and apply the trailing default weight.
Applied via `applyLayout(row, "mod-source-row")` in `appendModSourceEntry`
(replacing the current hand-rolled weights). The row's internal widgets
(type selector, dials, ROUTE/DEL buttons) are already dials/buttons and stay
as-is — only their arrangement becomes data.

### 2.3 Step cell — `StepCellStyle`

```c
typedef struct {
    Color bgEmpty;            /* no note, not playing */
    Color bgPlaying;          /* playhead on this step */
    Color borderSelected;     /* selected cursor outline */
    LabelStyle note;          /* note text font/spacing/colour */
} StepCellStyle;
```

`drawStepGuiNode` resolves state → colour from the style (empty/playing/
selected) instead of the inline `cs.defaultCell`/`cs.highlightedCell`/
`cs.outlineColour` if/else. The selected outline stays a 6px grow (geometry,
not style).

### 2.4 Route-dest — `DestStyle`

```c
typedef struct {
    Color border;
    Color borderSelected;
    int borderWidth;
} DestStyle;
```

`drawRouteDestGuiNode` reads `DestStyle` (replacing `cs.routeAdd`/
`cs.labelSelected` + the literal 2.0 width).

## 3. Mechanism additions

All four are *required by* the wiring above; none is a standalone refactor.

1. **State colours.** Component structs own their state-variant colours; draw
   fns resolve `gn->selected ? sel : base` from the style. Covers chip border,
   step-cell fills, route-dest outline (and, incidentally, keeps the dial's
   existing `label.colorSelected` untouched).
2. **`gap`.** `GuiNode` gains `int gap` (zero-initialized per the initGuiNode
   rule). `reflowCoordinates` distributes `contentDim − gap·(itemCount−1)`
   across the weight bands; x/y advance by `share + gap`; the last child
   absorbs the remainder. `gap == 0` reproduces today's output byte-for-byte.
   `LayoutDef` carries `gap`; `applyLayout` sets it on the container.
3. **Registry.** `StyleType` gains `STYLE_CHIP`, `STYLE_STEP_CELL`, `STYLE_DEST`;
   per-type class arrays + counts + `resolve<Type>Style(gn)` (same shape as the
   four existing resolvers); class names `"chip"`, `"step-cell"`, `"route-dest"`
   registered in the default-name table; `overlay*` parse fns for each new
   struct; `compileLayoutConfig` handles the new style objects.
4. **Palette in the theme.** `ColourScheme` + clr.json gain the 8 chip palette
   entries (defaults-before-load + alpha-0 rule apply as with every new theme
   key).

## 4. Wiring

- `createInstChipGuiNode`: regions drawn via `computeChipGeometry` +
  `resolveChipStyle`; `chipPalette` global removed (style owns it).
- `appendModSourceEntry`: `applyLayout(row, "mod-source-row")`.
- `drawStepGuiNode`: reads `resolveStepCellStyle`.
- `drawRouteDestGuiNode`: reads `resolveDestStyle`.
- All four still draw within their own rect (chip's expanded strip bounds
  unchanged).

## 5. Testing

- **Unit (tests/dsp/test_layout.c + test_graph_nav.c):**
  - gap distribution: `[1,1,1]` in a 100px row with `gap 4` → children fill
    `100 − 2·4`, remainder to last; `gap 0` matches the pinned geometry.
  - `computeChipGeometry`: each region's rect from a known style + node rect.
  - `chipComponentHeight`/`stepCellComponentHeight` intrinsic queries.
  - state-colour resolution: selected vs base for chip border + route-dest.
  - compile/extends for the new classes; palette resolved from theme names.
- **Fixtures:** existing 8/8 stay green (chip_meta + pagenav cover chip/mod-row
  nav; route_lines covers dest). No pixel-output asserts — the visual shift
  from moving hand-placed regions into the style is verified by capture only.

## 6. Scope notes

- The **buffer scroller + sample waveform** keep their bespoke bodies (the
  report's "contain, don't force-fit" rule) — out of scope here.
- The chip's **expanded swatch strip** stays bespoke internally but consumes
  `palette`/`border` from the style.
- `gap` is layout-scoped (per named layout), not a global default — current
  layouts render byte-identical until a layout opts in.