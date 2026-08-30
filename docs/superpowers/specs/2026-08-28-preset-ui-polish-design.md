# Preset UI polish — design

## Motivation

User-reported issues with the preset save/load UI that landed in the prior
plan (preset-save-load), plus three forward-looking asks:

1. **Arcade input**
   - (a) The name node auto-enters edit mode on fresh selection. The user wants
     an explicit enter: the node is selectable, and pressing `z` (KM_EDIT)
     enters edit mode while it's selected.
   - (b) The cursor currently wanders into empty slots beyond the typed name.
     The user wants the cursor bounded to the name's occupied range + one
     append slot. The cursor cannot move past the last available slot
     unless the user has explicitly committed a character there.
   - (c) Pressing `z` (KM_EDIT) + an arrow on the name node crashes the app
     (the name node's `p` is a `PresetNameGuiNode`, not a `Parameter`, and the
     KM_EDIT path calls `currentGraph->selected->callback(selected->p, delta)`
     with a NULL callback, segfaulting).

2. **Save / load**
   - (a) SAVE and LOAD render as dial knobs instead of buttons. Root cause:
     `createBtnGuiNode` sets `gn->draw = drawDialGuiNode` (gui.c:421), and
     `drawBtnGuiNode` is never assigned. Several "button" nodes (PRESET, BPM,
     RATIO1-4, FEEDBACK1-4, LEVEL1-4) were created with `createBtnGuiNode`
     but actually carry a value — they're really dials.
   - (b) Saving in the real app never prompts for overwriting (the harness
     fixture exercises it, so the modal works there). Likely a name-match
     issue in `presetNameExists` (`strncmp(..., 32)` can false-negative on
     shorter names / trailing whitespace).
   - (c) There is no visible feedback when a save succeeds — the user
     cannot tell saving works.
   - (d) LOAD does not confirm when the current instrument has unsaved
     changes.

3. **General UI**
   - (a) A general layer primitive (each layer = a draw function + z-index),
     used by the load list and the modals, because the in-graph UI is
     cramped. Aligns with the previously-deferred layer-system note.
   - (b) The PRESET controls row (PRESET, NAME, SAVE, LOAD) must be at least
     double the current vertical size.

User decisions captured during brainstorming:
- Full btn/dial split (not just label-only fix).
- Dirty = bool flag set on edit, cleared on save.
- Layer primitive is generic (not load-list-only).

## Design

### Section A — Button / dial taxonomy + crash fix

Introduce a clean taxonomy in `src/gui.h` + `src/gui.c`:

- `createDialGuiNode(x, y, w, h, padding, na, name, selected, callback, Parameter *p)`
  — value-bearing. `gn->draw = drawDialGuiNode`. Used for everything that
  shows + edits a value (PRESET, BPM, Swing, RATIO1-4, FEEDBACK1-4,
  LEVEL1-4, ALGO, the envelope-stage A/D controls, ROUTE, etc.).
- `createActionBtnGuiNode(x, y, w, h, padding, na, name, selected, void (*actionCb)(void *ctx), void *ctx)`
  — no value. `gn->draw = drawActionBtnGuiNode` (NEW: label + selected
  outline only, no knob, no value read). Used for SAVE, LOAD.
- Remove `createBtnGuiNode` (replaced by the two above). Migrate every
  caller.

Callback types:
- Dials keep `OnPressCallback = void (*)(Parameter *p, float delta)` for
  incParameterBaseValue and friends.
- Action buttons get `ActionCallback = void (*)(void *ctx)` (single
  argument, no delta). SAVE/LOAD become action buttons; their callbacks
  change signature to `(void *ctx)`.

Draw callbacks:
- `drawDialGuiNode` (unchanged): knob + value text + range math. Requires
  `gn->p` to be a real Parameter; the existing `if(!gn->p) return;` guard
  stays.
- `drawActionBtnGuiNode` (NEW): a rounded rect + the node's name text +
  the selected outline (mirrors `drawColourRectangle` styling). No value
  read.
