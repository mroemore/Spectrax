# Building a new component

This tutorial builds a widget that does not exist anywhere in Spectrax — a
**Macro Bank** panel — using nothing but the JSON styling system. It is the
hands-on counterpart to the [UI styling guide](../ui-styling.md): the guide
is the reference, this is the walk-through.

Read the guide's §1 (mental model), §4 (classes and resolution) and §8
(layouts) first if you have not. Everything below assumes you know that a
style is a lookup, not a cascade, and that a layout only moves and sizes
nodes.

The demo lives in `src/tools/styling_tutorial/` and is its own build
target. Nothing in it is linked into `spectrax`, and it reads its own
`layout.json` and `clr.json`, so it cannot change what the app looks like.

```
src/tools/styling_tutorial/
├── meson.build          the demo's own build target
├── styling_tutorial.c   the component, its draw code and its self-test
├── layout.json          the demo's classes and layouts
└── clr.json             the demo's theme
```

Build and run it:

```sh
meson compile -C build
./build/src/tools/styling_tutorial/styling_tutorial --selftest   # headless
./build/src/tools/styling_tutorial/styling_tutorial --dump       # geometry
./build/src/tools/styling_tutorial/styling_tutorial              # window
```

On a headless box (Void Linux, CI, a container) the window needs an X
server:

```sh
xvfb-run -a -s "-screen 0 800x600x24" \
  ./build/src/tools/styling_tutorial/styling_tutorial
```

`--selftest` is registered with meson, so `meson test -C build` runs it as
part of the suite.

What the component ends up being:

```
MACRO BANK                        <- text, centred
+---------------------------------------------+
| (AMT) (RAT) (SPR) (MODE) (CRV) |             |   <- dial row
+---------------------------------------------+
| MACRO | BYPASS | [chip] | (slot)            |   <- action row
+---------------------------------------------+
| C4 D4 E4 G4 A4 C5 D5 E5                       |   <- scale ladder
+---------------------------------------------+
|            SPREAD IS FM-ONLY                 |   <- dimmed hint
+---------------------------------------------+
```

The C code decides *what* the panel is — the hierarchy, the parameter
values, the draw primitives. Every colour, size, offset, alignment and
piece of text placement comes out of the JSON.

---

## 1. A class that extends a built-in type

A class is a named style. It always belongs to a **type** (`dial`, `btn`,
`chip`, …), and it inherits everything it does not override from that
type's entry.

The demo's first class is a macro dial. The type key `dial` is the class
every unclassed dial uses, so a variant starts by extending it:

```jsonc
"macro-dial": {
    "extends": "dial",
    "knob": { "size": 16, "radius": 8 },
    "value": { "format": "%03.1f", "width": 30, "height": 12,
               "offsetX": 18, "offsetY": 2, "fontSize": 8 },
    "label": { "fontSize": 8, "spacing": 1, "offsetX": 0, "offsetY": 20 }
}
```

The name after `extends` must exist in the same file, and it must
transitively reach a type key — a class that does not is dropped without a
word (guide §4.2).

Two rules bite early:

- **A class name is unique per type, not globally.** `macro-dial` under
  `dial` and `macro-dial` under `btn` are two different classes.
- **An unknown class name is a silent no-op.** The node falls back to the
  type's baked default and nothing complains. The demo's self-test pins
  this down:

```c
guiNodeSetClass(n, "no-such-class");
const DialStyle *fb = resolveDialStyle(n);
CHECK(fb->knob.size == 20, "unknown class falls back to the baked default");
```

## 2. A multi-level `extends` chain

`macro-amount` extends `macro-dial`, which extends `dial` — class → class
→ type:

```jsonc
"macro-amount": {
    "extends": "macro-dial",
    "knob": { "size": 22, "radius": 11 },
    "value": { "width": 34, "offsetX": 24 },
    "label": { "fontSize": 9, "offsetY": 26 }
}
```

The chain is overlaid root → leaf: the baked default, then `dial`, then
`macro-dial`, then `macro-amount`. So `macro-amount` ends up with the
knob size from itself (22), the value format from `macro-dial` (`%03.1f`)
and the arc sweep from `dial` (270°):

```c
guiNodeSetClass(n, "macro-amount");
const DialStyle *amt = resolveDialStyle(n);
CHECK(amt->knob.size == 22, "macro-amount knob.size 22 (its own)");
CHECK(strcmp(amt->value.format, "%03.1f") == 0,
      "macro-amount inherits macro-dial's format");
CHECK(amt->knob.sweep == 270, "macro-amount inherits dial's sweep");
```

`extends` is followed at most 16 hops; deeper chains fall back to the
baked defaults (guide §4.4).

## 3. A hollow class

A hollow class adds nothing. It exists to say "this widget is one of
these", and it inherits its parent's look exactly:

```jsonc
"macro-plain": { "extends": "dial" }
```

The demo's `SPR` dial uses it, so the panel shows what an untouched dial
looks like next to the rest. The self-test compares the resolved structs
field by field:

