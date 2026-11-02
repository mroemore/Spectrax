/* Draw-callback reachability for gui_arranger.c — runs under Xvfb.
 *
 * Covers drawArrangerCellGuiNode and drawInstChipGuiNode, which make real
 * raylib calls. Not a golden-image test: it asserts the draw paths execute and
 * take each branch, which is what closes their coverage branches.
 *
 * The arranger cell's three colour branches (playhead / default / blank) are
 * reached by populating song[][] and playhead_indices[] so each case exists.
 * The selected-cell outline branch needs gn->selected set. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "gui.h"
#include "io.h"
#include "modsystem.h"
#include "sample.h"
#include "voice.h"
#include "wavetable.h"
#include "graph_gui.h"
#include "sequencer.h"
#include "gui_internal.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

static PresetBank presetBank;

int main(void) {
	InitWindow(640, 480, "spx-arranger-draw-test");
	if(!IsWindowReady()) {
		fprintf(stderr, "FAIL: window not ready (needs a display / Xvfb)\n");
		return 1;
	}
	initDefaultColourScheme(&cs);

	Arranger *a = (Arranger *)calloc(1, sizeof(Arranger));
	ASSERT_TRUE(a != NULL, "calloc Arranger");
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			a->song[ch][r] = -1;
		}
		a->playhead_indices[ch] = -1;
		snprintf(a->label[ch], 9, "ch%d", ch);
		a->labelColourIdx[ch] = ch % 8;
	}
	a->enabledChannels = 3;

	/* Channel 0: cell (0,0) occupied and on the playhead, and playing --
	 * the playhead colour branch. Channel 0 row 1 occupied, not on the
	 * playhead -- the default branch. Everything else blank.
	 * Channel 1's playhead sits on a blank cell, which must still draw
	 * blank rather than highlighted (the stale-playhead case the comment in
	 * arrangerCellFill calls out). */
	a->song[0][0] = 3;
	a->song[0][1] = 7;
	a->playhead_indices[0] = 0;
	a->song[1][4] = 2;
	a->playhead_indices[1] = 4;
	a->playing = true;

	/* bpm / swing must be real Parameters: createDialGuiNode stores them. */
	ParamList *pl = createParamList();
	ASSERT_TRUE(pl != NULL, "createParamList");
	a->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	a->tempoSettings.swing = createParameterPro(pl, "Swing", 0.0f, 0.0f, 1.0f,
	                                            0.01f, 0.1f, NULL, NULL);
	ASSERT_TRUE(a->tempoSettings.bpm != NULL, "bpm param");
	ASSERT_TRUE(a->tempoSettings.swing != NULL, "swing param");

	PatternList *patterns = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(patterns != NULL, "calloc PatternList");
	patterns->pattern_count = 1;
	patterns->patterns[0].pattern_size = 8;
	for(int r = 0; r < MAX_SEQUENCE_LENGTH; r++) {
		patterns->patterns[0].notes[r][0] = r % 12;
		patterns->patterns[0].notes[r][1] = 3;
	}

	/* A real VoiceManager. createArrangerGraph hands a->vm to every chip, and
	 * drawInstChipGuiNode bails out at `if(!chip->vm->instruments[channel])`
	 * -- so without this the entire chip draw body (~110 lines) is skipped and
	 * the coverage stays misleadingly high on that function. */
	SamplePool *sp = createSamplePool();
	ASSERT_TRUE(sp != NULL, "createSamplePool");
	{
		enum { N = 1024 };
		float *buf = (float *)malloc(N * sizeof(float));
		ASSERT_TRUE(buf != NULL, "sample buffer");
		for(int i = 0; i < N; i++) {
			buf[i] = sinf(2.0f * (float)M_PI * (float)i / (float)N);
		}
		loadSample(sp, "sine", buf, 24, SPECTRAX_ENGINE_RATE, N);
		free(buf);
	}
	WavetablePool *wtp = createWavetablePool();
	ASSERT_TRUE(wtp != NULL, "createWavetablePool");
	initPresetBank(&presetBank);
	Preset p;
	initDefaultFmPreset(&p);
	addPresetToBank(&presetBank, p);

	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = 3;
	settings.defaultVoiceCount = 2;
	settings.defaultBPM = 120;
	a->vm = createVoiceManager(&settings, sp, wtp, &presetBank);
	ASSERT_TRUE(a->vm != NULL, "createVoiceManager");
	initModSystem();

	createArrangerGraph(a, patterns);

	/* Expanded chips take a different draw path than collapsed ones. */
	expandChip(0, false);
	expandChip(1, true);
	expandChip(2, true);

	/* Selected / not-selected cells, so both outline branches are taken. */
	GuiNode *sel = getArrangerRowCell(0, 0);
	if(sel != NULL) {
		sel->selected = true;
	}
	GuiNode *other = getArrangerRowCell(1, 1);
	if(other != NULL) {
		other->selected = false;
	}

	/* The arranger screen has no draw wrapper of its own — gui_core.c's
	 * dispatch calls the generic drawNode(root). The root is module-private,
	 * so walk up from the selected node's container chain. */
	GuiNode *probe = getArrangerSelectedNode();
	GuiNode *root = NULL;
	while(probe != NULL) {
		root = probe;
		probe = probe->container;
	}
	ASSERT_TRUE(root != NULL, "arranger graph root reachable via container chain");

	for(int frame = 0; frame < 3; frame++) {
		BeginDrawing();
		ClearBackground(BLACK);
		drawNode(root);
		EndDrawing();
	}

	/* Stopped playback: the playhead branch must not fire. */
	a->playing = false;
	BeginDrawing();
	ClearBackground(BLACK);
	drawNode(root);
	EndDrawing();

	/* Collapsed again, for the other chip branch. */
	expandChip(1, false);
	expandChip(2, false);
	BeginDrawing();
	ClearBackground(BLACK);
	drawNode(root);
	EndDrawing();

	/* A cell with a NULL arranger must be skipped rather than dereferenced:
	 * this is the guard drawArrangerCellGuiNode carries. */
	GuiNode *orphan = createArrangerCellGuiNode(0, 0, 12, 12, true, NULL, 0, 0);
	ASSERT_TRUE(orphan != NULL, "createArrangerCellGuiNode(NULL arranger)");
	BeginDrawing();
	if(orphan->draw != NULL) {
		orphan->draw(orphan);
	}
	EndDrawing();
	freeGuiNode(orphan);

	destroyArrangerGraph();
	free(a->vm);
	freeSamplePool(sp);
	freeWavetablePool(wtp);
	free(patterns);
	freeParamList(pl);
	free(a);
	CloseWindow();

	printf("PASSED (gui_arranger draw under Xvfb)\n");
	return 0;
}