- Delete `drawBtnGuiNode` (the old broken one that dereferenced `gn->p`
  without a guard).

KM_EDIT + arrow dispatch:
- The instrument input path in both `src/main.c` and
  `src/tools/instrument_harness/instrument_harness.c` dispatches KM_EDIT +
  arrows only when the selected node's `draw == drawDialGuiNode` AND its
  `callback != NULL`. Action buttons + the name node + the load-list node
  are skipped. This eliminates the crash and makes the dispatch correct
  for the new taxonomy.

### Section B — Arcade input rework

Explicit enter via `z` (KM_EDIT):
- `handlePresetUiInput`'s name-node section: arrows cycle the name only
  while `pn->editing == true`. Entering edit mode requires a KM_EDIT press
  while the name node is the selected node.
- KM_EDIT while editing toggles edit mode off.
- KM_SELECT (Shift) while editing toggles edit mode off AND the next arrow
  navigates away (same trap-avoidance as today).

Bounded cursor:
- `effectiveLen` = the index past the last non-space character in
  `pn->name` (0 if the name is empty or all spaces).
- The cursor can occupy `0 .. min(effectiveLen, PRESET_NAME_MAX - 1)`,
  where `PRESET_NAME_MAX = 32`.
- RIGHT at `effectiveLen` is a no-op (cannot move past the last available
  slot without first committing a character there).
- LEFT at 0 is a no-op.
- Typing at the append slot (`pn->name[pn->cursor]`) with a non-space char
  extends `effectiveLen` by one; cycling to space keeps the slot occupied
  so the cursor can advance past it on a future RIGHT (this is the "unless
  the user selects a space char" clause — the user explicitly opts into
  a longer name by placing a space).

Draw update:
- `drawPresetNameGuiNode` already renders red chars on black with an
  inverted cursor block. The cursor still draws at `pn->cursor`; the new
  bounds just prevent the user from leaving it in an invalid position.

### Section C — Save feedback + overwrite reliability

Save feedback:
- On a successful save (`guiSavePreset` returns OK, or overwrite completes),
  set a `pn->savedFlashUntil` timestamp (or frame counter) on the name
  node. `drawPresetNameGuiNode` flashes the background brighter for the
  next ~30 frames so the user sees the save land.
- On a save failure (e.g., file write error), set a `pn->errorFlashUntil`
  that flashes red. `guiSavePreset` already returns `PresetFileResult`; the
  caller (`commitPresetName`) routes OK vs error.

Overwrite reliability:
- `commitPresetName` trims trailing whitespace from `pn->name` before
  passing to `guiSavePreset` (so "Xm1  " and "Xm1" both match the bank
  entry "Xm1").
- `presetNameExists` (`src/io/preset_io.c`) compares up to the actual name
  length (not a fixed `strncmp(..., 32)`), so short names match without
  false negatives.

### Section D — Dirty flag (loaded preset instance)

The dirty flag belongs to the **loaded preset instance** — the snapshot of
what was loaded — not to the instrument in general. A `LoadedPreset` struct
captures the reference:

```c
typedef struct LoadedPreset {
    Preset snapshot;       // the patch as loaded (or last saved)
    bool dirty;            // true when the live instrument has diverged
    char name[33];         // the name of the loaded preset (for display)
} LoadedPreset;
```

`Instrument` (or `InstrumentGui`) holds a `LoadedPreset loaded;` field.

Lifecycle:
- **Load** (preset applied to instrument): `loaded.snapshot = presetFromInstrument(inst); loaded.dirty = false; strncpy(loaded.name, preset.name, 32);`.
- **Edit** (KM_EDIT + arrow on a dial changes a value): `loaded.dirty = true;`. The flag is set, not compared (comparing a full Preset snapshot every edit is too heavy).
- **Save** (successful): `loaded.snapshot = presetFromInstrument(inst); loaded.dirty = false;`. The snapshot is refreshed so dirty clears and the new state becomes the reference.
- **Load with dirty**: when the user activates LOAD and `loaded.dirty == true`, open a "DISCARD UNSAVED CHANGES?" layer (Section E). YES → discard (the load proceeds and overwrites the snapshot). NO → cancel, stay on the instrument screen.

