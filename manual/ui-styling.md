# Spectrax UI Styling Guide

How to style and arrange Spectrax's UI from JSON — every style type, every
field, the layout solver, the colour system, and the end-to-end recipes for
adding new fields, classes, layouts, colours and style types.

The short version: **there is no CSS.** A `GuiNode` tree is built in C; each
drawable picks a C struct of style values by name, and `layout.json` supplies
those values plus a few container layouts. Nothing hot-reloads — restart the
app after editing.

A worked example of everything below is in
[Building a new component](tutorials/building-a-component.md): a new layout
and a new widget, built step by step and tested.

---

## Contents

1. [Mental model](#1-mental-model)
2. [Where config is loaded from](#2-where-config-is-loaded-from)
3. [Startup order](#3-startup-order)
4. [Styles: classes and resolution](#4-styles-classes-and-resolution)
5. [Style type reference](#5-style-type-reference)
   - [Shared sub-objects](#51-shared-sub-objects)
   - [dial / dial-discrete](#52-dial--dial-discrete)
   - [btn](#53-btn)
   - [type-label](#54-type-label)
   - [route-dest](#55-route-dest)
   - [step-cell](#56-step-cell)
   - [chip](#57-chip)
   - [text](#58-text)
   - [Retired: curve-icon](#59-retired-curve-icon)
   - [Baked defaults](#510-baked-defaults)
6. [Draw behaviour and quirks](#6-draw-behaviour-and-quirks)
7. [Colours (`clr.json`)](#7-colours-clrjson)
8. [Layouts](#8-layouts)
9. [Recipes](#9-recipes)
10. [Testing](#10-testing)
11. [Gotcha checklist](#11-gotcha-checklist)
12. [File map](#12-file-map)

---

## 1. Mental model

Three JSON files, each with one job:

| File | Job | Written by the app? |
|---|---|---|
| `cfg.json` | settings; names the theme file | yes (settings) |
| `clr.json` | colour palette + font | **yes, on exit** |
| `layout.json` | per-widget styles + container layouts | no (read-only) |

A style is resolved in three layers, later layers winning:

```
baked C default (g_defaultX in gui_style.c)
   ↑ overlaid by   every JSON entry on the class's `extends` chain, root → leaf
   =               the resolved style for that class
```

At draw time each node does:

```c
const DialStyle *st = resolveDialStyle(gn);   /* class → type name → baked */
```

So **styling is a lookup, not a cascade of selectors**: one class name per
node, one struct out. Layout (`§8`) is separate and only moves/sizes nodes;
only two styles influence geometry (`computeDialGeometry`,
`computeChipGeometry`).

---

## 2. Where config is loaded from

`src/paths.c` resolves the **config dir** in this order (first hit wins):

1. `--config-dir DIR` on the command line
2. `$XDG_CONFIG_HOME/spectrax` — if it contains `cfg.json`
3. `$HOME/.config/spectrax` — if it contains `cfg.json`
4. `.` (current working directory)

The **data dir** resolves the same way (`--data-dir`, `XDG_DATA_HOME`,
`~/.local/share/spectrax`, `.`).

All three files are read from the chosen config dir:

```
<cfgDir>/cfg.json      → settings, incl. "theme": "clr.json"
<cfgDir>/clr.json      → colours + font
<cfgDir>/layout.json   → styles + layouts
```

### Install vs. your live config

`meson install -C build` runs `meson/install.sh`, which:

- copies `src/layout.json` → `bin/layout.json` **only if `bin/layout.json` is missing**
- seeds `~/.config/spectrax/{cfg,clr,layout}.json` **only if missing**

It never clobbers. Consequences:

- After the first install, edits to `src/layout.json` do **not** reach
  `bin/layout.json` or your live XDG config. Copy explicitly.
- A default `./bin/spectrax` launch loads `~/.config/spectrax/layout.json`,
  not `bin/layout.json`. If new classes don't appear, this is why.
- `bin/instrument_harness` runs with CWD `bin/` and calls
  `compileLayoutConfig("layout.json", …)` directly, so it reads
  `bin/layout.json`. **The harness and the app can read different files.**

### Dev loop

```sh
# JSON only — no rebuild:
cp src/layout.json bin/layout.json                 # harness
cp src/layout.json ~/.config/spectrax/layout.json  # app (or use --config-dir)

# or run the app against the checkout config:
./bin/spectrax --config-dir bin --data-dir bin
```

`clr.json` is overwritten by the app on exit (`saveThemeJson`), so edit it
while the app is closed (or expect your edits to be replaced by the in-memory
scheme on quit).

---

## 3. Startup order

```
cfg.json                     settings + theme filename
  ↓
initDefaultColourScheme()    seed every colour key
loadThemeJson(clr.json)      overlay the theme
  ↓
compileLayoutConfig(layout.json, cs)
  ↓  colours are resolved to RGBA *now* — they are baked into the style structs
InitGUI()                    builds the node tree; draw fns resolve styles
  ↓
finalizeStyles()             loads knob textures named by `knob.asset`
  ↓
draw loop                    styles are constants; nothing re-reads JSON
```

Because colours are baked at compile time, a style's colour field always
holds an RGBA value; changing `clr.json` requires a restart.

---

## 4. Styles: classes and resolution

### 4.1 Choosing a class

Each drawable's style is selected by its `className` (`GuiNode.className`,
a `char *`):

```c
int idx = -1;
if (gn && gn->className) {
    idx = findClass(gn->className, STYLE_DIAL);      /* type-qualified */
}
if (idx < 0) {
    idx = findTypeClass(STYLE_DIAL);                 /* "dial" by name */
}
if (idx >= 0) return &g_dialClasses[g_classMap[idx].index];
return &g_defaultDial;                               /* baked default */
```

The chain is therefore **explicit class → type-named class → baked
default**:

- An explicit, registered class name wins.
- No class name (or an unregistered one) → the class **named after the
  type**, if the config declares one: `"dial"`, `"dial-discrete"`,
  `"btn"`, `"type-label"`, `"step-cell"`, `"route-dest"`, `"chip"`,
  `"text"`.
- Only if the config declares no class of that name does the widget fall
  through to the compiled-in baked default.

This is why the per-type entries in `src/layout.json` are meaningful: the
app's typed factories (`createDialGuiNode` and friends) never call
`guiNodeSetClass`, so a widget with no class of its own still picks up its
type's entry. Before this fallback existed those entries were decorative —
editing `"dial"` did nothing unless a layout bound the node to it
explicitly.
- Matching is exact and case-sensitive.
- The same name may exist under different types (the lookup is
  type-qualified). Two entries with the same name **and** type: the first
  registered wins.

### 4.2 How a JSON entry becomes a class

`compileLayoutConfig` iterates the `styles` object. For each entry:

1. **Classify** it. If the name is one of the type keys (`dial`,
   `dial-discrete`, `btn`, `type-label`, `step-cell`, `route-dest`, `chip`,
   `text`), that's its type. Otherwise follow `extends` (up to 16 hops)
   until a type key is found.
   **A class that does not transitively extend a type key is ignored.**
2. **Build the chain**: starting at the baked default, overlay the root-most
   JSON ancestor, then each descendant, ending at the class itself.
3. Store the merged struct and register the name → `(type, index)`.

```jsonc
{
  "styles": {
    "dial":            { /* the class every unclassed dial uses */ },
    "dial-discrete":   { "extends": "dial", "value": { "format": "%i" } },

    "stage-curve":     { "extends": "dial", "knob": { "size": 12 } },
    "stage-curve-icon":{ "extends": "stage-curve",
                         "value":  { "width": 0, "height": 0 },
                         "curveIcon": { "frameCount": 32, "size": 8,
                                        "color": "text.value" } }
  }
}
```

Note what the last entry does: it inherits everything from `dial`, then from
`stage-curve`, then disables the numeric readout (`value.width/height = 0`)
and turns on the curve-icon overlay.

### 4.3 Binding a class to nodes

Three ways, in order of preference:

**a. `layouts.<name>.childClasses`** — positional, applied by `applyLayout`:

```jsonc
"env-stage": {
  "orientation": "horizontal", "padding": 2, "weights": [2, 1],
  "childClasses": ["stage-rate", "stage-curve-icon"]
}
```

Index *i* names the class of the *i*-th child **in append order**. Use `""`
to leave that child untouched.

**b. In C, when creating the node** — the text factory takes one:

```c
GuiNode *n = createTextGuiNode(x, y, w, h, "PATCH", "chip-patch");
```

**c. In C, after creation**:

```c
guiNodeSetClass(node, "stage-rate");   /* frees any previous name */
```

`guiNodeSetClass` is declared in `gui_style.h`, implemented in `graph_gui.c`.

### 4.4 Limits and silent failure

| Limit | Value | Behaviour past the limit |
|---|---|---|
| Style classes (all types) | 64 | extra classes dropped |
| Classes per type | 32 | extra classes dropped |
| `extends` depth | 16 | classification/stops; base defaults used |
| Layouts | 32 | extra layouts dropped |
| Children per container | 64 | `appendItem` refuses |
| Class / layout name | 63 chars | truncated |

`compileLayoutConfig` returning `false` (missing file, bad JSON, >1 MiB)
leaves the registry empty — **every widget silently renders with baked
defaults**. There is no error print; check the file parses.

**Each compile starts from a clean slate.** `compileLayoutConfig` resets
the class registry, every per-type entry count and the layout table, so a
second call *replaces* the previous config rather than appending to it. The
app and harness still compile once at startup; recompiling is what a future
hot-reload would do. Anything holding a `const LayoutDef *` or a resolved
style pointer across a recompile must re-resolve it.

---

## 5. Style type reference

All coordinates are pixels; angles are degrees. Offsets are `int` unless
noted. `color`-typed keys take a **colour name** from §7 — never a hex value.

### 5.1 Shared sub-objects

#### `knob` (dial only)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `size` | int | 20 | drawn knob diameter |
| `radius` | int | 10 | arc radius; also the texture rotation origin |
| `startAngle` | int | -225 | arc start, degrees |
| `sweep` | int | 270 | full-scale arc sweep, degrees |
| `offsetX` / `offsetY` | int | 0 | nudge the knob image from its computed position |
| `color` | colour | `dial` | arc colour |
| `asset` | string | `""` | PNG path; loaded in `finalizeStyles`, failure → procedural knob |

The arc fills `(baseValue - min) / (max - min) * sweep`. The procedural knob
is drawn over the arc; with an asset, the texture is rotated by
`startAngle + angle`.

#### `border`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `borderWidth` | float | 2.0 | outline thickness |
| `color` | colour | per-type | outline when not selected |
| `colorSelected` | colour | per-type | outline when selected |

`colorSelected` is honoured by every bordered widget. Leave it out (or set a
fully transparent colour) and the widget keeps its legacy selected outline —
theme `highlightedCell` for `dial`/`btn`, `border.color` for `type-label`.
`route-dest` and `chip` seed it with `labelSelected`/`outline`.

#### `value` (dial only)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `format` | string | `"%05.2f"` | `printf` format; continuous dials pass a `float`, discrete pass `(int)` |
| `width` / `height` | int | 38 / 14 | readout box; **≤ 0 hides the numeric** |
| `offsetX` / `offsetY` | int | 28 / 2 | from the knob's top-left |
| `fontSize` | int | 9 | 0 → 9 |
| `color` | colour | `valueText` | text colour |

The box is filled with theme `valueDisplayBg`; text is inset 4 px with a
hard-coded spacing of 1.

#### `label` (dial, btn, type-label, step-cell note, chip parts)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `font` | string | `"pixel"` | `"text"`, `"symbol"`, anything else → pixel font |
| `fontSize` | int | per-type | **0 disables the label** (dial, btn sublabel) |
| `spacing` | int | 1 | glyph spacing |
| `offsetX` / `offsetY` | int | per-type | meaning depends on the host (see each type) |
| `color` | colour | per-type | normal |
| `colorSelected` | colour | per-type | when the node is selected |

#### `curveIcon` (inside a dial)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `frameCount` | int | 0 | 0 = off; > 0 = draw the icon instead of the numeric |
| `size` | int | 8 | icon side length |
| `offsetX` / `offsetY` | int | 0 | nudge from the knob's centre |
| `color` | colour | `text.value` | seeded from the theme (see §5.10); it does not follow `value.color` |

#### `dots` (chip)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `size` | int | 4 | voice-light square |
| `gap` | int | 2 | between lights |
| `colour` | colour | `wrapperBorder` | inactive voice |
| `colourActive` | colour | `outline` | active voice |

Note the British spelling: `colour`, `colourActive`.

---

### 5.2 `dial` / `dial-discrete`

**Used by:** `drawDialGuiNode` / `drawDiscreteDialGuiNode` (continuous and
discrete parameter knobs). Both are the default for their factories when no
class is set.

**Keys:** `knob`, `border`, `value`, `label`, `curveIcon`, `vAlign`.

| Key | Type | Default |
|---|---|---|
| `vAlign` | string | `"center"` |

`vAlign` accepts exactly `"top"` or anything else (= centre). A centred knob
can never leave room for the numeric below it; `"top"` pins the knob to the
cell's padded top so `value.offsetY` can place the readout underneath.

**Geometry** (`computeDialGeometry`):

```
cx = x + padding;  cy = y + padding
cw = w - 2*padding; ch = h - 2*padding
groupW = knob.size + value.offsetX + value.width
knobX  = groupW < cw ? cx + (cw - groupW)/2 : cx + 2
knobY  = vAlign == top ? cy : (knob.size < ch ? cy + (ch - knob.size)/2 : cy)
valueX = knobX + value.offsetX;  valueY = knobY + value.offsetY
labelX = cx + cw/2 + label.offsetX      (a CENTRE, not a left edge)
labelY = knobY + label.offsetY          (relative to the KNOB TOP)
```

`dialComponentHeight(st) = max(knob.size, value.offsetY + value.height,
label.offsetY + label.fontSize)` — useful when sizing a row by hand.

`dial-discrete` ships with `extends: "dial"` and overrides only the numeric
format/size and the label offset.

---

### 5.3 `btn`

**Used by:** `drawActionBtnGuiNode` (PREV/NEXT, TOGGLE, action rows).

**Keys:** `border`, `label`, `sublabel`.

- **Label**: drawn at `(x + padding + label.offsetX, y + padding +
  label.offsetY)`, left-aligned, text = `gn->text ? gn->text : gn->name`.
- **Sublabel**: only when `sublabel.fontSize > 0` and `gn->sublabel` is set.
  Horizontally **centred** on the node; `y = gn->y + sublabel.offsetY`
  (no padding added).

---

### 5.4 `type-label`

**Used by:** the static type tag between PREV and NEXT on the instrument
screen (`drawTypeLabelGuiNode`).

**Keys:** `label`, `border`.

- Text is centred in the cell, then shifted by `label.offsetX/offsetY`.
- Text uses `label.color`, or `label.colorSelected` when the cell holds the
  cursor. Both default to the theme's `label` / `labelSelected`.
- Border is drawn **only when selected**; `border.colorSelected` if set,
  otherwise `border.color`.

---

### 5.5 `route-dest`

**Used by:** the mod-routing destination picker's empty slots
(`drawRouteDestGuiNode`).

**Keys:** `border` only.

No fill, no text: just an outline. `color` when unselected, `colorSelected`
when the cursor is on the slot. `borderWidth` scales per cell size.

---

### 5.6 `step-cell`

**Used by:** pattern-screen note cells (`drawStepGuiNode`) and pattern-track
automation cells (`drawTrackStepNode`).

**Keys:** `bgEmpty`, `bgPlaying`, `borderSelected` (colours), `note` (label
object).

| Key | Meaning |
|---|---|
| `bgEmpty` | cell fill, no note |
| `bgPlaying` | fill when the playhead hits the step; **also the bar fill colour** for automation cells |
| `borderSelected` | 3 px surround drawn around the selected cell |
| `note` | pitch text; `offsetX/offsetY` from the cell origin (no padding); `fontSize: 0` → `textFont.baseSize` |

---

### 5.7 `chip`

**Used by:** the instrument chips above the arranger (`InstChipGuiNode`,
which hard-codes `className = "chip"`).

**Keys:** `border`, `palette`, `voiceCount`, `typeTag`, `label`,
`patchName`, `dots`.

`palette` is an array of up to 8 colour names; the chip's background is
`palette[arranger->labelColourIdx[channel] % 8]`.

**Geometry** (`computeChipGeometry`):

```
voiceCount: (x + ox, y + oy)
typeTag:    right-aligned → (x + w - textWidth + ox, y + oy)
label:      centred      → (x + (w - textWidth)/2 + ox,
                           y + (h - fontSize)/2 + oy)
patchName:  (x + ox, y + h + oy)      ← use a NEGATIVE oy to sit inside
dots:       bottom-right → (x + w - rowWidth - 3, y + h - dots.size - 3)
```

`chipComponentHeight(st) = label.fontSize + patchName.fontSize + dots.size`
— the minimum height at which the centred label band and the bottom strip
(patch name + lights) do not overlap.

`voiceCount`, `typeTag`, `label`, `patchName` are label objects; their
`colorSelected` fields are **not** used (chips use the border for selection).

---

### 5.8 `text`

**Used by:** `createTextGuiNode` (static captions; **non-selectable**, so
navigation skips them).

**Keys:** `font`, `fontSize`, `spacing`, `offsetX`, `offsetY`, `hAlign`,
`vAlign`, `color`, `colorSelected`.

| Key | Values |
|---|---|
| `hAlign` | `"left"` (default), `"center"`, `"right"` |
| `vAlign` | `"top"` (default), `"middle"`, `"bottom"` |

Alignment is computed against the node's own `w`/`h`, *then* the offset is
added. No fill, no border. Note `"center"`/`"middle"` also accept `"top"` as
a synonym for the start value (the parser maps `left|top → 0`,
`center|middle → 1`, `right|bottom → 2`).

---

### 5.9 Retired: `curve-icon`

There is no standalone curve-icon style type. Configuring one through
`DialStyle.curveIcon` (§5.1) is the only mechanism, and the shipped config
must not define a `curve-icon` class — the shipped-config guard in
`test_layout` rejects it.

---

### 5.10 Baked defaults

These are the values used when a class omits a key (and when no class is
bound). They live in `src/gui_style.c`, and their colours are seeded from the
theme by `resolveDefaultColours`.

| Type | Notable baked values |
|---|---|
| `dial` | knob 20/10/−225/270; border 2; value `"%05.2f"` 38×14 at (+28,+2) fs 9; label pixel fs 9 sp 1 at (+0,+18); `vAlign` centre |
| `dial-discrete` | value `"%i"` 10×14 at (+6,+5); label at (+6,+21) |
| `btn` | border 2; label pixel fs 10 at (+4,+4); sublabel pixel fs 8 sp 0 at (+4,+12) |
| `type-label` | label pixel fs 10; border 2 |
| `route-dest` | border 2; `color = mod.routeAdd`, `colorSelected = text.mutedSelected` |
| `step-cell` | bg `cell.default` / `cell.highlighted`; surround `line.strong`; note `text` fs 0 sp 4 at (+4,+4) |
| `chip` | border 2; palette `chip.palette0..7`; voiceCount pixel 9 at (+4,+2); typeTag pixel 9 at (−4,+2); label pixel 12; patchName pixel 8 at (+4,−11); dots 4/2 |
| `text` | pixel fs 10 sp 1, left/top |

Theme seeds: `control.dial`, `text.value`, `text.muted`,
`text.mutedSelected`, `line.panel`, `line.strong`, `mod.routeAdd`,
`line.wrapper`, `cell.default`, `cell.highlighted`, `text.primary`,
`chip.palette0..7`. The embedded `curveIcon.color` is seeded with
`text.value`, so a class that only sets `frameCount` still draws a visible
glyph.

---

## 6. Draw behaviour and quirks

Read this before debugging a style that "does nothing".

1. **`border.colorSelected` needs a non-zero alpha to take effect.** An
   unset (or transparent) value deliberately falls back to the legacy
   highlight.
2. **Dial label offsets are relative to the knob**, not the cell, and X is a
   *centre*. The Y is `knobY + offsetY`, so a top-aligned knob moves the
   caption with it.
3. **A centred knob cannot fit a numeric below it.** In a 32 px box a
   20 px knob leaves 6 px above and below; the readout needs 14. Use
   `"vAlign": "top"` for dials that need a numeric under the knob.
4. **`value.width`/`height` ≤ 0 hides the numeric** — that is how the curve
   dial shows only its icon.
5. **Curve icon frames truncate.** The frame index is
   `round(baseValue × 31)`, clamped, then clamped again to
   `frameCount − 1`. A `frameCount` below 32 collapses the top of the value
   range onto the last frame. Use 32 (the atlas size) for a 1:1 mapping.
6. **Curve icon colour comes from `curveIcon.color`** (seeded from
   `valueText`).
7. **`knob.asset` paths are relative to the process CWD at startup**
   (`bin/` in practice). They load once in `finalizeStyles`; a failed load
   silently falls back to the procedural knob. `asset: ""` in a subclass does
   not clear an inherited `hasAsset`, but with no path nothing loads, so the
   fallback is what you see.
8. **`sublabel` is anchored without padding** and is horizontally centred;
    the main `label` is left-aligned and padding-relative. They are not
    siblings with the same origin.
9. **`step-cell` note offsets have no padding**; the cell's own `padding`
    only affects children.
10. **Chip `patchName.offsetY` is meant to be negative** (bottom-anchored:
    `y + h + oy`).
11. **Text nodes are non-selectable** — they never receive the cursor, so
    `colorSelected` is unreachable for them.
12. **Unknown colour names are silent no-ops**, and so are unknown class
    names — a widget falls through to the class named after its type,
    then to the baked default, and nothing warns. A typo leaves the
    inherited value in place.

---

## 7. Colours (`clr.json`)

```jsonc
{
  "schema": 2,
  "font": { "path": "resources/fonts/console.ttf", "size": 9, "spacing": 1 },
  "colors": {
    "surface": { "window": "#CF6E3AFF", "panel": "#503C3CFF", ... },
    "text":    { "primary": "#631100FF", "muted": "#C8B4B4FF", ... },
    "chip":    { "palette": [ "#4682B4FF", ... ] }
  }
}
```

### Format

- `#RRGGBB` or `#RRGGBBAA`; the leading `#` is optional.
- **Alpha 0 is treated as "unset"** on load (`#00000000` keeps the default).
  Do not use fully transparent values in the theme.
- Unknown keys are ignored; missing keys keep their seeded defaults.
- `colors` groups keys by use case. Groups are **nesting sugar only** —
  `surface.panel` is the key, whether it is written nested or (legacy)
  flat as `"panel": "..."`.
- `chip.palette` is the one array; it is equivalent to
  `chip.palette0` … `chip.palette7`.
- The app writes the **entire** scheme back on exit in canonical grouped
  form (`schema: 2`), so a legacy flat theme is migrated automatically
  the first time it quits.
- `font.path` and `font.size` feed `LoadFontEx` for the pixel font.
  `font.spacing` is parsed but unused (each label style carries its own).

### Canonical colour names

These are the valid tokens for any style's `color`/`colorSelected` field
(`themeFieldByName` is authoritative). The canonical list is the *path*
form; the flat name from an older theme is accepted as an alias.

| Group | Keys | Used for |
|---|---|---|
| `surface` | `window`, `panel`, `inset`, `dim` | app background, widget panels, value-display wells, modal scrim |
| `text` | `primary`, `secondary`, `muted`, `mutedSelected`, `value`, `gridLabel`, `disabled` | body text, dim text, labels, focused labels, dial readouts, arranger cell text, unavailable controls |
| `line` | `panel`, `wrapper`, `strong`, `grid`, `step` | panel borders, wrapper outlines, emphasis borders, graph grid lines, step-cell borders |
| `cell` | `default`, `blank`, `highlighted`, `selected` | sequencer/arranger cell states |
| `control` | `dial`, `accent` | knob fill, global accent (record/armed) |
| `waveform` | `bg`, `primary`, `alt`, `overlay`, `playhead` | scope/spectrogram backgrounds and traces |
| `sample` | `bg`, `bgAlt`, `border`, `closed` | sample editor rows, slice borders, closed-step label |
| `arranger` | `playhead` | arranger transport position |
| `mod` | `lfo`, `env`, `random`, `offset`, `default`, `routeAdd`, `routeMul` | mod-source strip colours and route-line kinds |
| `chip` | `palette` (array of 8) | instrument chip identities |

Legacy flat names (`background`, `panelBorder`, `modStripLfo`,
`chipPalette3`, …) still resolve to the same fields; see
`g_themeAliases` in `src/io/config_io.c` for the full mapping.

A style colour is resolved **at compile time** against this table. To add a
token, see Recipe F.

---

## 8. Layouts

`layouts` in `layout.json` describe **containers**, not widgets. A layout is
applied explicitly from C after the children exist:

```c
GuiNode *row = createGuiNode(0, 0, 100, 32, 2, na_horizontal, "row", 1, 0);
appendItem(row, rateDial, 2);
appendItem(row, curveDial, 1);
applyLayout(row, "env-stage");
```

### 8.1 Schema

| Key | Type | Default | Meaning |
|---|---|---|---|
| `orientation` | string | `"horizontal"` | `"vertical"` selects a column; anything else is horizontal |
| `padding` | int | 0 | inset applied to the container **and** copied to the container node |
| `gap` | int | 0 | pixels between adjacent children |
| `weights` | int array | all 1 | relative shares of the main axis |
| `childClasses` | string array | none | class name per child, by append order; `""` = leave alone |

`applyLayout` writes `orientation`, `padding` and `gap` onto the container,
assigns each child's weight, applies `childClasses`, recomputes
`totalItemWeights`, and reflows.

### 8.2 The solver (`reflowCoordinates`)

For a normal (non-scroll) container, along the main axis:

```
content = mainDimension - 2*padding
gaps    = min(gap * (n - 1), content)
avail   = content - gaps
for each child i:
    dim = (i == n-1) ? avail - used
                     : weight[i] * avail / totalWeight
    dim = max(dim, child.padding * 2 + 4)        // minimum size clamp
    position = start + padding + used
    used += dim
```

Cross axis: `position = containerStart + padding`,
`size = containerCrossDimension - 2*padding`. Children therefore fill the
cross axis completely and sit edge to edge along the main axis, separated by
`gap`.

Weights are **relative ratios, not pixels**: `[2, 1]` and `[20, 10]` are
identical. The last child absorbs the rounding remainder so the row fills
exactly.

### 8.3 Scroll containers

A `ScrollContainer` (`createScrollContainer`) ignores weights and lays
children out as fixed-height rows:

```
y = containerY + padding + i*rowH - scrollOffset
x = containerX + padding
w = containerW - 2*padding
h = rowH
contentH = itemCount*rowH + 2*padding
```

`scrollOffset` is clamped to `contentH - viewportH`. Changing it requires
re-running `reflowCoordinates` (children's positions are baked) — that is
what `scrollToVisible` does.

### 8.4 Gotchas

1. **Apply after appending.** `applyLayout` only touches children that
   already exist. A child appended later gets weight 1 and no class.
2. **Child padding, not layout padding, sets the minimum width.** The clamp
   uses each child's own `padding` (`childPadding*2 + 4`).
3. **Weights must be ≥ 1.** `applyLayout` clamps its array, but a direct
   `appendItem(parent, child, 0)` stores 0; a total of 0 is treated as 1.
4. **Minimum clamps can overflow the row.** If several children hit their
   minimum, `used` can exceed `avail`; the last child's remainder goes
   negative and is cast to `uint16_t` (≈65 000 px). Keep minimum sizes below
   the shares you assign.
5. **`childClasses` is positional.** It follows append order, not visual
   order, and ignores children added later.
6. **Re-applying a layout overwrites classes you set in C** for any non-empty
   `childClasses` entry for that index.
7. **A layout does not style the container itself** — only its children's
   geometry and classes. Container borders are drawn by its draw function
   (`drawWrapperNode` uses theme `panel` + `wrapperBorder`).

### 8.5 Shipped examples

```jsonc
"env-stage": {                       // one envelope stage box
  "orientation": "horizontal", "padding": 2, "weights": [2, 1],
  "childClasses": ["stage-rate", "stage-curve-icon"]
},

"mod-source-row-core": {             // an envelope core row
  "orientation": "horizontal", "padding": 2, "gap": 2,
  "weights": [8, 8, 3, 1]
},

"fm-op-row": {                       // five equal operator cells
  "orientation": "horizontal", "padding": 1, "weights": [1, 1, 1, 1, 1]
}
```

`env-row` also shows the empty-string convention:
`"childClasses": ["", "big-dial", ""]`.

---

## 9. Recipes

### Recipe A — restyle something with JSON only

1. Find the class the node uses (none → the type key, e.g. `dial`).
2. Add or edit that entry under `styles`. Omitted keys inherit.
3. Copy the file to wherever the app reads it (§2) and restart.

```jsonc
"styles": {
  "dial": { "knob": { "size": 24, "radius": 12 },
            "value": { "fontSize": 10 } }
}
```

### Recipe B — add a named variant and bind it

```jsonc
"styles": {
  "dial-tiny": { "extends": "dial", "knob": { "size": 12, "radius": 6 },
                 "value": { "width": 0, "height": 0 },
                 "label": { "fontSize": 8 } }
}
```

Bind at creation, after creation, or positionally through a layout's
`childClasses`. Verify the name appears in the class registry by testing the
resolved values (Recipe: §10), not by eyeballing.

### Recipe C — add a container layout

1. Add it under `layouts`.
2. Decide `weights` by counting children **in append order**.
3. In C, append all children, then `applyLayout(container, "<name>")`.
4. If the row looks wrong, print the children's `x/w` (or assert them in
   `tests/dsp/test_layout.c`) before changing weights — the min-size clamp
   and the last-child remainder are the usual culprits.

### Recipe D — add a new style field (end to end)

Example: a `blur` field on dials.

1. **Struct** — `src/gui_style.h`: add `int blur;` to `DialStyle`.
2. **Baked default** — `src/gui_style.c`: add `.blur = 0,` to
   `g_defaultDial`.
3. **Theme seed** (only if it is a colour) — `resolveDefaultColours`.
4. **Parser** — `overlayDial` in `gui_style.c`:
   `d->blur = jsonInt(o, "blur", d->blur);`
   (helpers: `jsonInt`, `jsonFloat`, `jsonStr`, `jsonColor`, `parseAlign`).
5. **Draw** — `drawDialGuiNode` in `src/gui_core.c` consumes `st->blur`.
6. **Geometry** (only if it changes layout) — update
   `computeDialGeometry` / `dialComponentHeight`.
7. **Config** — add the key to `src/layout.json` (and the live copies).
8. **Test** — compile a temp layout and assert the resolved struct (§10).
9. `meson compile -C build && meson test -C build`.

If the field is a new *sub-object*, also add an `overlayX` function and call
it from the type's overlay.

### Recipe E — add a new style type

1. `StyleType` enum (`gui_style.h`) — insert before `STYLE_COUNT`.
2. New struct in `gui_style.h`.
3. Baked default + theme seeding in `gui_style.c`.
4. `g_defaultClassName()` entry (this is the JSON type key).
5. Storage array, per-type counter, and a `resolveXStyle()`.
6. `overlayX()` parser.
7. A `case STYLE_X:` in `compileLayoutConfig` (copy an existing case; it is
   the same 8 lines).
8. A draw function that calls the resolver.
9. `finalizeStyles()` if the type loads textures.
10. A JSON entry named exactly the type key, plus a `styles` example.
11. Tests, and a `parse` sanity check for the new keys.

Watch the caps: 32 classes per type, 64 total.

### Recipe F — add a colour token

1. `src/theme.h` — add the `Color` field to `ColourScheme`.
2. `src/io/config_io.c` — add one row to `g_themePaths` (canonical
   `group.leaf` path + `offsetof`). Lookup, save and the validator all
   read that table, so there is nothing else to keep in sync.
3. Same file, only if you are renaming something — add a `g_themeAliases`
   row so existing themes keep loading.
4. `initDefaultColourScheme` in `src/gui_core.c` — seed a default.
5. `bin/clr.json` — add the key (the app will add it on next save anyway;
   `test_theme` fails until the shipped file covers every field).
6. Optional: use it as a seed in `resolveDefaultColours`.
7. Use the name in `layout.json` styles.

### Recipe G — add a knob texture / curve-icon set

- Knob: put the PNG under `resources/images/`, set
  `"knob": { "asset": "resources/images/foo.png" }`, and make sure the
  process CWD resolves it (run from `bin/`). It loads once after `InitGUI`.
- Curve icons: the atlas is built at startup by
  `buildCurveIconTexture()` (`src/curve_icons.c`) with
  `CURVE_ICON_COUNT` frames of `CURVE_ICON_SIZE`. Configure
  `curveIcon.frameCount` (32) and `size`; frame selection is value-driven,
  there is no per-frame JSON.

---

## 10. Testing

Unit tests live in `tests/dsp/` and run headless via meson.

```sh
meson compile -C build
meson test -C build
```

The layout tests (`tests/dsp/test_layout.c`) show the pattern: write a temp
`layout.json`, call `compileLayoutConfig`, then assert the resolved struct or
the reflowed geometry.

```c
initDefaultColourScheme(&cs);

FILE *f = fopen(path, "w");
fputs("{\"styles\":{\"dial-tiny\":{\"extends\":\"dial\","
      "\"knob\":{\"size\":12}}},"
      "\"layouts\":{\"row\":{\"weights\":[2,1]}}}", f);
fclose(f);

ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile");

GuiNode *n = createDialGuiNode(0, 0, 40, 40, 2, na_horizontal,
                               "tiny", false, NULL, &param);
guiNodeSetClass(n, "dial-tiny");
ASSERT_TRUE(resolveDialStyle(n)->knob.size == 12, "knob size");
```

The **shipped config is a test fixture**. `test_shipped_layout_json_is_valid`
in `test_layout.c` compiles `src/layout.json` and fails on anything the
parser would otherwise ignore: an unclassified class, an `extends` target
missing from the same file, a colour that is not a theme token, a
`childClasses` entry naming an unknown class, a `weights`/`childClasses`
length mismatch, or a missing per-type default class. Run
`meson test -C build` before committing a config change — that is the
difference between a typo that fails and a typo that ships as an invisible
no-op.

`styleClassType(name)` returns the `StyleType` a class compiled to (or `-1`),
handy when a class seems to "do nothing".

The theme has the same treatment. `test_theme.c` validates the **shipped**
`bin/clr.json`: all 47 canonical paths resolve to distinct fields, the file
supplies every one (a typo leaves a zero-alpha field and names the missing
path), the writer emits the grouped `schema: 2` form that round-trips, legacy
flat keys still load through the alias table, and unknown or zero-alpha keys
write nothing. `themePathCount()` / `themePathName(i)` enumerate the canonical
keys if you need them in your own assertions.

Caveats:

- Each `compileLayoutConfig` call replaces the previous config (§4.4), so
  tests may compile as many fixtures as they like without accumulating
  classes or layouts.
- Colours resolve against the `ColourScheme` you pass; initialise it first.
- For windowed checks, use the instrument harness fixtures
  (`src/tools/instrument_harness/run_scripted.sh fixtures/<name>.txt`) after
  `meson install -C build`, because it reads `bin/layout.json` and
  `bin/instrument_harness`.
- A component that is not part of the app cannot be driven by the
  instrument harness (it boots the shipped app). The self-contained demo in
  `src/tools/styling_tutorial/` shows the alternative: its own build target,
  its own config, and its own `--selftest` that asserts resolved styles and
  reflowed geometry. See
  [Building a new component](tutorials/building-a-component.md) §22.
- The shipped-config guards above only cover the *shipped* files. For a
  theme file of your own, `src/tools/theme_tool/` audits it: it resolves
  the config dirs the app would pick, reports every canonical colour path
  the file fails to supply, prints the canonical grouped form the app
  writes back on exit, and reports which classes the style config
  actually binds. See
  [Shipping a theme](tutorials/shipping-a-theme.md) §6, §7 and §8.

---

## 11. Gotcha checklist

1. Edit `src/layout.json` → copy to `bin/layout.json` **and** the live
   `~/.config/spectrax/layout.json` (or launch with `--config-dir bin`).
2. `meson install` only seeds missing files; it will not update yours.
3. Restart after any JSON change; nothing hot-reloads.
4. A custom class must `extends` a type key, or it is dropped silently.
5. Unknown class / colour names fall through silently: class →
   type-named class → baked default.
6. `border.colorSelected` only shows when its alpha is non-zero; unset
   means "keep the legacy highlight".
7. `type-label` draws its border only when selected.
8. Dial label X is a centre; Y is relative to the knob top.
9. Use `"vAlign": "top"` when a numeric must fit below the knob.
10. `value.width/height ≤ 0` hides the numeric.
11. `curveIcon.color` defaults to `text.value`; use `frameCount: 32` for the
    full range.
12. `knob.asset` paths are CWD-relative and load once, post-`InitGUI`.
13. Apply layouts after appending all children.
14. `childClasses` is append-order positional.
15. Weights are ratios; min-size clamps can overflow the last child.
16. An extra compile *replaces* the previous config; re-resolve any cached
    style/layout pointers afterwards.
17. `clr.json` is rewritten on exit in canonical grouped form; a legacy
    flat theme migrates itself on the next quit.
18. An unknown theme key is a silent no-op — `test_theme` is what turns it
    into a failure.
19. Text nodes are non-selectable.
20. Scroll containers ignore weights and need a reflow after
    `scrollOffset` changes.
21. A class that nothing binds does nothing. The app resolves a widget
    through the **baked** default unless a node names the class — the
    per-type entries in the shipped `src/layout.json` are only in force
    where a layout's `childClasses` (or a C `guiNodeSetClass()` call)
    asks for them. `src/tools/theme_tool/` reports which classes the
    config binds; see
    [Shipping a theme](tutorials/shipping-a-theme.md) §6.

---

## 12. File map

| Path | Contains |
|---|---|
| `src/theme.h` | `ColourScheme` (the colour token set) + `FontConfig` |
| `src/io/config_io.c` | canonical theme path table + aliases, colour parsing, theme load/save, settings |
| `src/gui_style.h` | style structs, `StyleType`, layout struct, resolver API |
| `src/gui_style.c` | baked defaults, class registry/compile, geometry helpers, `applyLayout`, `finalizeStyles` |
| `src/gui_core.c` | `drawPanelRect`, dial/btn/dest/text draw functions, `styleFont`, theme seeding, `InitGUI` |
| `src/graph_gui.c` | `GuiNode`, `appendItem`, `reflowCoordinates`, `guiNodeSetClass`, scroll containers |
| `src/gui_arranger.c` | chip draw (`drawInstChipGuiNode`) |
| `src/gui_pattern.c` | step-cell / automation-cell draw |
| `src/gui_instrument.c` | type-label draw |
| `src/layout.json` | shipped styles + layouts (source of truth) |
| `bin/layout.json` | what the harness reads |
| `~/.config/spectrax/layout.json` | what a default app launch reads |
| `bin/clr.json` | shipped theme (canonical `schema: 2` form) |
| `tests/dsp/test_layout.c` | compile/apply/geometry tests + shipped-layout validator |
| `tests/dsp/test_theme.c` | shipped-theme completeness, round-trip and alias tests |
| `src/tools/styling_tutorial/` | self-contained demo for the tutorial: its own build target, config and `--selftest` |
| `src/tools/theme_tool/` | companion tool for the theme tutorial: resolves the config dirs, audits a theme file, shows the canonical form the app writes back, reports which classes the config binds |
| `docs/superpowers/specs/2026-09-08-layout-config-design.md` | original three-tier design |
| `docs/superpowers/specs/2026-09-14-ui-theme-polish-design.md` | "every visual knob is a JSON field" convention |
