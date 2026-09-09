# Layout System Research — Component Model, CSS Parallels, Expansion + Simplification

Date: 2026-09-09 · Branch: `layout-config`
Status: research / contemplation — no implementation commitment

## 1. The dial as the reference component

The dial (`drawDialGuiNode` / `drawDiscreteDialGuiNode`) is the only node in the
system that has the full intended treatment, and it is the model the rest of this
report measures the other nodes against.

Its structure is a **composition of sub-elements**, each with its own style:

| sub-element | style struct | role |
|---|---|---|
| `border` | `BorderStyle` | panel rect + outline (roundness, borderWidth, colour) |
| `knob` | `KnobStyle` | arc sector + knob texture (size, radius, angles, offset, asset) |
| `value` | `ValueStyle` | numeric readout (format, w/h, offsets, colour) |
| `label` | `LabelStyle` | caption (font, size, spacing, offsets, colours) |

The style is resolved through a **class system**: a node carries an optional
`className`; `resolveDialStyle(gn)` returns the class entry or the type default;
`extends` chains give inheritance (e.g. `dial-discrete` derives from `dial`).
Colours in styles are **theme names**, not hex literals, resolved once at boot.

Two helpers make the composition *queryable*:

- `computeDialGeometry(gn, st, &g)` — positions all sub-elements from the cell
  rect + style. This is where the mini-layout lives (knob-centred group,
  cell-centred caption).
- `dialComponentHeight(st)` — the natural content height
  (`max(knob.size, value.offsetY+value.height, label.offsetY+label.fontSize)`),
  a first step toward **intrinsic sizing**.

**Why this is the right model:** the draw fn has no hardcoded geometry or colour
literals — every pixel derives from the style, and the geometry is separable
from the drawing (testable without a GL context).

## 2. Survey of the other nodes

| node | draw fn | composition | style-driven? | geometry helper? | private state? |
|---|---|---|---|---|---|
| dial | `drawDialGuiNode` | border + knob + value + label | yes (DialStyle) | `computeDialGeometry` | no |
| discrete dial | `drawDiscreteDialGuiNode` | same | yes | same | no |
| action button | `drawActionBtnGuiNode` | border + label | yes (BtnStyle) | inline offsets | no |
| type-label | (`typeLabel` draws) | border + label | yes (TypeLabelStyle) | inline offsets | no |
| route-dest | `drawRouteDestGuiNode` | outline only | **no** (`cs.routeAdd`/`cs.labelSelected`) | none | no |
| wrapper | `drawWrapperNode` | panel bg + border | **no** (`cs.panel`/`cs.wrapperBorder`) | none | no |
| instrument chip | `drawInstChipGuiNode` | bg + border + 4 text regions + N active-dots + expanded overlay | **no** (all `cs.*` + `chipPalette`) | **none — 6 regions hand-placed inline** | yes (expanded, swatchFocus) |
| step cell | `drawStepGuiNode` | bg fill + note text | **no** (`cs.*`) | none | yes (`StepNodeData` in `p`) |
| name input | `drawPresetNameGuiNode` | bg + outline + 32 char cells + cursor | **no** (BLACK/RED + `cs.*`) | none | yes (editing, cursor, flashes) |
| sample waveform | `drawSampleWaveformGuiNode` | custom waveform + loop lines + label | **no** (colours copied to node at create) | none | yes (own struct) |
| scroll container | (scrollable reflow) | container of rows + viewport + scrollbar | n/a | n/a | yes (offset, contentH, rowH) |
| plain container | (reflow) | weight-based children | n/a | n/a | no |

### How they are the same

- Every node is a `GuiNode` in a tree with `(x, y, w, h, padding, alignment)`,
  a `draw` fn, an optional `p` data slot, and a `className` slot.