```c
guiNodeSetClass(n, "macro-plain");
const DialStyle *plain = resolveDialStyle(n);
guiNodeSetClass(n, "dial");
const DialStyle *base = resolveDialStyle(n);
CHECK(plain->knob.size == base->knob.size, "hollow class: same knob size");
CHECK(plain->knob.size == 18, "hollow class: the JSON dial, not the baked default (20)");
```

That last check is the important one: a hollow class still resolves
through the JSON `dial` entry, which is *not* the same thing as the baked
default the compiler starts from.

## 4. Binding a class: three ways

The same style can be attached to a node in three different places. The
demo uses all three, on purpose, on the same dial row:

**a. In C, when the node is created or afterwards:**

```c
mb.dials[0] = demoDialNode("AMT", &g_params[0]);
guiNodeSetClass(mb.dials[0], "macro-amount");
```

**b. Through a layout's `childClasses`, positionally:**

```jsonc
"macro-dial-row": {
    "orientation": "horizontal", "padding": 1, "gap": 2,
    "weights": [ 3, 3, 3, 2, 2 ],
    "childClasses": [ "", "macro-ratio", "macro-plain", "macro-mode", "macro-curve" ]
}
```

Index *i* names the class of the *i*-th child **in append order**. An
empty string leaves that child alone — which is why the `AMT` dial keeps
the class set in C:

```c
CHECK(mb.dials[0]->className && strcmp(mb.dials[0]->className, "macro-amount") == 0,
      "the C-set class survived: childClasses[0] was empty");
CHECK(mb.dials[1]->className && strcmp(mb.dials[1]->className, "macro-ratio") == 0,
      "childClasses[1] bound macro-ratio");
```

**c. The per-type default class.** Every type has one class name that
means "the default look of this type" — `dial`, `btn`, `chip`, `text`,
`step-cell`, `type-label`, `route-dest`, `dial-discrete`. Binding that
name explicitly is the same as binding nothing at all *unless* the config
also defines an entry for it, in which case the JSON entry wins.

The demo shows both halves of that:

```c
mb.chip = demoChipNode(3, "FM", "V:4", "MACRO", "INIT PATCH", 3);
/* ... the action row's childClasses[2] is "chip" ... */

const ChipStyle *named = resolveChipStyle(mb.chip);
CHECK(named->dots.size == 5, "the named chip class is the compiled one");

GuiNode *plain = createBlankGuiNode();
const ChipStyle *baked = resolveChipStyle(plain);
CHECK(baked->dots.size == 4, "a classless node gets the baked default");
```

The demo's `chip` entry sets `dots.size` to 5; the baked default is 4. The
name `chip` therefore resolves to the *JSON* chip class, while a node with
no class at all gets the baked default.

> **Re-applying a layout overwrites a class you set in C.** If
> `childClasses[0]` had been `"macro-ratio"` instead of `""`, the layout
> would have replaced the `macro-amount` class the C code had set
> (guide §8.4).

## 5. Colours are tokens, never hex

A colour field takes the **name of a theme token** — `text.primary`,
`line.strong`, `chip.palette3`. It never takes a hex value, even though
`clr.json` is full of hex values:

```jsonc
"macro-hint": {
    "extends": "text",
    "fontSize": 8,
    "hAlign": "center",
    "vAlign": "middle",
    "color": "text.disabled"
}
```

The hex lives in the theme, not in the style. A raw hex in a style is not
an error — it is silently ignored, and the inherited value survives:

```c
/* "hex-dial": {"extends": "dial", "label": {"color": "#FF00FF"}} */
const DialStyle *d = resolveDialStyle(n);
CHECK(d->label.color.r == 11 && d->label.color.g == 22 && d->label.color.b == 33,
      "a raw hex colour is ignored: the inherited token colour survives");
CHECK(!(d->label.color.r == 255 && d->label.color.g == 0 && d->label.color.b == 255),
      "the raw hex value is NOT applied");
```

The demo's `clr.json` is a cut-down theme with exactly the tokens the
demo's `layout.json` names. Every one of them is checked by the demo's
own validator (see §22).

Legacy flat names still work — `chipPalette3` is an alias for
`chip.palette3` — but new config should use the grouped form.

## 6. The knob: size, radius, angles and asset

```jsonc
"knob": {
    "size": 18,
    "radius": 9,
    "startAngle": -225,
    "sweep": 270,
    "offsetX": 0,
    "offsetY": 0,
    "asset": "resources/images/dial2.png",
    "color": "control.dial"
}
```

- `size` is the drawn diameter, `radius` the arc radius (and the texture's
  rotation origin). Both are set explicitly; the demo keeps the radius at
  half the size, as the shipped config does.
- `startAngle`/`sweep` are degrees. `-225`/`270` is the usual dial sweep
  (a 270° arc starting at the lower left).
- `offsetX`/`offsetY` nudge the knob image off its computed position.
- `asset` is a PNG path **relative to the process working directory**. The
  demo therefore changes into the directory that holds `resources/` before
  it starts, and calls `finalizeStyles()` once the window exists:

