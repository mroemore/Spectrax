/* test_graph_nav.c — graph_navigation unit tests. Layered:
 *   Task 1: test scaffolding (rect wiring + tree shape). GREEN.
 *   Task 2: navigateGraphRefined + changeGraphSelection fix.
 *   Task 3: chip-row nav — RIGHT walks chip->chip, DOWN lands on the
 *           grid row beneath. Chips are built with NULL vm/arranger to
 *           keep the test free of raylib/voice init; the chip draw
 *           guards against NULL refs.
 *
 * See docs/superpowers/plans/2026-08-26-nav-refinement.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "graph_gui.h"
#include "gui.h"  /* ARRANGER_WINDOW_ROWS, createArrangerCellGuiNode, etc. */

/* ArrangerCellGuiNode's struct layout lives in gui.c (not gui.h); mirror
 * it locally so the edge-scroll test can flip `row` on a cell to simulate
 * the production retarget walk (see retargetWindowCells in gui.c). */
typedef struct {
    GuiNode base;
    Arranger *arranger;
    int ch;
    int row;
} TestArrangerCellGuiNode;

/* Task 3: forward-decls for the chip creator + recogniser so we don't
 * need to pull raylib/voice into the test TU. Symbols resolve from
 * core_lib (which already includes gui.o). */
struct VoiceManager;
/* Arranger is already typedef'd via graph_gui.h's sequencer.h include chain. */
GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, struct VoiceManager *vm, int channel, Arranger *arranger);
bool isInstChipNode(const GuiNode *n);

/* Task 3: forward-decl scrollArrangerWindowTo (gui.h declares it
 * after the chip section). The symbol resolves from gui.c via
 * core_lib. */
void scrollArrangerWindowTo(Arranger *a, int targetStart);

/* Task 1 (arranger window rework): cell node primitives. The creator
 * takes an Arranger* + channel + row; the recogniser + coord getter let
 * later nav tasks identify which song cell is currently selected without
 * reaching into the GuiNode internals. */
GuiNode *createArrangerCellGuiNode(int x, int y, int w, int h, bool selected, Arranger *arranger, int ch, int row);
bool isArrangerCellNode(const GuiNode *n);
void getArrangerCellCoords(const GuiNode *n, int *x, int *y);

/* Task 2 (arranger window rework): window scroll + row-cell getter.
 * scrollArrangerWindow adjusts arranger->visibleStart (clamped to
 * [0, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS]) and re-targets the
 * window's cell rows. getArrangerRowCell returns the cell GuiNode at
 * the given visible (rowIdx, ch). Both must be safe to call without a
 * built arranger graph (the unit test calls scrollArrangerWindow on a
 * zero-init Arranger to keep the test free of raylib/voice init). */
void scrollArrangerWindow(Arranger *a, int delta);
GuiNode *getArrangerRowCell(int rowIdx, int ch);

/* Task 3 (arranger window rework): syncArrangerSelectionTo writes
 * arranger->selected_x/selected_y from g->selected and fires the
 * selection callbacks (onCellSelect + onPatternSelection). The
 * pure-state test exercises it with a built graph and asserts both
 * the cursor write AND the callback dispatch. */
void syncArrangerSelectionTo(Graph *g, Arranger *a);

/* Task 3 (arranger window rework): navigateArrangerGraphTo is the
 * canonical (g, a, km) nav pipeline. Runs navigateGraphRefined,
 * edge-scrolls the visible window if the selection lands at the
 * top/bottom row, then syncs selection + callbacks. The pure-state
 * test drives it directly with a locally-built graph + arranger —
 * no global file-statics needed. */
void navigateArrangerGraphTo(Graph *g, Arranger *a, int keymapping);

/* ASSERT_* macros — verbatim copy of the style used in
 * tests/dsp/test_modsystem.c (same arity-dispatch trick, same
 * `return 1` on failure so main can sum fails). */
#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_2(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)
#define ASSERT_EQ_3(actual, expected, msg) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: %s — expected %lld, got %lld\n", \
                __FILE__, __LINE__, (msg), _e, _a); \
        return 1; \
    } \
} while (0)
#define ASSERT_EQ_GET(_1, _2, _3, NAME, ...) NAME
#define ASSERT_EQ(...) ASSERT_EQ_GET(__VA_ARGS__, ASSERT_EQ_3, ASSERT_EQ_2, MISSING)(__VA_ARGS__)

#define ASSERT_NEAR_3(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)
#define ASSERT_NEAR_4(actual, expected, tol, msg) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: %s — expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, (msg), _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)
#define ASSERT_NEAR_GET(_1, _2, _3, _4, NAME, ...) NAME
#define ASSERT_NEAR(...) ASSERT_NEAR_GET(__VA_ARGS__, ASSERT_NEAR_4, ASSERT_NEAR_3, MISSING, MISSING)(__VA_ARGS__)

