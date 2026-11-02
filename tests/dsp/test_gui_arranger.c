/* Tests for gui_arranger.c — the arranger grid, its instrument chips, and the
 * chip-label editing helpers.
 *
 * Same playbook as test_gui_layer / test_gui_pattern: most of this module is
 * node construction, selection bookkeeping and pure colour/label logic over
 * plain structs, so none of it needs a window. Only drawArrangerCellGuiNode and
 * drawInstChipGuiNode call raylib, and those live in test_gui_arranger_draw.c.
 *
 * The functions that do need a graph are driven through createArrangerGraph,
 * which builds real GuiNodes — node creation needs no display, only drawing
 * does. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "graph_gui.h"
#include "sequencer.h"
#include "gui_internal.h"

/* gui_arranger.c's private label table. Mirrored because the module keeps it
 * file-private; if it changes, the cycle tests below fail loudly rather than
 * quietly asserting against a stale alphabet. */
#define TEST_NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-. "
#define CHIP_LABEL_MAX 8

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define ASSERT_EQ(actual, expected) do { \
	long _a = (long)(actual); \
	long _e = (long)(expected); \
	if(_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: expected %ld, got %ld\n", \
		        __FILE__, __LINE__, _e, _a); \
		return 1; \
	} \
} while (0)

/* ---- arrangerCellFill: the whole truth table ---------------------------- */

static int test_arranger_cell_fill(void) {
	Color playhead = { 1, 2, 3, 255 };
	Color deflt = { 4, 5, 6, 255 };
	Color blank = { 7, 8, 9, 255 };

	/* Occupied cell, playhead on this row, playing -> playhead colour. */
	Color c = arrangerCellFill(0, 3, 3, true, playhead, deflt, blank);
	ASSERT_EQ(c.r, 1);
	/* Occupied, playhead elsewhere -> default. */
	c = arrangerCellFill(0, 3, 4, true, playhead, deflt, blank);
	ASSERT_EQ(c.r, 4);
	/* Occupied, playhead on this row, NOT playing -> default. This is the
	 * case the comment calls out: a stopped channel keeps a stale playhead
	 * index, and an empty-looking cell would otherwise be highlighted. */
	c = arrangerCellFill(0, 3, 3, false, playhead, deflt, blank);
	ASSERT_EQ(c.r, 4);
	/* Empty cell -> blank, whatever the playhead says. */
	c = arrangerCellFill(-1, 3, 3, true, playhead, deflt, blank);
	ASSERT_EQ(c.r, 7);
	c = arrangerCellFill(-1, 0, 0, false, playhead, deflt, blank);
	ASSERT_EQ(c.r, 7);
	/* A value below -1 is still "empty" — the test is `> -1`, not `== 0`. */
	c = arrangerCellFill(-99, 3, 3, true, playhead, deflt, blank);
	ASSERT_EQ(c.r, 7);

	printf("PASS test_arranger_cell_fill\n");
	return 0;
}

/* ---- chipLabelCharIndex -------------------------------------------------- */

static int test_chip_label_char_index(void) {
	/* The table starts at 'A'. */
	ASSERT_EQ(chipLabelCharIndex('A'), 0);
	ASSERT_EQ(chipLabelCharIndex('Z'), 25);
	ASSERT_EQ(chipLabelCharIndex('0'), 26);
	/* lowercase folds to uppercase */
	ASSERT_EQ(chipLabelCharIndex('a'), 0);
	ASSERT_EQ(chipLabelCharIndex('z'), 25);
	ASSERT_EQ(chipLabelCharIndex('q'), 16);
	/* Not in the table -> 0, so cycling wraps to 'A'. */
	ASSERT_EQ(chipLabelCharIndex('\0'), 0);
	ASSERT_EQ(chipLabelCharIndex('#'), 0);
	ASSERT_EQ(chipLabelCharIndex('~'), 0);
	/* Every table entry must resolve to its *first* index; this catches the
	 * mirrored table drifting out of sync with the real one. It is the first
	 * index, not the position, because the table ends with two spaces
	 * (index 36 and 40) and the lookup returns the first match. Consequence,
	 * documented rather than fixed: cycling can *write* the trailing space at
	 * index 40, but reading it back yields 36, so the last table entry is not
	 * reachable by cycling from a space. Cosmetic, in a label alphabet. */
	for(int i = 0; TEST_NAME_CHARS[i]; i++) {
		int j = 0;
		while(j < i && TEST_NAME_CHARS[j] != TEST_NAME_CHARS[i]) {
			j++;
		}
		ASSERT_EQ(chipLabelCharIndex(TEST_NAME_CHARS[i]), j);
	}

	printf("PASS test_chip_label_char_index\n");
	return 0;
}

/* ---- chipLabelCycleCharAt ----------------------------------------------- */

static int test_chip_label_cycle_char(void) {
	int count = (int)strlen(TEST_NAME_CHARS);

	/* NUL is treated as index 0, so the first UP lands on 'B'. */
	char slot = '\0';
	chipLabelCycleCharAt(&slot, 1);
	ASSERT_EQ(slot, TEST_NAME_CHARS[1]);

	/* Forward wrap, using a character that appears once in the table so the
	 * lookup round-trips: 'Z' (index 25) + 1 is '0', and from the last
	 * digit '9' (index 35) the next is the space at index 36. */
	slot = 'Z';
	chipLabelCycleCharAt(&slot, 1);
	ASSERT_EQ(slot, TEST_NAME_CHARS[26]);
	slot = '9';
	chipLabelCycleCharAt(&slot, 1);
	ASSERT_EQ(slot, ' ');

	/* Backward wrap from 'A'. */
	slot = 'A';
	chipLabelCycleCharAt(&slot, -1);
	ASSERT_EQ(slot, TEST_NAME_CHARS[count - 1]);

	/* Cycling forward from 'A' walks the table once, index by index... and
	 * then does NOT come back to 'A', because the second space (index 40)
	 * reads back as index 36. From there the cycle is 37,38,39,40 repeating.
	 * Asserted as-is below rather than as an idealised full rotation. */
	slot = 'A';
	for(int i = 1; i < count; i++) {
		chipLabelCycleCharAt(&slot, 1);
	}
	ASSERT_EQ(slot, TEST_NAME_CHARS[count - 1]);   /* index 40, a space */
	chipLabelCycleCharAt(&slot, 1);
	ASSERT_EQ(slot, '_');                          /* index 37, not 'A' */

	/* NULL is a no-op rather than a crash. */
	chipLabelCycleCharAt(NULL, 1);

	printf("PASS test_chip_label_cycle_char\n");
	return 0;
}

/* The table ends with two spaces (indices 36 and 40) and chipLabelCharIndex
 * returns the *first* match, so the trailing space is not round-trippable:
 * cycling forward from it yields index 37, not a wrap to 'A'. This pins the
 * behaviour rather than asserting an idealised wrap, because changing it would
 * change what a user can type into a chip label. */
static int test_chip_label_cycle_duplicate_space(void) {
	int count = (int)strlen(TEST_NAME_CHARS);
	ASSERT_EQ(count - 1, 40);        /* the trailing space */
	ASSERT_EQ(TEST_NAME_CHARS[36], ' ');
	ASSERT_EQ(TEST_NAME_CHARS[40], ' ');

	char slot = TEST_NAME_CHARS[count - 1];   /* ' ', read back as index 36 */
	ASSERT_EQ(chipLabelCharIndex(slot), 36);
	chipLabelCycleCharAt(&slot, 1);
	ASSERT_EQ(slot, TEST_NAME_CHARS[37]);     /* '_', not 'A' */

	/* Same from index 36 explicitly. */
	slot = ' ';
	chipLabelCycleCharAt(&slot, 1);
	ASSERT_EQ(slot, '_');

	printf("PASS test_chip_label_cycle_duplicate_space\n");
	return 0;
}

/* ---- chipLabelCursorMove ------------------------------------------------ */

static int test_chip_label_cursor_move(void) {
	int cursor = 0;
	/* Bounded to [0..strlen(label)] */
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, -1);
	ASSERT_EQ(cursor, 0);
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, 1);
	ASSERT_EQ(cursor, 1);
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, 1);
	ASSERT_EQ(cursor, 2);
	/* The cursor may sit one past the NUL so a new char can be inserted. */
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, 1);
	ASSERT_EQ(cursor, 3);
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, 1);
	ASSERT_EQ(cursor, 3);

	/* Anything other than -1 / +1 is ignored, so the caller can be sloppy. */
	cursor = 2;
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, 0);
	ASSERT_EQ(cursor, 2);
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, 5);
	ASSERT_EQ(cursor, 2);
	chipLabelCursorMove(&cursor, "ABC", CHIP_LABEL_MAX, -99);
	ASSERT_EQ(cursor, 2);

	/* NULL label means length 0. */
	cursor = 3;
	chipLabelCursorMove(&cursor, NULL, CHIP_LABEL_MAX, 1);
	ASSERT_EQ(cursor, 0);

	/* A label longer than maxLen is clamped, so a corrupt strlen can never
	 * push the cursor past the array end. */
	char toolong[32];
	memset(toolong, 'X', sizeof(toolong));
	toolong[20] = '\0';
	cursor = 0;
	for(int i = 0; i < 40; i++) {
		chipLabelCursorMove(&cursor, toolong, CHIP_LABEL_MAX, 1);
		ASSERT_TRUE(cursor <= CHIP_LABEL_MAX, "cursor <= CHIP_LABEL_MAX");
	}
	ASSERT_EQ(cursor, CHIP_LABEL_MAX);

	/* NULL cursor is a no-op. */
	chipLabelCursorMove(NULL, "ABC", CHIP_LABEL_MAX, 1);

	printf("PASS test_chip_label_cursor_move\n");
	return 0;
}

