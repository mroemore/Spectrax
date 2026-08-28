# Preset UI polish implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the instrument-screen preset save/load UI so SAVE/LOAD are real buttons (not dials), the name-entry node uses arcade-style explicit edit mode with a bounded cursor, the save flow shows feedback and reliably triggers the overwrite prompt, LOAD confirms when there are unsaved changes, and overlay surfaces (modals + load list) move into a generic graph-based layer system with a taller preset row.

**Architecture:** Split `createBtnGuiNode` into a true action-button creator (label, no value, action callback) and `createDialGuiNode` (value, knob, `OnPressCallback`). Introduce a generic `LayerStack` where each layer is a full `Graph` (selectable + navable + drawable); `InstrumentGui` owns the stack, draws layers ascending-z, and routes input to the topmost layer that captures input. Migrate the overwrite modal, dirty-confirm modal, and load list to layers. Track dirty via a `LoadedPreset` snapshot on `Instrument`.

**Tech Stack:** C, meson + ninja, raylib (draw), existing `graph_gui` (Graph/GuiNode + nav + draw), the instrument-harness scripted fixture.

## Global Constraints

- All work lands on branch `preset-save-load` (currently at commit `8c1e2aa`).
- Run spectrax from `bin/`; assets are cwd-relative (memory #1054).
- Use `tests/dsp/` for any new automated tests; build + run via `ninja -C build && meson test -C build`.
- The instrument-harness scripted fixtures run from `bin/` under Xvfb; the wrapper `src/tools/instrument_harness/run_scripted.sh` cleans up `data/instrument_presets/xm1.ipb` and `Xm1.ipb` before and after the run.
- Final gate per task: `ninja -C build` clean, `meson test -C build` green, both fixtures (`fixtures/preset_save_load.txt` + `fixtures/add_route_delete.txt`) PASS, app boots from `bin/`.
- Test-first when adding testable behavior. The existing fixture `fixtures/preset_save_load.txt` is updated as each UI behavior lands.
- Leave the user's uncommitted `src/vizfx.c` WIP untouched.
- Every task's gate ends with `cd bin && timeout 90 bash ../src/tools/instrument_harness/run_scripted.sh fixtures/preset_save_load.txt` + the default fixture + `meson test -C build` all green.

**Existing types/functions the plan reuses (verify by reading):**
- `Instrument` (`src/voice.h`): `paramList`, `modList`, `selectedPresetIndex`, `voiceType`, `id.fm/sampler/blep`, `panning`.
- `Preset` (`src/voice.h`): `name[33]`, `voiceType`, `pd`, `modSettings`, `modSettingsCount`.
- `PresetBank` (`src/voice.h`): `patches[]`, `presetCount`.
- `applyInstrumentPreset(Instrument*, Preset)` (`src/voice.c`).
- `presetFromInstrument(Instrument*) -> Preset` (`src/voice.c`).
- `saveInstrumentAsPreset(Instrument*, const char*, const char*) -> PresetFileResult`, `saveInstrumentAsPresetOverwrite(...)`, `presetNameExists(...)` (`src/io/preset_io.c`).
- `Graph`, `GuiNode`, `OnPressCallback`, `drawNode`, `navigateGraphRefined`, `changeGraphSelection` (`src/graph_gui.h/.c`).
- `isKeyJustPressed(InputState*, KeyMapping)`, `isKeyHeld` (`src/input.h`).
- `getSelectedInstGraph()`, `getSelectedInstInstrument()`, `InstrumentGui` (`src/gui.h/.c`).
- `handlePresetUiInput(InputState*, Instrument*)` (`src/gui.c`) — consumed by `src/main.c` and `src/tools/instrument_harness/instrument_harness.c`.

---

### Task 1: Btn/dial taxonomy — creators + draw callbacks

**Files:** Modify `src/gui.h`, `src/gui.c`.

**Adds:**
- `typedef void (*ActionCallback)(void *ctx);`
- `GuiNode *createDialGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback cb, Parameter *p);` — `gn->draw = drawDialGuiNode`.
- `GuiNode *createActionBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, ActionCallback cb, void *ctx);` — `gn->draw = drawActionBtnGuiNode` (label + selected outline, no value read).
- `void drawActionBtnGuiNode(void *self);`
- `GuiNode` fields: `ActionCallback actionCb; void *actionCtx;`.

**Removes:** old `createBtnGuiNode` body + old `drawBtnGuiNode` (Task 2 deletes once migration done).

**Why:** `createBtnGuiNode` sets `gn->draw = drawDialGuiNode` (gui.c:421). The old `drawBtnGuiNode` derefs `gn->p->currentValue` with no guard.

- [ ] **Step 1:** In `src/gui.h`, add after `OnPressCallback`: the `ActionCallback` typedef and the three new declarations.
- [ ] **Step 2:** In `src/gui.c`, add `createDialGuiNode`, `createActionBtnGuiNode`, `drawActionBtnGuiNode` after the existing `drawDialGuiNode` body (~line 580). The `drawActionBtnGuiNode` body: `drawColourRectangle(gn->x, gn->y, gn->w, gn->h, 0.125, 2.0, gn->selected)`; label text at `(gn->x+gn->padding+4, gn->y+gn->padding+4)` using `pixelFont`, size 10, color `(255,180,180,255)` when selected else `(200,180,180,255)`. `drawColourRectangle` exists already.
- [ ] **Step 3:** Add `ActionCallback actionCb; void *actionCtx;` to the `GuiNode` struct (in `src/graph_gui.h` if that's where `callback`/`p` live).
- [ ] **Step 4:** `ninja -C build` — expect 0 errors. The old `createBtnGuiNode` is unchanged so callers compile.
- [ ] **Step 5:** Commit: `git commit -m "feat(gui): add createDialGuiNode + createActionBtnGuiNode (taxonomy split)"`.

---

### Task 2: Btn/dial migration — 17+ call sites

**Files:** Modify `src/gui.c`.

**Migrates:** every `createBtnGuiNode(... incParameterBaseValue, <param>)` → `createDialGuiNode(...)` (preserve all other args). The SAVE/LOAD `createBtnGuiNode(... cbFocusNameNode|cbOpenLoadList, (Parameter*)inst)` → `createActionBtnGuiNode(...)`.

**Sites (preserve args except the function name):** BPM (183), Swing (184), PRESET (1102), RATIO1-4/FEEDBACK1-4/LEVEL1-4 (1131-1142), ALG (1143), PAN FM (1144), SAMPLE/PAN sampler/LOOP/START/END/PLAYBACK (1191-1198), SHAPE/PAN BLEP (1232-1233), ATTACK (1293) + every remaining AD control. After replacement, `grep -n 'createBtnGuiNode.*incParameterBaseValue' src/gui.c` returns no hits.

**SAVE/LOAD (in `appendPresetControlNode`):**
```c
GuiNode *saveBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "SAVE", 0, cbFocusNameNode, inst);
GuiNode *loadBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LOAD", 0, cbOpenLoadList, inst);
```

Update `cbFocusNameNode` + `cbOpenLoadList` to the `(void *ctx)` signature (drop the `(Parameter *p, float v)` params; the callbacks already ignored both). Update their forward decls in `handlePresetUiInput`.

- [ ] **Step 1:** Replace the 17+ value-bearing call sites with `createDialGuiNode`.
- [ ] **Step 2:** Migrate SAVE/LOAD + update the two callback signatures + forward decls.
- [ ] **Step 3:** Remove the old `createBtnGuiNode` (gui.h + gui.c ~412-423) + the old `drawBtnGuiNode` (gui.c ~536-555).
- [ ] **Step 4:** Gate: `ninja -C build` clean, `meson test -C build` 7/7, both fixtures PASS.
- [ ] **Step 5:** Commit: `git commit -m "refactor(gui): split btn/dial taxonomy; SAVE/LOAD render as buttons"`.

---

### Task 3: KM_EDIT dispatch guard — fix `z`+DOWN crash

**Files:** Modify `src/main.c:325-341`, `src/tools/instrument_harness/instrument_harness.c:652-665`, `src/gui.h`, `src/gui.c`.

**Adds:** `bool isSelectedDialNode(const Graph *g);` in `src/gui.h` + `src/gui.c`:
```c
bool isSelectedDialNode(const Graph *g) {
    if(!g || !g->selected) return false;
    GuiNode *n = g->selected;
    return n->draw == drawDialGuiNode && n->callback != NULL;
}
```

**Fixes:** wrap the four `currentGraph->selected->callback(...)` lines in BOTH `src/main.c` and the harness's `handleInstrumentInput` with `if(isKeyHeld(KM_EDIT) && isSelectedDialNode(currentGraph))`. Skip the entire block when the selection is anything else (action button, name node, load-list node).

- [ ] **Step 1:** Add `isSelectedDialNode` declaration + definition.
- [ ] **Step 2:** Guard `src/main.c` lines 325-341.
- [ ] **Step 3:** Guard `src/tools/instrument_harness/instrument_harness.c` lines 652-665.
- [ ] **Step 4:** Append a crash-regression block to `fixtures/preset_save_load.txt` (just before the final `QUIT`): `EDIT` then `DOWN` then `ASSERT selected==PRESET_NAME`. If the fixture reaches the assert, the crash is gone.
- [ ] **Step 5:** Gate: both fixtures PASS (the new fixture block exercises the regression).
- [ ] **Step 6:** Commit: `git commit -m "fix(gui): guard KM_EDIT+arrow dispatch; z+DOWN on name node no longer crashes"`.

---

### Task 4: Arcade input rework — explicit edit mode + bounded cursor

**Files:** Modify `src/gui.c` (`handlePresetUiInput`, `drawPresetNameGuiNode`, `createPresetNameGuiNode`, the `NAME_CHARS` constant).

**Behavior changes:**

1. **Explicit edit enter/exit.** The name node no longer auto-enters edit mode when selected. `handlePresetUiInput`'s name-node branch:
   - Selected but NOT editing: arrows fall through (return false) so the user can navigate away. `KM_EDIT` (z) sets `pn->editing = true`.
   - Editing: arrows cycle the active char (`cycleNameChar`) + move the cursor (`LEFT/RIGHT`). `KM_START` commits (`commitPresetName`). `KM_EDIT` (z) toggles edit off. `KM_SELECT` (Shift) toggles edit off.
   - The `lastNameGraph` / `lastNameNode` static tracker from Task 7's fix stays — it only matters if `editing` is somehow entered by means other than KM_EDIT. After Task 4, the tracker is no longer needed for enter (KM_EDIT is explicit). Remove the `if(lastNameGraph != g || lastNameNode != sel) pn->editing = true;` line. Keep the tracker reset in the `else` branch.

2. **Bounded cursor.** New helper:
   ```c
   static int effectiveNameLen(const char *name) {
       int last = -1;
       for(int i = 0; i < PRESET_NAME_MAX; i++) {
           if(name[i] == '\0') break;
           if(name[i] != ' ') last = i;
       }
       return last + 1;  /* 0 for empty/all-space */
   }
   ```
   In the `KM_LEFT`/`KM_RIGHT` branches:
   ```c
   int max = effectiveNameLen(pn->name);
   if(max > PRESET_NAME_MAX - 1) max = PRESET_NAME_MAX - 1;
   if(isKeyJustPressed(is, KM_LEFT)  && pn->cursor > 0)             pn->cursor--;
   if(isKeyJustPressed(is, KM_RIGHT) && pn->cursor < max)            pn->cursor++;
   ```
   Typing at `pn->cursor == max` (the append slot) with a non-space char extends `max` by one; cycling to space keeps the slot occupied. `drawPresetNameGuiNode` continues to render red chars on black + inverted cursor block; no change needed there.

- [ ] **Step 1:** Add `effectiveNameLen` + replace the `KM_LEFT`/`KM_RIGHT` branches in `handlePresetUiInput`'s name-node section.
- [ ] **Step 2:** Restructure the name-node input: remove the auto-edit-on-fresh-selection; add `KM_EDIT` enter + `KM_EDIT`/`KM_SELECT` exit; arrows consume only while editing.
- [ ] **Step 3:** Remove the `lastNameGraph`/`lastNameNode` enter logic (no longer needed). Keep the reset on selection-moved-away.
- [ ] **Step 4:** Update `fixtures/preset_save_load.txt`: change the early nav/edit flow to use `EDIT` to enter edit mode + `EDIT`/`SELECT` to exit (the fixture's char-cycle block currently assumes auto-edit). The fixture still PASSes with the new flow because the harness injects explicit `EDIT` frames.
- [ ] **Step 5:** Gate: both fixtures PASS, 7/7 tests.
- [ ] **Step 6:** Commit: `git commit -m "feat(gui): arcade name entry uses explicit KM_EDIT enter + bounded cursor"`.

---

### Task 5: Save feedback + overwrite reliability

**Files:** Modify `src/gui.c`, `src/io/preset_io.c`.

**Changes:**

1. **`commitPresetName` whitespace trim.** After the existing "UNNAMED if all-spaces" guard, trim trailing whitespace:
   ```c
   for(int i = (int)strlen(pn->name) - 1; i >= 0 && pn->name[i] == ' '; i--) pn->name[i] = '\0';
   ```
   (handles the "Xm1  " → "Xm1" case that broke the EXISTS match.)

2. **`presetNameExists` length-aware compare.** Replace the fixed `strncmp(..., 32)` (gui.c/preset_io.c) with a compare up to the shorter string's length:
   ```c
   bool presetNameExists(PresetBank *pb, const char *name) {
       if(!pb || !name) return false;
       for(int i = 0; i < pb->presetCount; i++) {
           if(strcmp(pb->patches[i].name, name) == 0) return true;
       }
       return false;
   }
   ```

3. **Save feedback flash.** Add to `PresetNameGuiNode` two fields:
   ```c
   int savedFlashUntil;   /* frame index; > current frame => flash on success */
   int errorFlashUntil;   /* frame index; > current frame => flash red on error */
   ```
   In `commitPresetName` (after `guiSavePreset`), on success set `pn->savedFlashUntil = currentFrameIndex + 30`. If `guiSavePreset` returns PRESET_ERROR (new return value), set `pn->errorFlashUntil`. `guiSavePreset` currently has no return value — change it to return `PresetFileResult`. In `drawPresetNameGuiNode`, render the background brighter (`(Color){ 60, 0, 0, 255 }`) when savedFlashUntil is active, redder when errorFlashUntil is active.

   Get the current frame index with a tiny helper: `static int currentFrameIndex(void) { return (int)(GetTime() * 60.0f); }` (raylib's GetTime returns seconds; 60fps assumption). Place near the other static helpers.

- [ ] **Step 1:** Trim trailing whitespace in `commitPresetName`.
- [ ] **Step 2:** Replace `presetNameExists`'s `strncmp(..., 32)` with `strcmp` length-aware loop.
- [ ] **Step 3:** Add `savedFlashUntil` + `errorFlashUntil` to `PresetNameGuiNode`; init to 0 in `createPresetNameGuiNode`.
- [ ] **Step 4:** Change `guiSavePreset` to return `PresetFileResult`. Update `commitPresetName` to set the flash on the appropriate return value.
- [ ] **Step 5:** Update `drawPresetNameGuiNode` to render the flash tint when the flash is active.
- [ ] **Step 6:** Append a fixture block that asserts the saved-flash: after a successful save, navigate back to the name node and verify the draw fires the flash (use the existing `ASSERT selected==PRESET_NAME` + add `ASSERT pn->savedFlashUntil > currentFrameIndex`). If a `pn->` introspection assert is too invasive, instead capture the frame index at save time and assert it changed.
- [ ] **Step 7:** Gate: both fixtures PASS, 7/7 tests.
- [ ] **Step 8:** Commit: `git commit -m "feat(presets): save feedback flash + length-aware overwrite match"`.

---

### Task 6: Dirty flag via LoadedPreset snapshot

**Files:** Modify `src/voice.h`, `src/voice.c`, `src/main.c`, `src/tools/instrument_harness/instrument_harness.c`, `src/gui.h`, `src/gui.c`.

**Adds:**
- `typedef struct LoadedPreset { Preset snapshot; bool dirty; char name[33]; } LoadedPreset;` (in `src/voice.h`).
- `LoadedPreset loaded;` field on `Instrument` (in `src/voice.h`). Default `dirty = false`, `name[0] = '\0'`, snapshot zero-initialized.

**Lifecycle:**
- **Load** (`applyInstrumentPreset` success, or `cbSetInstrumentPreset` / the harness's fixture-time load): set `inst->loaded.snapshot = presetFromInstrument(inst); inst->loaded.dirty = false; strncpy(inst->loaded.name, preset.name, 32);`. Centralize in a helper `markPresetLoaded(Instrument*, const char *name)` (src/voice.c).
- **Edit** (`KM_EDIT` + arrow fires a dial callback that actually changes a value): set `inst->loaded.dirty = true;`. The cleanest hook: in `isSelectedDialNode`'s caller (the app + harness instrument input), when the callback fires, also set dirty (or compute by comparing the new currentValue vs the snapshot). Simplest reliable: set dirty=true on every callback fire (the callback only fires when the user edits; even non-changing fires are negligible — but to avoid false positives on repeat arrow presses, also check the value actually changed). For this task, set dirty=true whenever the KM_EDIT+arrow block fires a callback.
- **Save** (`guiSavePreset` / `saveInstrumentAsPreset` / `saveInstrumentAsPresetOverwrite` success): refresh the snapshot via `markPresetLoaded(inst, name)`.

**LOAD gate:** the LOAD button's callback (`cbOpenLoadList`) first checks `inst->loaded.dirty`; if true, push the dirty-confirm layer (Task 7). The dirty-confirm's YES callback discards (`markPresetLoaded` with the current snapshot first? No — discard means proceed to load, so the next load resets it). NO callback cancels.

**Implementation order:**
- Add the struct + field.
- Add `markPresetLoaded`.
- Wire the KM_EDIT+arrow hook in main.c + harness: after a successful callback fire, `if(Instrument *inst = getSelectedInstInstrument()) inst->loaded.dirty = true;`.
- Wire the save refresh in `guiSavePreset` (or in `commitPresetName` after success).
- Wire the load refresh in `applyInstrumentPreset` (or wherever loads happen).
- Wire the LOAD button to check dirty + push dirty-confirm layer (Task 7 creates the layer; this task creates the dirty check + dirty-set).

- [ ] **Step 1:** Define `LoadedPreset` in `src/voice.h`; add `loaded` field to `Instrument`.
- [ ] **Step 2:** Implement `markPresetLoaded(Instrument*, const char *name)` in `src/voice.c` — sets snapshot via `presetFromInstrument`, clears dirty, copies name.
- [ ] **Step 3:** Call `markPresetLoaded` after every successful save (`guiSavePreset`) and every successful load (`applyInstrumentPreset`).
- [ ] **Step 4:** In `src/main.c` and the harness, after the KM_EDIT+arrow callback fires (inside the `isSelectedDialNode` guard), call `getSelectedInstInstrument()->loaded.dirty = true;`. Make sure the dial callback actually changed the value before setting dirty (compare before/after, OR check `getParameterDirty`-style flag if Parameter tracks it; for this task, set dirty unconditionally on callback fire, then refine if false positives appear).
- [ ] **Step 5:** Modify `cbOpenLoadList` to check `inst->loaded.dirty` before opening the load list; if dirty, push the dirty-confirm layer (Task 7 provides `guiShowDirtyConfirmModal`; this task calls it).
- [ ] **Step 6:** Add tests in `tests/dsp/test_mod_voice.c`:
   - `test_loaded_preset_clean_after_load`: load a preset, assert `dirty == false`.
   - `test_loaded_preset_dirty_after_edit`: load + mutate a param via `setParameterBaseValue` + assert `dirty == true`.
   - `test_loaded_preset_clean_after_save`: load + edit + `markPresetLoaded` + assert `dirty == false`.
- [ ] **Step 7:** Gate: 7/7 + new tests + both fixtures PASS.
- [ ] **Step 8:** Commit: `git commit -m "feat(presets): LoadedPreset snapshot + dirty flag on edit/load/save"`.

---

### Task 7: Layer primitive + overlay migrations

**Files:** Create `src/gui_layer.h`, `src/gui_layer.c`. Modify `src/gui.h`, `src/gui.c`, `src/main.c`, `src/tools/instrument_harness/instrument_harness.c`.

**Layer primitive (`src/gui_layer.h`):**
```c
#include "graph_gui.h"

typedef struct Layer {
    Graph *graph;
    int z;
    bool visible;
    bool capturesInput;
    const char *name;  /* for debugging; not owned */
} Layer;

typedef struct LayerStack {
    Layer *items;
    int count;
    int cap;
} LayerStack;

void layerStackInit(LayerStack *ls);
void layerStackPush(LayerStack *ls, Graph *g, int z, bool capturesInput, const char *name);
void layerStackPopByGraph(LayerStack *ls, Graph *g);   /* find by graph pointer, free its graph */
void layerStackDraw(LayerStack *ls);                   /* ascending z, visible only */
bool layerStackInput(LayerStack *ls, InputState *is);  /* returns true if the topmost visible+captures layer consumed input */
void layerStackFree(LayerStack *ls);                   /* frees graphs + items */
```

`src/gui_layer.c`: a small array-backed stack. `layerStackInput` iterates top→bottom (descending z), calls `handlePresetUiInput`-style routing for the first visible+captures layer (returns true and stops). For now, the input handler in the top layer does its own nav + assertion; the layer input is a thin "does any layer want input?" gate.

**Wire into `InstrumentGui`:**
```c
typedef struct {
    Graph *instrumentScreenGraphs[MAX_SEQUENCER_CHANNELS];
    int instrumentCount;
    int *selectedInstrument;
    Shape shape;
    struct VoiceManager *vm;
    LayerStack overlays;            /* NEW */
} InstrumentGui;
```

**Overlay factories (in `src/gui.c`):**
- `Graph *buildOverwriteModal(const char *name)` — centered panel (e.g., 280×80), root na_vertical, two action buttons `YES` (callback calls `saveInstrumentAsPresetOverwrite` + pops layer) and `NO` (callback pops layer). Both use `createActionBtnGuiNode`. The panel's action buttons live in a horizontal container.
- `Graph *buildDirtyConfirmModal()` — same shape, `YES` (calls `guiOpenLoadList` after marking the load, pops dirty layer) and `NO` (just pops).
- `Graph *buildLoadList(PresetLoadList *list)` — panel ~320×LOADLIST_VISIBLE_ROWS*ROW_PX, na_vertical, rows are action buttons (one per preset name); up/down nav within the layer (uses `navigateGraphRefined`), `KM_START` applies the preset + pops, `KM_SELECT` pops. The row callback is `cbApplyLoadedPreset(name, &inst->loaded.snapshot)` — loads + applies the preset + clears dirty via `markPresetLoaded`.

Helpers:
- `void guiShowOverwriteModal(const char *name)` — pushes the overwrite layer.
- `void guiShowDirtyConfirmModal(void)` — pushes the dirty-confirm layer.
- `void guiOpenLoadList(void)` — if `inst->loaded.dirty`, push dirty-confirm (which on YES pushes the load-list layer); else push the load-list layer directly.
- `void guiHideOverwriteModal(void)` / `void guiCloseTopLayer(void)` — pop the top layer.

**Wire into the input path (src/main.c SCENE_INSTRUMENT + harness):**
Before the existing `handlePresetUiInput` call, check `layerStackInput(&igui->overlays, inputState)`. If true, skip the rest of the instrument input. `handlePresetUiInput` continues to handle name edit + START-activation (it now routes START/KM_EDIT/KM_SELECT within the currently-focused layer — see Step 3).

**Wire into DrawGUI (SCENE_INSTRUMENT):**
After `drawNode(igui->instrumentScreenGraphs[*igui->selectedInstrument]->root)`, call `layerStackDraw(&igui->overlays);`. Remove the existing `drawPresetModal()` call (it's now a layer).

**Remove the load-list graph node from the instrument graph.** In `appendPresetControlNode`, delete the `LOADLIST` node creation + its `appendItem` into the container. Remove the `drawPresetLoadListNode` function + the `LOADLIST` graph-node plumbing (the new layer uses a fresh draw fn `drawLoadListPanel` that draws the panel + highlights the current row + handles up/down via the graph's nav).

- [ ] **Step 1:** Create `src/gui_layer.h` + `src/gui_layer.c` with the interface above. Add to `meson.build` (`src/gui_layer.c`).
- [ ] **Step 2:** Add `LayerStack overlays;` to `InstrumentGui`; init it in `createInstrumentGui`; free in the cleanup function near line 1349.
- [ ] **Step 3:** Build `buildOverwriteModal`, `buildDirtyConfirmModal`, `buildLoadList` in `src/gui.c`. Implement their action-button callbacks.
- [ ] **Step 4:** Migrate `drawPresetModal` → a layer; remove the `drawPresetModal()` call from DrawGUI; replace `guiSetOverwritePending` (which currently opens the modal directly) with `guiShowOverwriteModal`.
- [ ] **Step 5:** Migrate `guiOpenLoadList` → layer; remove the `LOADLIST` graph node from `appendPresetControlNode`; remove the old `g_loadListActive` + `drawPresetLoadListNode` plumbing.
- [ ] **Step 6:** Wire `layerStackInput` into the instrument input path in `src/main.c` and the harness. Route `handlePresetUiInput`'s START-activation check to the topmost overlay's graph's selected node (not the base graph) when an overlay captures input.
- [ ] **Step 7:** Wire `layerStackDraw` into DrawGUI.
- [ ] **Step 8:** Update `fixtures/preset_save_load.txt`: the load-list section now navigates within the overlay layer (assertions target the overlay's graph). The overwrite modal flow similarly targets the overlay graph. Add a dirty-confirm flow: navigate, edit a param (sets dirty), press LOAD, the dirty-confirm layer opens, navigate to YES, confirm — load list appears.
- [ ] **Step 9:** Gate: both fixtures PASS, 7/7 tests, app boots.
- [ ] **Step 10:** Commit: `git commit -m "feat(gui): layer primitive + modal/loadlist migrate to overlays"`.

---

### Task 8: Taller preset row + final gate

**Files:** Modify `src/gui.c` (`appendPresetControlNode`'s call site in `createInstGraph`).

**Change:** give the preset-controls-row container weight **2** (was implicit 1 / default). In `appendPresetControlNode`, find the `appendItem(container, ...)` call that adds the row to the root (the row is added as a single horizontal container). If `appendItem` accepts a weight parameter, pass 2. If weight is set on the node itself, set `gn->weight = 2` on the row container. Combined with the load-list node removal (Task 7), the remaining controls (PRESET, NAME, SAVE, LOAD) gain roughly 2× the vertical space.

- [ ] **Step 1:** In `createInstGraph` (around line 1100), find the preset row container and set its weight to 2.
- [ ] **Step 2:** Rebuild + verify the instrument screen renders with a taller preset row (visual check: the four controls are clearly larger than before).
- [ ] **Step 3:** Final gate:
   ```bash
   ninja -C build
   meson test -C build
   meson install -C build
   cd bin
   bash ../src/tools/instrument_harness/run_scripted.sh
   bash ../src/tools/instrument_harness/run_scripted.sh fixtures/preset_save_load.txt
   xvfb-run -a -s "-screen 0 1280x800x24" timeout 6 ./spectrax
   ```
   Expected: 0 build errors, 7/7, both fixtures PASS, app prints `PRESETS LOADED`.
- [ ] **Step 4:** Whole-branch review: dispatch the plan-review subagent over `8c1e2aa..HEAD` to verify all 6 spec sections. Fix any Critical/Important findings.
- [ ] **Step 5:** Commit the taller row: `git commit -m "feat(gui): taller preset controls row (2x weight)"`.

---

## End-to-End Self-Review (run after Task 8)

- Section A (btn/dial): Tasks 1-2.
- Section B (arcade): Task 4.
- Section C (save feedback + overwrite): Task 5.
- Section D (dirty + LoadedPreset): Task 6.
- Section E (layer system): Task 7.
- Section F (taller row): Task 8.

All six spec sections map to a task. The plan's Global Constraints (above) appear in every task's gate.

## Out of scope

- Persisting `dirty` to disk.
- Auto-save.
- Generalizing the layer system beyond the instrument screen (other scenes can adopt it later).