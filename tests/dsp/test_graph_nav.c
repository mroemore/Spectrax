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

/* Task 3: forward-decls for the chip creator + recogniser so we don't
 * need to pull raylib/voice into the test TU. Symbols resolve from
 * core_lib (which already includes gui.o). */
struct VoiceManager;
/* Arranger is already typedef'd via graph_gui.h's sequencer.h include chain. */
GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, struct VoiceManager *vm, int channel, Arranger *arranger);
bool isInstChipNode(const GuiNode *n);

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
 * createArrangerGuiNode then had a non-NULL garbage callback pointer,
 * and arrangerGraphControlInput(KM_*) called it -> SEGV on EDIT+arrows
 * over the grid. initGuiNode must always NULL the callback. */
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

int main(void) {
    int fails = 0;
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
    if (fails == 0) {
        printf("ALL graph_nav tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d graph_nav test(s) failed\n", fails);
    return 1;
}