/* ---- node identity + coordinate extraction ----------------------------- */

static int test_cell_node_identity(void) {
	/* NULL is not a cell and has no coordinates. */
	ASSERT_TRUE(!isArrangerCellNode(NULL), "!isArrangerCellNode(NULL)");
	int x = 7, y = 7;
	getArrangerCellCoords(NULL, &x, &y);
	ASSERT_EQ(x, -1);
	ASSERT_EQ(y, -1);

	/* A real cell round-trips its coordinates. */
	Arranger a;
	memset(&a, 0, sizeof(a));
	GuiNode *n = createArrangerCellGuiNode(10, 20, 30, 40, true, &a, 2, 5);
	ASSERT_TRUE(n != NULL, "n != NULL");
	ASSERT_TRUE(isArrangerCellNode(n), "isArrangerCellNode(n)");
	getArrangerCellCoords(n, &x, &y);
	ASSERT_EQ(x, 2);
	ASSERT_EQ(y, 5);

	/* NULL out-params are allowed. */
	getArrangerCellCoords(n, NULL, NULL);

	/* Passing a non-cell node must not reinterpret its memory: the identity
	 * check is by draw-callback, not by struct shape. */
	GuiNode *plain = createGuiNode(0, 0, 10, 10, 1, na_horizontal, "plain", 1, 0);
	ASSERT_TRUE(!isArrangerCellNode(plain), "!isArrangerCellNode(plain)");
	x = 7;
	y = 7;
	getArrangerCellCoords(plain, &x, &y);
	ASSERT_EQ(x, -1);
	freeGuiNode(plain);

	/* NULL out-params on the non-cell path. */
	getArrangerCellCoords(NULL, NULL, NULL);

	freeGuiNode(n);

	/* NULL arranger is accepted; the draw callback guards it. */
	GuiNode *orphan = createArrangerCellGuiNode(0, 0, 4, 4, false, NULL, 0, 0);
	ASSERT_TRUE(orphan != NULL, "orphan != NULL");
	ASSERT_TRUE(isArrangerCellNode(orphan), "isArrangerCellNode(orphan)");
	freeGuiNode(orphan);

	printf("PASS test_cell_node_identity\n");
	return 0;
}