#define ASSERT_EQ_F(actual, expected, msg) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > 1e-4f) { \
        fprintf(stderr, "FAIL %s:%d: %s — expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, (msg), _e, _a, 1e-4f); \
        return 1; \
    } \
} while (0)

static int test_window_scale_helpers(void) {
	ASSERT_EQ_F(nextWholeScale(1.0f), 2.0f, "1.0 whole-up -> 2");
	ASSERT_EQ_F(nextWholeScale(1.5f), 2.0f, "1.5 whole-up -> 2");
	ASSERT_EQ_F(nextWholeScale(3.0f), 4.0f, "3.0 whole-up -> 4");
	ASSERT_EQ_F(prevWholeScale(2.0f), 1.0f, "2.0 whole-down -> 1");
	ASSERT_EQ_F(prevWholeScale(1.75f), 1.0f, "1.75 whole-down -> 1");
	ASSERT_EQ_F(prevWholeScale(1.0f), 0.25f, "1.0 whole-down -> 0.25 (clamped floor)");
	printf("PASS test_window_scale_helpers\n");
	return 0;
}

/* freeGuiNode recurses through items + itemWeights, frees name. */
static void teardown_graph(Graph *g) {
    freeGuiNode(g->root);
    free(g);
}

/* Two-column graph: left col A/B, right col C/D. Container rects are
 * explicit at createGuiNode time, but appendItem triggers
 * reflowCoordinates which overwrites the leaf rects from parent
 * geometry + weights. After all wiring, the leaves are forced to the
 * explicit (x,y,w,h) the geometric nav expects:
 *   A: 10,10 / 40x20   (top-left)
 *   B: 10,40 / 40x20   (bottom-left)
 *   C: 100,10 / 40x20  (top-right)
 *   D: 100,40 / 40x20  (bottom-right) */
static Graph *build_2col_graph(void) {
    Graph *g = createGraph(na_vertical);
    /* left column: A (top), B (bottom) */
    GuiNode *a = createGuiNode(10, 10, 40, 20, 2, na_vertical, "A", true, false);
    GuiNode *b = createGuiNode(10, 40, 40, 20, 2, na_vertical, "B", true, false);
    /* right column: C (top), D (bottom) */
    GuiNode *c = createGuiNode(100, 10, 40, 20, 2, na_vertical, "C", true, false);
    GuiNode *d = createGuiNode(100, 40, 40, 20, 2, na_vertical, "D", true, false);
    /* two section containers, one per column */
    GuiNode *colL = createGuiNode(10, 10, 40, 60, 2, na_vertical, "colL", false, false);
    GuiNode *colR = createGuiNode(100, 10, 40, 60, 2, na_vertical, "colR", false, false);
    appendItem(colL, a, 1);
    appendItem(colL, b, 1);
    appendItem(colR, c, 1);
    appendItem(colR, d, 1);
    appendItem(g->root, colL, 1);
    appendItem(g->root, colR, 1);
    /* explicit geometry — appendItem reflows, so set leaf rects AFTER wiring */
    a->x = 10; a->y = 10; a->w = 40; a->h = 20;
    b->x = 10; b->y = 40; b->w = 40; b->h = 20;
    c->x = 100; c->y = 10; c->w = 40; c->h = 20;
    d->x = 100; d->y = 40; d->w = 40; d->h = 20;
    return g;
}

static int test_trivial_rect_wiring(void) {
    Graph *g = build_2col_graph();
    /* Root exists and was created with na_vertical. */
    ASSERT_TRUE(g->root != NULL, "root");
    ASSERT_EQ(g->root->nodeAlignment, na_vertical);
    /* Root's first item is the left column container (colL),
     * which is non-selectable but holds selectable leaves. */
    GuiNode *colL = *(GuiNode **)g->root->items->head->data;
    ASSERT_TRUE(colL != NULL, "first root child is colL");
    ASSERT_TRUE(!colL->selectable, "colL itself is not selectable");
    ASSERT_TRUE(colL->hasSelectableItems, "colL contains selectable leaves");
    /* colL's first leaf is A; it is selectable. */
    GuiNode *a = *(GuiNode **)colL->items->head->data;
    ASSERT_TRUE(a->selectable, "A is selectable");
    ASSERT_TRUE(strcmp(a->name, "A") == 0, "first leaf is named A");
    /* Task 2 adaptation: build_2col_graph now sets explicit rects
     * AFTER all appendItem calls (because appendItem reflows and
     * would otherwise overwrite them). The rects now persist, so we
     * pin exact values used by the geometric nav. */
    ASSERT_EQ(a->x, 10);
    ASSERT_EQ(a->y, 10);
    ASSERT_EQ(a->w, 40);
    ASSERT_EQ(a->h, 20);
    /* Names match across the helper so navigation tests in Task 2
     * can rely on them. */
    GuiNode *b = *(GuiNode **)colL->items->head->next->data;
    ASSERT_TRUE(strcmp(b->name, "B") == 0, "second leaf in colL is B");
    ASSERT_EQ(b->x, 10);
    ASSERT_EQ(b->y, 40);
    ASSERT_EQ(b->w, 40);
    ASSERT_EQ(b->h, 20);
    GuiNode *colR = *(GuiNode **)g->root->items->head->next->data;
    ASSERT_TRUE(strcmp(colR->name, "colR") == 0, "second root child is colR");
    GuiNode *c = *(GuiNode **)colR->items->head->data;
    ASSERT_TRUE(strcmp(c->name, "C") == 0, "first leaf in colR is C");
    ASSERT_EQ(c->x, 100);
    ASSERT_EQ(c->y, 10);
    ASSERT_EQ(c->w, 40);
    ASSERT_EQ(c->h, 20);
    GuiNode *d = *(GuiNode **)colR->items->head->next->data;
    ASSERT_TRUE(strcmp(d->name, "D") == 0, "second leaf in colR is D");
    ASSERT_EQ(d->x, 100);
    ASSERT_EQ(d->y, 40);
    ASSERT_EQ(d->w, 40);
    ASSERT_EQ(d->h, 20);
    teardown_graph(g);
    printf("PASS test_trivial_rect_wiring\n");
    return 0;
}

/* ----- Task 2: navigateGraphRefined + changeGraphSelection fix ----- */

/* customNav on a node intercepts the key and returns true -> geometry skipped */
static bool interceptNav(void *self, int keymapping) {
    (void)self; (void)keymapping;
    return true;
}
/* customNav returning false falls through to geometry */
static bool passthroughNav(void *self, int keymapping) {
    (void)self; (void)keymapping;
    return false;
}

static int test_first_selection(void) {
    Graph *g = build_2col_graph();
    ASSERT_TRUE(g->selected == NULL, "no selection initially");
    navigateGraphRefined(g, KM_DOWN);
    ASSERT_TRUE(g->selected != NULL, "first selection established");
    ASSERT_TRUE(strcmp(g->selected->name, "A") == 0, "first DFS-pre-order leaf selected");
    teardown_graph(g);
    printf("PASS test_first_selection\n");
    return 0;
}

static int test_col_up_down(void) {
    Graph *g = build_2col_graph();
    navigateGraphRefined(g, KM_DOWN); /* selects A */
    navigateGraphRefined(g, KM_DOWN);
    ASSERT_TRUE(strcmp(g->selected->name, "B") == 0, "down moves to B in the column");
    navigateGraphRefined(g, KM_UP);
    ASSERT_TRUE(strcmp(g->selected->name, "A") == 0, "up moves back to A");
    teardown_graph(g);
    printf("PASS test_col_up_down\n");
    return 0;
}

static int test_row_left_right(void) {
    Graph *g = build_2col_graph();
    navigateGraphRefined(g, KM_DOWN); /* A */
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(strcmp(g->selected->name, "C") == 0, "right moves to C");
    navigateGraphRefined(g, KM_LEFT);
    ASSERT_TRUE(strcmp(g->selected->name, "A") == 0, "left moves back to A");
    teardown_graph(g);
    printf("PASS test_row_left_right\n");
    return 0;
}

static int test_boundary_no_move(void) {
    Graph *g = build_2col_graph();
    navigateGraphRefined(g, KM_DOWN); /* A */
    navigateGraphRefined(g, KM_UP);   /* top boundary: no move */
    ASSERT_TRUE(strcmp(g->selected->name, "A") == 0, "no move at top edge");
    navigateGraphRefined(g, KM_LEFT); /* left edge: no move */
    ASSERT_TRUE(strcmp(g->selected->name, "A") == 0, "no move at left edge");
    teardown_graph(g);
    printf("PASS test_boundary_no_move\n");
    return 0;
}

static int test_cross_section_jump(void) {
    /* from B (left column bottom), RIGHT must pick D (nearest right-column
     * node by center alignment), not C */
    Graph *g = build_2col_graph();
    navigateGraphRefined(g, KM_DOWN);
    navigateGraphRefined(g, KM_DOWN); /* B */
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(strcmp(g->selected->name, "D") == 0, "right from B lands on D (center-aligned)");
    teardown_graph(g);
    printf("PASS test_cross_section_jump\n");
    return 0;
}

/* Intruder E sits between the columns on the TOP row: closer in travel
 * distance from B, but outside the perpendicular cone (centerY 20 vs B's
 * 50; cone limit = B.h/2 + E.h/2 + tol = 24). The cone must exclude it. */
static Graph *build_cone_graph(void) {
    Graph *g = createGraph(na_vertical);
    GuiNode *colL = createGuiNode(10, 40, 40, 20, 2, na_vertical, "colL", false, false);
    GuiNode *b = createGuiNode(10, 40, 40, 20, 2, na_vertical, "B", true, false);
    GuiNode *e = createGuiNode(70, 10, 20, 20, 2, na_vertical, "E", true, false);
    GuiNode *colR = createGuiNode(100, 40, 40, 20, 2, na_vertical, "colR", false, false);
    GuiNode *d = createGuiNode(100, 40, 40, 20, 2, na_vertical, "D", true, false);
    appendItem(colL, b, 1);
    appendItem(g->root, colL, 1);
    appendItem(g->root, e, 1);
    appendItem(colR, d, 1);
    appendItem(g->root, colR, 1);
    b->x = 10; b->y = 40; b->w = 40; b->h = 20;
    e->x = 70; e->y = 10; e->w = 20; e->h = 20;
    d->x = 100; d->y = 40; d->w = 40; d->h = 20;
    return g;
}

static int test_cone_excludes_misaligned(void) {
    Graph *g = build_cone_graph();
    navigateGraphRefined(g, KM_DOWN); /* first selection -> B (DFS order) */
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(strcmp(g->selected->name, "D") == 0,
                "closer-but-misaligned E excluded by the cone; aligned D wins");
    teardown_graph(g);
    printf("PASS test_cone_excludes_misaligned\n");
    return 0;
}

static int test_custom_nav_precedence(void) {
    Graph *g = build_2col_graph();
    GuiNode *colL = *(GuiNode **)g->root->items->head->data;
    GuiNode *a = *(GuiNode **)colL->items->head->data;
    a->customNav = interceptNav;
    navigateGraphRefined(g, KM_DOWN); /* selects A (first selection) */
    navigateGraphRefined(g, KM_DOWN); /* interceptNav returns true -> no move */
    ASSERT_TRUE(strcmp(g->selected->name, "A") == 0, "customNav intercepts down");
    teardown_graph(g);
    printf("PASS test_custom_nav_precedence\n");
    return 0;
}

static int test_custom_nav_fallthrough(void) {
    Graph *g = build_2col_graph();
    GuiNode *colL = *(GuiNode **)g->root->items->head->data;
    GuiNode *a = *(GuiNode **)colL->items->head->data;
    a->customNav = passthroughNav;
    navigateGraphRefined(g, KM_DOWN);
    navigateGraphRefined(g, KM_DOWN); /* passthrough -> geometry moves to B */
    ASSERT_TRUE(strcmp(g->selected->name, "B") == 0, "passthrough falls through to geometry");
    teardown_graph(g);
    printf("PASS test_custom_nav_fallthrough\n");
    return 0;
}

static int test_null_safety(void) {
    navigateGraphRefined(NULL, KM_DOWN);           /* no crash */
    Graph *g = build_2col_graph();
    g->selected = NULL;
    navigateGraphRefined(g, KM_DOWN);              /* establishes first selection, no crash */
    ASSERT_TRUE(g->selected != NULL, "null-selection recovers");
    teardown_graph(g);
    printf("PASS test_null_safety\n");
    return 0;
}

/* ----- Task 3: instrument-chip row nav -----
 *
 * Mirrors the production layout:
 *   root (na_vertical)
 *     gridColumn (na_vertical, wrapper)
 *       chipRow (na_horizontal) -- [chip0, chip1, chip2]  (selectable, weight 1 each)
 *       grid   (na_vertical)    -- [gridCell0, gridCell1] (selectable, weight 1 each)
 *
 * chipRow sits ABOVE grid in the column; chipRow is horizontal so
 * RIGHT within chips walks chip0 -> chip1 -> chip2; DOWN from a chip
 * should land in the topmost grid cell (the geometrically nearest row
 * below).
 *
 * Chips are built with NULL vm/arranger. The draw function must
 * early-return on NULL so a stray nav test can run without raylib /
 * voice setup. We assert the early-return by calling draw directly
 * after nav to ensure no crash, then assert the chip recogniser works.
 */
static Graph *build_chip_row_graph(void) {
    Graph *g = createGraph(na_vertical);
    GuiNode *gridColumn = createGuiNode(0, 0, 100, 100, 2, na_vertical, "gcol", false, false);
    GuiNode *chipRow = createGuiNode(0, 0, 100, 30, 2, na_horizontal, "chipr", false, false);
    GuiNode *c0 = createInstChipGuiNode(10, 0, 30, 30, false, NULL, 0, NULL);
    GuiNode *c1 = createInstChipGuiNode(40, 0, 30, 30, false, NULL, 1, NULL);
    GuiNode *c2 = createInstChipGuiNode(70, 0, 30, 30, false, NULL, 2, NULL);
    appendItem(chipRow, c0, 1);
    appendItem(chipRow, c1, 1);
    appendItem(chipRow, c2, 1);
    GuiNode *grid = createGuiNode(0, 50, 100, 50, 2, na_vertical, "grid", false, false);
    GuiNode *g0 = createGuiNode(10, 50, 40, 20, 2, na_vertical, "g0", true, false);
    GuiNode *g1 = createGuiNode(60, 50, 40, 20, 2, na_vertical, "g1", true, false);
    appendItem(grid, g0, 1);
    appendItem(grid, g1, 1);
    appendItem(gridColumn, chipRow, 1);
    appendItem(gridColumn, grid, 8);
    appendItem(g->root, gridColumn, 1);
    /* pin rects AFTER all appendItem reflows */
    c0->x = 10; c0->y = 0; c0->w = 30; c0->h = 30;
    c1->x = 40; c1->y = 0; c1->w = 30; c1->h = 30;
    c2->x = 70; c2->y = 0; c2->w = 30; c2->h = 30;
    g0->x = 10; g0->y = 50; g0->w = 40; g0->h = 20;
    g1->x = 60; g1->y = 50; g1->w = 40; g1->h = 20;
    return g;
}

static int test_chip_row_nav(void) {
    Graph *g = build_chip_row_graph();
    ASSERT_TRUE(g != NULL, "chip-row graph built");
    /* First selection: DFS pre-order -> first chip (c0). */
    navigateGraphRefined(g, KM_DOWN);
    ASSERT_TRUE(g->selected != NULL, "first selection established");
    ASSERT_TRUE(isInstChipNode(g->selected), "first selected node is a chip");
    ASSERT_TRUE(strcmp(g->selected->name, "chip") == 0, "first chip name is 'chip'");
    /* RIGHT walks within chipRow: c0 -> c1 -> c2. */
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(isInstChipNode(g->selected), "RIGHT stays in chip row");
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(isInstChipNode(g->selected), "RIGHT stays in chip row");
    /* RIGHT at the end of chip row is a boundary — selection stays on c2. */
    navigateGraphRefined(g, KM_RIGHT);
    ASSERT_TRUE(isInstChipNode(g->selected), "RIGHT at right edge: still a chip");
    /* LEFT walks back to c1 then c0. */
    navigateGraphRefined(g, KM_LEFT);
    ASSERT_TRUE(isInstChipNode(g->selected), "LEFT within chip row");
    /* DOWN from a chip lands in the grid (the next selectable row in
     * the gridColumn container). We don't pin the exact name because
     * geometry depends on cone alignment; what matters is that we
     * leave the chip row. */
    navigateGraphRefined(g, KM_DOWN);
    ASSERT_TRUE(!isInstChipNode(g->selected),
                "DOWN from chip row leaves chip row");
    /* Invoking draw on a NULL-vm chip must not crash — this is the
     * contract that lets the nav test run without a VoiceManager. */
    GuiNode *chip0 = *(GuiNode **)((GuiNode **)g->root->items->head->data);
    GuiNode *chipRow = *(GuiNode **)chip0->items->head->data;
    GuiNode *c0 = *(GuiNode **)chipRow->items->head->data;
    if(c0->draw) c0->draw(c0);
    teardown_graph(g);
    printf("PASS test_chip_row_nav\n");
    return 0;
}

/* Regression: the chip row rendered as an empty cs.panel strip because
 * createInstChipGuiNode set `draw` but never `drawable` (initGuiNode
 * leaves drawable = 0). Chips were selectable + navigable but never
 * drawn. */
static int test_chip_node_is_drawable(void) {
    GuiNode *c = createInstChipGuiNode(10, 0, 30, 30, false, NULL, 0, NULL);
    ASSERT_TRUE(c != NULL, "chip created");
    ASSERT_TRUE(c->drawable, "chip node is drawable (draw will run)");
    ASSERT_TRUE(c->draw != NULL, "chip has a draw fn");
    free(c->name);
    freeList(c->items);
    freeList(c->itemWeights);
    free(c);
    printf("PASS test_chip_node_is_drawable\n");
    return 0;
}

/* Regression: initGuiNode left `callback` uninitialised (malloc garbage).
 * The legacy createArrangerGuiNode then had a non-NULL garbage callback
 * pointer, and arrangerGraphControlInput(KM_*) called it -> SEGV on
 * EDIT+arrows over the grid. initGuiNode must always NULL the callback. */
static int test_init_gui_node_null_callback(void) {
    GuiNode n;
    ASSERT_TRUE(initGuiNode(&n, 0, 0, 50, 50, 0, na_vertical, "probe", false, false), "init ok");
    ASSERT_TRUE(n.callback == NULL, "callback defaults to NULL");
    free(n.name);
    freeList(n.items);
    freeList(n.itemWeights);
    printf("PASS test_init_gui_node_null_callback\n");
    return 0;
}

/* ----- Task 2 (arranger window rework): scrollArrangerWindow ----- *
 *
 * Pure-state test: scrollArrangerWindow operates on `arranger->visibleStart`
 * with no dependency on the graph being built (it has its own NULL-graph
 * early-return inside gui.c so the production path is still safe). The
 * test exercises the clamp boundaries so a regression in either direction
 * shows up immediately. ARRANGER_WINDOW_ROWS lives in gui.h which pulls
 * raylib — hard-code the literal here to keep this TU header-light.
 */
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
    a.visibleStart = MAX_SONG_LENGTH - 8; /* ARRANGER_WINDOW_ROWS */
    scrollArrangerWindow(&a, 3);
    ASSERT_EQ(a.visibleStart, MAX_SONG_LENGTH - 8, "clamped at max");
    printf("PASS test_scroll_arranger_window\n");
    return 0;
}

