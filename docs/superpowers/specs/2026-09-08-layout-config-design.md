# Spectrax Layout Config System: class-based styles + module layouts

Date: 2026-09-08
Status: Approved design

## Problem

Every aspect of the GUI's look and arrangement is hardcoded in the graph
builders and draw functions. `GuiNode` geometry (x/y/w/h/padding) is passed
at node creation; `reflowCoordinates` distributes children by weights that
builders bake in via `appendItem`; and the draw functions hardcode their
internal widget geometry — e.g. `drawDialGuiNode` bakes in a 20px knob,
a 38x14 value display at +28/+2, a label at -28/+18, a 9px font, a 0.125
corner radius and a 2.0 border width. Changing any of these today means
editing C code in one of several draw functions, with no way to restyle the
app from outside.

The theme (`clr.json` -> `ColourScheme`) and settings (`cfg.json` ->
`Settings`) are already config-driven with per-key fallback. Layout is the
remaining hardcoded surface.

## Model: three tiers

1. **Composition (code).** Each module's widget structure is defined in code:
   which elements an LFO row or an FM operator group needs, and in what
   order. This never moves to config — the code knows what a module must
   contain to function.
2. **Look (styles).** A class-based style system defines each component
   type's intrinsic geometry, typography, colours and assets. Classes are
   named; a node may be bound to any class; each type has a default.
3. **Arrangement (layouts).** A module/layout system defines how a code-built
   group of elements sits together: orientation, ratios/weights, padding,
   and optional per-child class bindings.

The GUI graph stays the source of truth for structure; styles + layouts only
parameterize it. The declarative-tree end state (the whole GUI in JSON) is
reachable later on this foundation, but the code never loses control of what
a module must contain.

## File structure

One new file, `layout.json`, in the resolved base directory (sibling of
`cfg.json` / `clr.json`), with two top-level sections:

```json
{
  "styles":  { ... per-class styles ... },
  "layouts": { ... named module arrangements ... }
}
```

Same load semantics as cfg/clr: defaults-before-load, per-key fallback, a
malformed file falls back to all defaults, a missing file changes nothing.

## A. Standardized nomenclature

Every property has one canonical name across all sub-elements:

| concept            | canonical key   |
|--------------------|-----------------|
| size               | `width`, `height` |
| position (rel. to node content origin) | `offsetX`, `offsetY` |
| corner             | `roundness`      |
| line weight        | `borderWidth`    |
| text               | `font`, `fontSize`, `spacing` |
| colour             | `color` — always a theme colour name |
| image              | `asset` — always a path |

The node content origin is the node's `(x, y) + padding` — the point the
draw functions already derive as their starting `tmpx/tmpy`.

**Shared sub-element base.** Every positioned sub-element uses the same
position keys (`offsetX`, `offsetY`), and every drawable sub-element can
carry `color` (a `ColourScheme` name) or `asset` (an image path; when
present it is drawn instead of the procedural fallback).

## B. Style classes

### B.1 Dial worked example

Both dial subtypes are expressible from the values currently hardcoded in
`drawDialGuiNode` (continuous) and `drawDiscreteDialGuiNode` (discrete):

```json
"dial": {
  "knob": {
    "offsetX": 0, "offsetY": 0,
    "width": 20, "height": 20,
    "radius": 10, "startAngle": -225, "sweep": 270,
    "color": "dial",
    "asset": "resources/images/dial_knob.png"
  },
  "border": { "roundness": 0.125, "borderWidth": 2, "color": "outlineColour" },
  "value":  { "format": "%05.2f", "width": 38, "height": 14,
              "offsetX": 28, "offsetY": 2, "color": "valueText" },
  "label":  { "font": "pixel", "fontSize": 9, "spacing": 1,
              "offsetX": -28, "offsetY": 18, "color": "label" }
},
"dial-discrete": {
  "extends": "dial",
  "value":  { "format": "%i", "width": 10, "offsetX": 6, "offsetY": 5 },
  "label":  { "offsetX": 6, "offsetY": 21 }
}
```

- `knob.startAngle`/`sweep` replace the `-225` + hardcoded 2.7-rad sweep.
- `value.format` replaces the `%05.2f` / `%i` literals.
- `"extends"` gives a class a parent to inherit keys from; later keys win.
  Missing keys fall through the chain.
- A node's draw function determines its **type default**: continuous dials
  -> `dial`, discrete dials -> `dial-discrete`.

### B.2 Fallback chain

For any node:

```
explicit class (set + defined) -> its extends chain -> the draw fn's type default -> built-in hardcoded defaults
```