```c
if(assetDir && assetDir[0] && chdir(assetDir) != 0) { /* warn */ }
...
InitWindow(640, 480, "Spectrax styling tutorial - Macro Bank");
loadFonts();
g_curveIcons = buildCurveIconTexture();
finalizeStyles();   /* loads the knob texture named by knob.asset */
```

If the file is missing, the load fails silently and the widget draws the
procedural knob instead. The demo's fallback path is the else-branch of
its dial draw function.

## 7. The value readout — and how to hide it

```jsonc
"value": {
    "format": "%03.1f",     /* printf format for the number */
    "width": 30,            /* the readout box */
    "height": 12,
    "offsetX": 18,          /* from the knob's top-left */
    "offsetY": 2,
    "fontSize": 8,
    "color": "text.value"
}
```

The offsets are relative to the **knob**, not to the cell, so the readout
travels with the knob when the layout moves it.

`width` or `height` of **0 hides the numeric**. That is the only way to
get a dial with no readout — a missing `value` block means "inherit the
default", not "hide":

```jsonc
"macro-curve": {
    "extends": "macro-dial",
    "value": { "width": 0, "height": 0, "fontSize": 0 }
}
```

```c
CHECK(curve->value.width == 0 && curve->value.height == 0, "macro-curve hides the numeric");
```

## 8. The label, and `vAlign`

```jsonc
"label": {
    "font": "pixel",
    "fontSize": 8,
    "spacing": 1,
    "offsetX": 0,
    "offsetY": 20,
    "color": "text.muted",
    "colorSelected": "text.mutedSelected"
}
```

`fontSize: 0` disables the label entirely. `spacing` is the gap between
glyphs. `offsetY` is measured from the **knob's top**, and `offsetX` is a
fine-tune from the **cell's centre** — the dial's caption is centred on
`labelX`, so the draw code measures the text and shifts it left by half.

`vAlign` decides where the knob sits in its cell:

```jsonc
"macro-amount": { "vAlign": "top", ... }
```

`"top"` pins the knob to the cell's padded top, which is what leaves room
for a readout underneath. The default (`"center"`) centres the knob, and
a centred knob can never leave room for a 14 px numeric below it — in a
32 px cell a 20 px knob leaves 6 px above and below (guide §6.3).

The demo's dial row leaves the dials centred and puts the readout to the
right of the knob, which is why `macro-dial` uses `offsetX: 18` rather
than the default `28`.

## 9. The curve icon

A dial can show a curve glyph instead of a number:

```jsonc
"curveIcon": {
    "frameCount": 32,
    "size": 12,
    "offsetX": 0,
    "offsetY": 0,
    "color": "text.value"
}
```

`frameCount` is the opt-in: 0 means "no icon, draw the value text". The
frame is picked from the parameter's normalised value, so use the atlas
size (32) for a 1:1 mapping — a smaller `frameCount` collapses the top of
the range onto the last frame. The icon is centred on the knob and
`offsetX`/`offsetY` nudge it from there.

`color` is its own field — it does not follow `value.color`. The baked
default seeds it from the theme's `text.value`, so a class that only sets
`frameCount` still draws a visible glyph; set it explicitly for any other
colour.

The icon replaces the readout, so a curve-icon dial normally hides the
numeric as well (§7). The demo's `CRV` dial does both.

## 10. Buttons and their sublabel

```jsonc
"macro-bypass": {
    "extends": "btn",
    "border": { "color": "control.accent" },
    "label": { "fontSize": 11, "offsetX": 6, "offsetY": 4 },
    "sublabel": { "fontSize": 8, "offsetX": 4, "offsetY": 18 }
}
```

The main label is left-aligned and padding-relative. The sublabel is
**centred on the node** and anchored to the node's top edge — the layout
padding is not added to it. They are not siblings with the same origin,
which is the single most common "why is my text in the wrong place"
moment (guide §6.8).

A sublabel only draws when the node has one:

```c
guiNodeSetSublabel(gn, "ON");
```

and only when `sublabel.fontSize > 0`.

## 11. Type labels and empty slots

The type tag between PREV and NEXT is a `type-label`: centred text, and
a border **only when it holds the cursor**.

```jsonc
"macro-type": {
    "extends": "type-label",
    "label": { "fontSize": 11, "color": "text.primary" }
}
```

The empty macro slot is a `route-dest`: an outline and nothing else.

```jsonc
"macro-slot": {
    "extends": "route-dest",
    "border": { "borderWidth": 1.0, "color": "line.grid",
                "colorSelected": "text.mutedSelected" }
}
```

`border.colorSelected` is honoured by every bordered widget — but only
when its alpha is non-zero. Leave it out and the widget keeps the legacy
highlight, so a transparent colour is not the same as "unset".

## 12. The chip: palette and voice lights