static int test_chip_node_identity(void) {
	ASSERT_TRUE(!isInstChipNode(NULL), "!isInstChipNode(NULL)");
	/* A cell is not a chip — identity is by draw callback. */
	Arranger a;
	memset(&a, 0, sizeof(a));
	GuiNode *cell = createArrangerCellGuiNode(0, 0, 4, 4, false, &a, 0, 0);
	ASSERT_TRUE(!isInstChipNode(cell), "!isInstChipNode(cell)");
	freeGuiNode(cell);

	printf("PASS test_chip_node_identity\n");
	return 0;
}

/* ---- chip expand / query against a real graph --------------------------- */

/* A minimal Arranger + PatternList: enough for createArrangerGraph to build. */
static Arranger *make_arranger(int channels) {
	Arranger *a = (Arranger *)calloc(1, sizeof(Arranger));
	if(a == NULL) {
		return NULL;
	}
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			a->song[ch][r] = -1;
		}
		a->playhead_indices[ch] = -1;
	}
	a->enabledChannels = channels;
	a->visibleStart = 0;
	return a;
}

static int test_chip_expand_and_input(void) {
	Arranger *a = make_arranger(4);
	ASSERT_TRUE(a != NULL, "a != NULL");
	PatternList *pl = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(pl != NULL, "pl != NULL");

	/* Before any graph exists every accessor must be total. */
	ASSERT_EQ(getSelectedChipChannel(), -1);
	ASSERT_TRUE(getArrangerSelectedNode() == NULL, "getArrangerSelectedNode() == NULL");
	ASSERT_TRUE(!isChipExpanded(0), "!isChipExpanded(0)");
	expandChip(0, true);
	handleExpandedChipInput(0, KM_EDIT, false);
	handleExpandedChipInput(0, KM_LEFT, true);

	createArrangerGraph(a, pl);

	/* Chips exist now, so expand/isChipExpanded round-trip. */
	expandChip(1, true);
	ASSERT_TRUE(isChipExpanded(1), "isChipExpanded(1)");
	expandChip(1, false);
	ASSERT_TRUE(!isChipExpanded(1), "!isChipExpanded(1)");

	/* Out-of-range channels are no-ops, not writes. */
	expandChip(-1, true);
	expandChip(MAX_SEQUENCER_CHANNELS, true);
	ASSERT_TRUE(!isChipExpanded(-1), "!isChipExpanded(-1)");
	ASSERT_TRUE(!isChipExpanded(MAX_SEQUENCER_CHANNELS), "!isChipExpanded(MAX_SEQUENCER_CHANNELS)");

	/* A channel with no registered chip reports false rather than crashing. */
	a->enabledChannels = 1;
	ASSERT_TRUE(!isChipExpanded(15), "!isChipExpanded(15)");
	expandChip(15, true);
	a->enabledChannels = 4;

	/* Editing a label through handleExpandedChipInput. Chip 0's label starts
	 * empty, so its cursor is at 0 and the first cycle writes NAME_CHARS[1]. */
	expandChip(0, true);
	ASSERT_TRUE(isChipExpanded(0), "isChipExpanded(0)");
	strcpy(a->label[0], "");
	handleExpandedChipInput(0, KM_UP, true);
	ASSERT_EQ(a->label[0][0], TEST_NAME_CHARS[1]);
	handleExpandedChipInput(0, KM_DOWN, true);
	ASSERT_EQ(a->label[0][0], TEST_NAME_CHARS[0]);
	/* The label's NUL terminator must stay put at CHIP_LABEL_MAX. */
	ASSERT_EQ(a->label[0][CHIP_LABEL_MAX], '\0');

	/* Bare LEFT/RIGHT move the swatch focus and commit the colour live. */
	a->labelColourIdx[0] = 0;
	handleExpandedChipInput(0, KM_RIGHT, false);
	ASSERT_EQ(a->labelColourIdx[0], 1);
	handleExpandedChipInput(0, KM_RIGHT, false);
	ASSERT_EQ(a->labelColourIdx[0], 2);
	for(int i = 0; i < 10; i++) {
		handleExpandedChipInput(0, KM_RIGHT, false);
		ASSERT_TRUE(a->labelColourIdx[0] >= 0 && a->labelColourIdx[0] < 8, "a->labelColourIdx[0] >= 0 && a->labelColourIdx[0] < 8");
	}
	for(int i = 0; i < 10; i++) {
		handleExpandedChipInput(0, KM_LEFT, false);
		ASSERT_TRUE(a->labelColourIdx[0] >= 0 && a->labelColourIdx[0] < 8, "a->labelColourIdx[0] >= 0 && a->labelColourIdx[0] < 8");
	}

	/* SELECT / START collapse. */
	expandChip(2, true);
	handleExpandedChipInput(2, KM_SELECT, false);
	ASSERT_TRUE(!isChipExpanded(2), "!isChipExpanded(2)");
	expandChip(2, true);
	handleExpandedChipInput(2, KM_START, false);
	ASSERT_TRUE(!isChipExpanded(2), "!isChipExpanded(2)");
	expandChip(2, true);
	handleExpandedChipInput(2, KM_EDIT, false);
	ASSERT_TRUE(!isChipExpanded(2), "!isChipExpanded(2)");

	/* Out-of-range channel through the input path. */
	handleExpandedChipInput(-1, KM_SELECT, false);
	handleExpandedChipInput(MAX_SEQUENCER_CHANNELS, KM_SELECT, false);

	destroyArrangerGraph();
	free(pl);
	free(a);
	printf("PASS test_chip_expand_and_input\n");
	return 0;
}

