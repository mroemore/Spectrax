# Instrument chips + meta row Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Per-channel instrument chips on the arranger (label colour + 8-char label + type/voice/patch/active info, EDIT+arrows shortcut + EDIT+UP label editing) and a meta row on the instrument page (type selector + voice count).

**Architecture:** The chip row lives in a new vertical column above the arranger grid (chips aligned to grid columns). Chips are new graph nodes with their own draw + a per-channel data model (`labelColourIdx` + `label[9]` on the Arranger) persisted in a new `LABL` sng chunk. The instrument page gets a meta row (type selector + voice count) that drives instrument re-init + voice-pool resize, all wrapped in the `rebuilding` audio-thread guard.

**Tech Stack:** C99, raylib, meson/ninja, existing test harness (`tests/dsp/`) + instrument_harness scripted fixtures.

## Global Constraints

- `MAX_SEQUENCER_CHANNELS` channels; `MAX_VOICES_PER_CHANNEL = 8` (settings.h).
- Voice types cycle SAMPLE → FM → BLEP only (GRAIN/SPECTRAL exist in the enum but are not built out).
- All instrument/mods mutations that the audio thread reads must run with `inst->rebuilding = true` (established preset-apply pattern).
- Label chars: A-Z, 0-9, space, `_`, `-`, `.` (arcade set); label capped at 8 + NUL.
- Type icon = text tag (`FM`/`SMP`/`BLP`) — NOT the corrupt synthicon_sheet sprite.
- Do not touch `src/vizfx.c` beyond what this feature requires.
- Test gate: `ninja -C build` clean, `meson test -C build` 8/8, both scripted fixtures (`add_route_delete`, `preset_save_load`) PASS, app boots (`PRESETS LOADED`).

---

### Task 1: Arranger label data model + sng persistence (LABL chunk)

**Files:**
- Modify: `src/sequencer.h` (Arranger struct)
- Modify: `src/io/sequencer_io.c` (save + load)
- Test: `tests/dsp/test_io.c`

**Interfaces:**
- Produces: `Arranger` gains `int labelColourIdx[MAX_SEQUENCER_CHANNELS];` and `char label[MAX_SEQUENCER_CHANNELS][9];`. New chunk magic `LABL` (define `CHIP_LABELS_SECTION "LABL"` in `src/io.h`).
- Consumes: `writeChunkHeader`, `readAndVerifyChunkHeader` (io.h, already exported).

- [ ] **Step 1: Write the failing tests**