- No class set -> the type default -> renders exactly as today.
- A class set but not defined -> the type default.
- A `color` name that doesn't resolve -> the chain -> the hardcoded colour.
- An `asset` that fails to load -> a "no asset" marker -> procedural draw.

The built-in defaults are the current hardcoded literals, so a missing or
malformed `layout.json`, or an unknown class, render byte-for-byte as the
app does today.

## C. Consumption

### C.1 Class naming

`GuiNode` gains an optional `className` field (zero-initialised in
`initGuiNode`). A builder that wants a non-default class calls a setter after
creating the node:

```c
GuiNode *dial = createDialGuiNode(...);
guiNodeSetClass(dial, "env-attack");
```

No class set -> the draw fn's type default. Every existing
`createDialGuiNode` call therefore renders identically today. Adding the
field to `GuiNode` follows the rule that any new walk/dispatch-readable
field is zero-initialised in `initGuiNode`.

### C.2 StyleSet

Compiled once at startup into a flat set of fully-merged classes:

- `extends` chains resolved depth-first, later keys win.
- `color` names resolved to `ColourScheme` values.
- `asset` paths loaded at compile time; failures marked so the draw falls
  back to procedural.

Plus a `LayoutSet` of named layouts (orientation, padding, weights,
childClasses).

### C.3 Draw-time lookup

Each draw function resolves the node's style through a type-specific helper:

```c
const DialStyle *st = resolveDialStyle(gn);   /* class -> extends -> type default -> baked defaults */
```

A pointer chase into the StyleSet; no per-frame string lookups. Each leaf
type gets a `resolve<Type>Style(gn)` + a type-default struct built from its
current draw-fn literals.

## D. Module/layout tier

A named **layout** parameterizes one container's arrangement of its direct
children. The composition (which children exist, in code order) stays in the
builder; the layout only says how they sit together. It slots cleanly over
the existing weight-based `reflowCoordinates`.

```json
"layouts": {
  "env-row": {
    "orientation": "horizontal",
    "padding": 4,
    "weights": [1, 1, 1, 1, 1, 1, 1],
    "childClasses": ["", "env-attack", "env-curve", "env-decay", "env-curve", "", ""]
  },
  "mod-source-column": {
    "orientation": "vertical",
    "padding": 2,
    "weights": [3, 3, 3, 3, 1]
  }
}
```

The builder creates its container + children as today, then applies the
named layout:

```c
GuiNode *row = createGuiNode(...);
/* children appended in code order as now */
applyLayout(row, "env-row");
```

- `weights[]` maps to children **by append order**.
- `childClasses[]` binds class names to children by position; an empty string
  (or missing entry) leaves the child's code-set class alone. A non-empty
  binding is authoritative (it overrides whatever the builder set).
- Same fallback chain as styles: named layout -> if missing, the container
  keeps its current code-set orientation/padding/weights (today's behavior).
- No element *definition* in the layout: `applyLayout` is purely arrangement
  + optional style binding.

## E. Boot wiring

The style/layout compile slots into the existing startup chain, after the
data-dir `chdir` and **after** `clr.json` (styles reference colour names, so
`ColourScheme` must be resolved first):

```
resolve data dir -> chdir -> load cfg.json -> load clr.json (ColourScheme)
   -> compile layout.json -> StyleSet + LayoutSet
```

Both sets are plain structs owned by the GUI module (parallel to
`ColourScheme`), with the same "defaults before load, per-key fallback,
malformed -> defaults" semantics as cfg/clr.

## F. Testing + rollout

Bottom-up; each step independently verifiable:

1. **Dial style plumbing** — `className` on GuiNode, `resolveDialStyle`
   (continuous + discrete), the two draw fns read the resolved style. A
   `layout.json` fixture drives both dial subtypes with a custom class;
   asserts geometry + that a missing class renders the baked default. Old
   look unchanged without config.
2. **Mechanism generalization** — the generic `StyleSet`/resolve helper
   extracted; each leaf type adds its type-default struct + draw-fn lookup
   (btn, label, slider, chip, name-input...). Each migrated type gets the
   same "custom class renders / no class = today" pair.
3. **Layout tier** — `applyLayout` + `childClasses` binding; a fixture
   applies a named layout to a known row and asserts the reflowed geometry
   matches the config's weights/padding; child-class binding asserts the
   bound dials resolve their custom style.
4. **Full gate** — every existing fixture still passes unchanged (they all
   exercise the default look), proving the migration non-destructive.

The existing scripted fixtures are the regression safety net: they drive the
real UI and catch any geometry drift the moment a default class accidentally
changes.