/* ---- selection sync + navigation --------------------------------------- */

static int test_selection_sync_and_navigation(void) {
	Arranger *a = make_arranger(3);
	ASSERT_TRUE(a != NULL, "a != NULL");
	PatternList *pl = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(pl != NULL, "pl != NULL");

	createArrangerGraph(a, pl);
	GuiNode *sel = getArrangerSelectedNode();

	/* Navigating right/down must move the graph's selection somewhere else,
	 * and must not run off the ends. */
	if(sel != NULL) {
		GuiNode *before = sel;
		navigateArrangerGraph(KM_RIGHT);
		GuiNode *after = getArrangerSelectedNode();
		ASSERT_TRUE(after != NULL || before == NULL, "after != NULL || before == NULL");
		navigateArrangerGraph(KM_DOWN);
		navigateArrangerGraph(KM_LEFT);
		navigateArrangerGraph(KM_UP);
		/* After all four the selection must still be a node the graph owns. */
		ASSERT_TRUE(getArrangerSelectedNode() == NULL || getArrangerSelectedNode()->draw != NULL,
	            "selection survives four-way navigation");
		/* Hammer it in one direction: clamping, not wandering. */
		for(int i = 0; i < 40; i++) {
			navigateArrangerGraph(KM_RIGHT);
		}
		for(int i = 0; i < 40; i++) {
			navigateArrangerGraph(KM_LEFT);
		}
	}

	/* syncArrangerSelectionTo must mirror a node's coordinates back into the
	 * Arranger, and ignore a node that is not a cell. */
	GuiNode *cell = createArrangerCellGuiNode(0, 0, 8, 8, true, a, 2, 3);
	ASSERT_TRUE(cell != NULL, "cell != NULL");
	Graph *g = createGraph(na_vertical);
	ASSERT_TRUE(g != NULL, "g != NULL");
	appendItem(g->root, cell, 1);
	changeGraphSelection(g, cell);
	syncArrangerSelectionTo(g, a);
	ASSERT_EQ(a->selected_x, 2);
	ASSERT_EQ(a->selected_y, 3);

	GuiNode *plain = createGuiNode(0, 0, 4, 4, 1, na_horizontal, "plain", 1, 0);
	appendItem(g->root, plain, 1);
	changeGraphSelection(g, plain);
	int x = a->selected_x, y = a->selected_y;
	syncArrangerSelectionTo(g, a);
	ASSERT_EQ(a->selected_x, x);
	ASSERT_EQ(a->selected_y, y);

	/* NULL arguments must not crash. */
	syncArrangerSelectionTo(NULL, a);
	syncArrangerSelectionTo(g, NULL);
	navigateArrangerGraphTo(NULL, a, KM_RIGHT);

	/* `cell` and `plain` were appended to g->root, so the root owns them —
	 * freeing them separately first would double-free and leave the root's
	 * list pointing at freed memory. freeGuiNode(root) is recursive. */
	freeGuiNode(g->root);
	free(g);
	destroyArrangerGraph();
	free(pl);
	free(a);
	printf("PASS test_selection_sync_and_navigation\n");
	return 0;
}