- Every leaf draws **within its own rect**; none draw outside it (except the
  chip's expanded overlay, which is explicitly bounded).
- Most colour choices come from the shared `ColourScheme` (`cs.*`), so the
  *theme* is data-driven even where the *style* is not.
- The reflow (`reflowCoordinates`) sizes every node top-down by weight — the
  same mechanism for all containers.

### How they differ

1. **Style-driven vs hardcoded.** Only dial/btn/type-label read styles.
   Route-dest, wrapper, chip, step cell, name input and the waveform all bake
   `cs.*` literals (or worse, `BLACK`/`RED`) into their draw fns. A theme
   change can recolor the dials but not the chips.
2. **Composition vs bespoke.** Only the dial has sub-element styles + a
   geometry helper. The chip is the most extreme counterpoint: six separately
   positioned regions (V count, type tag, label, patch name, active dots,
   expanded swatches/input) all hand-placed inline in one ~120-line draw fn.
3. **Private state.** Chip, name input, step cell and waveform subclass
   `GuiNode` with their own structs; their draw fns branch on that state
   (`expanded`, `editing`/`cursor`, `stepIndex`, sample ptr). The dial has no
   state — pure style + parameter.
4. **State-dependent visuals are hardcoded ternaries.** `gn->selected ? A : B`
   appears in the dial/btn labels (now styled via `colorSelected`), but the
   step cell's selected/playhead/empty fills, the chip's selected border, and
   the name input's editing cursor are all inline `if/else`s on colours.
5. **Custom rendering.** The waveform + buffer scroller draw actual geometry
   (lines per column, sample points) that no style vocabulary covers yet.

## 3. CSS/HTML parallels

Our system is already a small CSS in disguise. Mapping:

| our concept | CSS equivalent | present? |
|---|---|---|
| GuiNode tree (built in code) | DOM tree | yes (structure in code, not markup) |
| `className` + style classes | CSS class selectors | yes |
| `extends` chain | CSS inheritance (partial) | yes |
| `nodeAlignment` + weights | flexbox (row/column + flex-grow) | yes |
| `padding` | CSS padding | yes |
| `gn->selected` | `:focus` / `:active` pseudo-state | partial (only label colours) |
| layouts registry + `applyLayout` | CSS layout declarations / classes | partial (2 entries, 1 used) |
| scroll container | `overflow: scroll` + viewport | yes |
| theme colour names (`cs.*`) | CSS custom properties (variables) | yes (all colours) |
| **margin / gap** | CSS margin / gap | **no** |
| **intrinsic sizing** | `min-content` / `auto` | **no** (added for dial height only) |
| **bottom-up size pass** | CSS layout algorithm | **no** (purely top-down) |
| **contextual rules** (descendant selectors) | `container .dial { ... }` | **no** |
| **state styles** (`:hover`, `:selected`) | full pseudo-class styling | **no** |
| box-sizing / content-box | `box-sizing` | implicit (padding inflates) |

The biggest structural gaps against CSS are **intrinsic sizing** (we have no
bottom-up size pass — containers cannot size to their content) and **margin/
gap** (the system uses padding only, which is why blank spacer nodes existed
until the FM op-row fix removed one instance).

## 4. How the system could be expanded

### 4.1 Generalized component model
Generalize the dial's pattern into a **composed node**: a leaf whose draw is a
small list of positioned sub-elements, each with a style + a layout. The chip
(6 regions), name input (bg + cells + cursor) and route-dest (outline + label)
all become instances of the same mechanism. Each gains:

- a `StyleType` (chip-style, name-input-style, dest-style) registered like
  dial/btn/type-label;
- sub-element styles parsed from `layout.json` (so the chip's V count, type
  tag, patch name and dots become styleable);
- a geometry helper (like `computeDialGeometry`) so positions are computed,
  not hand-written;
- a content-size query (like `dialComponentHeight`).

This is the single highest-leverage expansion: it converts the last bespoke
draw fns into data.

### 4.2 Intrinsic sizing (bottom-up pass)
Let each leaf report its natural size (`componentHeight` / `componentWidth`
per style type — the dial already has the former). Containers then:
- size to the tallest child when a child has weight 0 (CSS `auto`/`fit-content`);
- give the scroll container a natural `contentH` instead of hardcoded `rowH`;
- let the FM op rows size to `dialComponentHeight` instead of the magic 35.

CSS computes bottom-up (content → item → container) then top-down (position).
We only have the top-down half. A second, pre-layout intrinsic pass is the
structural change that removes most hardcoded sizes.

### 4.3 Margin + gap
Add `margin` to the box model and `gap` to the layout tier. This:
- kills the remaining blank-node spacers (the pattern still used in several
  builders);
- lets a layout say `"gap": 4` instead of padding tricks;
- aligns with flexbox's `gap`/`justify` semantics the user already referenced.

### 4.4 Contextual + state styles
- **Pseudo-state styles:** resolve style by `selected`/`editing`/`expanded`
  state, replacing the `gn->selected ? A : B` ternaries and the step cell's
  inline fill logic. E.g. `"label": { "color": "label", "colorSelected":
  "labelSelected" }` already exists — generalize to `borderSelected`,
  `bgSelected`, etc.
- **Contextual classes:** resolve a node's class by ancestor class
  (descendant selector). This is how the route-dest in the picker layer could
  get its own look, or how "dials inside a scroll container" get tighter
  geometry, without per-node `guiNodeSetClass` calls.

### 4.5 Data-driven layouts
The `layouts` registry is the seed. Grow it to cover every container: `gap`,
`justify` (start/centre/stretch), `minContentRow`, `wrap`. The FM op row is
the first consumer; the mod-wrap rows, envelope rows, chip row and the
arranger rows are natural next consumers.

### 4.6 Scroll container as a first-class layout
The scrollable reflow branch is a special case in `reflowCoordinates`. Promote
it: a scroll container is a container whose layout says `overflow: scroll`,
`rowH`, `scrollbar`. The `contentH`/`scrollOffset`/`scrollToVisible` machinery
is already a clean viewport primitive; giving it a layout descriptor would let
any container become scrollable.

## 5. How the system could be simplified

### 5.1 Collapse bespoke draw fns into the composed model
The chip, route-dest, name input, step cell and waveform each exist because
"composed" wasn't general. Once the component model lands, ~5 draw fns +
their inline geometry collapse into data. This is both expansion (4.1) and
simplification — one mechanism replaces many.

### 5.2 One generic style resolver
Four resolver pairs (`resolveDialStyle`/`resolveDiscreteDialStyle`/
`resolveBtnStyle`/`resolveTypeLabelStyle`) share the same shape. A node's
`styleType` field + one `resolveStyle(gn)` returning a `void *` (or a tagged
union) would replace them, and new types stop requiring a new resolver pair.

### 5.3 Reflow as a table, not a switch
`reflowCoordinates` branches on `scrollable`, then `nodeAlignment`, with the
min-size clamp in both axes. A per-alignment layout table (orientation, gap,
justify, child sizing rule) would make the one function declarative — the
layouts registry becomes the source of truth and the switch shrinks to a
lookup.

### 5.4 Colour literals → theme names
The remaining `cs.*` reads in draw fns are already theme-backed, but route-dest
and the chip bypass the style layer entirely. Routing them through styles
removes the last hardcoded colour decisions (the chip palette is the notable
exception — a fixed 8-colour set keyed by `labelColourIdx`, which is data the
style layer should eventually own).

### 5.5 State out of draw fns
`expanded`, `editing`, `cursor`, flash timers currently mix state with
rendering. Formalizing state flags on the node (or in `p`) and letting style
resolution key on them is both simpler (no ternaries) and more consistent.

## 6. Trade-offs / watch-outs

- **Bottom-up sizing is the risky one.** It changes the reflow contract
  (currently: weights are the only size input). Must land behind the existing
  fixture gate (8/8) + the geometry tests; the proportional-reflow change
  already taught us every weight layout shifts when the math changes.
- **The composed-node generalization touches the hot path.** Every draw fn is
  called per frame; the sub-element resolution must stay a pointer chase (as
  the dial's does) — no per-frame string lookups.
- **Bespoke nodes have legitimately bespoke rendering** (waveform, buffer
  scroller). The component model should *contain* them (styleable frame +
  label) while keeping their custom body, not force-fit their internals.
- **Class binding is currently opt-in per node.** Contextual styles (4.4)
  would make classes apply to whole sub-trees; that's a real behavioural
  change for existing fixtures that assert nav by name.

## 7. Suggested order

1. **Intrinsic sizing query per style type** (dial done; add btn, type-label,
   then the chip's regions) — cheap, removes hardcoded heights.
2. **Margin + gap in the layout tier** — kills blank nodes, small surface.
3. **Composed-node model for the chip** (the largest bespoke draw) — proves
   the generalization on the hardest case; route-dest + name input follow.
4. **State/pseudo styles** — replaces the `selected` ternaries.
5. **One resolver + reflow table** — the final simplification pass, guarded by
   the full fixture gate.