/* ----- Task 1 (arranger window rework): cell node primitive -----
 *
 * createArrangerCellGuiNode builds a drawable, selectable GuiNode that
 * renders one arranger song cell. isArrangerCellNode identifies them
 * by draw fn pointer. getArrangerCellCoords reads the (channel, row)
 * back out without forcing callers to know the struct internals.
 *
 * The test allocates a stub Arranger by hand (memset to zero, then
 * populate the few fields the draw path consults) so we don't need
 * to spin up createArranger() / a VoiceManager / pattern state. The
 * draw fn never actually runs in the test — the test only verifies
 * the node was built, recognised, and exposes its coords.
 */
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

/* ----- Task 3 (arranger window rework): edge-scroll + selection sync -----
 *
 * The three new tests exercise the per-frame nav pipeline:
 *   - arrangerWindowStart(a, row): pure-state clamp helper used by the
 *     main loop playhead-follow rule.
 *   - syncArrangerSelectionTo(a, agui): walks the current GuiNode
 *     selection, reads (ch, row) off it, writes cursorX/cursorY +
 *     visibleStart.
 *   - navigateArrangerGraphTo(KM_*): runs navigateGraphRefined, then
 *     syncArrangerSelectionTo so the Arranger state always matches the
 *     visual cursor.
 *
 * These functions live in gui.c and take raylib-coupled types, but the
 * test never invokes a draw / raylib call so it stays header-light. */

