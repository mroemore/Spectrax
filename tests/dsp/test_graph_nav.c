/* test_graph_nav.c — graph_navigation unit tests. Layered:
 *   Task 1: test scaffolding (rect wiring + tree shape). GREEN.
 *   Task 2: navigateGraphRefined + changeGraphSelection fix.
 *
 * See docs/superpowers/plans/2026-08-26-nav-refinement.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "graph_gui.h"

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
    if (fails == 0) {
        printf("ALL graph_nav tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d graph_nav test(s) failed\n", fails);
    return 1;
}