```jsonc
"chip": {
    "border": { "borderWidth": 2.0, "color": "line.wrapper",
                "colorSelected": "line.strong" },
    "palette": [ "chip.palette0", "chip.palette1", ..., "chip.palette7" ],
    "voiceCount": { "fontSize": 9, "offsetX": 4, "offsetY": 2 },
    "typeTag":    { "fontSize": 9, "offsetX": -4, "offsetY": 2 },
    "label":      { "fontSize": 12 },
    "patchName":  { "fontSize": 8, "offsetX": 4, "offsetY": -11 },
    "dots": { "size": 5, "gap": 3,
              "colour": "line.wrapper", "colourActive": "line.strong" }
}
```

- `palette` is an array of up to eight **token names**; the chip's
  background is `palette[channel % 8]`.
- `patchName.offsetY` is meant to be **negative**: the patch name is
  anchored to the cell's bottom edge, `y + h + offsetY`.
- The dots are the voice lights. Note the British spelling: `colour`,
  `colourActive`.
- `voiceCount`, `typeTag`, `label` and `patchName` are label objects; their
  `colorSelected` fields are not used — a chip shows selection with its
  border.

The chip's own geometry comes from `computeChipGeometry()`, which places
the voice count top-left, the type tag top-right, the label centred, the
patch name bottom-left and the lights bottom-right.

```c
ChipGeometry g;
computeChipGeometry(gn, st, chip->typeTag, chip->label, &g);
```

`chipComponentHeight()` gives the minimum height at which the centred
label band and the bottom strip do not overlap:

```c
CHECK(chipComponentHeight(chip) == 12 + 8 + 5, "chip component height");
```

## 13. Step cells: the note and the surround

```jsonc
"step-cell": {
    "bgEmpty": "cell.default",
    "bgPlaying": "cell.highlighted",
    "borderSelected": "line.strong",
    "note": { "font": "text", "fontSize": 9, "spacing": 4,
              "offsetX": 4, "offsetY": 4, "color": "text.primary" }
}
```

- `bgEmpty` fills a cell with no note, `bgPlaying` fills the cell the
  playhead is on, `borderSelected` is the 3 px surround drawn around the
  selected cell.
- The note's offsets are **not** padding-relative; the cell's own padding
  only affects children.
- `fontSize: 0` means "use the text font's base size".

The demo's scale ladder uses three different bindings of this one type,
and you can see the difference on screen:

```c
if(i == 0) {
    guiNodeSetClass(cell, "macro-step"); /* a variant */
} else if(i == 1) {
    guiNodeSetClass(cell, "step-cell"); /* the per-type default class, named */
}
/* cells 2..7 carry no class at all -> the baked default */
```

```c
CHECK(step->bgEmpty.r == g_cs.blankCell.r, "macro-step bgEmpty is cell.blank");
```

## 14. Text: `hAlign` and `vAlign`

```jsonc
"macro-title": {
    "extends": "text",
    "fontSize": 12,
    "spacing": 2,
    "hAlign": "center",
    "vAlign": "middle",
    "color": "text.primary"
}
```

`hAlign` takes `left`, `center` or `right`; `vAlign` takes `top`,
`middle` or `bottom`. The alignment is computed against the node's own
`w`/`h` and the offset is added afterwards.

Text nodes are **non-selectable**, so they never take the cursor and
`colorSelected` is unreachable for them. The demo's title and hint are
built that way.

## 15. Layouts: a weighted row

A layout describes a **container**, not a widget. It is applied from C
after the children exist:

```c
mb.dialRow = createGuiNode(x, y, w, h, 1, na_horizontal, "macro-dials", false, false);
... append five dials ...
applyLayout(mb.dialRow, "macro-dial-row");
```

```jsonc
"macro-dial-row": {
    "orientation": "horizontal", "padding": 1, "gap": 2,
    "weights": [ 3, 3, 3, 2, 2 ]
}
```

The weights are **ratios, not pixels**. `[3,3,3,2,2]` and `[30,30,30,20,20]`
describe the same row; the demo proves it by scaling the weights in a
copy of its own config and comparing the result:

```c
CHECK(mb.dials[0]->w == 135, "the scaled weights give the same first cell (135 px)");
CHECK(mb.dials[3]->w == 90, "the scaled weights give the same fourth cell (90 px)");
```

The solver, for a horizontal row:

```
content = row.w - 2*padding
gaps    = min(gap * (children - 1), content)
avail   = content - gaps
child i (not the last) = weight[i] * avail / totalWeight
the last child         = whatever is left over
```

so the row always fills its content width exactly, and the last child
absorbs the rounding remainder.

> **Apply a layout after appending.** `applyLayout` only touches children
> that already exist. A child appended afterwards gets weight 1 and no
> class (guide §8.4).

## 16. Layouts: `padding` and `gap`

`padding` is the container's inset, and it is copied onto the container
node as well as applied to its children. `gap` is the space between
adjacent children.

**What gap costs you:** the gaps are taken out of the content *before* the
weights are distributed, so every child's share shrinks:

```
avail = (row.w - 2*padding) - gap*(children - 1)
```

With five children and `gap: 2`, the row gives up 8 px, and each
child's share is 8/5 px smaller than it would be without the gap. The
demo's row is 596 px wide with 1 px of padding: 594 px of content, 8 px
of gaps, 586 px to share — 135, 135, 135, 90 and 91 px.

