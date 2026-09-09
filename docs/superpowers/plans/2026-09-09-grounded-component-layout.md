# Grounded Component/Layout Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define and wire the representations of four real components (chip, mod source rows, step cells, route-dest) so the layout/style system's feature gaps (composed-node model, intrinsic sizing, gap, state colours, theme-name wiring) are solved as a necessity.

**Architecture:** Each component becomes a `StyleType` with per-component sub-element structs (the dial's pattern), registered in the existing class-map/extends machinery and overridable from `layout.json`. `reflowCoordinates` gains a layout-scoped `gap`. The chip palette moves into the `ColourScheme` (theme names in styles). Draw fns resolve styles at draw time.

**Tech Stack:** C, raylib 5.5 (vendored), meson/ninja, cJSON. Tests in `tests/dsp/` run via `meson test -C build` (no window). GUI verification via `src/tools/instrument_harness/run_scripted.sh fixtures/<name>.txt` (windowed; run from repo root).

## Global Constraints

- Colours in `layout.json` styles are **theme names**, never hex — resolve through `themeFieldByName` / `jsonColor`.
- Every field a walk or generic dispatch reads must be zero-initialized in `initGuiNode` (project rule #1154). `GuiNode.gap` is read by reflow → must be zeroed there.
- `gap == 0` must reproduce today's reflow output byte-for-byte.
- No unconditional temp hooks in `main.c` (rule #1136); no Python in build tooling (rule #1126); restore `bin/s1.sng` + `bin/data/instrument_presets/fm1.ipb` from HEAD after any fixture run (rule #1167).
- Baked defaults must equal the current hardcoded draw-fn literals; a missing/malformed `layout.json` renders byte-for-byte as today.
- New theme colour keys must be covered in `tests/dsp/test_cfg.c` (rule #1148) and added to `themeFieldByName` + the save-names list in `config_io.c` (they must stay in sync).

---

### Task 1: `gap` in the layout tier

**Files:**
- Modify: `src/graph_gui.h` (GuiNode struct + initGuiNode), `src/graph_gui.c:6-55` (initGuiNode) + `src/graph_gui.c` (reflowCoordinates)
- Modify: `src/gui_style.h` (LayoutDef), `src/gui_style.c` (applyLayout + layout parse)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: the current `reflowCoordinates` (proportional distribution, last child absorbs remainder — landed in the prior review round).
- Produces: `GuiNode.gap` (uint16_t, 0 default); `LayoutDef.gap` (int); `applyLayout(container, name)` sets `container->gap` from the layout; `reflowCoordinates` reserves `gap · (itemCount−1)` from the content dimension and positions children `share + gap` apart.

- [ ] **Step 1: Write the failing tests**

Add to `tests/dsp/test_layout.c`:

```c
static int test_reflow_gap_distribution(void) {
	/* [1,1,1] in a 100px row, padding 0, gap 4:
	 * availDim = 100 - 8 = 92; shares 30,30,32; positions 0,34,68. */
	GuiNode *row = createGuiNode(0, 0, 100, 20, 0, na_horizontal, "row", 0, 0);
	GuiNode *c[3];
	for(int i = 0; i < 3; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 1);
	}
	row->gap = 4;
	reflowCoordinates(row);
	ASSERT_TRUE(c[0]->x == 0 && c[0]->w == 30, "child 0 at 0, w 30");
	ASSERT_TRUE(c[1]->x == 34 && c[1]->w == 30, "child 1 at 34 (30+gap 4)");
	ASSERT_TRUE(c[2]->x == 68 && c[2]->w == 32, "child 2 at 68, absorbs remainder");
	ASSERT_TRUE(c[2]->x + c[2]->w == 100, "row fills exactly");
	freeGuiNode(row);
	printf("PASS test_reflow_gap_distribution\n");
	return 0;
}

static int test_reflow_gap_zero_matches_pinned(void) {
	GuiNode *row = createGuiNode(0, 0, 566, 35, 1, na_horizontal, "row", 0, 0);
	GuiNode *c[6];
	for(int i = 0; i < 5; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 60);
	}
	c[5] = createBlankGuiNode();
	appendItem(row, c[5], 4);
	/* gap is 0 by default -> unchanged pinned geometry */
	ASSERT_TRUE(c[0]->w == 111 && c[5]->x + c[5]->w == 564, "gap 0 unchanged");
	freeGuiNode(row);
	printf("PASS test_reflow_gap_zero_matches_pinned\n");
	return 0;
}
```

Register both in `main()` after `test_reflow_fills_weighted_row`.

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C build && meson test -C build test_layout`
Expected: `test_reflow_gap_distribution` FAIL — children are laid out with no gap (positions 0,30,60; last at 90). `test_reflow_gap_zero_matches_pinned` PASS (gap 0 is current behavior).

- [ ] **Step 3: Implement `GuiNode.gap` + `LayoutDef.gap` + reflow**

`src/graph_gui.h` — add the field to the struct (after `padding`):

```c
	uint16_t padding;
	uint16_t gap;
	CustomNavFunc customNav;
```

`src/graph_gui.c` `initGuiNode` — after `gn->scrollable = false;` add:

```c
	gn->gap = 0;
```

`src/gui_style.h` — add `gap` to `LayoutDef`:

```c
typedef struct {
	int orientation;
	int padding;
	int gap;
	int weightCount;
	int weights[MAX_NODE_CHILDREN];
	int childClassCount;
	const char *childClasses[MAX_NODE_CHILDREN];
} LayoutDef;
```

`src/gui_style.c` `applyLayout` — after `container->padding = (uint16_t)ld->padding;` add:

```c
	container->gap = (uint16_t)ld->gap;
```

`src/gui_style.c` — in the layout parse (find the `cJSON` object handling for `layouts`; it sets `ld.padding` via `jsonInt`), add `ld.gap = jsonInt(o, "gap", 0);`.

`src/graph_gui.c` `reflowCoordinates` — replace the content-dimension computation + the loop with a gap-aware version:

```c
	int totalW = n->totalItemWeights > 0 ? n->totalItemWeights : 1;
	int contentDim = 0;
	if(n->nodeAlignment == na_horizontal) {
		contentDim = (int)n->w - 2 * (int)n->padding;
	} else {
		contentDim = (int)n->h - 2 * (int)n->padding;
	}
	if(contentDim < 0) {
		contentDim = 0;
	}
	int gap = (int)n->gap;
	int gaps = gap * (n->itemCount - 1);
	if(gaps > contentDim) {
		gaps = contentDim;
	}
	int availDim = contentDim - gaps;
	int pos = 0;
	int used = 0;

	ListElement *current = n->items->head;
	for(int i = 0; i < n->itemCount; i++) {
		GuiNode *cn = *(GuiNode **)current->data;

		cn->x = n->x + n->padding;
		cn->y = n->y + n->padding;
		cn->w = n->w - n->padding * 2;
		cn->h = n->h - n->padding * 2;

		switch(n->nodeAlignment) {
			case na_vertical: {
				int ch = (i == n->itemCount - 1) ? availDim - used
				           : (*(int *)cn->weightRef->data * availDim) / totalW;
				int minH = (int)cn->padding * 2 + 4;
				if(ch < minH) {
					ch = minH;
				}
				cn->y = n->y + n->padding + pos;
				cn->h = (uint16_t)ch;
				break;
			}
			case na_horizontal: {
				int cw = (i == n->itemCount - 1) ? availDim - used
				           : (*(int *)cn->weightRef->data * availDim) / totalW;
				int minW = (int)cn->padding * 2 + 4;
				if(cw < minW) {
					cw = minW;
				}
				cn->x = n->x + n->padding + pos;
				cn->w = (uint16_t)cw;
				break;
			}
		}

		pos += (int)((n->nodeAlignment == na_horizontal) ? cn->w : cn->h) + gap;
		used += (int)((n->nodeAlignment == na_horizontal) ? cn->w : cn->h);
		reflowCoordinates(cn);
		current = current->next;
	}
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C build && meson test -C build test_layout`
Expected: both new tests PASS; all other layout tests PASS (gap 0 path is identical).

- [ ] **Step 5: Full suite + commit**

Run: `meson test -C build` → 17/17 OK.
Commit: `git add src/graph_gui.h src/graph_gui.c src/gui_style.h src/gui_style.c tests/dsp/test_layout.c && git commit -m "feat(style): layout gap — reflow reserves gap·(n-1), positions children share+gap apart; gap 0 is byte-identical"`

---

### Task 2: Chip palette into the theme

**Files:**
- Modify: `src/theme.h` (ColourScheme), `src/gui_core.c` (`initDefaultColourScheme`), `src/io/config_io.c` (`themeFieldByName` + save-names list)
- Modify: `bin/clr.json`, `src/clr.json` (if it exists — check; otherwise `bin/clr.json` only)
- Test: `tests/dsp/test_cfg.c`

**Interfaces:**
- Consumes: the existing `themeFieldByName` name→field map and `initDefaultColourScheme`.
- Produces: `ColourScheme.chipPalette0..chipPalette7` (Color), resolvable by the theme names `"chipPalette0"`…`"chipPalette7"` in `themeFieldByName` + persisted by the save-names list.

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_cfg.c`:

```c
static int test_chip_palette_theme_roundtrip(void) {
	/* themeFieldByName must resolve the 8 palette names */
	ColourScheme cs;
	initDefaultColourScheme(&cs);
	ASSERT_TRUE(themeFieldByName(&cs, "chipPalette0") != NULL, "chipPalette0 field exists");
	ASSERT_TRUE(themeFieldByName(&cs, "chipPalette7") != NULL, "chipPalette7 field exists");
	ASSERT_TRUE(themeFieldByName(&cs, "chipPalette0")->r == 70, "chipPalette0 default is steel-blue");
	ASSERT_TRUE(themeFieldByName(&cs, "chipPalette7")->r == 180, "chipPalette7 default is brick");
	printf("PASS test_chip_palette_theme_roundtrip\n");
	return 0;
}
```

Register in `main()`. The defaults (70,130,180 / 180,90,90) come from the current `chipPalette[8]` in `src/gui_arranger.c:46`.

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_cfg`
Expected: FAIL — `themeFieldByName` returns NULL for `chipPalette0`.

- [ ] **Step 3: Implement**

`src/theme.h` — add to `ColourScheme`:

```c
	Color chipPalette0;
	Color chipPalette1;
	Color chipPalette2;
	Color chipPalette3;
	Color chipPalette4;
	Color chipPalette5;
	Color chipPalette6;
	Color chipPalette7;
```

`src/gui_core.c` `initDefaultColourScheme` — add (values from `gui_arranger.c` `chipPalette[8]`):

```c
	colourScheme->chipPalette0 = (Color){ 70, 130, 180, 255 }; /* steel-blue */
	colourScheme->chipPalette1 = (Color){ 200, 120, 60, 255 }; /* amber      */
	colourScheme->chipPalette2 = (Color){ 90, 160, 90, 255 };  /* moss       */
	colourScheme->chipPalette3 = (Color){ 170, 80, 130, 255 }; /* mulberry   */
	colourScheme->chipPalette4 = (Color){ 210, 180, 70, 255 }; /* gold       */
	colourScheme->chipPalette5 = (Color){ 110, 90, 170, 255 }; /* violet     */
	colourScheme->chipPalette6 = (Color){ 80, 160, 160, 255 }; /* teal       */
	colourScheme->chipPalette7 = (Color){ 180, 90, 90, 255 };  /* brick      */
```

`src/io/config_io.c` `themeFieldByName` — add after the `routeMul` line:

```c
	if(!strcmp(name, "chipPalette0")) return &cs->chipPalette0;
	if(!strcmp(name, "chipPalette1")) return &cs->chipPalette1;
	if(!strcmp(name, "chipPalette2")) return &cs->chipPalette2;
	if(!strcmp(name, "chipPalette3")) return &cs->chipPalette3;
	if(!strcmp(name, "chipPalette4")) return &cs->chipPalette4;
	if(!strcmp(name, "chipPalette5")) return &cs->chipPalette5;
	if(!strcmp(name, "chipPalette6")) return &cs->chipPalette6;
	if(!strcmp(name, "chipPalette7")) return &cs->chipPalette7;
```

`src/io/config_io.c` save-names list — append the 8 names:

```c
		"routeAdd", "routeMul", "chipPalette0", "chipPalette1", "chipPalette2",
		"chipPalette3", "chipPalette4", "chipPalette5", "chipPalette6", "chipPalette7" };
```

`bin/clr.json` — add the 8 keys (as `#RRGGBBAA` hex of the defaults) to the colors object.

- [ ] **Step 4: Run to verify it passes**

Run: `ninja -C build && meson test -C build test_cfg`
Expected: PASS.

- [ ] **Step 5: Commit**

`git add src/theme.h src/gui_core.c src/io/config_io.c bin/clr.json tests/dsp/test_cfg.c && git commit -m "feat(theme): chip palette colours as theme keys (chipPalette0..7)"`

---

### Task 3: Step-cell style (`StepCellStyle`)

**Files:**
- Modify: `src/gui_style.h` (struct + registry decls), `src/gui_style.c` (defaults + resolver + overlay + compile case + class name), `src/gui_pattern.c` (`drawStepGuiNode`)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: `classifyClass` default-name table, `g_classMap`, `jsonColor`, `overlayLabel`, `styleFont`.
- Produces: `StepCellStyle` struct; `const StepCellStyle *resolveStepCellStyle(const GuiNode *)`; `STYLE_STEP_CELL`; class name `"step-cell"`; `jsonInt`/`jsonColor`-parsed fields `bgEmpty`, `bgPlaying`, `borderSelected`, `note`.

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_layout.c`:

```c
static int test_step_cell_style_resolves_and_draws(void) {
	/* resolveStepCellStyle returns the baked default (empty/playing/selected) */
	GuiNode *n = createGuiNode(0, 0, 30, 30, 0, na_horizontal, "step", 1, 0);
	const StepCellStyle *st = resolveStepCellStyle(n);
	ASSERT_TRUE(st != NULL, "step-cell style resolves");
	ASSERT_TRUE(st->bgEmpty.r == 255 && st->bgPlaying.g == 255, "baked default colours non-zero");
	ASSERT_TRUE(strcmp(st->note.fontName, "text") == 0, "note uses text font");
	freeGuiNode(n);
	printf("PASS test_step_cell_style_resolves_and_draws\n");
	return 0;
}
```

(The `bgEmpty.r == 255` pins `defaultCell`; `bgPlaying.g == 255` pins `highlightedCell` — check `initDefaultColourScheme` for the exact values and use those.)

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_layout`
Expected: FAIL — `resolveStepCellStyle` undefined.

- [ ] **Step 3: Implement**

`src/gui_style.h` — add the struct + resolver decl:

```c
typedef struct {
	Color bgEmpty;
	Color bgPlaying;
	Color borderSelected;
	LabelStyle note;
} StepCellStyle;
```

after the `LabelStyle` typedef. Add `STYLE_STEP_CELL` to the `StyleType` enum (before `STYLE_COUNT`) and `const StepCellStyle *resolveStepCellStyle(const GuiNode *gn);` near the other resolvers.

`src/gui_style.c`:
- class name: `case STYLE_STEP_CELL: return "step-cell";` in `g_defaultClassName`.
- arrays + count: `static StepCellStyle g_stepCellClasses[MAX_CLASS_ENTRIES_PER_TYPE];` + `static int g_stepCellClassCount = 0;`
- baked default:

```c
static StepCellStyle g_defaultStepCell = {
	.bgEmpty = { 0, 0, 0, 0 },
	.bgPlaying = { 0, 0, 0, 0 },
	.borderSelected = { 0, 0, 0, 0 },
	.note = { "text", 10, 4, 0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};
```

- in `resolveDefaultColours` add:

```c
	g_defaultStepCell.bgEmpty = cs->defaultCell;
	g_defaultStepCell.bgPlaying = cs->highlightedCell;
	g_defaultStepCell.borderSelected = cs->outlineColour;
	g_defaultStepCell.note.color = cs->fontColour;
```

- resolver:

```c
const StepCellStyle *resolveStepCellStyle(const GuiNode *gn) {
	if(gn && gn->className) {
		int idx = findClass(gn->className, STYLE_STEP_CELL);
		if(idx >= 0) return &g_stepCellClasses[g_classMap[idx].index];
	}
	return &g_defaultStepCell;
}
```

- overlay fn + compile case (mirror `overlayBtn`/`case STYLE_BTN`, using `jsonColor` for the three colours + `overlayLabel` for `note`).
- the compile `switch(type)` gets a `case STYLE_STEP_CELL:` registering into `g_stepCellClasses` + `g_classMap`.

`src/gui_pattern.c` `drawStepGuiNode` — read the style:

```c
void drawStepGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	StepNodeData *d = (StepNodeData *)gn->p;
	const StepCellStyle *st = resolveStepCellStyle(gn);
	int currentlyPlaying = -1;
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		if(d->seq->pattern_index[i] == d->patternIndex) {
			currentlyPlaying = i;
			break;
		}
	}
	Rectangle cell = (Rectangle){ gn->x, gn->y, gn->w, gn->h };
	if(*d->selectedStepPtr == d->stepIndex) {
		DrawRectangle(gn->x - 3, gn->y - 3, gn->w + 6, gn->h + 6, st->borderSelected);
	}
	if(currentlyPlaying > -1 && d->seq->running[currentlyPlaying] && d->seq->playhead_index[currentlyPlaying] == d->stepIndex) {
		DrawRectangleRec(cell, st->bgPlaying);
	} else {
		DrawRectangleRec(cell, st->bgEmpty);
	}
	int *note = getStep(d->pl, d->patternIndex, d->stepIndex);
	char *noteString = getNoteString(note[0], note[1]);
	Font *nf = styleFont(st->note.fontName);
	DrawTextEx(*nf, noteString, (Vector2){ gn->x + 4, gn->y + 4 }, st->note.fontSize, st->note.spacing, st->note.color);
}
```

(Check `gui_pattern.c` includes `gui_style.h`; add if missing.)

- [ ] **Step 4: Run to verify it passes + gate**

Run: `ninja -C build && meson test -C build` → 17/17 OK.
Run the pattern fixtures if any exist (check `src/tools/instrument_harness/fixtures/`). The existing step-cell draw must render identically (same colours/fonts).

- [ ] **Step 5: Commit**

`git add src/gui_style.h src/gui_style.c src/gui_pattern.c tests/dsp/test_layout.c && git commit -m "feat(style): StepCellStyle — state colours (empty/playing/selected) + note font replace inline cs.* in drawStepGuiNode"`

---

### Task 4: Route-dest style (`DestStyle` + `BorderStyle.colorSelected`)

**Files:**
- Modify: `src/gui_style.h` (BorderStyle + DestStyle + enum + resolver), `src/gui_style.c` (defaults + resolver + overlay + compile + resolveDefaultColours), `src/gui_core.c` (`drawRouteDestGuiNode`)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: `BorderStyle`, `jsonColor`, the compile dispatch.
- Produces: `BorderStyle.colorSelected`; `DestStyle { BorderStyle border; }`; `const DestStyle *resolveDestStyle(const GuiNode *)`; `STYLE_DEST`; class name `"route-dest"`.

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_layout.c`:

```c
static int test_dest_style_resolves_and_colours(void) {
	GuiNode *n = createGuiNode(0, 0, 40, 30, 0, na_horizontal, "dest", 1, 0);
	const DestStyle *st = resolveDestStyle(n);
	ASSERT_TRUE(st != NULL, "route-dest style resolves");
	ASSERT_TRUE(st->border.borderWidth == 2, "dest border width 2");
	ASSERT_TRUE(st->border.color.r > 0, "dest border colour non-zero (routeAdd)");
	ASSERT_TRUE(st->border.colorSelected.g > 0, "dest selected colour non-zero (labelSelected)");
	freeGuiNode(n);
	printf("PASS test_dest_style_resolves_and_colours\n");
	return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_layout`
Expected: FAIL — `resolveDestStyle` undefined.

- [ ] **Step 3: Implement**

`src/gui_style.h`:

```c
typedef struct {
	float roundness;
	float borderWidth;
	Color color;
	Color colorSelected;
} BorderStyle;
```

```c
typedef struct {
	BorderStyle border;
} DestStyle;
```

enum: add `STYLE_DEST` before `STYLE_COUNT`. Add `const DestStyle *resolveDestStyle(const GuiNode *gn);`.

`src/gui_style.c`:
- class name: `case STYLE_DEST: return "route-dest";`
- array + count: `static DestStyle g_destClasses[MAX_CLASS_ENTRIES_PER_TYPE];` + `static int g_destClassCount = 0;`
- baked default:

```c
static DestStyle g_defaultDest = {
	.border = { 0.0f, 2.0f, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};
```

- `resolveDefaultColours` add:

```c
	g_defaultDest.border.color = cs->routeAdd;
	g_defaultDest.border.colorSelected = cs->labelSelected;
```

- resolver `resolveDestStyle` (same shape as the others).
- `overlayBorder` gains `jsonColor(o, "colorSelected", cs, &b->colorSelected);`
- `overlayDest` + compile `case STYLE_DEST:` (mirror `STYLE_BTN`).

`src/gui_core.c` `drawRouteDestGuiNode` — read the style:

```c
void drawRouteDestGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const DestStyle *st = resolveDestStyle(gn);
	Color outline = gn->selected ? st->border.colorSelected : st->border.color;
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h },
	                     st->border.borderWidth, outline);
}
```

- [ ] **Step 4: Run to verify it passes + gate**

Run: `ninja -C build && meson test -C build` → 17/17 OK.
Run `src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/route_lines_follow_selection.txt` → PASS (dest outline colours come from the style now, same values).

- [ ] **Step 5: Commit**

`git add src/gui_style.h src/gui_style.c src/gui_core.c tests/dsp/test_layout.c && git commit -m "feat(style): DestStyle — route-dest outline via style (theme-name colours replace cs.routeAdd/labelSelected)"`

---

### Task 5: Chip style (`ChipStyle`) — the composed-node model

**Files:**
- Modify: `src/gui_style.h`, `src/gui_style.c`, `src/gui_arranger.c` (`drawInstChipGuiNode`, `createInstChipGuiNode`, remove `chipPalette`)
- Modify: `src/gui_internal.h` (remove `chipPalette` extern if present)
- Test: `tests/dsp/test_layout.c`, `tests/dsp/test_graph_nav.c`

**Interfaces:**
- Consumes: Task 2 palette (`cs->chipPalette0..7`), Task 4 `BorderStyle.colorSelected`, the dial's registry/resolver/geometry pattern.
- Produces: `DotsStyle`, `ChipStyle`, `ChipGeometry`, `computeChipGeometry`, `chipComponentHeight`, `resolveChipStyle`, `STYLE_CHIP`, class name `"chip"`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/dsp/test_layout.c`:

```c
static int test_chip_style_resolves_and_geometry(void) {
	GuiNode *n = createGuiNode(0, 0, 120, 30, 0, na_horizontal, "chip", 1, 0);
	const ChipStyle *st = resolveChipStyle(n);
	ASSERT_TRUE(st != NULL, "chip style resolves");
	ASSERT_TRUE(st->border.borderWidth == 2, "chip border width");
	ASSERT_TRUE(st->dots.size == 4 && st->dots.gap == 2, "chip dots defaults");
	ASSERT_TRUE(chipComponentHeight(st) == 24, "chip intrinsic height (label 12 + patch 8 + dots 4)");

	ChipGeometry g;
	computeChipGeometry(n, st, "FM", "chan", &g);
	ASSERT_TRUE(g.voiceCountY == 2, "voice count top at +2");
	ASSERT_TRUE(g.labelY == 9, "label vertically centred");
	ASSERT_TRUE(g.patchNameY == 19, "patch name at bottom (h-11)");
	freeGuiNode(n);
	printf("PASS test_chip_style_resolves_and_geometry\n");
	return 0;
}

static int test_chip_palette_resolved_from_theme(void) {
	/* compile a chip class with a palette + verify it resolves via theme names */
	const char *path = ".tmp_files/layout_test_chip.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"chip\":{\"palette\":[\"chipPalette0\",\"chipPalette3\"]}}}", f);
	fclose(f);
	ColourScheme cs;
	initDefaultColourScheme(&cs);
	compileLayoutConfig(path, &cs);
	GuiNode *n = createGuiNode(0, 0, 120, 30, 0, na_horizontal, "chip", 1, 0);
	guiNodeSetClass(n, "chip");
	const ChipStyle *st = resolveChipStyle(n);
	ASSERT_TRUE(st->palette[0].r == 70, "palette[0] resolved steel-blue");
	ASSERT_TRUE(st->palette[3].r == 170, "palette[3] resolved mulberry");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_chip_palette_resolved_from_theme\n");
	return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_layout`
Expected: FAIL — `ChipStyle`/`computeChipGeometry`/`chipComponentHeight` undefined.

- [ ] **Step 3: Implement the style layer**

`src/gui_style.h`:

```c
typedef struct { int size; int gap; Color colour; Color colourActive; } DotsStyle;

typedef struct {
	BorderStyle border;
	Color palette[8];
	LabelStyle voiceCount;
	LabelStyle typeTag;
	LabelStyle label;
	LabelStyle patchName;
	DotsStyle dots;
} ChipStyle;

typedef struct {
	int voiceCountX, voiceCountY;
	int typeTagX, typeTagY;
	int labelX, labelY;
	int patchNameX, patchNameY;
	int dotsX, dotsY;
} ChipGeometry;
```

enum: add `STYLE_CHIP` before `STYLE_COUNT`. Decls:

```c
const ChipStyle *resolveChipStyle(const GuiNode *gn);
int chipComponentHeight(const ChipStyle *st);
void computeChipGeometry(const GuiNode *gn, const ChipStyle *st,
                         const char *typeTag, const char *label, ChipGeometry *out);
```

`src/gui_style.c`:
- class name: `case STYLE_CHIP: return "chip";`
- array + count: `static ChipStyle g_chipClasses[MAX_CLASS_ENTRIES_PER_TYPE];` + `static int g_chipClassCount = 0;`
- baked default:

```c
static ChipStyle g_defaultChip = {
	.border = { 0.0f, 2.0f, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	.palette = { {0},{0},{0},{0},{0},{0},{0},{0} },
	.voiceCount = { "pixel", 9, 1, 4, 2, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	.typeTag    = { "pixel", 9, 1, -4, 2, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	.label      = { "pixel", 12, 1, 0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	.patchName  = { "pixel", 8, 1, 4, -11, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	.dots = { 4, 2, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};
```

- `resolveDefaultColours` add:

```c
	g_defaultChip.border.color = cs->wrapperBorder;
	g_defaultChip.border.colorSelected = cs->outlineColour;
	for(int i = 0; i < 8; i++) {
		g_defaultChip.palette[i] = (&cs->chipPalette0)[i];
	}
	g_defaultChip.voiceCount.color = cs->label;
	g_defaultChip.typeTag.color = cs->label;
	g_defaultChip.label.color = cs->label;
	g_defaultChip.patchName.color = cs->label;
	g_defaultChip.dots.colour = cs->wrapperBorder;
	g_defaultChip.dots.colourActive = cs->outlineColour;
```

- resolver `resolveChipStyle` (same shape as the others).
- `overlayDots` (size/gap/colour/colourActive), `overlayChip` (border, palette array of 8 strings → `themeFieldByName`, voiceCount/typeTag/label/patchName via `overlayLabel`, dots) + compile `case STYLE_CHIP:`.

The palette parse in `overlayChip`:

```c
	cJSON *pal = cJSON_GetObjectItemCaseSensitive(o, "palette");
	if(cJSON_IsArray(pal)) {
		int n = cJSON_GetArraySize(pal);
		if(n > 8) n = 8;
		for(int i = 0; i < n; i++) {
			cJSON *e = cJSON_GetArrayItem(pal, i);
			if(cJSON_IsString(e)) {
				Color *c = themeFieldByName((ColourScheme *)cs, e->valuestring);
				if(c) {
					ch->palette[i] = *c;
				}
			}
		}
	}
```

- `chipComponentHeight`:

```c
int chipComponentHeight(const ChipStyle *st) {
	/* The chip's natural height = the centred label band + the
	 * bottom strip (patch name + active dots), which must not overlap. */
	return st->label.fontSize + st->patchName.fontSize + st->dots.size;
}
```

The baked defaults give `12 + 8 + 4 = 24`. The test must assert 24 (not 30).

- `computeChipGeometry` (positions from the style; typeTag right-aligned, label centred, dots bottom-right):

```c
void computeChipGeometry(const GuiNode *gn, const ChipStyle *st,
                         const char *typeTag, const char *label, ChipGeometry *out) {
	Font *pf = styleFont("pixel");
	out->voiceCountX = gn->x + st->voiceCount.offsetX;
	out->voiceCountY = gn->y + st->voiceCount.offsetY;
	int tagW = MeasureText(typeTag, st->typeTag.fontSize);
	out->typeTagX = gn->x + gn->w - tagW + st->typeTag.offsetX;
	out->typeTagY = gn->y + st->typeTag.offsetY;
	int lblW = MeasureText(label, st->label.fontSize);
	out->labelX = gn->x + (gn->w - lblW) / 2 + st->label.offsetX;
	out->labelY = gn->y + (gn->h - st->label.fontSize) / 2 + st->label.offsetY;
	out->patchNameX = gn->x + st->patchName.offsetX;
	out->patchNameY = gn->y + gn->h + st->patchName.offsetY;   /* offsetY -11 -> h-11 */
	out->dotsX = 0;  /* computed in the draw (depends on voice count) */
	out->dotsY = gn->y + gn->h - st->dots.size - 3;
}
```

- [ ] **Step 4: Migrate `drawInstChipGuiNode`**

Rewrite `src/gui_arranger.c` `drawInstChipGuiNode` to resolve the style + compute geometry. Key substitutions:
- `Color bg = chipPalette[paletteIdx];` → `Color bg = st->palette[paletteIdx];`
- border: `DrawRectangleLinesEx(rect, 2.0, gn->selected ? cs.outlineColour : cs.wrapperBorder);` → `st->border.borderWidth` + `gn->selected ? st->border.colorSelected : st->border.color`
- the four `DrawTextEx(pixelFont, ...)` calls use `styleFont(st-><region>.fontName)` + `st-><region>.fontSize/spacing/color` at the geometry positions.
- dots: `st->dots.size`/`st->dots.gap`/`st->dots.colour`/`st->dots.colourActive`.
- swatches (expanded strip): `chipPalette[i]` → `st->palette[i]`.

Remove the `chipPalette` global from `gui_arranger.c:46` + the extern from `gui_internal.h`.

Ensure `src/gui_arranger.c` includes `gui_style.h` + `graph_gui.h`.

- [ ] **Step 5: Run to verify it passes + gate**

Run: `ninja -C build && meson test -C build` → 17/17 OK.
Run `src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/chip_meta.txt` (restore `bin/s1.sng` from HEAD first) → PASS.

- [ ] **Step 6: Commit**

`git add src/gui_style.h src/gui_style.c src/gui_arranger.c src/gui_internal.h tests/dsp/test_layout.c && git commit -m "feat(style): ChipStyle — composed chip (border/palette/4 label regions/dots) with computeChipGeometry + intrinsic height; chipPalette global removed"`

---

### Task 6: `mod-source-row` layout + `gap` wiring

**Files:**
- Modify: `src/layout.json`, `bin/layout.json`
- Modify: `src/gui_inst_mod.c` (`appendModSourceEntry`)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: Task 1 `gap`, `applyLayout`, `LayoutDef`.
- Produces: `"mod-source-row"` layout in layout.json (applied by `appendModSourceEntry`).

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_layout.c` (a compile of the shipped layout + a probe row):

```c
static int test_mod_source_row_layout_gap(void) {
	/* compile a temp layout with the mod-source-row definition, apply it,
	 * assert gap + orientation land on the container */
	const char *path = ".tmp_files/layout_test_msr.json";
	FILE *f = fopen(path, "w");
	fputs("{\"layouts\":{\"mod-source-row\":{\"orientation\":\"horizontal\","
	      "\"padding\":1,\"gap\":2,\"weights\":[3,4,4,4,4,3,2,1]}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile msr layout");
	GuiNode *row = createGuiNode(0, 0, 566, 40, 1, na_vertical, "row", 0, 0);
	GuiNode *c[3];
	for(int i = 0; i < 3; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 1);
	}
	applyLayout(row, "mod-source-row");
	ASSERT_TRUE(row->gap == 2, "layout gap applied to container");
	ASSERT_TRUE(row->nodeAlignment == na_horizontal, "orientation applied");
	freeGuiNode(row);
	remove(path);
	printf("PASS test_mod_source_row_layout_gap\n");
	return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_layout`
Expected: FAIL — layout name not found (not yet in the temp file) / gap not applied.

- [ ] **Step 3: Implement**

`src/layout.json` + `bin/layout.json` — add to `layouts`:

```json
"mod-source-row": { "orientation": "horizontal", "padding": 1, "gap": 2,
                    "weights": [3, 4, 4, 4, 4, 3, 2, 1] }
```

`src/gui_inst_mod.c` `appendModSourceEntry` — after the row's children are appended and before `appendItem(container, wrap, weight);` add:

```c
	applyLayout(wrap, "mod-source-row");
```

(Include `gui_style.h` in `gui_inst_mod.c` if missing.) Note: the existing `appendItem(wrap, ..., <weight>)` calls pass the same relative weights (3/4/3/2/1) that the layout now sets — they are overridden by `applyLayout`; the trailing blank (weight 1) is covered by the layout's last weight slot.

- [ ] **Step 4: Run to verify it passes + gate**

Run: `ninja -C build && meson test -C build` → 17/17 OK.
Run `src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/mod_sources.txt` + `pagenav.txt` (restore `bin/s1.sng` + `fm1.ipb` from HEAD first). If a fixture's geometric nav shifts (the row layout re-spaces children), adjust the fixture's arrow counts and re-run.

- [ ] **Step 5: Commit**

`git add src/layout.json bin/layout.json src/gui_inst_mod.c tests/dsp/test_layout.c && git commit -m "feat(layout): mod-source-row named layout with gap 2 — appendModSourceEntry applies it"`

---

### Task 7: Full gate + captures

**Files:**
- Test: all suites + fixtures
- Capture: `src/tools/instrument_harness` SHOT

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces: a green full gate + visual confirmation.

- [ ] **Step 1: Full test suite**

Run: `meson test -C build` → 17/17 OK.

- [ ] **Step 2: All fixtures**

Restore `bin/s1.sng` + `bin/data/instrument_presets/fm1.ipb` from HEAD, then run every fixture:

```bash
for f in pagenav add_route_delete clear_routes mod_sources route_lines_follow_selection preset_save_load save_overwrite chip_meta; do
  src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/$f.txt
done
```

All must PASS. Restore `bin/s1.sng` + `fm1.ipb` again after the run.

- [ ] **Step 3: Visual capture**

Boot the instrument screen + arranger via a temp fixture (SHOT verb), confirm the chip palette, mod rows and step cells render (restore the committed song/preset after). Verify the chip's regions sit where the baked geometry places them (voice count top-left, type tag top-right, label centred, patch name bottom-left, dots bottom-right).

- [ ] **Step 4: Commit remaining binaries**

`git add bin/spectrax bin/instrument_harness && git commit -m "build: rebuild binaries after grounded component wiring"`

---

## Self-review notes

- **Spec coverage:** chip (T5), mod rows (T6), step cells (T3), route-dest (T4), gap (T1), state colours (T3/T4/T5), palette-in-theme (T2), registry/resolvers (T3/T4/T5). All four spec sections + all four mechanism additions mapped.
- **Ordering rationale:** gap + palette first (infrastructure), then the two small components (prove the registry/state-colour pattern), then the chip (largest), then the layout consumer, then the gate. Each task is independently reviewable + revertible.
- **Type consistency:** `resolve<Type>Style`, `<Type>Style` struct names, `STYLE_<TYPE>` enum values, `computeChipGeometry` signature, `chipComponentHeight`, `dialComponentHeight` (existing) — all match across tasks.