In `tests/dsp/test_io.c` add:
```c
static int test_arranger_labels_roundtrip(void) {
	Arranger a1 = { 0 }, a2 = { 0 };
	createArranger(&a1, NULL, NULL);
	createArranger(&a2, NULL, NULL);
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		a1.labelColourIdx[i] = i % 8;
		snprintf(a1.label[i], 9, "CH%02d", i);
	}
	ensure_tmp_dirs();
	const char *f = ".tmp_files/arr_labels.sng";
	saveSequencerState(f, &a1, NULL);
	loadSequencerState(f, &a2, NULL);
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		check(a1.labelColourIdx[i] == a2.labelColourIdx[i], "label colour %d", i);
		check(strcmp(a1.label[i], a2.label[i]) == 0, "label text %d", i);
	}
	return 0;
}

static int test_arranger_labels_old_file_defaults(void) {
	/* An SEQ1/old file without the LABL chunk must load with defaults:
	 * colour 0, empty labels. Build a minimal file with only PATT+ARRG. */
	Arranger a = { 0 };
	createArranger(&a, NULL, NULL);
	ensure_tmp_dirs();
	const char *f = ".tmp_files/arr_labels_old.sng";
	FILE *fp = fopen(f, "wb");
	check(fp != NULL, "open");
	writeChunkHeader(fp, SEQ_MAGIC_HEADER);   /* SEQ1 */
	writeChunkHeader(fp, PATTERN_SECTION);
	int zero = 0;
	fwrite(&zero, sizeof(int), 1, fp);
	writeChunkHeader(fp, ARRANGER_SECTION);
	fwrite(a.playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, fp);
	int en = a.enabledChannels;
	fwrite(&en, sizeof(int), 1, fp);
	fwrite(&a.selected_x, sizeof(int), 1, fp);
	fwrite(&a.selected_y, sizeof(int), 1, fp);
	fwrite(&a.tempoSettings.loop, sizeof(int), 1, fp);
	int bpm = 120;
	fwrite(&bpm, sizeof(int), 1, fp);
	fwrite(&a.playing, sizeof(int), 1, fp);
	fwrite(a.song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, fp);
	fclose(fp);

	Arranger a2 = { 0 };
	createArranger(&a2, NULL, NULL);
	loadSequencerState(f, &a2, NULL);
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		check(a2.labelColourIdx[i] == 0, "default colour %d", i);
		check(a2.label[i][0] == '\0', "default label %d", i);
	}
	return 0;
}
```
Register both in `main()` of test_io.c. Ensure `saveSequencerState`/`loadSequencerState` tolerate a NULL PatternList (they already only touch `patterns` if non-NULL — verify; if not, guard the pattern section when `patterns == NULL`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: `ninja -C build && meson test -C build`
Expected: `test_io` FAILs (the new fields are read as garbage / defaults don't hold).

- [ ] **Step 3: Implement the data model + chunk**

`src/sequencer.h` Arranger — add the two fields (after `channelSlots`).
`src/io.h` — add `#define CHIP_LABELS_SECTION "LABL"`.

`src/io/sequencer_io.c`:
- In `saveSequencerState`, after the `channelSlots` write (line ~45):
```c
	if(!writeChunkHeader(file, CHIP_LABELS_SECTION)) { fclose(file); return SEQ_ERROR_WRITE; }
	fwrite(arranger->labelColourIdx, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
	fwrite(arranger->label, sizeof(char), MAX_SEQUENCER_CHANNELS * 9, file);
```
- In `loadSequencerState`, after the `channelSlots` read (line ~151): read the LABL chunk if present, else default:
```c
	/* Optional LABL chunk (V2.1). Absent -> defaults. */
	long pos = ftell(file);
	char labl_magic[4];
	if(fread(labl_magic, 1, 4, file) == 4 && memcmp(labl_magic, CHIP_LABELS_SECTION, 4) == 0) {
		int sz;
		fread(&sz, sizeof(int), 1, file);
		fread(arranger->labelColourIdx, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
		fread(arranger->label, sizeof(char), MAX_SEQUENCER_CHANNELS * 9, file);
	} else {
		for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) { arranger->labelColourIdx[i] = 0; arranger->label[i][0] = '\0'; }
		fseek(file, pos, SEEK_SET);
	}
```
Clamp each `labelColourIdx` to `[0, 7]` on load (defensive).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `ninja -C build && meson test -C build`
Expected: `test_io` PASSes (both new tests + existing 14).

- [ ] **Step 5: Commit**

```bash
git add src/sequencer.h src/io.h src/io/sequencer_io.c tests/dsp/test_io.c
git commit -m "feat(seq): per-channel label colour + 8-char label, LABL sng chunk"
```

---

### Task 2: Instrument type swap + voice-count resize (voice.c)

**Files:**
- Modify: `src/voice.c`
- Modify: `src/voice.h`
- Test: `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Produces: `bool setInstrumentVoiceType(VoiceManager *vm, int channel, VoiceType vt);` — swaps the channel instrument to a fresh `vt` (defaults, no loaded preset), rebuilds its voices, wrapped in `inst->rebuilding`. `bool setChannelVoiceCount(VoiceManager *vm, int channel, int count);` — resizes the voice pool (clamped 1..MAX_VOICES_PER_CHANNEL), wrapped in `inst->rebuilding`.
- Consumes: `init_instrument(&inst, vt, sp, pb)`, `initVoicePool(vm, ch, count, inst)`, `rebuildVoicesForInstrument`, `freeVoiceManager`-style teardown of the old instrument (use the existing `freeVoice` per voice + `cleanupModSystem(inst->modList)` + `free(inst->paramList)` + `free(inst)` — match the freeVoiceManager path in voice.c).

- [ ] **Step 1: Write the failing tests**

In `tests/dsp/test_mod_voice.c` add (using the established VM setup pattern — see `test_preset_load_rebuilds_voices`):
```c
static int test_set_instrument_voice_type(void) {
	SamplePool *sp = createSamplePool();
	WavetablePool *wtp = createWavetablePool();
	PresetBank pb;
	initPresetBank(&pb);
	Settings s = { .enabledChannels = 1, .defaultVoiceCount = 2, .defaultBPM = 120 };
	VoiceManager *vm = createVoiceManager(&s, sp, wtp, &pb);
	ASSERT_TRUE(vm != NULL, "createVoiceManager");
	ASSERT_TRUE(setInstrumentVoiceType(vm, 0, VOICE_TYPE_FM), "switch to FM");
	ASSERT_EQ(vm->instruments[0]->voiceType, VOICE_TYPE_FM, "type applied");
	ASSERT_TRUE(setInstrumentVoiceType(vm, 0, VOICE_TYPE_BLEP), "switch to BLEP");
	ASSERT_EQ(vm->instruments[0]->voiceType, VOICE_TYPE_BLEP, "type applied 2");
	ASSERT_FALSE(setInstrumentVoiceType(vm, 0, VOICE_TYPE_GRAIN), "GRAIN rejected");
	ASSERT_TRUE(vm->voicePools[0][0] != NULL, "voices rebuilt onto new instrument");
	freeVoiceManager(vm);
	freeWavetablePool(wtp);
	freeSamplePool(sp);
	printf("PASS test_set_instrument_voice_type\n");
	return 0;
}

static int test_set_channel_voice_count(void) {
	SamplePool *sp = createSamplePool();
	WavetablePool *wtp = createWavetablePool();
	PresetBank pb;
	initPresetBank(&pb);
	Settings s = { .enabledChannels = 1, .defaultVoiceCount = 2, .defaultBPM = 120 };
	VoiceManager *vm = createVoiceManager(&s, sp, wtp, &pb);
	ASSERT_TRUE(setChannelVoiceCount(vm, 0, 4), "resize to 4");
	ASSERT_EQ(vm->voiceCount[0], 4, "count applied");
	ASSERT_FALSE(setChannelVoiceCount(vm, 0, 99), "over-8 rejected");
	ASSERT_EQ(vm->voiceCount[0], 4, "unchanged on reject");
	ASSERT_FALSE(setChannelVoiceCount(vm, 0, 0), "zero rejected");
	freeVoiceManager(vm);
	freeWavetablePool(wtp);
	freeSamplePool(sp);
	printf("PASS test_set_channel_voice_count\n");
	return 0;
}
```
Register both in `main()`. (Check the harness's assert macros — `ASSERT_TRUE`/`ASSERT_EQ`/`ASSERT_FALSE` are what the file uses; match them.)

- [ ] **Step 2: Run to verify they fail**

Run: `ninja -C build && meson test -C build`
Expected: `test_mod_voice` FAILs (functions not defined).

- [ ] **Step 3: Implement**

In `src/voice.c`:
```c
static void freeInstrument(Instrument *inst) {
	if(!inst) return;
	cleanupModSystem(inst->modList);          /* frees the Mod entries */
	freeParamList(inst->paramList);
	free(inst);
}

bool setInstrumentVoiceType(VoiceManager *vm, int channel, VoiceType vt) {
	if(!vm || channel < 0 || channel >= vm->enabledChannels) return false;
	if(vt != VOICE_TYPE_SAMPLE && vt != VOICE_TYPE_FM && vt != VOICE_TYPE_BLEP) return false;
	Instrument *old = vm->instruments[channel];
	if(old) old->rebuilding = true;
	Instrument *fresh = NULL;
	/* The Instrument carries its own presetBank + samplePool refs. */
	init_instrument(&fresh, vt, old ? old->samplePool : vm->samplePool,
	                old ? old->presetBank : NULL);
	if(!fresh) { if(old) old->rebuilding = false; return false; }
	vm->instruments[channel] = fresh;
	freeInstrument(old);
	rebuildVoicesForInstrument(vm, fresh);
	fresh->rebuilding = false;
	return true;
}

bool setChannelVoiceCount(VoiceManager *vm, int channel, int count) {
	if(!vm || channel < 0 || channel >= vm->enabledChannels) return false;
	if(count < 1 || count > MAX_VOICES_PER_CHANNEL) return false;
	Instrument *inst = vm->instruments[channel];
	if(inst) inst->rebuilding = true;
	initVoicePool(vm, channel, count, inst);
	if(inst) inst->rebuilding = false;
	return true;
}
```
Declare both in `src/voice.h`. Facts verified: `Instrument` has `presetBank` (voice.h:191) + `samplePool` (voice.h:234); `VoiceManager` has `samplePool` (voice.h:283) + `enabledChannels` + `voiceCount`; `freeParamList` is in modsystem.h; `cleanupModSystem` frees the Mod entries (its per-type switch) — do NOT call freeVoice here (voices are rebuilt by `rebuildVoicesForInstrument`). `freeVoiceManager` does not free instruments today (pre-existing), so this helper is the first proper instrument teardown.

- [ ] **Step 4: Run to verify they pass**

Run: `ninja -C build && meson test -C build`
Expected: `test_mod_voice` PASSes.

- [ ] **Step 5: Commit**

```bash
git add src/voice.c src/voice.h tests/dsp/test_mod_voice.c
git commit -m "feat(voice): setInstrumentVoiceType + setChannelVoiceCount (rebuilding-guarded)"
```

---

### Task 3: The chip node (draw + graph placement + nav)

**Files:**
- Modify: `src/gui.h`
- Modify: `src/gui.c`
- Test: `tests/dsp/test_graph_nav.c` (geometry/nav sanity only — chips are simple selectables)

**Interfaces:**
- Produces: `GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, VoiceManager *vm, int channel, Arranger *arranger);` — a selectable node drawing the chip. `bool isInstChipNode(const GuiNode *n);`. The graph adds a `chipRow` (horizontal, one chip per enabled channel, equal weights) inside a new `gridColumn` vertical container above the grid.
- Consumes: `cs.*` theme colours, `vm->voiceCount[channel]`, `vm->instruments[channel]->voiceType`, `inst->loaded.name`, `arranger->labelColourIdx[channel]`, `arranger->label[channel]`, and the voice-active state (`vm->voicePools[channel][i]->active`).

- [ ] **Step 1: Implement the chip draw + creator (with a nav sanity test)**

`src/gui.h`:
```c
typedef struct {
	GuiNode base;
	VoiceManager *vm;
	int channel;
	Arranger *arranger;
	bool expanded;
	int swatchFocus;
} InstChipGuiNode;
GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, VoiceManager *vm, int channel, Arranger *arranger);
bool isInstChipNode(const GuiNode *n);
```
`src/gui.c` — `drawInstChipGuiNode`:
```c
static const Color kLabelPalette[8] = {
	{ 200, 60, 60, 255 }, { 60, 180, 70, 255 }, { 70, 120, 220, 255 },
	{ 60, 190, 200, 255 }, { 200, 90, 190, 255 }, { 210, 190, 60, 255 },
	{ 230, 230, 230, 255 }, { 130, 130, 130, 255 },
};
static void drawInstChipGuiNode(void *self) {
	InstChipGuiNode *chip = (InstChipGuiNode *)self;
	GuiNode *gn = (GuiNode *)chip;
	Arranger *a = chip->arranger;
	Instrument *inst = chip->vm->instruments[chip->channel];
	Color bg = kLabelPalette[a->labelColourIdx[chip->channel] % 8];
	DrawRectangle(gn->x, gn->y, gn->w, gn->h, bg);
	/* top-left V:n, top-right type tag */
	DrawText(TextFormat("V:%d", chip->vm->voiceCount[chip->channel]), gn->x + 2, gn->y + 2, 9, BLACK);
	const char *tag = inst->voiceType == VOICE_TYPE_FM ? "FM" : (inst->voiceType == VOICE_TYPE_SAMPLE ? "SMP" : "BLP");
	int tw = MeasureText(tag, 9);
	DrawText(tag, gn->x + gn->w - tw - 2, gn->y + 2, 9, BLACK);
	/* centre: the 8-char label */
	DrawText(a->label[chip->channel], gn->x + 2, gn->y + gn->h / 2 - 6, 12, BLACK);
	/* bottom: patch name (left) + voice-active light (right) */
	DrawText(inst->loaded.name[0] ? inst->loaded.name : "----", gn->x + 2, gn->y + gn->h - 16, 8, BLACK);
	bool anyActive = false;
	for(int i = 0; i < chip->vm->voiceCount[chip->channel]; i++)
		if(chip->vm->voicePools[chip->channel][i]->active) { anyActive = true; break; }
	DrawCircle(gn->x + gn->w - 5, gn->y + gn->h - 5, 3, anyActive ? GREEN : (Color){ 40, 40, 40, 255 });
	if(gn->selected) DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0f, WHITE);
}
```
Creator:
```c
GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, VoiceManager *vm, int channel, Arranger *arranger) {
	InstChipGuiNode *chip = malloc(sizeof(InstChipGuiNode));
	GuiNode *gn = (GuiNode *)chip;
	if(!initGuiNode(gn, x, y, w, h, 2, na_horizontal, "chip", 1, selected)) return NULL;
	chip->vm = vm; chip->channel = channel; chip->arranger = arranger;
	chip->expanded = false; chip->swatchFocus = 0;
	gn->drawable = true; gn->draw = drawInstChipGuiNode;
	return gn;
}
bool isInstChipNode(const GuiNode *n) { return n && n->draw == drawInstChipGuiNode; }
```

**Graph placement** in `createArrangerGraph` (gui.c:261): replace the `arrWrap` layout so the grid sits under a chip row:
```c
	/* gridColumn: chipRow (1) above the grid (8) */
	GuiNode *gridColumn = createGuiNode(0, 0, 100, 100, 0, na_vertical, "gridcol", 0, 0);
	GuiNode *chipRow = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "chiprow", 0, 0);
	chipRow->draw = drawWrapperNode; chipRow->drawable = true;
	for(int ch = 0; ch < a->enabledChannels; ch++) {
		appendItem(chipRow, createInstChipGuiNode(0, 0, 100, 100, ch == a->selected_x, a->vm, ch, a), 1);
	}
	appendItem(gridColumn, chipRow, 1);
	appendItem(gridColumn, gn, 8);          /* gn = the ArrangerGuiNode grid */
	appendItem(arrWrap, gridColumn, 4);     /* was: appendItem(arrWrap, gn, 4) */