The dump shows the gap between two children:

```
    AMT          macro-amount  x= 23 y= 79 w=135 h= 83
    RAT          macro-ratio   x=160 y= 79 w=135 h= 83
```

23 + 135 + 2 = 160.

> A child's own `padding` sets its **minimum** size
> (`child.padding*2 + 4`). If several children hit their minimum, the row
> can overflow and the last child's remainder goes negative — keep the
> minimums below the shares you assign (guide §8.4).

## 17. Layouts: a vertical column

```jsonc
"macro-bank": {
    "orientation": "vertical", "padding": 2, "gap": 2,
    "weights": [ 2, 5, 3, 6, 2 ],
    "childClasses": [ "macro-title", "", "", "", "macro-hint" ]
}
```

`"vertical"` is the only spelling that selects a column; anything else is
horizontal. The weights then divide the container's **height** instead of
its width, and the children fill the width:

```
macro-bank   (no class)    x= 20 y= 40 w=600 h=320
  MACRO BANK   macro-title   x= 22 y= 42 w=596 h= 34
  macro-dials  (no class)    x= 22 y= 78 w=596 h= 85
  macro-actions (no class)    x= 22 y=165 w=596 h= 51
  macro-steps  (no class)    x= 22 y=218 w=596 h=102
  SPREAD IS FM-ONLY macro-hint x= 22 y=322 w=596 h= 36
```

The panel's own children are full width; only the main axis is divided.

## 18. `childClasses`, and the append-order trap

`childClasses` is **positional in append order**, not visual order, and
it ignores children added later. Three ways to get it wrong, all of which
the demo's self-test pins down:

```c
/* 1. The empty string means "leave this child alone". */
"childClasses": [ "", "macro-ratio", "macro-plain", "macro-mode", "macro-curve" ]

/* 2. A non-empty entry OVERWRITES a class set in C. */
CHECK(mb.dials[0]->className && strcmp(mb.dials[0]->className, "macro-amount") == 0,
      "the C-set class survived: childClasses[0] was empty");

/* 3. A child appended after applyLayout gets weight 1 and no class. */
```

The dump makes the positional rule visible — the classes run down the
tree in the order the children were appended, not in the order they
appear on screen.

## 19. Nested layouts

A container can be a child of another container and carry its own layout.
The demo has two: the dial row and the action row are both children of
the bank, and both have their own layout applied:

```c
applyLayout(mb.dialRow, "macro-dial-row");
applyLayout(mb.actionRow, "macro-action-row");
applyLayout(mb.bank, "macro-bank");
```

The inner layout owns the inner children's widths; the outer layout only
sizes the inner container. Order does not matter — applying the outer
layout reflows the inner container, which re-runs the inner layout's own
weights.

## 20. Scroll containers

A scroll container lays its children out as fixed-height rows and ignores
weights entirely:

```c
mb.steps = (ScrollContainer *)createScrollContainer(x, y, w, h, MACRO_ROW_H, "macro-steps");
for(int i = 0; i < MACRO_STEPS; i++) {
    appendItem(&mb.steps->base, cell, 1);
}
appendItem(mb.bank, &mb.steps->base, 1);
```

```
contentH = itemCount * rowH + 2*padding
y        = containerY + padding + i*rowH - scrollOffset
```

**The rule that bites:** the children's positions are *baked* at reflow
time. Changing `scrollOffset` does not move anything until you reflow:

```c
sc->scrollOffset = 32;
reflowCoordinates(&sc->base);   /* now the rows move */
```

```c
int y0 = mb->stepCells[0]->y;
sc->scrollOffset = 32;
reflowCoordinates(&sc->base);
CHECK(mb->stepCells[0]->y == y0 - 32, "the reflow moved the rows by scrollOffset");
```

That is exactly what `scrollToVisible()` does for the app. The scroll
offset is clamped to `contentH - viewportH`, and `drawNode` clips the
children to the viewport with a scissor, so the rows that fall outside
are cut off rather than drawn over the rest of the panel.

## 21. A brand-new colour token, end to end

Tokens live in the theme, and the theme is a fixed table of canonical
paths. Adding one takes five edits.

**1. `src/theme.h` — the field:**

```c
typedef struct {
    ...
    Color valueText;
    /* Dimmed text for controls/values that exist but are not currently
     * available (e.g. a macro that only applies to some voice types). */
    Color disabledText;
    Color vline;
    ...
} ColourScheme;
```

**2. `src/io/config_io.c` — the canonical path.** The table is the single
mapping between the JSON names and the C fields; the lookup, the writer
and the validator all walk it:

```c
{ "text.value",          offsetof(ColourScheme, valueText) },
{ "text.gridLabel",      offsetof(ColourScheme, arrangerCellText) },
{ "text.disabled",       offsetof(ColourScheme, disabledText) },
```

**3. `src/gui_core.c` — the default.** Every field needs a seeded value,
or a theme that omits the key leaves it transparent:

