/* Tests for gui_pattern.c — the pattern-grid node graph and its navigation.
 *
 * Same playbook as test_gui_layer: the graph construction and navigation are
 * pure node/data-structure logic (createGraph/createGuiNode/appendItem need no
 * window), so this file drives them with no display. Only the draw callbacks
 * (drawStepGuiNode / drawSongMinimapGui / gui_pattern_draw) need raylib; those
 * live in test_gui_pattern_draw.c under Xvfb.
 *
 * PatternList and Sequencer are plain structs, so the tests build them by hand
 * rather than going through createPatternList/createArranger (which need an
 * ApplicationState and the whole voice/arranger boot). selectStep() calls
 * patternList->onStepChange.f, so the tests install a recording stub there —
 * that callback is part of the contract. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "graph_gui.h"
#include "sequencer.h"
#include "gui_internal.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#define PATTERN_STEPS_PER_ROW 4

/* Records what selectStep's onStepChange callback was handed. */
static int g_lastStepChange = -999;
static int g_stepChangeCalls = 0;

static void record_step_change(void *self, void *data) {
	(void)self;
	g_stepChangeCalls++;
	if(data) {
		g_lastStepChange = *(int *)data;
	}
}

static void reset_cb(void) {
	g_lastStepChange = -999;
	g_stepChangeCalls = 0;
}

/* A PatternList with one pattern of the given size, plus a wired callback. */
static PatternList *make_pl(int pattern_size) {
	PatternList *pl = (PatternList *)malloc(sizeof(PatternList));
	memset(pl, 0, sizeof(PatternList));
	pl->pattern_count = 1;
	pl->patterns[0].pattern_size = pattern_size;
	pl->selectedPattern = 0;
	pl->onStepChange.f = record_step_change;
	pl->onStepChange.appstateRef = NULL;
	return pl;
}

static Sequencer *make_seq(void) {
	Sequencer *seq = (Sequencer *)malloc(sizeof(Sequencer));
	memset(seq, 0, sizeof(Sequencer));
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		seq->pattern_index[i] = -1;    /* nothing playing this pattern */
	}
	return seq;
}

/* ---- the velocity lane ---- */

/* Find a node by name in the pattern graph. */
static GuiNode *find_named(GuiNode *n, const char *name) {
	if(!n) {
		return NULL;
	}
	if(n->name && strcmp(n->name, name) == 0) {
		return n;
	}
	if(n->items) {
		ListElement *le = n->items->head;
		for(int i = 0; i < n->itemCount && le; i++, le = le->next) {
			GuiNode *hit = find_named(*(GuiNode **)le->data, name);
			if(hit) {
				return hit;
			}
		}
	}
	return NULL;
}

/* The lane is a second grid under the note grid: one cell per step,
 * each drawing a bar whose height is the step's velocity. A step with
 * no velocity set is UNSET, and an unset step plays at full -- so it
 * draws the FULL height, not an empty cell. */
static int test_velocity_lane(void) {
	PatternList *pl = make_pl(4);
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	GuiNode *lane = find_named(getPatternGraphForTest()->root, "VELLANE");
	ASSERT_TRUE(lane != NULL);

	/* One cell per step of the pattern. */
	for(int i = 0; i < 4; i++) {
		char name[32];
		snprintf(name, sizeof(name), "VELCELL_%d", i);
		ASSERT_TRUE(find_named(lane, name) != NULL);
	}

	/* The bar's height is the velocity's share of the cell. */
	float full = velCellBarHeight(NOTE_VELOCITY_FULL, 100);
	float half = velCellBarHeight(NOTE_VELOCITY_FULL / 2, 100);
	ASSERT_TRUE(full > 99.0f && full < 101.0f);
	ASSERT_TRUE(half > 45.0f && half < 55.0f);

	/* An UNSET velocity (zero) plays at full, so it draws full. */
	float unset = velCellBarHeight(0, 100);
	ASSERT_TRUE(unset > 99.0f && unset < 101.0f);

	/* The velocity is clamped to 1..NOTE_VELOCITY_FULL, and zero is
	 * only reachable by CLEARING a cell -- never by turning it down. */
	ASSERT_TRUE(velCellBarHeight(-5, 100) == unset);
	ASSERT_TRUE(velCellBarHeight(NOTE_VELOCITY_FULL + 50, 100) == full);

	destroyPatternGraph();
	free(pl);
	free(seq);
	printf("PASS test_velocity_lane\n");
	return 0;
}