/* scrollArrangerWindowTo jumps visibleStart to an absolute target and
 * re-targets the graph's cell rows. Pure-state test (no graph built)
 * exercises the clamp boundaries: under-clamp, in-range, over-clamp,
 * and no-op when the target equals the current visibleStart.
 */
static int test_scroll_arranger_window_to(void) {
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 2;
    a.visibleStart = 0;
    a.playing = 0;
    for(int c = 0; c < 2; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    /* in-range target */
    scrollArrangerWindowTo(&a, 5);
    ASSERT_EQ(a.visibleStart, 5, "in-range jump");
    /* no-op when target == current */
    scrollArrangerWindowTo(&a, 5);
    ASSERT_EQ(a.visibleStart, 5, "no-op on equal");
    /* under-clamp */
    scrollArrangerWindowTo(&a, -10);
    ASSERT_EQ(a.visibleStart, 0, "under-clamp to 0");
    /* over-clamp */
    int maxStart = MAX_SONG_LENGTH - 8;
    scrollArrangerWindowTo(&a, maxStart + 100);
    ASSERT_EQ(a.visibleStart, maxStart, "over-clamp to max");
    /* NULL is a safe no-op */
    scrollArrangerWindowTo(NULL, 5);
    ASSERT_TRUE(true, "NULL arranger: no crash");
    printf("PASS test_scroll_arranger_window_to\n");
    return 0;
}

/* Callback-recorder state — syncArrangerSelectionTo MUST fire both
 * onCellSelect and onPatternSelection for the pattern screen + app
 * state to follow arranger nav. The recorders write what they saw so
 * the test can assert the dispatch happened. */
typedef struct {
    int cellXY[2];
    int cellCallCount;
    int patternIdx;
    int patternCallCount;
} SyncCbRecord;
static void syncCellCb(void *appstate, void *data) {
    SyncCbRecord *r = (SyncCbRecord *)appstate;
    int *xy = (int *)data;
    r->cellXY[0] = xy[0];
    r->cellXY[1] = xy[1];
    r->cellCallCount++;
}
static void syncPatternCb(void *appstate, void *data) {
    SyncCbRecord *r = (SyncCbRecord *)appstate;
    int *idx = (int *)data;
    r->patternIdx = *idx;
    r->patternCallCount++;
}

/* syncArrangerSelectionTo writes selected_x/selected_y from
 * g->selected and dispatches onCellSelect + onPatternSelection
 * callbacks. Build a minimal graph by hand with two cells (row 0
 * and row 5) on two channels, point g->selected at a cell, call
 * sync, and verify both the cursor write and the callback dispatch.
 *
 * Per the contract: sync does NOT re-aim visibleStart — only nav
 * edge-scroll or main.c's playhead-follow does that. */
static int test_sync_arranger_selection(void) {
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 2;
    a.visibleStart = 0;
    a.selected_x = 0;
    a.selected_y = 0;
    /* populate song so patternIndex (= song[x][y]) is well-defined */
    for(int c = 0; c < 2; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    a.song[0][5] = 7;
    a.song[1][5] = 9;
    SyncCbRecord rec = {0};
    a.onCellSelect.f = syncCellCb;
    a.onCellSelect.appstateRef = &rec;
    a.onPatternSelection.f = syncPatternCb;
    a.onPatternSelection.appstateRef = &rec;
    /* build a tiny graph: root -> col -> row0, row1 */
    Graph *g = createGraph(na_vertical);
    GuiNode *col = createGuiNode(0, 0, 100, 100, 2, na_vertical, "gcol", false, false);
    GuiNode *row0 = createGuiNode(0, 0, 100, 20, 2, na_horizontal, "row0", false, false);
    GuiNode *row1 = createGuiNode(0, 50, 100, 20, 2, na_horizontal, "row1", false, false);
    GuiNode *c0_0 = createArrangerCellGuiNode(0, 0, 50, 20, true, &a, 0, 0);
    GuiNode *c0_1 = createArrangerCellGuiNode(50, 0, 50, 20, true, &a, 1, 0);
    GuiNode *c1_0 = createArrangerCellGuiNode(0, 50, 50, 20, true, &a, 0, 5);
    GuiNode *c1_1 = createArrangerCellGuiNode(50, 50, 50, 20, true, &a, 1, 5);
    appendItem(row0, c0_0, 1);
    appendItem(row0, c0_1, 1);
    appendItem(row1, c1_0, 1);
    appendItem(row1, c1_1, 1);
    appendItem(col, row0, 1);
    appendItem(col, row1, 1);
    appendItem(g->root, col, 1);
    /* select (ch=0, row=5) and sync — should fire both callbacks with
     * cellXY={0,5} and patternIndex=a.song[0][5]=7. visibleStart stays 0
     * because sync never re-aims it. */
    g->selected = c1_0;
    syncArrangerSelectionTo(g, &a);
    ASSERT_EQ(a.selected_x, 0, "selected_x synced to selected cell channel");
    ASSERT_EQ(a.selected_y, 5, "selected_y synced to selected cell row");
    ASSERT_EQ(a.visibleStart, 0, "visibleStart unchanged by sync");
    ASSERT_EQ(rec.cellCallCount, 1, "onCellSelect fired once");
    ASSERT_EQ(rec.cellXY[0], 0, "onCellSelect arg: x");
    ASSERT_EQ(rec.cellXY[1], 5, "onCellSelect arg: y");
    ASSERT_EQ(rec.patternCallCount, 1, "onPatternSelection fired once");
    ASSERT_EQ(rec.patternIdx, 7, "onPatternSelection arg: song[0][5]");
    /* switch selection to (ch=1, row=5) — sync again, callbacks refire. */
    g->selected = c1_1;
    syncArrangerSelectionTo(g, &a);
    ASSERT_EQ(a.selected_x, 1, "selected_x tracks new selection");
    ASSERT_EQ(a.selected_y, 5, "selected_y still 5");
    ASSERT_EQ(rec.cellCallCount, 2, "onCellSelect fired again");
    ASSERT_EQ(rec.cellXY[0], 1, "onCellSelect arg: new x");
    ASSERT_EQ(rec.patternIdx, 9, "onPatternSelection arg: song[1][5]");
    teardown_graph(g);
    printf("PASS test_sync_arranger_selection\n");
    return 0;
}

/* navigateArrangerGraphTo(g, a, km) is the canonical (g, a, km)
 * entry point — no file-statics, no setter. The test builds a graph
 * with two cells on a single row and drives RIGHT/LEFT through the
 * pipeline directly. After each call, g->selected is a cell, the
 * Arranger's selected_x/selected_y track the cell's coords, AND
 * the callbacks fired. */
static int test_navigate_arranger_graph_to(void) {
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 2;
    a.visibleStart = 0;
    a.selected_x = 0;
    a.selected_y = 0;
    for(int c = 0; c < 2; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    a.song[0][0] = 100;
    a.song[1][0] = 200;
    SyncCbRecord rec = {0};
    a.onCellSelect.f = syncCellCb;
    a.onCellSelect.appstateRef = &rec;
    a.onPatternSelection.f = syncPatternCb;
    a.onPatternSelection.appstateRef = &rec;
    Graph *g = createGraph(na_vertical);
    GuiNode *col = createGuiNode(0, 0, 100, 100, 2, na_vertical, "gcol", false, false);
    GuiNode *row0 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "row0", false, false);
    GuiNode *c0_0 = createArrangerCellGuiNode(0, 0, 100, 100, true, &a, 0, 0);
    GuiNode *c0_1 = createArrangerCellGuiNode(0, 0, 100, 100, true, &a, 1, 0);
    appendItem(row0, c0_0, 1);
    appendItem(row0, c0_1, 1);
    appendItem(col, row0, 1);
    appendItem(g->root, col, 1);
    /* No setArrangerNavState — we drive the pipeline directly with our
     * local pointers. First RIGHT seeds the initial selection (c0_0),
     * second RIGHT moves to c0_1. */
    navigateArrangerGraphTo(g, &a, KM_RIGHT);
    navigateArrangerGraphTo(g, &a, KM_RIGHT);
    ASSERT_TRUE(g->selected != NULL, "after RIGHT, selection is set");
    ASSERT_TRUE(isArrangerCellNode(g->selected), "selection is a cell");
    int cx = -1, cy = -1;
    getArrangerCellCoords(g->selected, &cx, &cy);
    ASSERT_EQ(cx, 1, "after RIGHT: channel = 1");
    ASSERT_EQ(a.selected_x, 1, "selected_x synced to selected cell channel");
    ASSERT_EQ(a.selected_y, 0, "selected_y synced to selected cell row");
    ASSERT_EQ(rec.cellCallCount, 2, "onCellSelect fired for seed + RIGHT");
    ASSERT_EQ(rec.cellXY[0], 1, "last onCellSelect arg: x=1");
    ASSERT_EQ(rec.patternIdx, 200, "last onPatternSelection arg: song[1][0]");
    /* LEFT walks back to c0_0. */
    navigateArrangerGraphTo(g, &a, KM_LEFT);
    getArrangerCellCoords(g->selected, &cx, &cy);
    ASSERT_EQ(cx, 0, "after LEFT: channel = 0");
    ASSERT_EQ(a.selected_x, 0, "selected_x back to 0");
    ASSERT_EQ(rec.cellXY[0], 0, "after LEFT, cell cb saw x=0");
    ASSERT_EQ(rec.patternIdx, 100, "after LEFT, pattern cb saw song[0][0]");
    teardown_graph(g);
    printf("PASS test_navigate_arranger_graph_to\n");
    return 0;
}

/* Edge-scroll in navigateArrangerGraphTo: KM_DOWN at the bottom row of
 * the visible window must increment visibleStart; KM_UP at the top row
 * must decrement it. Also exercises the non-edge branch: KM_DOWN from
 * a MIDDLE row must NOT scroll (selection stays put, visibleStart
 * unchanged).
 *
 * Layout mirrors the production arranger grid column: root -> gcol ->
 * row0..row7 (each na_horizontal, 2 cells). Cell row r at absolute row
 * `r` (ch0 at x=0, ch1 at x=50; row at y=r*20, h=20) so with
 * visibleStart=0 DOWN at row 7 fires the edge-scroll (y == 7 ==
 * visibleStart+ARRANGER_WINDOW_ROWS-1). The file-static g_arranger /
 * agui are NULL during this test, so scrollArrangerWindow's
 * retargetWindowCells early-returns and our cell `row` fields stay
 * stable across the scroll (which is exactly what lets us assert
 * visibleStart transitions purely from the public API). */
static Graph *build_windowed_grid_graph(Arranger *a) {
    Graph *g = createGraph(na_vertical);
    GuiNode *col = createGuiNode(0, 0, 100, 8 * 20, 2, na_vertical, "gcol", false, false);
    GuiNode *cells[ARRANGER_WINDOW_ROWS][2];
    for(int r = 0; r < ARRANGER_WINDOW_ROWS; r++) {
        GuiNode *row = createGuiNode(0, r * 20, 100, 20, 2, na_horizontal, "row", false, false);
        cells[r][0] = createArrangerCellGuiNode(0, r * 20, 50, 20, true, a, 0, r);
        cells[r][1] = createArrangerCellGuiNode(50, r * 20, 50, 20, true, a, 1, r);
        appendItem(row, cells[r][0], 1);
        appendItem(row, cells[r][1], 1);
        appendItem(col, row, 1);
        /* pin rects AFTER reflow so geometric nav has predictable centers */
        cells[r][0]->x = 0; cells[r][0]->y = r * 20; cells[r][0]->w = 50; cells[r][0]->h = 20;
        cells[r][1]->x = 50; cells[r][1]->y = r * 20; cells[r][1]->w = 50; cells[r][1]->h = 20;
    }
    appendItem(g->root, col, 1);
    return g;
}

static int test_nav_edge_scroll(void) {
    Arranger a; memset(&a, 0, sizeof(a));
    a.enabledChannels = 2;
    a.visibleStart = 0;
    a.selected_x = 0;
    a.selected_y = 0;
    a.playing = 0;
    for(int c = 0; c < 2; c++) for(int r = 0; r < 12; r++) a.song[c][r] = -1;
    SyncCbRecord rec = {0};
    a.onCellSelect.f = syncCellCb;
    a.onCellSelect.appstateRef = &rec;
    a.onPatternSelection.f = syncPatternCb;
    a.onPatternSelection.appstateRef = &rec;
    Graph *g = build_windowed_grid_graph(&a);

    /* Grab pointers to a top, middle, and bottom cell by DFS order:
     * col.children[0] = row0, row0.children = {cell_ch0, cell_ch1}.
     * Walk down through col->items->head->next (etc.) to reach row7. */
    GuiNode *col = *(GuiNode **)g->root->items->head->data;
    ListElement *rowLe = col->items->head;
    GuiNode *row0 = *(GuiNode **)rowLe->data;
    GuiNode *cell_topleft = *(GuiNode **)row0->items->head->data;       /* ch0, row 0 */
    /* walk to row 7 */
    GuiNode *row7 = NULL;
    ListElement *le = col->items->head;
    for(int r = 0; r < ARRANGER_WINDOW_ROWS; r++) {
        row7 = *(GuiNode **)le->data;
        le = le->next;
    }
    GuiNode *cell_bottomleft = *(GuiNode **)row7->items->head->data;     /* ch0, row 7 */
    /* middle = row 3 */
    GuiNode *row3 = NULL;
    le = col->items->head;
    for(int r = 0; r < 4; r++) { row3 = *(GuiNode **)le->data; le = le->next; }
    GuiNode *cell_midleft = *(GuiNode **)row3->items->head->data;        /* ch0, row 3 */

    /* --- DOWN at the BOTTOM row: visibleStart must increment by 1 --- */
    g->selected = cell_bottomleft;          /* selection already on row 7 */
    a.visibleStart = 0;
    a.selected_x = 0; a.selected_y = 7;
    rec.cellCallCount = 0; rec.patternCallCount = 0;
    navigateArrangerGraphTo(g, &a, KM_DOWN);
    /* KM_DOWN from the bottom row is a geometric boundary -> selection
     * stays on cell_bottomleft. Edge-scroll branch fires
     * (y == visibleStart + 7 == 7). visibleStart: 0 -> 1. */
    ASSERT_TRUE(g->selected == cell_bottomleft,
                "DOWN at bottom keeps selection on the bottom row (geometric clamp)");
    ASSERT_EQ(a.visibleStart, 1, "DOWN at bottom row scrolls window down by 1");
    ASSERT_TRUE(a.selected_y == 7 || a.selected_y == 8,
                "selected_y tracked (7 unchanged OR 8 if retarget moved it; "
                "no graph -> retarget is a no-op so it stays 7)");
    /* The callback still fires (sync step always runs). */
    ASSERT_TRUE(rec.cellCallCount >= 1, "onCellSelect fired after DOWN-scroll");

    /* --- UP at the TOP row: visibleStart must decrement by 1 --- */
    g->selected = cell_topleft;             /* selection on row 0 */
    a.visibleStart = 5;
    a.selected_x = 0; a.selected_y = 5;
    /* Note: navigateArrangerGraphTo's edge-scroll compares against the
     * CURRENT visibleStart AND the stored `row` on the cell. Because
     * retargetWindowCells is a no-op (no graph in this TU), cell_topleft
     * still has row=0, NOT row=5. To exercise the top-edge branch
     * correctly, we use a cell whose stored row matches visibleStart.
     * The bottom-edge case above worked because we set visibleStart=0
     * AND the bottom cell has row=7. For the top-edge case we need a
     * cell with row == visibleStart, so retarget cell_topleft's stored
     * row to 5 directly via the ArrangerCellGuiNode cast. */
    GuiNode *topCell = cell_topleft;
    {
        /* Cast through TestArrangerCellGuiNode so we can flip `row` to 5.
         * Mirrors the production retargetWindowCells walk in gui.c. */
        TestArrangerCellGuiNode *ac = (TestArrangerCellGuiNode *)topCell;
        ac->row = 5;
    }
    a.selected_y = 5;
    rec.cellCallCount = 0; rec.patternCallCount = 0;
    navigateArrangerGraphTo(g, &a, KM_UP);
    /* KM_UP from the top row is a geometric boundary -> selection
     * stays on topCell. Edge-scroll branch fires
     * (y == 5 == visibleStart). visibleStart: 5 -> 4. */
    ASSERT_TRUE(g->selected == topCell,
                "UP at top keeps selection on the top row (geometric clamp)");
    ASSERT_EQ(a.visibleStart, 4, "UP at top row scrolls window up by 1");

    /* --- Non-edge DOWN: selection in the middle row -> no scroll --- */
    g->selected = cell_midleft;            /* row 3, with visibleStart=0 */
    a.visibleStart = 4;
    a.selected_x = 0; a.selected_y = 3;
    rec.cellCallCount = 0; rec.patternCallCount = 0;
    navigateArrangerGraphTo(g, &a, KM_DOWN);
    /* Geometric DOWN from row 3 lands on row 4 (middle, no edge). The
     * edge-scroll branch must NOT fire (y=4 != visibleStart+7=11). */
    int cx = -1, cy = -1;
    getArrangerCellCoords(g->selected, &cx, &cy);
    ASSERT_EQ(cx, 0, "DOWN from middle stays on ch 0");
    ASSERT_EQ(cy, 4, "DOWN from row 3 lands on row 4");
    ASSERT_EQ(a.visibleStart, 4, "DOWN in the middle does not scroll");

    /* --- Non-edge UP: same guard in the other direction --- */
    g->selected = cell_midleft;            /* row 3, with visibleStart=0 */
    a.visibleStart = 3;
    a.selected_x = 0; a.selected_y = 3;
    rec.cellCallCount = 0; rec.patternCallCount = 0;
    navigateArrangerGraphTo(g, &a, KM_UP);
    getArrangerCellCoords(g->selected, &cx, &cy);
    ASSERT_EQ(cy, 2, "UP from row 3 lands on row 2");
    ASSERT_EQ(a.visibleStart, 3, "UP in the middle does not scroll");

    /* --- Edge-scroll is clamped at MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS --- */
    g->selected = cell_bottomleft;         /* row 7 */
    a.visibleStart = MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS;
    a.selected_x = 0; a.selected_y = 7;
    rec.cellCallCount = 0; rec.patternCallCount = 0;
    navigateArrangerGraphTo(g, &a, KM_DOWN);
    ASSERT_EQ(a.visibleStart, MAX_SONG_LENGTH - ARRANGER_WINDOW_ROWS,
              "DOWN at bottom row at max start does not scroll past max");

    /* --- Edge-scroll is clamped at 0 (UP guard) --- */
    /* Place selection on a cell whose stored row == 0, with visibleStart=0. */
    {
        TestArrangerCellGuiNode *ac = (TestArrangerCellGuiNode *)cell_topleft;
        ac->row = 0;
    }
    g->selected = cell_topleft;
    a.visibleStart = 0;
    a.selected_x = 0; a.selected_y = 0;
    rec.cellCallCount = 0; rec.patternCallCount = 0;
    navigateArrangerGraphTo(g, &a, KM_UP);
    ASSERT_EQ(a.visibleStart, 0, "UP at top row at start=0 does not under-scroll");

    teardown_graph(g);
    printf("PASS test_nav_edge_scroll\n");
    return 0;
}

int main(void) {
    int fails = 0;
    fails += test_window_scale_helpers();
    fails += test_trivial_rect_wiring();
    fails += test_first_selection();
    fails += test_col_up_down();
    fails += test_row_left_right();
    fails += test_boundary_no_move();
    fails += test_cross_section_jump();
    fails += test_cone_excludes_misaligned();
    fails += test_custom_nav_precedence();
    fails += test_custom_nav_fallthrough();
    fails += test_null_safety();
    fails += test_chip_row_nav();
    fails += test_chip_node_is_drawable();
    fails += test_init_gui_node_null_callback();
    fails += test_scroll_arranger_window();
    fails += test_arranger_cell_node();
    fails += test_scroll_arranger_window_to();
    fails += test_sync_arranger_selection();
    fails += test_navigate_arranger_graph_to();
    fails += test_nav_edge_scroll();
    if (fails == 0) {
        printf("ALL graph_nav tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d graph_nav test(s) failed\n", fails);
    return 1;
}