```c
colourScheme->valueText = (Color){ 255, 0, 0, 255 };
colourScheme->disabledText = (Color){ 110, 96, 96, 255 };
```

**4. `bin/clr.json` — the shipped theme.** The theme validator fails until
the file covers every canonical path:

```jsonc
"text": {
    "primary": "#631100FF",
    "secondary": "#B03500FF",
    "muted": "#C8B4B4FF",
    "mutedSelected": "#FFB4B4FF",
    "value": "#FF0000FF",
    "gridLabel": "#C8B4B4FF",
    "disabled": "#6E6060FF"
}
```

**5. Use it.** The token is now a legal value for any `color` /
`colorSelected` field:

```jsonc
"macro-hint": { "extends": "text", "color": "text.disabled" }
```

```c
CHECK(hint->color.r == g_cs.disabledText.r && hint->color.a == 255,
      "macro-hint uses the text.disabled token");
```

Two things to remember: the app rewrites `clr.json` on exit in the
canonical grouped form, and a token with **alpha 0** is treated as
"unset" on load, so never write a fully transparent colour.

## 22. Testing what you built

Three levels of checking, cheapest first.

**A. Assert on a resolved style.** The resolved struct is what the widget
actually draws, so assert on it:

```c
guiNodeSetClass(n, "macro-amount");
const DialStyle *amt = resolveDialStyle(n);
CHECK(amt->knob.size == 22, "macro-amount knob.size 22 (its own)");
CHECK(strcmp(amt->value.format, "%03.1f") == 0,
      "macro-amount inherits macro-dial's format");
```

**B. Assert on the reflowed geometry.** Styles do not move anything; the
layout does. Assert on the numbers the layout produced:

```c
int contentW = (int)row->w - 2 * (int)row->padding;
int gaps = (int)row->gap * 4;             /* gap * (children - 1) */
int sum = 0;
for(int i = 0; i < 5; i++) { sum += mb->dials[i]->w; }
CHECK(sum == contentW - gaps, "the dial row fills its content width, minus the gaps");
CHECK(mb->dials[1]->x == mb->dials[0]->x + mb->dials[0]->w + 2,
      "the gap sits between children");
```

**C. Validate the config.** A typo in a class name, an `extends` target
that does not exist, a colour that is not a token, or a
`weights`/`childClasses` length mismatch are all **silent no-ops** at
runtime. The shipped config is a test fixture for exactly this reason:
`tests/dsp/test_layout.c::test_shipped_layout_json_is_valid` compiles
`src/layout.json` and fails on anything the parser would ignore.

```sh
meson compile -C build
meson test -C build test_layout   # just the shipped-config validator
meson test -C build               # the whole suite
```

The demo ships its own copy of those rules so its config gets the same
treatment (`validateLayoutFile()` and `validateColourTokens()` in
`styling_tutorial.c`): every class must compile, every `extends` target
must exist in the same file, every layout must register, the
weights/`childClasses` lengths must match, every `childClasses` entry must
name a class that exists, and every colour must name a theme token.

**D. A second compile replaces everything.** Worth knowing before you
wonder why your classes vanished:

```c
CHECK(compileLayoutConfig(path, &cs), "compile the hex fixture");
CHECK(styleClassType("macro-amount") < 0, "the second compile wiped the class registry");
CHECK(layoutByName("macro-bank") == NULL, "the second compile wiped the layout table");
```

`compileLayoutConfig()` resets the class registry and the layout table on
every call. Anything holding a resolved style pointer or a `LayoutDef *`
across a recompile has to re-resolve it (guide §4.4).

Run everything:

```sh
$ ./build/src/tools/styling_tutorial/styling_tutorial --selftest
styling tutorial: all checks passed
```

## 23. Coverage

Every feature the styling system offers, and where this tutorial
demonstrates it.