The dirty flag is purely in-memory (not persisted to the preset file).

### Section E — Layer system (graph-based)

Each layer is a full **graph** with its own selection / navigation / drawable
nodes, not a thin draw callback. The screen orchestrates visibility, draw
order, and which layer captures input.

**Architecture**

```c
typedef struct Layer {
    Graph *graph;          // the layer's own graph (selectable + navable)
    int z;                 // draw + input order (higher = on top)
    bool visible;          // rendered + receives input when true
    bool capturesInput;    // intercepts input (vs passthrough to lower z)
    const char *name;      // for debugging
} Layer;

typedef struct LayerStack {
    Layer *items;
    int count;
    int cap;
} LayerStack;
```

`InstrumentGui` owns a `LayerStack overlays;` (plus the base instrument
graph as the implicit z=0 layer).

**Overlay layers** (pushed on demand, popped on close):
- **Overwrite modal** — `guiShowOverwriteModal(name)` pushes a centered
  panel graph containing two action-button nodes: `YES` (overwrites the
  bank entry + file) and `NO` (closes the modal, returns to the name
  entry). KM_SELECT on either node closes the modal. z is above the base
  layer; `capturesInput = true`.
- **Dirty confirm modal** — `guiShowDirtyConfirmModal()` pushes a centered
  panel with `YES` (discard changes, proceed with the load) and `NO`
  (cancel, stay on the instrument screen). Same shape as the overwrite
  modal.
- **Load list** — `guiOpenLoadList()` pushes a scrollable list graph. Rows
  are selectable action-button nodes (one per on-disk preset, sorted).
  Up/Down navigate within the layer; KM_START on a row applies that
  preset and closes the layer; KM_SELECT closes the layer. z above the
  base; `capturesInput = true`.

**Input routing**

The instrument input handler iterates the layer stack from highest z to
lowest. The first visible layer with `capturesInput = true` consumes the
input (its graph's nav + any selected-node handlers). Lower layers do not
see the input that frame. If no overlay captures input, the base
instrument graph handles it.

**Draw order**

`DrawGUI` (instrument scene) iterates layers ascending z and calls
`drawNode(layer.graph->root)` for each visible layer. The instrument
graph draws first (base); overlays paint on top.

**Removal of the load-list graph node**

The current `LOADLIST` graph node inside the instrument's PRESET
controls row is removed. Its slot is reclaimed for the bigger row
(Section F). The load list is now an overlay layer, which also resolves
the cramping (overlays have their own centered layout, not crammed into
the instrument graph).

**Why graph-based (not a thin draw-callback layer)**

A thin overlay (`Layer { draw fn; ctx; z }`) would handle rendering but
not selection/navigation within the overlay. The load list needs up/down
scrolling + KM_START to load; the overwrite modal needs YES/NO selection.
Both require a full graph with nav, not just a draw rectangle.

### Section F — Bigger preset row

- `appendPresetControlNode`'s call site (gui.c, in `createInstGraph`) gives
  the preset controls row container weight **2** (was implicit 1 / default).
  After reflow the row gets roughly 2x the vertical space. Combined with
  the load-list node removal, the remaining controls (PRESET, NAME, SAVE,
  LOAD) have room to breathe.

## Verification gate per task

Each phase ends with:
- `ninja -C build` clean,
- `meson test -C build` green,
- the scripted fixture (`fixtures/preset_save_load.txt`) updated + PASS,
- the existing fixture (`fixtures/add_route_delete.txt` or equivalent)
  still PASS,
- the app boots from `bin/` and the instrument screen renders without
  crashing on `z` + arrows.

## Out of scope (deferred)

- Persisting `dirty` to disk.
- Auto-save.
- Multi-channel preset routing beyond what the project file already does.