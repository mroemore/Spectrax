# Arranger scrollable-window rework + instrument control sizing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single-node arranger grid with a scrollable window of per-cell selectable graph nodes (with column-matched channel chips), and bring the instrument screen's meta row + preset row up to a standard control height with PREV/NEXT type selection.

**Architecture:** The arranger graph's `gridColumn` becomes `[chipRow, row0..row7]` — each visible row a horizontal container of 8 selectable `ArrangerCellGuiNode` cells; `arranger->visibleStart` tracks the window; navigation uses the graph nav + an edge-scroll + a post-nav sync that updates `selected_x/y` and fires the existing `onCellSelect`/`onPatternSelection` callbacks. The instrument meta row (weight 1→4) becomes PREV/type-tag/NEXT/VOICES; the preset row (`presetwrappa` 5→4) matches the AD-env row height.

**Tech Stack:** C99, raylib, graph_gui.c (drawNode/navigateGraph), meson/ninja, test_graph_nav harness, instrument_harness scripted fixtures.

## Global Constraints

- `ARRANGER_WINDOW_ROWS = 8`; `MAX_SONG_LENGTH = 255`; `MAX_SEQUENCER_CHANNELS = 8`.
- The graph limits: `MAX_NODE_CHILDREN = 32` per container, `NAV_CAND_CAP = 256` candidates.
- The `rebuilding` flag + `g_audioLock` guard all GUI-thread mutations the audio thread reads (established pattern).
- Standard control height target: **35px rows, 31px content** (the AD-env dial rows).
- Type cycle order: SAMPLE → FM → BLEP → SAMPLE (NEXT), reversed for PREV.
- The sprite icon row is DROPPED (chips already show the type tag; the sheet is corrupt).
- Test gate: `ninja -C build` clean, `meson test -C build` 8/8, all scripted fixtures PASS, app boots (`PRESETS LOADED`).
- Do not touch `src/vizfx.c` beyond what the feature requires.

---

### Task 1: Arranger window state + cell node primitives

**Files:**
- Modify: `src/sequencer.h` (Arranger struct)
- Modify: `src/gui.h`, `src/gui.c`
- Test: `tests/dsp/test_graph_nav.c`

**Interfaces:**
- Produces: `#define ARRANGER_WINDOW_ROWS 8` (in `src/gui.h`). `Arranger` gains `int visibleStart;`. `GuiNode *createArrangerCellGuiNode(int x, int y, int w, int h, bool selected, Arranger *arranger, int ch, int row);` `bool isArrangerCellNode(const GuiNode *n);` `void getArrangerCellCoords(const GuiNode *n, int *x, int *y);`
- Consumes: `initGuiNode`, `cs.*`, `pixelFont`, `MeasureText`, `DrawRectangle`, `DrawRectangleLinesEx`, `DrawTextEx`.

- [ ] **Step 1: Write the failing tests**