| Feature | Demonstrated in | Status |
|---|---|---|
| **Styles / classes** | | |
| Class that `extends` a built-in type | §1 (`macro-ratio` → `dial`) | covered |
| Multi-level `extends` chain (class → class → type) | §2 (`macro-amount` → `macro-dial` → `dial`) | covered |
| Hollow / pass-through class | §3 (`macro-plain`) | covered |
| `knob`: size, radius, startAngle/sweep, offsetX/Y, asset | §6 | covered |
| `border`: borderWidth, color, colorSelected | §6, §11 | covered |
| `value`: format, width, height, offsetX/Y, fontSize, color | §7 | covered |
| `value.width/height = 0` hides the numeric | §7 (`macro-curve`) | covered |
| `label`: font, fontSize, spacing, offsetX/Y, color/colorSelected | §8 | covered |
| `curveIcon`: frameCount, size, offsetX/Y, color | §9 | covered |
| `dots`: size, gap, colour, colourActive | §12 | covered |
| `btn.sublabel` | §10 | covered |
| `chip.palette` | §12 | covered |
| `step-cell`: `note` + `borderSelected` surround | §13 | covered |
| `text.hAlign` / `text.vAlign` | §14 | covered |
| `dial.vAlign` | §8 | covered |
| Colour tokens vs raw hex (only tokens resolve) | §5 | covered |
| One class bound three ways (C, `childClasses`, per-type default) | §4 | covered |
| **Layouts** | | |
| Horizontal row with `weights` | §15 | covered |
| Vertical column | §17 | covered |
| `padding` and `gap` (and what gap costs) | §16 | covered |
| `childClasses`, positional / append-order trap | §18 | covered |
| Nested layout (layout on a container that is a child) | §19 | covered |
| Scroll container + reflow after `scrollOffset` | §20 | covered |
| Weights are ratios, not pixels | §15 | covered |
| **Colours** | | |
| Using a token from `clr.json` | §5 | covered |
| New token end to end (theme.h → `g_themePaths` → `initDefaultColourScheme` → `bin/clr.json` → use) | §21 | covered |
| **Testing** | | |
| Assertion on a resolved style | §22 A | covered |
| Assertion on reflowed geometry | §22 B | covered |
| Shipped-config validator | §22 C | covered |
| Demo's own config validator | §22 C | covered |
| A second compile replaces the registry | §22 D | covered |
| Instrument-harness fixture | — | **not covered**, see below |
| **Also demonstrated** | | |
| `dial-discrete` type (integer format) | §15 (`MODE`) | covered |
| `type-label` type | §11 | covered |
| `route-dest` type | §11 | covered |
| Per-type default class vs baked default | §4 | covered |
| Class name is type-qualified | §1 | covered |
| Unknown class / unknown colour are silent no-ops | §1, §5 | covered |

### What is not covered, and why

- **An instrument-harness fixture.** The harness in
  `src/tools/instrument_harness/` boots the *shipped app* and drives the
  app's own graph, so it cannot host a component that does not exist in
  the app. The demo's `--selftest` is the equivalent: it asserts the
  resolved styles and the reflowed geometry, and it runs in
  `meson test -C build`. A harness fixture for the demo would mean
  teaching the app to build the demo's component — which is exactly the
  thing this tutorial deliberately avoids, because the demo must not
  change shipped behaviour.