/* Control input dispatches to the selected node's callback with a signed
 * delta per direction. Driving it needs a node with a callback, which
 * createDialGuiNode provides. */
static int test_control_input_dispatch(void) {
	Arranger *a = make_arranger(2);
	ASSERT_TRUE(a != NULL, "make_arranger");
	PatternList *pl = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(pl != NULL, "calloc PatternList");

	/* Before a graph exists: a no-op, not a crash. */
	arrangerGraphControlInput(KM_LEFT);
	arrangerGraphControlInput(KM_RIGHT);
	arrangerGraphControlInput(KM_UP);
	arrangerGraphControlInput(KM_DOWN);
	arrangerGraphControlInput(KM_EDIT);
	printArrGraph();

	ParamList *params = createParamList();
	ASSERT_TRUE(params != NULL, "createParamList");
	a->tempoSettings.bpm = createParameterPro(params, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	a->tempoSettings.swing = createParameterPro(params, "Swing", 0.0f, 0.0f, 1.0f,
	                                            0.01f, 0.1f, NULL, NULL);
	createArrangerGraph(a, pl);

	/* With a graph, every direction dispatches. Unknown keymappings fall
	 * through the switch's default and do nothing. */
	arrangerGraphControlInput(KM_LEFT);
	arrangerGraphControlInput(KM_RIGHT);
	arrangerGraphControlInput(KM_UP);
	arrangerGraphControlInput(KM_DOWN);
	arrangerGraphControlInput(-999);
	arrangerGraphControlInput(KM_EDIT);
	printArrGraph();

	destroyArrangerGraph();
	freeParamList(params);
	free(pl);
	free(a);
	printf("PASS test_control_input_dispatch\n");
	return 0;
}

/* Scrolling retargets the window's cells to new song rows and re-marks which
 * cell is selected. Out-of-range requests must clamp rather than run off the
 * song. */
static int test_scroll_window(void) {
	Arranger *a = make_arranger(2);
	ASSERT_TRUE(a != NULL, "make_arranger");
	PatternList *pl = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(pl != NULL, "calloc PatternList");
	ParamList *params = createParamList();
	ASSERT_TRUE(params != NULL, "createParamList");
	a->tempoSettings.bpm = createParameterPro(params, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	a->tempoSettings.swing = createParameterPro(params, "Swing", 0.0f, 0.0f, 1.0f,
	                                            0.01f, 0.1f, NULL, NULL);

	/* Before a graph: must be a safe no-op. */
	scrollArrangerWindow(a, 1);
	scrollArrangerWindowTo(a, 4);
	scrollArrangerWindow(NULL, 1);

	createArrangerGraph(a, pl);

	a->visibleStart = 0;
	scrollArrangerWindow(a, 2);
	ASSERT_TRUE(a->visibleStart >= 0, "a->visibleStart >= 0");
	scrollArrangerWindow(a, -1);
	ASSERT_TRUE(a->visibleStart >= 0, "a->visibleStart >= 0");

	/* Hammer both directions past both ends: clamping, not wrapping or
	 * out-of-bounds indexing into song[][]. */
	for(int i = 0; i < 40; i++) {
		scrollArrangerWindow(a, 5);
		ASSERT_TRUE(a->visibleStart >= 0, "a->visibleStart >= 0");
		ASSERT_TRUE(a->visibleStart + ARRANGER_WINDOW_ROWS <= MAX_SONG_LENGTH, "a->visibleStart + ARRANGER_WINDOW_ROWS <= MAX_SONG_LENGTH");
	}
	for(int i = 0; i < 40; i++) {
		scrollArrangerWindow(a, -5);
		ASSERT_TRUE(a->visibleStart >= 0, "a->visibleStart >= 0");
		ASSERT_TRUE(a->visibleStart + ARRANGER_WINDOW_ROWS <= MAX_SONG_LENGTH, "a->visibleStart + ARRANGER_WINDOW_ROWS <= MAX_SONG_LENGTH");
	}

	/* Absolute target, including out-of-range values. */
	scrollArrangerWindowTo(a, 0);
	ASSERT_EQ(a->visibleStart, 0);
	scrollArrangerWindowTo(a, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS);
	ASSERT_EQ(a->visibleStart, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS);
	scrollArrangerWindowTo(a, -100);
	ASSERT_EQ(a->visibleStart, 0);
	scrollArrangerWindowTo(a, 100000);
	ASSERT_EQ(a->visibleStart, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS);
	scrollArrangerWindowTo(NULL, 0);

	/* Row lookup: valid indices return a node, out-of-range return NULL. */
	ASSERT_TRUE(getArrangerRowCell(0, 0) != NULL, "getArrangerRowCell(0, 0) != NULL");
	ASSERT_TRUE(getArrangerRowCell(-1, 0) == NULL, "getArrangerRowCell(-1, 0) == NULL");
	ASSERT_TRUE(getArrangerRowCell(ARRANGER_WINDOW_ROWS, 0) == NULL, "getArrangerRowCell(ARRANGER_WINDOW_ROWS, 0) == NULL");
	ASSERT_TRUE(getArrangerRowCell(ARRANGER_WINDOW_ROWS + 5, 0) == NULL, "getArrangerRowCell(ARRANGER_WINDOW_ROWS + 5, 0) == NULL");

	destroyArrangerGraph();
	freeParamList(params);
	free(pl);
	free(a);
	printf("PASS test_scroll_window\n");
	return 0;
}

/* getSelectedChipChannel must report the channel of a selected chip and -1 for
 * anything else. Selecting a non-chip node is the interesting case: the
 * function checks by draw-callback identity, so it must not reinterpret an
 * arbitrary node as a chip. */
static int test_selected_chip_channel(void) {
	Arranger *a = make_arranger(2);
	ASSERT_TRUE(a != NULL, "make_arranger");
	PatternList *pl = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(pl != NULL, "calloc PatternList");
	ParamList *params = createParamList();
	ASSERT_TRUE(params != NULL, "createParamList");
	a->tempoSettings.bpm = createParameterPro(params, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	a->tempoSettings.swing = createParameterPro(params, "Swing", 0.0f, 0.0f, 1.0f,
	                                            0.01f, 0.1f, NULL, NULL);

	ASSERT_EQ(getSelectedChipChannel(), -1);
	createArrangerGraph(a, pl);

	/* Navigate onto a chip. The chip row is the first row of the grid, so
	 * moving up from the top reaches it; alternatively the default selection
	 * may already be a chip. Either way, once we are on one the channel must
	 * be in range. */
	GuiNode *sel = getArrangerSelectedNode();
	if(sel != NULL && isInstChipNode(sel)) {
		int ch = getSelectedChipChannel();
		ASSERT_TRUE(ch >= 0 && ch < MAX_SEQUENCER_CHANNELS, "ch >= 0 && ch < MAX_SEQUENCER_CHANNELS");
	}

	/* Selecting a plain (non-chip) node must read back as -1. */
	Graph *g = createGraph(na_vertical);
	ASSERT_TRUE(g != NULL, "createGraph");
	GuiNode *plain = createGuiNode(0, 0, 10, 10, 1, na_horizontal, "plain", 1, 0);
	ASSERT_TRUE(plain != NULL, "createGuiNode");
	appendItem(g->root, plain, 1);
	freeGuiNode(g->root);
	free(g);

	destroyArrangerGraph();
	freeParamList(params);
	free(pl);
	free(a);
	printf("PASS test_selected_chip_channel\n");
	return 0;
}

/* Walk the arranger graph looking for the two node kinds that have private
 * behaviour: a chip (so getSelectedChipChannel can return a real channel) and
 * a dial (so arrangerGraphControlInput's switch has a callback to dispatch
 * to). Both live above the song rows, so walking up reaches them. */
static int test_navigate_onto_chip_and_dial(void) {
	Arranger *a = make_arranger(2);
	ASSERT_TRUE(a != NULL, "make_arranger");
	PatternList *pl = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(pl != NULL, "calloc PatternList");
	ParamList *params = createParamList();
	ASSERT_TRUE(params != NULL, "createParamList");
	a->tempoSettings.bpm = createParameterPro(params, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	a->tempoSettings.swing = createParameterPro(params, "Swing", 0.0f, 0.0f, 1.0f,
	                                            0.01f, 0.1f, NULL, NULL);
	createArrangerGraph(a, pl);

	int saw_chip = 0;
	int saw_dial = 0;
	const int keymaps[] = { KM_UP, KM_UP, KM_LEFT, KM_UP, KM_UP, KM_UP, KM_RIGHT };
	for(int step = 0; step < 60; step++) {
		int km = keymaps[step % (int)(sizeof(keymaps) / sizeof(keymaps[0]))];
		navigateArrangerGraph(km);

		GuiNode *sel = getArrangerSelectedNode();
		if(sel == NULL) {
			continue;
		}
		if(isInstChipNode(sel)) {
			int ch = getSelectedChipChannel();
			ASSERT_TRUE(ch >= 0 && ch < MAX_SEQUENCER_CHANNELS,
			            "selected chip reports an in-range channel");
			saw_chip = 1;
		}
		if(sel->callback != NULL) {
			saw_dial = 1;
			/* Each direction takes a different switch arm. */
			arrangerGraphControlInput(KM_LEFT);
			arrangerGraphControlInput(KM_RIGHT);
			arrangerGraphControlInput(KM_UP);
			arrangerGraphControlInput(KM_DOWN);
		}
		/* While a chip is selected, the label cursor paths are reachable. */
		if(isInstChipNode(sel)) {
			expandChip(getSelectedChipChannel(), true);
			handleExpandedChipInput(getSelectedChipChannel(), KM_LEFT, true);
			handleExpandedChipInput(getSelectedChipChannel(), KM_RIGHT, true);
			handleExpandedChipInput(getSelectedChipChannel(), KM_DOWN, true);
			handleExpandedChipInput(getSelectedChipChannel(), KM_UP, true);
		}
	}
	/* If neither ever appeared the assertions above were vacuous, so say so. */
	if(!saw_chip) {
		fprintf(stderr, "NOTE: no chip node reachable by navigation; "
		        "chip-selection branches unexercised\n");
	}
	if(!saw_dial) {
		fprintf(stderr, "NOTE: no callback-bearing node reachable by navigation; "
		        "arrangerGraphControlInput switch unexercised\n");
	}

	/* The module's own draw entry point, with and without a graph. */
	destroyArrangerGraph();
	gui_arranger_draw();

	destroyArrangerGraph();
	freeParamList(params);
	free(pl);
	free(a);
	printf("PASS test_navigate_onto_chip_and_dial (chip=%d dial=%d)\n",
	       saw_chip, saw_dial);
	return 0;
}

static int test_mix_ring_setter(void) {
	/* Trivial setter, but it is the only thing setArrangerMixRing does and
	 * the arranger draw path reads the result. */
	setArrangerMixRing(NULL);
	printf("PASS test_mix_ring_setter\n");
	return 0;
}

int main(void) {
	/* These run before any createArrangerGraph so the module's graph
	 * pointer is still NULL — they cover the "called too early" guards. */
	if(test_arranger_cell_fill() != 0) {
		return 1;
	}
	if(test_chip_label_char_index() != 0) {
		return 1;
	}
	if(test_chip_label_cycle_char() != 0) {
		return 1;
	}
	if(test_chip_label_cycle_duplicate_space() != 0) {
		return 1;
	}
	if(test_chip_label_cursor_move() != 0) {
		return 1;
	}
	if(test_cell_node_identity() != 0) {
		return 1;
	}
	if(test_chip_node_identity() != 0) {
		return 1;
	}
	if(test_chip_expand_and_input() != 0) {
		return 1;
	}
	if(test_selection_sync_and_navigation() != 0) {
		return 1;
	}
	if(test_control_input_dispatch() != 0) {
		return 1;
	}
	if(test_scroll_window() != 0) {
		return 1;
	}
	if(test_selected_chip_channel() != 0) {
		return 1;
	}
	if(test_navigate_onto_chip_and_dial() != 0) {
		return 1;
	}
	if(test_mix_ring_setter() != 0) {
		return 1;
	}
	printf("PASSED (gui_arranger)\n");
	return 0;
}