```
`a->enabledChannels` (was `arranger->enabledChannels`) — the function's local `Arranger *a`.

Nav sanity test in `tests/dsp/test_graph_nav.c`:
```c
static int test_chip_row_nav(void) {
	/* build a tiny graph: root vertical -> [chipRow(horizontal, 2 chips), grid] */
	Graph *g = createGraph(na_vertical);
	GuiNode *row = createGuiNode(0, 0, 200, 20, 0, na_horizontal, "chips", 0, 0);
	GuiNode *c0 = createInstChipGuiNode(0, 0, 100, 20, true, NULL, 0, NULL);
	GuiNode *c1 = createInstChipGuiNode(0, 0, 100, 20, false, NULL, 1, NULL);
	appendItem(row, c0, 1); appendItem(row, c1, 1);
	appendItem(g->root, row, 1);
	GuiNode *grid = createGuiNode(0, 0, 200, 100, 0, na_vertical, "grid", 1, 0);
	appendItem(g->root, grid, 4);
	g->selected = c0;
	navigateGraph(g, KM_RIGHT);
	check(g->selected == c1, "chip right -> c1");
	navigateGraph(g, KM_DOWN);
	check(g->selected == grid, "chip down -> grid");
	freeList(g->root->items); free(g->root); free(row); free(c0); free(c1); free(grid); free(g);
	return 0;
}
```
(createInstChipGuiNode with NULL vm/arranger only draws `V:0` + empty — the draw guards `chip->vm`/`inst` NULL before deref. Adjust the draw to early-return on `!chip->vm || !inst`.)

- [ ] **Step 2: Run to verify**

Run: `ninja -C build && meson test -C build`
Expected: build clean, `test_graph_nav` PASSes (10 + 1 new). App still boots (`PRESETS LOADED`), no crash on the arranger.

- [ ] **Step 3: Commit**

```bash
git add src/gui.h src/gui.c tests/dsp/test_graph_nav.c
git commit -m "feat(gui): arranger chip row (type/voice/label/patch/active + colour bg)"
```

---

### Task 4: Chip input routing (EDIT+arrows) in main.c

**Files:**
- Modify: `src/main.c`
- Modify: `src/gui.h` / `src/gui.c` (small helper: current selected chip channel)

**Interfaces:**
- Produces: `int getSelectedChipChannel(void);` in gui.c — returns the channel of the currently-selected chip in `agui`, or -1. `void expandChip(int channel, bool expanded);` — sets the chip's `expanded`.
- Consumes: `appState->currentScene`, `selectedArrangerCell`, `setSelectedPattern` (unchanged), the `instrumentScreenGraphs` selection.

- [ ] **Step 1: Implement the helpers + input routing**

`src/gui.c`:
```c
int getSelectedChipChannel(void) {
	if(!agui || !agui->selected || !isInstChipNode(agui->selected)) return -1;
	InstChipGuiNode *chip = (InstChipGuiNode *)agui->selected;
	return chip->channel;
}
void expandChip(int channel, bool expanded) {
	if(!agui) return;
	/* walk agui->root for the chip with this channel; set expanded */
	for(ListElement *l = agui->root->items ? agui->root->items->head : NULL; l; l = l->next) {
		GuiNode **pn = (GuiNode **)l->data;
		GuiNode *gridCol = *pn;   /* find chipRow recursively */
		/* simple depth-1 walk of chipRow items */
		if(gridCol->itemCount) {
			for(ListElement *r = gridCol->items->head; r; r = r->next) {
				GuiNode **rn = (GuiNode **)r->data;
				if(isInstChipNode(*rn) && ((InstChipGuiNode *)*rn)->channel == channel)
					((InstChipGuiNode *)*rn)->expanded = expanded;
			}
		}
	}
}
```
Declare both in gui.h.

`src/main.c` SCENE_ARRANGER — in the `isKeyHeld(KM_EDIT)` branch (main.c:308-317), before the `arrangerGraphControlInput` calls, handle the chip:
```c
} else if(isKeyHeld(appState->inputState, KM_EDIT)) {
	int chipChannel = getSelectedChipChannel();
	if(chipChannel >= 0) {
		if(isKeyJustPressed(appState->inputState, KM_LEFT) || isKeyJustPressed(appState->inputState, KM_RIGHT)) {
			/* jump to the instrument page for this channel */
			appState->selectedArrangerCell[0] = chipChannel;
			appState->selectedArrangerCell[1] = arranger->selected_y;
			appState->currentScene = SCENE_INSTRUMENT;
		}
		if(isKeyJustPressed(appState->inputState, KM_UP)) {
			expandChip(chipChannel, true);
		}
		break;  /* skip the dial callback dispatch */
	}
	if(isKeyJustPressed(appState->inputState, KM_LEFT)) { arrangerGraphControlInput(KM_LEFT); }
	...
}
```
`arranger` = `data.arranger` (the local). Verify the SCENE_INSTRUMENT scene entry selects the right graph (the `selectedArrangerCell` already drives `instrumentScreenGraphs` selection — check the DrawGUI/input side uses it; if the inst screen selection needs a nudge, call the existing selection helper).

- [ ] **Step 2: Verify**

Run: build + `meson test` 8/8 + boot. Scripted fixture: add a `chip_jump` fixture later (Task 7); for now a manual/boot check that the scene-switch code path compiles + the arranger still navigates.

- [ ] **Step 3: Commit**

```bash
git add src/main.c src/gui.c src/gui.h
git commit -m "feat(input): chip EDIT+arrows jump to instrument page; EDIT+UP expands chip"
```

---

### Task 5: Expanded chip — colour swatches + 8-char label input

**Files:**
- Modify: `src/gui.c` (chip draw expansion + input handling)
- Modify: `src/main.c` (edit-mode dispatch for the chip label)

**Interfaces:**
- Consumes: `expandChip(channel, bool)`, `InstChipGuiNode.expanded`/`swatchFocus`.
- Produces: when a chip is `expanded`, its draw renders (top row) the 8 swatches + (below) the 8-char label input; `main.c` routes KM_EDIT+arrow + KM_SELECT input to the expanded chip: swatch LEFT/RIGHT moves `swatchFocus` + sets `arranger->labelColourIdx[channel] = swatchFocus`; label-edit arrows edit the label text (arcade char set, cursor, bounded at 8); KM_SELECT collapses (sets expanded=false).

- [ ] **Step 1: Implement the expanded draw + a label-edit helper**

In `drawInstChipGuiNode`, when `chip->expanded`, draw above the chip's normal rect (the chip node's own rect grows upward is NOT needed — draw the expanded controls in the space the graph reflow gives the row; simplest: draw the swatches + input over the row area):
```c
	if(chip->expanded) {
		/* swatch row along the top */
		for(int s = 0; s < 8; s++) {
			Rectangle r = { gn->x + s * (gn->w / 8), gn->y - 18, gn->w / 8, 14 };
			DrawRectangle(r.x, r.y, r.width, r.height, kLabelPalette[s]);
			if(s == chip->swatchFocus) DrawRectangleLinesEx(r, 2.0f, WHITE);
		}
		DrawText("label:", gn->x, gn->y - 32, 8, WHITE);
	}