- **A new style type** (`STYLE_METER`, say) and **a new style field**
  (`knob.glow`, say). Both need edits in `src/gui_style.h` and
  `src/gui_style.c` — the app's style registry — so they cannot live in a
  self-contained demo target. The reference guide has both recipes:
  [Recipe D](../ui-styling.md#recipe-d--add-a-new-style-field-end-to-end)
  (new field) and
  [Recipe E](../ui-styling.md#recipe-e--add-a-new-style-type) (new type).
  Everything this tutorial covers is the part that needs **no C change at
  all**.
- **Hot reload.** Nothing re-reads the JSON; the app restarts. The demo
  inherits that (see the guide's §3 startup order).

## 24. Build-system integration

The demo is a meson target of its own, sitting next to the other tools in
`src/tools/`. The repo root gets one line:

```meson
subdir('src/tools/styling_tutorial')
```

and `src/tools/styling_tutorial/meson.build` holds the target:

```meson
styling_tutorial = executable('styling_tutorial',
  files('styling_tutorial.c', '../../curve_icons.c'),
  link_with: [core_lib],
  include_directories: inc,
  dependencies: system_deps,
  c_args: [
    '-DSPECTRAX_TUTORIAL_LAYOUT_JSON="' + meson.current_source_dir() + '/layout.json"',
    '-DSPECTRAX_TUTORIAL_THEME_JSON="' + meson.current_source_dir() + '/clr.json"',
    '-DSPECTRAX_TUTORIAL_ASSET_DIR="' + meson.project_source_root() + '/bin"',
  ],
)

# The headless half of the demo: resolved styles + reflowed geometry.
test('styling_tutorial_selftest', styling_tutorial, args: ['--selftest'])
```

Three things that buys:

- **Nothing leaks into the app.** `spectrax` does not compile
  `styling_tutorial.c` and links no demo code. The demo reuses
  `src/curve_icons.c` — which the app already had — by naming it in its
  own source list; that is a second copy, not a shared one. The demo
  links `core_lib` — the same static library the app links — but a
  static library only contributes the objects a program actually
  references, so the demo ends up with the style core
  (`gui_style.c`, `graph_gui.c`, `io/config_io.c`) and the curve-icon
  helper, and none of the app's GUI layer:

  ```sh
  $ nm build/src/tools/styling_tutorial/styling_tutorial | grep -c drawGuiNode
  0
  $ nm bin/spectrax | grep -c buildMacroBank
  0
  ```

- **The self-test is behind the test gate.** `meson test -C build` runs
  it, so a change to the styling core that breaks the demo breaks the
  suite:

  ```
   10/18 spectrax:styling_tutorial_selftest OK              0.02s
  ```

- **The default paths are compiled in.** The `-D` flags bake the demo's
  own `layout.json`, `clr.json` and asset directory into the binary, so
  it can be started from anywhere; `--layout`, `--theme` and
  `--asset-dir` override them at runtime.

The demo is **not** installed. `meson/install.sh` copies an explicit
allow-list of binaries into `bin/`, and the demo is not on it — this
tutorial adds nothing to the shipped `bin/`.

Build and run:

```sh
meson compile -C build
./build/src/tools/styling_tutorial/styling_tutorial --selftest   # headless
./build/src/tools/styling_tutorial/styling_tutorial --dump       # geometry
./build/src/tools/styling_tutorial/styling_tutorial              # window
```

The windowed mode needs a display. On Void Linux, or on a headless box
(CI, a container, a bare SSH session):

```sh
xvfb-run -a -s "-screen 0 800x600x24" \
  ./build/src/tools/styling_tutorial/styling_tutorial
```

`--selftest` and `--dump` are headless and need no X server at all.

## 25. Known limits and quirks

Limits of the demo itself:

- It is a **picture** of a component, not a component: no audio, no
  parameter editing, no scrollbar. The arrow keys move a cursor so you
  can see the selected-state colours.
- The panel is laid out once, at a fixed size. Nothing re-flows it when
  the window is resized.
- The scale ladder is a real scroll container, but nothing scrolls it —
  §20 proves the reflow rule with the self-test instead.

Quirks you will hit, in the order this demo hits them:

| Symptom | Cause |
|---|---|
| A style change does nothing | the class name is misspelled, or the class does not reach a type — unknown names are silent no-ops (§1) |
| A raw hex colour is ignored | colour fields take theme token names only (§5) |
| The numeric readout is still there | a missing `value` block means "inherit", not "hide" — set `"width": 0, "height": 0` (§7) |
| The curve glyph has the wrong colour | `curveIcon.color` is its own field; it does not follow `value.color` (§9) |
| The sublabel is in the wrong place | it is centred on the node and anchored to the node's top edge, with no padding added (§10) |
| The caption is off-centre | a dial's `label.offsetX` is a nudge from the cell's **centre**; `offsetY` is measured from the knob's top (§8) |
| The readout is clipped | a centred knob cannot fit a 14 px numeric under it — use `"vAlign": "top"` (§8) |
| A child got the wrong class | `childClasses` is positional, in append order (§18) |
| The class set in C vanished | a non-empty `childClasses` entry overwrites it (§4) |
| The row overflowed | a child's own `padding` sets its minimum size, and the last child absorbs a negative remainder (§16) |
| The scroll did not move | `scrollOffset` is baked in at reflow time — call `reflowCoordinates` (§20) |
| The classes are gone after a reload | a second `compileLayoutConfig` resets the registry (§22) |
| The border did not change when selected | `border.colorSelected` needs a non-zero alpha; a transparent one means "unset" (§11) |

Platform notes:

- Void Linux is the primary target. The window needs raylib, GLFW and a
  display; `--selftest` and `--dump` need none of it.
- `knob.asset` is resolved against the process working directory, so the
  demo `chdir`s into `bin/` before it opens the window (§6).
- The self-test writes its two throwaway fixtures into `.tmp_files/`
  under the working directory, which is gitignored.

## 26. What next

- **Change the demo and watch it move.** Edit
  `src/tools/styling_tutorial/layout.json`, then re-run `--dump` for the
  geometry and `--selftest` for the assertions. The self-test pins the
  numbers this tutorial quotes, so if you change one, the test tells you
  which claim went stale.
- **Read the guide's recipes.** [Recipe A](../ui-styling.md#recipe-a--restyle-something-with-json-only)
  to [Recipe G](../ui-styling.md#recipe-g--add-a-knob-texture--curve-icon-set)
  are the short forms of what §21 did in full.
- **Add a field or a type to the app.** A new style field
  ([Recipe D](../ui-styling.md#recipe-d--add-a-new-style-field-end-to-end))
  or a new style type
  ([Recipe E](../ui-styling.md#recipe-e--add-a-new-style-type)) need
  edits in `src/gui_style.h` and `src/gui_style.c` — that is the part a
  self-contained demo target cannot do.
- **Build it for real.** The next step past this tutorial is a component
  in the app's own graph: a `GuiNode` tree in `src/gui_*.c`, a class in
  `src/layout.json`, and a test in `tests/dsp/test_layout.c`.
- **Then read the gotcha checklist.** The guide's §11 is the one-page
  list of everything that silently does nothing.

## 27. Where the pieces live

| Path | What it is |
|---|---|
| `src/tools/styling_tutorial/styling_tutorial.c` | the component, its draw code, the self-test |
| `src/tools/styling_tutorial/layout.json` | the demo's classes and layouts |
| `src/tools/styling_tutorial/clr.json` | the demo's theme |
| `src/tools/styling_tutorial/meson.build` | the demo's own build target |
| `src/gui_style.c` | the class registry, the compile step, `applyLayout` |
| `src/graph_gui.c` | `GuiNode`, `appendItem`, `reflowCoordinates`, scroll containers |
| `src/gui_core.c` | the shipped draw functions the demo mirrors |
| `manual/ui-styling.md` | the reference guide |