`tests/dsp/test_graph_nav.c`:
```c
static int test_arranger_cell_node(void) {
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 4;
    a.visibleStart = 0;
    for(int c = 0; c < 4; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    a.song[1][3] = 42;
    GuiNode *c0 = createArrangerCellGuiNode(0, 0, 50, 20, true, &a, 1, 3);
    ASSERT_TRUE(c0 != NULL, "cell created");
    ASSERT_TRUE(isArrangerCellNode(c0), "is a cell node");
    ASSERT_TRUE(c0->drawable, "cell is drawable");
    int x = -1, y = -1;
    getArrangerCellCoords(c0, &x, &y);
    ASSERT_EQ(x, 1, "cell x");
    ASSERT_EQ(y, 3, "cell y");
    free(c0->name); freeList(c0->items); freeList(c0->itemWeights); free(c0);
    printf("PASS test_arranger_cell_node\n");
    return 0;
}
```
Register in `main()` (check the file's assert macros — `ASSERT_TRUE`/`ASSERT_EQ`).

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: `test_graph_nav` FAILs (createArrangerCellGuiNode undefined).

- [ ] **Step 3: Implement**

`src/sequencer.h` — add `int visibleStart;` to `Arranger`.
`src/gui.h` — `#define ARRANGER_WINDOW_ROWS 8` + the three prototypes (the creator takes `struct Arranger *` — forward-declare or include sequencer.h).
`src/gui.c`:
```c
typedef struct {
	GuiNode base;
	Arranger *arranger;
	int x;
	int y;
} ArrangerCellGuiNode;

static void drawArrangerCellGuiNode(void *self) {
	ArrangerCellGuiNode *cell = (ArrangerCellGuiNode *)self;
	GuiNode *gn = (GuiNode *)cell;
	if(!cell->arranger) return;
	Arranger *a = cell->arranger;
	Color bg;
	if(a->playhead_indices[cell->x] == cell->y && a->playing) {
		bg = cs.arrangerPlayhead;
	} else if(a->song[cell->x][cell->y] > -1) {
		bg = cs.defaultCell;
	} else {
		bg = cs.blankCell;
	}
	DrawRectangle(gn->x, gn->y, gn->w, gn->h, bg);
	char buf[4];
	if(a->song[cell->x][cell->y] > -1) {
		snprintf(buf, sizeof(buf), "%02i", a->song[cell->x][cell->y]);
	} else {
		snprintf(buf, sizeof(buf), "--");
	}
	int fs = (gn->h / 3 > 6) ? gn->h / 3 : 6;
	int tw = MeasureText(buf, fs);
	DrawTextEx(pixelFont, buf, (Vector2){ gn->x + (gn->w - tw) / 2, gn->y + (gn->h - fs) / 2 }, fs, 1, cs.arrangerCellText);
	if(gn->selected) {
		DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0, cs.outlineColour);
	}
}

GuiNode *createArrangerCellGuiNode(int x, int y, int w, int h, bool selected, Arranger *arranger, int ch, int row) {
	ArrangerCellGuiNode *cell = malloc(sizeof(ArrangerCellGuiNode));
	if(!cell) return NULL;
	memset(cell, 0, sizeof(ArrangerCellGuiNode));
	if(!initGuiNode(&cell->base, x, y, w, h, 0, na_horizontal, "cell", true, selected)) {
		free(cell);
		return NULL;
	}
	cell->base.draw = drawArrangerCellGuiNode;
	cell->base.drawable = true;
	cell->arranger = arranger;
	cell->x = ch;
	cell->y = row;
	return &cell->base;
}

bool isArrangerCellNode(const GuiNode *n) { return n && n->draw == drawArrangerCellGuiNode; }

void getArrangerCellCoords(const GuiNode *n, int *x, int *y) {
	if(isArrangerCellNode(n)) {
		ArrangerCellGuiNode *c = (ArrangerCellGuiNode *)n;
		*x = c->x;
		*y = c->y;
	}
}
```
(`cs.arrangerPlayhead`, `cs.defaultCell`, `cs.blankCell`, `cs.arrangerCellText`, `cs.outlineColour` already exist in the ColourScheme.)

- [ ] **Step 4: Run to verify it passes**

Run: `ninja -C build && meson test -C build`
Expected: `test_graph_nav` PASSes.

- [ ] **Step 5: Commit**

```bash
git add src/sequencer.h src/gui.h src/gui.c tests/dsp/test_graph_nav.c
git commit -m "feat(gui): arranger cell node primitive + window state"
```

---

### Task 2: Windowed arranger graph build + scroll

**Files:**
- Modify: `src/gui.c` (`createArrangerGraph`, new `scrollArrangerWindow`)
- Modify: `src/gui.h`
- Test: `tests/dsp/test_graph_nav.c`

**Interfaces:**
- Consumes: Task 1's `createArrangerCellGuiNode`/`getArrangerCellCoords`/`ARRANGER_WINDOW_ROWS` + `a->visibleStart`.
- Produces: `void scrollArrangerWindow(Arranger *a, int delta);` — clamps `visibleStart` to `[0, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS]`, re-targets the 8 row containers' cells to `y = visibleStart + rowIdx`, keeps the selection on the same visible row. `GuiNode *getArrangerRowCell(int rowIdx, int ch);` — the cell node at visible position (rowIdx, ch) (used by sync/scroll).

- [ ] **Step 1: Write the failing tests**

`tests/dsp/test_graph_nav.c`:
```c
static int test_scroll_arranger_window(void) {
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 2;
    a.visibleStart = 0;
    a.playing = 0;
    for(int c = 0; c < 2; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    scrollArrangerWindow(&a, 1);
    ASSERT_EQ(a.visibleStart, 1, "scroll down");
    scrollArrangerWindow(&a, -1);
    ASSERT_EQ(a.visibleStart, 0, "scroll up");
    scrollArrangerWindow(&a, -5);
    ASSERT_EQ(a.visibleStart, 0, "clamped at 0");
    a.visibleStart = MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS;
    scrollArrangerWindow(&a, 3);
    ASSERT_EQ(a.visibleStart, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS, "clamped at max");
    printf("PASS test_scroll_arranger_window\n");
    return 0;
}
```
Register it.

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: FAIL (scrollArrangerWindow undefined).

- [ ] **Step 3: Implement**

`src/gui.c` — add a file-static `static Arranger *g_arranger = NULL;` (set in `createArrangerGraph`). Rebuild `createArrangerGraph`'s grid section (replace the `gridColumn` block at gui.c:301-320 with):
```c
	GuiNode *gridColumn = createGuiNode(0, 0, 100, 100, 2, na_vertical, "gcol", 0, 0);
	gridColumn->drawable = true;
	gridColumn->draw = drawWrapperNode;
	GuiNode *chipRow = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "chipr", 0, 0);
	chipRow->drawable = true;
	chipRow->draw = drawWrapperNode;
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		g_chipNodes[ch] = NULL;
	}
	for(int ch = 0; ch < a->enabledChannels; ch++) {
		GuiNode *chip = createInstChipGuiNode(0, 0, 100, 40, false, a->vm, ch, a);
		g_chipNodes[ch] = chip;
		appendItem(chipRow, chip, 1);
	}
	appendItem(gridColumn, chipRow, 2);
	GuiNode *selectedCell = NULL;
	for(int r = 0; r < ARRANGER_WINDOW_ROWS; r++) {
		GuiNode *rowc = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "row", 0, 0);
		rowc->drawable = true;
		rowc->draw = drawWrapperNode;
		for(int ch = 0; ch < a->enabledChannels; ch++) {
			bool sel = (ch == a->selected_x && a->visibleStart + r == a->selected_y);
			GuiNode *cell = createArrangerCellGuiNode(0, 0, 100, 100, sel, a, ch, a->visibleStart + r);
			if(sel) selectedCell = cell;
			appendItem(rowc, cell, 1);
		}
		appendItem(gridColumn, rowc, 1);
	}
	g_arranger = a;
	appendItem(arrWrap, gridColumn, 4);
	agui->selected = selectedCell ? selectedCell : agui->selected;
```
(The old `ArrangerGuiNode *agn = createArrangerGuiNode(...)` + `agui->selected = gn;` + `appendItem(arrWrap, gridColumn, 4)` block is REPLACED by the above. The `demoStack` below stays.)

`scrollArrangerWindow`:
```c
void scrollArrangerWindow(Arranger *a, int delta) {
	if(!a) return;
	int maxStart = MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS;
	if(maxStart < 0) maxStart = 0;
	int ns = a->visibleStart + delta;
	if(ns < 0) ns = 0;
	if(ns > maxStart) ns = maxStart;
	if(ns == a->visibleStart) return;
	a->visibleStart = ns;
	if(!agui || !agui->root || !agui->root->items) return;
	/* re-target the row cells: gridColumn is the 3rd child of arrWrap,
	 * which is the 2nd child of root. Find gridColumn + walk rows. */
	GuiNode *gridColumn = NULL;
	for(ListElement *l = agui->root->items->head; l; l = l->next) {
		GuiNode *n = *(GuiNode **)l->data;
		if(!n || !n->items) continue;
		for(ListElement *m = n->items->head; m; m = m->next) {
			GuiNode *c = *(GuiNode **)m->data;
			if(c && strcmp(c->name, "gcol") == 0) { gridColumn = c; break; }
		}
		if(gridColumn) break;
	}
	if(!gridColumn || !gridColumn->items) return;
	int r = 0;
	ListElement *rl = gridColumn->items->head; rl = rl->next; /* skip chipRow */
	for(; rl && r < ARRANGER_WINDOW_ROWS; rl = rl->next, r++) {
		GuiNode *rowc = *(GuiNode **)rl->data;
		if(!rowc || !rowc->items) continue;
		for(ListElement *cl = rowc->items->head; cl; cl = cl->next) {
			GuiNode *cell = *(GuiNode **)cl->data;
			if(isArrangerCellNode(cell)) {
				int x, y; getArrangerCellCoords(cell, &x, &y);
				ArrangerCellGuiNode *ac = (ArrangerCellGuiNode *)cell;
				ac->y = a->visibleStart + r;
				/* keep the selection pinned to its visible row */
				cell->selected = (x == a->selected_x && a->visibleStart + r == a->selected_y) ? 1 : 0;
			}
		}
	}
}
```
`getArrangerRowCell`:
```c
GuiNode *getArrangerRowCell(int rowIdx, int ch) {
	if(!agui || !agui->root || !agui->root->items) return NULL;
	for(ListElement *l = agui->root->items->head; l; l = l->next) {
		GuiNode *n = *(GuiNode **)l->data;
		if(!n || !n->items) continue;
		for(ListElement *m = n->items->head; m; m = m->next) {
			GuiNode *c = *(GuiNode **)m->data;
			if(!c || strcmp(c->name, "gcol") != 0 || !c->items) continue;
			int r = 0;
			ListElement *rl = c->items->head; rl = rl->next;
			for(; rl && r <= rowIdx; rl = rl->next, r++) {
				if(r == rowIdx) {
					GuiNode *rowc = *(GuiNode **)rl->data;
					if(!rowc || !rowc->items) return NULL;
					int cc = 0;
					for(ListElement *cl = rowc->items->head; cl && cc <= ch; cl = cl->next, cc++) {
						if(cc == ch) return *(GuiNode **)cl->data;
					}
				}
			}
		}
	}
	return NULL;
}
```
Declare both in `src/gui.h`.

- [ ] **Step 4: Run to verify it passes**

Run: `ninja -C build && meson test -C build`
Expected: PASS. The app must still boot + render the arranger (chip row + cells) — verify with `cd bin && timeout 8 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax 2>&1 | grep -E "PRESETS LOADED|fpe|segfault"`.

- [ ] **Step 5: Commit**

```bash
git add src/gui.c src/gui.h tests/dsp/test_graph_nav.c
git commit -m "feat(gui): arranger scrollable window graph (chip row + 8 cell rows)"
```

---

### Task 3: Nav + sync + remove the old grid

**Files:**
- Modify: `src/gui.c`, `src/gui.h`
- Modify: `src/main.c` (playhead-follow hook)
- Test: `tests/dsp/test_graph_nav.c`

**Interfaces:**
- Consumes: Tasks 1-2 (`isArrangerCellNode`, `getArrangerCellCoords`, `getArrangerRowCell`, `scrollArrangerWindow`, `g_arranger`).
- Produces: `void syncArrangerSelectionTo(Graph *g, Arranger *a);` + `void syncArrangerSelection(void);` (reads the selected node, updates `selected_x/y`, fires `onCellSelect` + `onPatternSelection`). `void navigateArrangerGraphTo(Graph *g, Arranger *a, int keymapping);` + `void navigateArrangerGraph(int keymapping);` — graph-nav + edge-scroll + sync. The `ArrangerGuiNode` struct + `createArrangerGuiNode` + `drawArrangerGuiNode` + `navigateArrangerGuiNode` are DELETED.

- [ ] **Step 1: Write the failing tests**

`tests/dsp/test_graph_nav.c`:
```c
static int test_cell_nav_and_sync(void) {
    /* Build a mini windowed arranger graph: chipRow + 2 rows x 2 channels,
     * then navigate + assert the sync helper picks the right cell + that
     * UP from row 0 lands on the column-matched chip. */
    Graph *g = createGraph(na_vertical);
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 2;
    a.visibleStart = 0;
    a.playing = 0;
    a.selected_x = 0; a.selected_y = 0;
    for(int c = 0; c < 2; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    a.song[1][0] = 7;
    GuiNode *col = createGuiNode(0, 0, 200, 200, 2, na_vertical, "gcol", 0, 0);
    GuiNode *chipRow = createGuiNode(0, 0, 200, 20, 2, na_horizontal, "chipr", 0, 0);
    GuiNode *chip0 = createInstChipGuiNode(0, 0, 50, 20, false, NULL, 0, NULL);
    GuiNode *chip1 = createInstChipGuiNode(50, 0, 50, 20, false, NULL, 1, NULL);
    appendItem(chipRow, chip0, 1); appendItem(chipRow, chip1, 1);
    GuiNode *row0 = createGuiNode(0, 20, 200, 30, 2, na_horizontal, "row", 0, 0);
    GuiNode *r0c0 = createArrangerCellGuiNode(0, 20, 50, 30, true, &a, 0, 0);
    GuiNode *r0c1 = createArrangerCellGuiNode(50, 20, 50, 30, false, &a, 1, 0);
    appendItem(row0, r0c0, 1); appendItem(row0, r0c1, 1);
    GuiNode *row1 = createGuiNode(0, 50, 200, 30, 2, na_horizontal, "row", 0, 0);
    GuiNode *r1c0 = createArrangerCellGuiNode(0, 50, 50, 30, false, &a, 0, 1);
    GuiNode *r1c1 = createArrangerCellGuiNode(50, 50, 50, 30, false, &a, 1, 1);
    appendItem(row1, r1c0, 1); appendItem(row1, r1c1, 1);
    appendItem(col, chipRow, 1); appendItem(col, row0, 1); appendItem(col, row1, 1);
    appendItem(g->root, col, 1);
    g->selected = r0c0;
    navigateGraphRefined(g, KM_DOWN);
    ASSERT_TRUE(g->selected == r1c0, "down to r1c0");
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(g->selected == r1c1, "right to r1c1");
    int x = -1, y = -1;
    getArrangerCellCoords(g->selected, &x, &y);
    ASSERT_EQ(x, 1, "cell x");
    ASSERT_EQ(y, 1, "cell y");
    /* UP from the top row lands on the column-matched chip */
    g->selected = r0c0;
    navigateGraphRefined(g, KM_UP);
    ASSERT_TRUE(isInstChipNode(g->selected), "up lands on a chip");
    ASSERT_TRUE(g->selected == chip0, "chip for column 0");
    /* the sync helper reads the selected node + updates the arranger */
    syncArrangerSelectionTo(g, &a);
    ASSERT_EQ(a.selected_x, 0, "sync x");
    ASSERT_EQ(a.selected_y, 0, "sync y");
    teardown_graph(g);
    printf("PASS test_cell_nav_and_sync\n");
    return 0;
}
```
Register it. (Check `teardown_graph` frees a graph built this way — if it doesn't free the appended nodes, mirror the manual free pattern from `test_chip_node_is_drawable`.)

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: FAIL (syncArrangerSelection undefined).

- [ ] **Step 3: Implement**

`src/gui.c`:
```c
void syncArrangerSelectionTo(Graph *g, Arranger *a) {
	if(!g || !g->selected || !a) return;
	if(isArrangerCellNode(g->selected)) {
		int x = 0, y = 0;
		getArrangerCellCoords(g->selected, &x, &y);
		a->selected_x = x;
		a->selected_y = y;
		int cell[2] = { x, y };
		if(a->onCellSelect.f) a->onCellSelect.f(a->onCellSelect.appstateRef, cell);
		int patternIndex = a->song[x][y];
		if(a->onPatternSelection.f) a->onPatternSelection.f(a->onPatternSelection.appstateRef, &patternIndex);
	}
}
void syncArrangerSelection(void) {
	syncArrangerSelectionTo(agui, g_arranger);
}
```
`navigateArrangerGraphTo` — the core (graph nav + edge-scroll + sync):
```c
void navigateArrangerGraphTo(Graph *g, Arranger *a, int keymapping) {
	if(!g) return;
	navigateGraphRefined(g, keymapping);
	if(g->selected && isArrangerCellNode(g->selected) && a) {
		int x, y;
		getArrangerCellCoords(g->selected, &x, &y);
		if(keymapping == KM_UP && y == a->visibleStart && a->visibleStart > 0) {
			scrollArrangerWindow(a, -1);
		}
		if(keymapping == KM_DOWN && y == a->visibleStart + ARRANGER_WINDOW_ROWS - 1
				&& a->visibleStart < MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS) {
			scrollArrangerWindow(a, 1);
		}
	}
	syncArrangerSelectionTo(g, a);
}
void navigateArrangerGraph(int keymapping) {
	navigateArrangerGraphTo(agui, g_arranger, keymapping);
}
```
Note: `scrollArrangerWindow` (Task 2) re-targets the cells of the LIVE app graph via `agui`; in the unit test the scroll path isn't hit (the mini graph's rows don't change), so the test exercises the sync + chip-column nav only — the scroll logic is verified in the app (Task 5) + the clamp test (Task 2).

Delete `ArrangerGuiNode` (the struct), `createArrangerGuiNode`, `drawArrangerGuiNode`, `navigateArrangerGuiNode` (gui.c) + their gui.h declarations + the `instrumentIcons` sprite references in the arranger path. Check nothing else references `createArrangerGuiNode` (the harness may — grep before deleting; if `instrument_harness.c` calls it, it must be updated to use the new graph build).

**Playhead follow** — `src/main.c`, in the main loop (after `updateBufferScrollerData`, before the input handling), add:
```c
		/* TEMP->FINAL: keep the playing row in the arranger window. */
		{
			extern void scrollArrangerWindow(struct Arranger *, int);
			extern int arrangerWindowStart(void);
			int ws = arrangerWindowStart();
			int ph = data.arranger->playhead_indices[data.arranger->selected_x];
			if(data.arranger->playing) {
				if(ph < ws) scrollArrangerWindow(data.arranger, ph - ws);
				else if(ph > ws + ARRANGER_WINDOW_ROWS - 1) scrollArrangerWindow(data.arranger, ph - (ws + ARRANGER_WINDOW_ROWS - 1));
			}
		}
```
Add `int arrangerWindowStart(void) { return g_arranger ? g_arranger->visibleStart : 0; }` to gui.c + gui.h. (If the include of sequencer.h is already present in main.c, drop the extern.)

- [ ] **Step 4: Run to verify it passes**

Run: build + `meson test` 8/8 + boot check (`PRESETS LOADED`, no fpe/segfault). The arranger must render the chip row + the window cells. The `chip_meta` fixture will likely need updating — defer to Task 5 but note failures.

- [ ] **Step 5: Commit**

```bash
git add src/gui.c src/gui.h src/main.c tests/dsp/test_graph_nav.c
git commit -m "feat(gui): arranger nav edge-scroll + selection sync; drop legacy grid"
```

---

### Task 4: Instrument meta row rework + standard control sizes

**Files:**
- Modify: `src/gui.c` (`appendMetaControlNode`, `createInstGraph`, `appendPresetControlNode`)
- Modify: `src/gui.h`
- Test: `tests/dsp/test_mod_voice.c` (type-cycle helpers)

**Interfaces:**
- Consumes: `setInstrumentVoiceType` (Task 2), `rebuildInstrumentGraph`, `drawActionBtnGuiNode`.
- Produces: `static void cbTypePrev(void *ctx); static void cbTypeNext(void *ctx);` — resolve the channel by pointer identity (`vm->instruments[i] == getSelectedInstInstrument()`), step the type ±1 in the SAMPLE→FM→BLEP cycle, call `setInstrumentVoiceType` + `rebuildInstrumentGraph`. A new static `drawTypeLabelGuiNode` draws the current type tag as text. `appendMetaControlNode` layout: `[PREV] [type-label] [NEXT] [VOICES] [WIDTH]`.

- [ ] **Step 1: Write the failing tests**

`tests/dsp/test_mod_voice.c`:
```c
static int test_type_cycle_order(void) {
	/* The meta-row type cycle is SAMPLE->FM->BLEP->SAMPLE. Expose a small
	 * pure helper `nextVoiceType(VoiceType)`/`prevVoiceType(VoiceType)` in
	 * gui.c and pin the wrap here (they live in the gui layer but the
	 * cycle order is the load-bearing part). */
	ASSERT_EQ(nextVoiceType(VOICE_TYPE_SAMPLE), VOICE_TYPE_FM, "sample->fm");
	ASSERT_EQ(nextVoiceType(VOICE_TYPE_FM), VOICE_TYPE_BLEP, "fm->blep");
	ASSERT_EQ(nextVoiceType(VOICE_TYPE_BLEP), VOICE_TYPE_SAMPLE, "blep->sample");
	ASSERT_EQ(prevVoiceType(VOICE_TYPE_SAMPLE), VOICE_TYPE_BLEP, "sample->blep");
	ASSERT_EQ(prevVoiceType(VOICE_TYPE_BLEP), VOICE_TYPE_FM, "blep->fm");
	printf("PASS test_type_cycle_order\n");
	return 0;
}
```
Register it. (`nextVoiceType`/`prevVoiceType` are exported from gui.h for this test.)

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build`
Expected: FAIL (helpers undefined).

- [ ] **Step 3: Implement**

`src/gui.c`:
```c
VoiceType nextVoiceType(VoiceType t) {
	switch(t) {
		case VOICE_TYPE_SAMPLE: return VOICE_TYPE_FM;
		case VOICE_TYPE_FM:     return VOICE_TYPE_BLEP;
		case VOICE_TYPE_BLEP:   return VOICE_TYPE_SAMPLE;
		default:                return VOICE_TYPE_FM;
	}
}
VoiceType prevVoiceType(VoiceType t) {
	switch(t) {
		case VOICE_TYPE_SAMPLE: return VOICE_TYPE_BLEP;
		case VOICE_TYPE_BLEP:   return VOICE_TYPE_FM;
		case VOICE_TYPE_FM:     return VOICE_TYPE_SAMPLE;
		default:                return VOICE_TYPE_FM;
	}
}
```
Rewrite `cbCycleVoiceType` into two callbacks sharing a resolver:
```c
static int resolveTypeChannel(VoiceManager *vm, Instrument *sel) {
	if(!vm || !sel) return -1;
	for(int i = 0; i < vm->enabledChannels; i++) {
		if(vm->instruments[i] == sel) return i;
	}
	return -1;
}
static void cbTypePrev(void *ctx) {
	VoiceManager *vm = (VoiceManager *)ctx;
	Instrument *sel = getSelectedInstInstrument();
	int ch = resolveTypeChannel(vm, sel);
	if(ch < 0) return;
	if(setInstrumentVoiceType(vm, ch, prevVoiceType(sel->voiceType))) rebuildInstrumentGraph();
}
static void cbTypeNext(void *ctx) {
	VoiceManager *vm = (VoiceManager *)ctx;
	Instrument *sel = getSelectedInstInstrument();
	int ch = resolveTypeChannel(vm, sel);
	if(ch < 0) return;
	if(setInstrumentVoiceType(vm, ch, nextVoiceType(sel->voiceType))) rebuildInstrumentGraph();
}
```
Type label draw:
```c
static void drawTypeLabelGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	/* the current tag comes from the selected instrument's voiceType */
	Instrument *inst = getSelectedInstInstrument();
	const char *tag = inst ? voiceTypeTag(inst->voiceType) : "--";
	int tw = MeasureText(tag, 10);
	DrawTextEx(pixelFont, tag, (Vector2){ gn->x + (gn->w - tw) / 2, gn->y + (gn->h - 10) / 2 }, 10, 1, cs.label);
	if(gn->selected) DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0, cs.outlineColour);
}
```
`appendMetaControlNode` rework (gui.c:2196) — the meta row:
```c
void appendMetaControlNode(Graph *g, GuiNode *container, Instrument *inst, VoiceManager *vm, int channel, int weight, bool selected) {
	(void)g; (void)channel;
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "META", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;
	GuiNode *prevBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "TPREV", selected, cbTypePrev, vm);
	GuiNode *tag = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "TYPE", 0, 0);
	tag->draw = drawTypeLabelGuiNode; tag->drawable = true;
	GuiNode *nextBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "TNEXT", 0, cbTypeNext, vm);
	GuiNode *voices = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "VOICES", 0, incParameterBaseValue, inst->voiceCountParam);
	GuiNode *width = createBlankGuiNode();
	appendItem(btnwrap, prevBtn, 1);
	appendItem(btnwrap, tag, 2);
	appendItem(btnwrap, nextBtn, 1);
	appendItem(btnwrap, voices, 1);
	appendItem(btnwrap, width, 2);
	appendItem(container, btnwrap, weight);
}
```
`createInstGraph` (gui.c:234) — change the meta-row append to `appendMetaControlNode(instGraph, instwrap, inst, vm, channel, 4, false);` (weight 1→4).
`appendPresetControlNode` — change the `appendItem(container, btnwrap, weight)` to pass the row through; in `createInstGraph` the presetWrap append weight stays `5` but the preset row must render ~35px: change the presetWrap weight 5→4 (`appendItem(instwrap, presetWrap, 4)`).
`gui.h`: declare `VoiceType nextVoiceType(VoiceType);` + `VoiceType prevVoiceType(VoiceType);`.

- [ ] **Step 4: Run to verify it passes**

Run: build + `meson test` 8/8 + boot. Then verify the sizes via the harness: `cd bin && ALSA_CONFIG_PATH=<nullalsa> xvfb-run ...` — capture the instrument screen (the harness SHOT verb) and confirm the META row is ~35px (not 4px) and the preset row matches. Also verify the cursor can navigate onto + off the meta row (LEFT/RIGHT/DOWN all move).

- [ ] **Step 5: Commit**

```bash
git add src/gui.c src/gui.h tests/dsp/test_mod_voice.c
git commit -m "feat(gui): meta row PREV/NEXT type + standard 35px control rows"
```

---

### Task 5: Fixtures + full gate

**Files:**
- Modify: `src/tools/instrument_harness/instrument_harness.c` (arranger nav may need re-wiring; `chip_meta.txt`)
- Modify: `src/tools/instrument_harness/fixtures/chip_meta.txt`

**Interfaces:**
- Consumes: the new arranger nav (bare arrows now move the graph selection), the meta row's TPREV/TNEXT buttons (the old `FM`-named TYPE button is gone).

- [ ] **Step 1: Update the harness arranger dispatch**

The harness's `handleArrangerInput` mirrors main.c's SCENE_ARRANGER input. Verify it calls `navigateArrangerGraph` for bare arrows (the new version does graph-nav + scroll + sync) + `syncArrangerSelection` is covered (it is, inside navigateArrangerGraph). Update any references to the removed `createArrangerGuiNode`.

- [ ] **Step 2: Update `chip_meta.txt`**

The fixture navigates UP from the grid → chip. With the rework, UP from a row-0 cell lands on the column-matched chip. The fixture's initial selection: `agui->selected` is now the cell at `(selected_x, selected_y - visibleStart)`. Adjust the fixture's first `UP` + `ASSERT selected==chip` (the chip name is still "chip" — verify) + the meta-row section: the TYPE button is now named `TPREV`/`TNEXT` + the label `TYPE`. The fixture's `JUMP VOICES` + `LEFT` reach... `VOICES` is still a valid name. Update the `EDIT` presses on the old `FM`/type button to `EDIT` on `TPREV`/`TNEXT` + the voiceType asserts stay.

- [ ] **Step 3: Full gate**

Run: `ninja -C build` clean, `meson test -C build` 8/8, ALL scripted fixtures PASS (add_route_delete, preset_save_load, chip_meta), boot `PRESETS LOADED` clean + no fpe/segfault. Manually capture the arranger (harness SHOT) + confirm the chips + the window cells render + the cursor trap on the meta row is gone.

- [ ] **Step 4: Commit**

```bash
git add src/tools/instrument_harness/instrument_harness.c src/tools/instrument_harness/fixtures/chip_meta.txt
git commit -m "test(harness): arranger window + meta-row PREV/NEXT fixtures"
```

---

## Notes for the implementer

- The `demoStack` (vline/poly) + the `songControls` (BPM/Swing) stay untouched.
- `getArrangerSelectedNode` (T7 accessor) still works — it returns `agui->selected`.
- `scrollArrangerWindow` re-targets cells by walking the gridColumn; `getArrangerRowCell` uses the same walk — keep the walk in ONE helper if you prefer (DRY).
- The sync fires `onPatternSelection` (→ `setSelectedPattern` → `rebuildPatternGraph`) on every nav step — this matches the pre-rework behaviour (selectArrangerCell fired them every step).
- The old `drawSprite(instrumentIcons, ...)` icon row is gone; `instrumentIcons` may become unused — remove if nothing else references it.