```
(The exact geometry is implementation-detail — the row above the chip row has room because the graph reflow gives the chipRow a modest height; keep the swatches within the row's bounds.)

Label-edit helper in gui.c:
```c
static void editChipLabelChar(InstChipGuiNode *chip, char ch) {
	Arranger *a = chip->arranger;
	char *label = a->label[chip->channel];
	int len = (int)strlen(label);
	if(ch == '\b') { if(len > 0) label[len - 1] = '\0'; return; }
	if(len >= 8) return;
	label[len] = ch; label[len + 1] = '\0';
}
```
Arcade char-set cycling: reuse the preset-name node's char table (find `kArcadeChars` or equivalent in gui.c; if it's inline, add a shared static `static const char kLabelChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.";`).

- [ ] **Step 2: Wire the input in main.c**

In the SCENE_ARRANGER KM_EDIT branch (from Task 4), extend for the expanded state:
```c
	if(chipChannel >= 0) {
		if(isKeyJustPressed(appState->inputState, KM_UP)) expandChip(chipChannel, true);
		if(isKeyJustPressed(appState->inputState, KM_DOWN) || isKeyJustPressed(appState->inputState, KM_SELECT))
			expandChip(chipChannel, false);
		if(chipExpanded(chipChannel)) {
			/* swatches + label edit handled by gui.c helper */
			handleExpandedChipInput(chipChannel, keyEvent);
			break;
		}
		... (existing jump-to-page handling) ...
	}
```
Add `bool chipExpanded(int channel);` + `void handleExpandedChipInput(int channel, int km);` to gui.h/gui.c: the helper applies swatch LEFT/RIGHT (colour) + label-char edit (KM_EDIT+UP/DOWN cycles the last char through the arcade table, KM_EDIT+LEFT/RIGHT moves a cursor — a simple cursor+char-cycle matching the preset-name node's model; keep the model: cursor at the end, UP/DOWN cycles the char under the cursor, LEFT/RIGHT moves the cursor, KM_START commits (collapses), KM_SELECT exits edit).

The exact arcade-edit mechanics should mirror `handlePresetUiInput`'s name-node editing — copy that model (cursor index + char-table cycle) into `handleExpandedChipInput`. See `src/gui.c` `commitPresetName`/the name-node editing branch for the pattern.

- [ ] **Step 3: Verify + commit**

Run: build + `meson test` 8/8 + boot. Commit:
```bash
git add src/gui.c src/gui.h src/main.c
git commit -m "feat(gui): expanded chip colour swatches + 8-char label editing"
```

---

### Task 6: Instrument page meta row (type selector + voice count)

**Files:**
- Modify: `src/gui.c` (createInstGraph + a meta-row builder)
- Modify: `src/gui.h`

**Interfaces:**
- Consumes: `setInstrumentVoiceType(vm, channel, vt)`, `setChannelVoiceCount(vm, channel, count)` (Task 2), `rebuildInstrumentGraph()`.
- Produces: `void appendMetaControlNode(Graph *g, GuiNode *container, Instrument *inst, VoiceManager *vm, int channel, int weight, bool selected);` — a horizontal row at the top of the instrument graph: `TYPE` action button (cycles SAMPLE→FM→BLEP, shows current tag), `VOICES` dial (1..8), `WIDTH` placeholder (disabled).

- [ ] **Step 1: Implement the meta row builder**

`src/gui.c` (mirror `appendPresetControlNode`'s shape):
```c
static void cbCycleVoiceType(void *ctx) {
	/* ctx = VoiceManager*; channel resolved via the selected instrument */
	VoiceManager *vm = (VoiceManager *)ctx;
	Instrument *inst = getSelectedInstInstrument();
	int ch = -1;
	for(int i = 0; i < vm->enabledChannels; i++) if(vm->instruments[i] == inst) { ch = i; break; }
	if(ch < 0) return;
	VoiceType next = (inst->voiceType + 1 == VOICE_TYPE_BLEP + 1) ? VOICE_TYPE_SAMPLE : (VoiceType)(inst->voiceType + 1);
	if(setInstrumentVoiceType(vm, ch, next)) rebuildInstrumentGraph();
}
void appendMetaControlNode(Graph *g, GuiNode *container, Instrument *inst, VoiceManager *vm, int channel, int weight, bool selected) {
	GuiNode *meta = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "META", 0, 0);
	meta->draw = drawWrapperNode; meta->drawable = true;
	GuiNode *typeBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "TYPE", selected, cbCycleVoiceType, vm);
	/* voice-count dial bound to a static int-backed Parameter (rebuilt per graph) */
	GuiNode *voices = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "VOICES", 0, incParameterBaseValue, vm->voiceCountParam);
	GuiNode *width = createBlankGuiNode();
	appendItem(meta, typeBtn, 2); appendItem(meta, voices, 2); appendItem(meta, width, 6);
	appendItem(container, meta, weight);
}
```
The voice-count dial needs a Parameter. The cleanest: add a per-instrument `Parameter *voiceCountParam` (range 1..8, int) to the Instrument (voice.h), created in `init_instrument`, synced to `vm->voiceCount[channel]`. Its `onChange` calls `setChannelVoiceCount(vm, channel, round(value))` + rebuild. Add that param + wiring in Task 2's files (voice.h/voice.c) as part of this task: create it in `init_instrument`; the meta row reads/writes it; `setChannelVoiceCount` writes the param's baseValue too.

In `createInstGraph`, add `appendMetaControlNode(instGraph, instwrap, inst, vm, channel, 1, false);` as the FIRST item of `instwrap` (above `presetWrap`).

- [ ] **Step 2: Wire the type tag display**

The TYPE button shows the current type tag. `drawActionBtnGuiNode` draws the name — extend `appendMetaControlNode` to set the type button's label to the current tag at build time (build the button name with `TextFormat("%s", tag)`). Rebuilds refresh it.

- [ ] **Step 3: Verify + commit**

Run: build + `meson test` 8/8 + both fixtures PASS + boot. The existing fixtures navigate the inst screen — verify they still PASS (the new meta row must not break the `rat1`-first-selection assumption — the meta row is added at the TOP of instwrap, so the default selection (`g->selected`) must still land correctly; check `createInstGraph`'s selection assignment + adjust so the meta row doesn't steal the initial selection).

Commit:
```bash
git add src/gui.c src/gui.h src/voice.h src/voice.c
git commit -m "feat(gui): instrument meta row (type cycle + voice count)"
```

---

### Task 7: Scripted fixtures + full gate

**Files:**
- Modify: `src/tools/instrument_harness/instrument_harness.c` (SHOT already exists)
- Create: `src/tools/instrument_harness/fixtures/chip_meta.txt`

**Interfaces:**
- Consumes: the SCENE_ARRANGER chip nav + the meta row from Tasks 3-6. The harness boots to the instrument screen; extend it to switch to the arranger scene (add a script verb `SCENE <n>` that sets `appState->currentScene`, or drive via the same input paths) so the fixture can reach the chip row.

- [ ] **Step 1: Add a SCENE script verb**

`src/tools/instrument_harness/instrument_harness.c`: add `SOP_SCENE` + a `SCENE <n>` fixture verb that sets `appState->currentScene = n` (and for SCENE_ARRANGER, ensures the arranger graph exists — it's created at initApplication; verify). Register it in `processScriptStep`/the parser.

- [ ] **Step 2: Write the fixture**

`fixtures/chip_meta.txt` (approx; uses the existing verb set + the new SCENE):
```
SCENE 0
FRAMES 2
UP            ; grid row 0 -> chip row (chip for the cursor channel)
ASSERT selected chip0        ; harness: add SOP_ASSERT_CHIP or reuse selected-name assert with the chip's node name
EDIT UP       ; expand
EDIT RIGHT    ; swatch right (colour 0 -> 1)
EDIT DOWN     ; collapse
EDIT RIGHT    ; jump to instrument page
ASSERT scene 2
FRAMES 2
ASSERT selected type   ; the meta row TYPE button
EDIT          ; cycle type
ASSERT voiceType fm    ; add a voiceType assert verb OR assert via state
QUIT
```
The harness needs the exact assert verbs — add `SOP_ASSERT_SCENE`, `SOP_ASSERT_CHIP_CHANNEL`, `SOP_ASSERT_VOICETYPE` as needed (each ~10 lines, mirroring the existing `runAssertSelected`). The edit-mode arrow dispatch for the chip label needs the KM_EDIT-hold injection the scripted mode already supports.

- [ ] **Step 3: Full gate**

Run: `ninja -C build` clean, `meson test -C build` 8/8, both existing fixtures PASS, the new `chip_meta` fixture PASS, app boots (`PRESETS LOADED`), and a clean-exit save keeps the labels (verify via a temp run that saves + reloads with labels intact — the Task 1 round-trip test covers the io; a boot-level check that `LABL` doesn't break the existing `s1.sng` load).

- [ ] **Step 4: Commit**

```bash
git add src/tools/instrument_harness/instrument_harness.c src/tools/instrument_harness/fixtures/chip_meta.txt
git commit -m "test(harness): chip/meta scripted fixture + SCENE/voiceType asserts"
```

---

## Notes for the implementer

- `agui` is a file-static in gui.c (the arranger graph). `isInstChipNode` must be reachable from main.c — declare the accessors in gui.h.
- The preset-name arcade input (gui.c `createPresetNameGuiNode` + `handlePresetUiInput`'s editing branch) is the reference for the chip label's edit model (cursor + char-table cycle). Copy the model, don't refactor the preset node (its fixtures are load-bearing).
- `freeInstrument` (Task 2) must not double-free: `cleanupModSystem(inst->modList)` frees the Mod entries; `freeParamList(inst->paramList)` frees the params. Do NOT call `freeVoice` (voices are owned by the pool + already rebuilt).
- The grid cursor is already gated on `gn->selected` (the arranger grid node), so moving the selection to a chip hides it automatically.