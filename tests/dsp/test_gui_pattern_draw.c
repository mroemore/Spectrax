/* Draw-callback reachability for gui_pattern.c — runs under Xvfb.
 *
 * Covers drawStepGuiNode, drawSongMinimapGui and gui_pattern_draw, which make
 * real raylib calls. Not a golden-image test: it asserts the draw paths
 * execute and are not skipped, which is what closes their coverage branches.
 *
 * The song-minimap branch matrix (selected / playing / default / blank) is
 * built by populating arranger->song and playhead_indices so each of the four
 * DrawRectangle colour branches is taken. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "graph_gui.h"
#include "sequencer.h"
#include "vizfx.h"
#include "gui_internal.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static void noop_step_change(void *self, void *data) {
	(void)self;
	(void)data;
}

/* StepNodeData's layout lives in gui_pattern.c (file-private). Mirror it
 * locally -- the same approach test_graph_nav.c takes for
 * ArrangerCellGuiNode -- so the draw entry point can be driven with a
 * deliberately out-of-range pattern index. */
typedef struct {
	PatternList *pl;
	Sequencer *seq;
	int patternIndex;
	int stepIndex;
	int *selectedStepPtr;
} TestStepNodeData;

/* drawStepGuiNode with an out-of-range pattern index: getStep returns NULL and
 * the cell must be left blank rather than dereferenced. drawStepGuiNode issues
 * raylib calls before that point, so this case needs a real context and lives
 * here rather than in the windowless test. */
static int draw_step_node_null_step(void) {
	PatternList *pl = (PatternList *)malloc(sizeof(PatternList));
	memset(pl, 0, sizeof(PatternList));
	pl->pattern_count = 1;
	pl->patterns[0].pattern_size = 4;
	Sequencer *seq = (Sequencer *)malloc(sizeof(Sequencer));
	memset(seq, 0, sizeof(Sequencer));
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		seq->pattern_index[i] = -1;
		seq->running[i] = false;
		seq->playhead_index[i] = -1;
	}
	static int dummySel = 0;

	GuiNode *node = createGuiNode(0, 0, 20, 20, 1, na_vertical, "step", 1, 0);
	TestStepNodeData d;
	d.pl = pl;
	d.seq = seq;
	d.patternIndex = MAX_PATTERNS + 10;   /* past what getStep accepts */
	d.stepIndex = 0;
	d.selectedStepPtr = &dummySel;
	node->p = (Parameter *)&d;
	node->draw = drawStepGuiNode;
	node->drawable = true;

	BeginDrawing();
	drawStepGuiNode(node);       /* must return without drawing the note */
	EndDrawing();

	freeGuiNode(node);
	free(seq);
	free(pl);
	return 0;
}

int main(void) {
	InitWindow(640, 480, "spx-pattern-draw-test");
	if(!IsWindowReady()) {
		fprintf(stderr, "FAIL: window not ready (needs a display / Xvfb)\n");
		return 1;
	}
	initDefaultColourScheme(&cs);

	/* The draw entry points must tolerate being called before the pattern
	 * graph exists (an early frame, or a screen switch). */
	gui_pattern_draw();
	setPatternBufferScroller(NULL);

	/* A pattern with some content so step nodes are built and drawable. */
	PatternList *pl = (PatternList *)malloc(sizeof(PatternList));
	memset(pl, 0, sizeof(PatternList));
	pl->pattern_count = 1;
	pl->patterns[0].pattern_size = 8;
	for(int i = 0; i < 8; i++) {
		pl->patterns[0].notes[i][0] = i;    /* note index  */
		pl->patterns[0].notes[i][1] = 0;    /* octave      */
	}
	pl->onStepChange.f = noop_step_change;
	pl->onStepChange.appstateRef = NULL;

	Sequencer *seq = (Sequencer *)malloc(sizeof(Sequencer));
	memset(seq, 0, sizeof(Sequencer));
	/* Phase 1: the first channel whose pattern_index matches the displayed
	 * pattern is running with its playhead on step 2, so drawStepGuiNode takes
	 * the "currently playing" path for that cell and the not-playing path for
	 * the rest. Channel 1 targets a different pattern, so the comparison's
	 * false operand runs before any match. */
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		seq->pattern_index[i] = -1;
		seq->running[i] = false;
		seq->playhead_index[i] = -1;
	}
	seq->pattern_index[1] = 5;     /* different pattern: comparison is false */
	seq->pattern_index[0] = 0;     /* matches, running, playhead on step 2 */
	seq->running[0] = true;
	seq->playhead_index[0] = 2;

	int selectedPattern = 0;
	int selectedStep = 0;
	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	/* Arranger for the minimap: cover all four colour branches.
	 *   cell (0,0): the selected song index  -> selectedCellColour
	 *   cell (1,1): playhead, not selected   -> playingCellColour
	 *   cell (2,2): has a pattern           -> defaultCellColour
	 *   everything else: blank               -> blankCellColour           */
	Arranger *arranger = (Arranger *)malloc(sizeof(Arranger));
	memset(arranger, 0, sizeof(Arranger));
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			arranger->song[ch][r] = -1;
		}
		arranger->playhead_indices[ch] = -1;
	}
	arranger->song[0][0] = 3;      /* selected (songIndex == {0,0}) */
	arranger->song[0][5] = 4;      /* same channel, different song row: the
	                                 * songIndex[1] == i operand is false */
	arranger->song[1][1] = 5;      /* playhead at [1][1]             */
	arranger->song[2][2] = 7;      /* plain populated cell           */
	arranger->playhead_indices[1] = 1;

	int songIndex[2] = { 0, 0 };
	SongMinimapGui *mg = createSongMinimapGui(arranger, songIndex, 4, 4);
	ASSERT_TRUE(mg != NULL);
	setSongMinimapGui(mg);

	/* Two frames: each step node's drawStepGuiNode runs, then the minimap. */
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();

	/* With the minimap detached the pattern graph still draws. */
	setSongMinimapGui(NULL);
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();

	/* Navigation with the graph live (exercises changeGraphSelection under a
	 * real context and re-draws at the new selection). */
	navigatePatternGraph(KM_RIGHT);
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();

	/* A pattern no sequencer channel targets: drawStepGuiNode's channel scan
	 * runs to completion without a match. */
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		seq->pattern_index[i] = -1;
	}
	rebuildPatternGraph();
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();

	/* Phase 2: the first matching channel targets the pattern but is stopped,
	 * so the "running" operand is false on its own. The scan stops at the
	 * first match, so this needs a separate pass from the running case. */
	seq->pattern_index[0] = 0;
	seq->running[0] = false;
	seq->playhead_index[0] = 2;
	rebuildPatternGraph();
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();

	/* A real buffer scroller so the scroller strip actually draws, then back
	 * to NULL so the "no scroller attached" path is taken too. */
	BufferScroller bs = { 0 };
	for(int i = 0; i < 8; i++) {
		pushBufferScrollerFrame(&bs, (float)i * 0.1f);
	}
	setPatternBufferScroller(&bs);
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();
	setPatternBufferScroller(NULL);
	BeginDrawing();
	ClearBackground(BLACK);
	gui_pattern_draw();
	EndDrawing();

	/* The null-step guard: getStep rejects the index, the cell is left blank. */
	if(draw_step_node_null_step() != 0) {
		return 1;
	}

	free(mg);
	free(arranger);
	free(seq);
	free(pl);
	CloseWindow();

	printf("PASSED (gui_pattern draw under Xvfb)\n");
	return 0;
}