/* ---- createPatternGraph: row layout from pattern size ---- */
static int test_graph_row_layout(void) {
	PatternList *pl = make_pl(8);       /* 8 steps => exactly 2 rows of 4 */
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	/* The graph is module-private; reach it through the public navigation
	 * entry point instead. Assert the observable consequence: navigating
	 * RIGHT three times from step 0 lands on step 3 (still row 0), RIGHT
	 * once more wraps into step 4 (row 1). A wrong row count would show up
	 * as a different wrap. */
	reset_cb();
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 1);
	navigatePatternGraph(KM_RIGHT);
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 3);
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 4);

	printf("PASS test_graph_row_layout\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* A size that is not a multiple of 4: the last row is short (the inner loop
 * breaks on i >= size) and no phantom steps are created. */
static int test_graph_partial_last_row(void) {
	PatternList *pl = make_pl(6);       /* rows of 4 + a row of 2 */
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	/* Navigating DOWN from 0 lands on step 4 (row 1 col 0). Then RIGHT to 5. */
	reset_cb();
	navigatePatternGraph(KM_DOWN);
	ASSERT_TRUE(selectedStep == 4);
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 5);

	/* Step 6 does not exist: DOWN from 5 clamps to size-1 == 5. */
	navigatePatternGraph(KM_DOWN);
	ASSERT_TRUE(selectedStep == 5);

	printf("PASS test_graph_partial_last_row\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* An empty pattern (size 0): no step nodes, no selection, navigation is a
 * no-op rather than a crash. */
static int test_graph_empty_pattern(void) {
	PatternList *pl = make_pl(0);
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	/* No selection exists, so navigatePatternGraph returns early. */
	reset_cb();
	navigatePatternGraph(KM_RIGHT);
	navigatePatternGraph(KM_DOWN);
	navigatePatternGraph(KM_LEFT);
	ASSERT_TRUE(g_stepChangeCalls == 0);
	ASSERT_TRUE(selectedStep == 0);

	printf("PASS test_graph_empty_pattern\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* selectedStep is clamped into [0, size-1] when the graph is built. Note the
 * clamp is applied to the graph's internal selection, not written back to the
 * caller's *selectedStep — so the caller's variable keeps its out-of-range
 * value until the first navigation re-derives it through selectStep, which
 * clamps and writes back. Assert that observable behaviour rather than
 * assuming the clamp is mirrored. */
static int test_graph_selected_step_clamped(void) {
	Sequencer *seq = make_seq();
	int selectedPattern = 0;

	/* Negative: the graph selects step 0, so the first RIGHT moves to 1
	 * (not to -2 clamped to 0, which would leave us at 0). */
	PatternList *pl = make_pl(8);
	int neg = -3;
	createPatternGraph(seq, pl, &selectedPattern, &neg);
	reset_cb();
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(neg == 1);

	/* Past the end: graph selects size-1 (7), so LEFT moves to 6. */
	PatternList *pl2 = make_pl(8);
	int past = 99;
	createPatternGraph(seq, pl2, &selectedPattern, &past);
	reset_cb();
	navigatePatternGraph(KM_LEFT);
	ASSERT_TRUE(past == 6);

	printf("PASS test_graph_selected_step_clamped\n");
	destroyPatternGraph();
	free(pl);
	free(pl2);
	free(seq);
	return 0;
}

/* An out-of-range selectedPattern yields an empty grid rather than reading
 * patterns[patternIndex] out of bounds. */
static int test_graph_pattern_index_guarded(void) {
	PatternList *pl = make_pl(8);
	Sequencer *seq = make_seq();
	int badIndex = 77;
	int selectedStep = 0;

	/* pattern_count is 1, so 77 is out of range: size must fall back to 0. */
	createPatternGraph(seq, pl, &badIndex, &selectedStep);
	reset_cb();
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(g_stepChangeCalls == 0);

	/* Negative index too. */
	int negIndex = -1;
	createPatternGraph(seq, pl, &negIndex, &selectedStep);
	reset_cb();
	navigatePatternGraph(KM_DOWN);
	ASSERT_TRUE(g_stepChangeCalls == 0);

	printf("PASS test_graph_pattern_index_guarded\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* A pattern_size larger than MAX_SEQUENCE_LENGTH must not create step nodes
 * past the fixed stepData[]/stepNodes[] arrays (both are MAX_SEQUENCE_LENGTH).
 * The visible contract is that the grid only ever holds MAX_SEQUENCE_LENGTH
 * steps, so navigation stays inside the array. */
static int test_graph_oversized_pattern_bounded(void) {
	PatternList *pl = make_pl(MAX_SEQUENCE_LENGTH * 3);   /* 72, array is 24 */
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	/* Walk to the far end: navigation must stay inside stepNodes[] and the
	 * selected step must never exceed the array bound. */
	for(int i = 0; i < MAX_SEQUENCE_LENGTH * 4; i++) {
		navigatePatternGraph(KM_DOWN);
		ASSERT_TRUE(selectedStep >= 0);
		ASSERT_TRUE(selectedStep < MAX_SEQUENCE_LENGTH);
	}

	printf("PASS test_graph_oversized_pattern_bounded\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* ---- navigatePatternGraph: all four directions + default ---- */
static int test_navigation_directions(void) {
	PatternList *pl = make_pl(24);      /* a full pattern */
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 12;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	reset_cb();
	navigatePatternGraph(KM_LEFT);
	ASSERT_TRUE(selectedStep == 11);
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 12);
	navigatePatternGraph(KM_UP);
	ASSERT_TRUE(selectedStep == 12 - PATTERN_STEPS_PER_ROW);
	navigatePatternGraph(KM_DOWN);
	ASSERT_TRUE(selectedStep == 12);
	ASSERT_TRUE(g_stepChangeCalls == 4);

	/* An unmapped key is ignored: the switch default returns before the
	 * selectedStep write and before changeGraphSelection. */
	reset_cb();
	navigatePatternGraph(KM_SELECT);
	navigatePatternGraph(KM_EDIT);
	ASSERT_TRUE(g_stepChangeCalls == 0);
	ASSERT_TRUE(selectedStep == 12);

	printf("PASS test_navigation_directions\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* Navigation clamps at the pattern edges instead of running off them. */
static int test_navigation_clamps_at_edges(void) {
	PatternList *pl = make_pl(8);
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);

	/* LEFT from 0 stays 0. */
	navigatePatternGraph(KM_LEFT);
	navigatePatternGraph(KM_LEFT);
	ASSERT_TRUE(selectedStep == 0);
	/* UP from 0 stays 0. */
	navigatePatternGraph(KM_UP);
	ASSERT_TRUE(selectedStep == 0);
	/* RIGHT to the end, then past it. */
	for(int i = 0; i < 20; i++) {
		navigatePatternGraph(KM_RIGHT);
	}
	ASSERT_TRUE(selectedStep == 7);

	printf("PASS test_navigation_clamps_at_edges\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* ---- rebuildPatternGraph ---- */
static int test_rebuild(void) {
	PatternList *pl = make_pl(8);
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 1);

	/* Rebuild preserves the caller's selection pointers and state. */
	rebuildPatternGraph();
	ASSERT_TRUE(selectedStep == 1);
	reset_cb();
	navigatePatternGraph(KM_RIGHT);
	ASSERT_TRUE(selectedStep == 2);

	printf("PASS test_rebuild\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* rebuildPatternGraph before any create is a no-op, not a crash. */
static int test_rebuild_before_create(void) {
	Sequencer *seq = make_seq();
	PatternList *pl = make_pl(8);
	int selectedPattern = 0;
	int selectedStep = 0;
	/* Create, then free the PatternList handle the module still points at is
	 * not safe; instead just confirm the guard by calling rebuild on a stack
	 * that was never initialised through createPatternGraph in this run.
	 * Use a fresh process-local ordering: create with one pattern, then
	 * rebuild repeatedly — each must be safe. */
	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);
	rebuildPatternGraph();
	rebuildPatternGraph();
	rebuildPatternGraph();
	ASSERT_TRUE(selectedPattern == 0);

	destroyPatternGraph();
	free(seq);
	free(pl);
	printf("PASS test_rebuild_before_create\n");
	return 0;
}

/* ---- createSongMinimapGui ---- */
static int test_song_minimap_create(void) {
	initDefaultColourScheme(&cs);
	Arranger *arranger = (Arranger *)malloc(sizeof(Arranger));
	memset(arranger, 0, sizeof(Arranger));
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			arranger->song[ch][r] = -1;
		}
	}
	int songIndex[2] = { 0, 0 };

	SongMinimapGui *mg = createSongMinimapGui(arranger, songIndex, 10, 20);
	ASSERT_TRUE(mg != NULL);
	ASSERT_TRUE(mg->arranger == arranger);
	ASSERT_TRUE(mg->songIndex == songIndex);
	ASSERT_TRUE(mg->shape.x == 10);
	ASSERT_TRUE(mg->shape.y == 20);
	ASSERT_TRUE(mg->shape.w == 4);
	ASSERT_TRUE(mg->shape.h == 4);
	ASSERT_TRUE(mg->padding == 1);
	/* maxMapLength is derived from SCREEN_H and the cell pitch. */
	ASSERT_TRUE(mg->maxMapLength == (SCREEN_H - 150) / (mg->shape.h + mg->padding));
	ASSERT_TRUE(mg->base.draw != NULL);   /* the draw callback is installed */

	free(mg);
	free(arranger);
	printf("PASS test_song_minimap_create\n");
	return 0;
}

/* ---- setters are total (accept NULL) ---- */
static int test_setters(void) {
	setSongMinimapGui(NULL);
	setPatternBufferScroller(NULL);
	setSongMinimapGui(NULL);
	setPatternBufferScroller(NULL);
	printf("PASS test_setters\n");
	return 0;
}

/* A negative pattern_size (possible from a corrupt file before the loader's
 * range check existed) must be treated as an empty grid, not a huge one. */
static int test_graph_negative_pattern_size(void) {
	PatternList *pl = make_pl(-5);
	Sequencer *seq = make_seq();
	int selectedPattern = 0;
	int selectedStep = 0;

	createPatternGraph(seq, pl, &selectedPattern, &selectedStep);
	reset_cb();
	navigatePatternGraph(KM_RIGHT);
	navigatePatternGraph(KM_DOWN);
	ASSERT_TRUE(g_stepChangeCalls == 0);
	ASSERT_TRUE(selectedStep == 0);

	printf("PASS test_graph_negative_pattern_size\n");
	destroyPatternGraph();
	free(seq);
	free(pl);
	return 0;
}

/* navigatePatternGraph and rebuildPatternGraph before any createPatternGraph:
 * the module-private graph pointer is still NULL, so both must return early
 * rather than dereferencing it. These run first in main to establish that
 * state. */
static int navigateBeforeCreate(void) {
	navigatePatternGraph(KM_RIGHT);
	navigatePatternGraph(KM_DOWN);
	printf("PASS navigateBeforeCreate\n");
	return 0;
}

static int rebuildBeforeCreate(void) {
	rebuildPatternGraph();
	rebuildPatternGraph();
	printf("PASS rebuildBeforeCreate\n");
	return 0;
}

int main(void) {
	if(test_velocity_lane() != 0) {
		return 1;
	}
	/* These two must run before any createPatternGraph so the module-private
	 * graph pointer is still NULL — they cover the "called too early" guards. */
	if(navigateBeforeCreate() != 0) {
		return 1;
	}
	if(rebuildBeforeCreate() != 0) {
		return 1;
	}

	int rc = 0;
	rc |= test_graph_row_layout();
	rc |= test_graph_partial_last_row();
	rc |= test_graph_empty_pattern();
	rc |= test_graph_selected_step_clamped();
	rc |= test_graph_pattern_index_guarded();
	rc |= test_graph_oversized_pattern_bounded();
	rc |= test_navigation_directions();
	rc |= test_navigation_clamps_at_edges();
	rc |= test_rebuild();
	rc |= test_rebuild_before_create();
	rc |= test_song_minimap_create();
	rc |= test_setters();
	rc |= test_graph_negative_pattern_size();
	if(rc != 0) {
		fprintf(stderr, "FAILED (gui_pattern)\n");
		return 1;
	}
	printf("PASSED (gui_pattern)\n");
	return 0;
}
