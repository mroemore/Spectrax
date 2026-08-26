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

/* Two-column graph: left col A/B, right col C/D. Rect numbers come
 * from the plan and exercise appendItem/reflowCoordinates. */
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
    /* appendItem + reflowCoordinates overwrite a's explicit
     * createGuiNode rect with values derived from colL's layout.
     * a's rect is whatever the layout produced — it just must be
     * non-zero and sane. (Exact values are pinned later when the
     * reflow scheme is reviewed; here we only pin that wiring ran.) */
    ASSERT_TRUE(a->w > 0, "A has nonzero width");
    ASSERT_TRUE(a->h > 0, "A has nonzero height");
    /* Names match across the helper so navigation tests in Task 2
     * can rely on them. */
    GuiNode *b = *(GuiNode **)colL->items->head->next->data;
    ASSERT_TRUE(strcmp(b->name, "B") == 0, "second leaf in colL is B");
    GuiNode *colR = *(GuiNode **)g->root->items->head->next->data;
    ASSERT_TRUE(strcmp(colR->name, "colR") == 0, "second root child is colR");
    GuiNode *c = *(GuiNode **)colR->items->head->data;
    ASSERT_TRUE(strcmp(c->name, "C") == 0, "first leaf in colR is C");
    GuiNode *d = *(GuiNode **)colR->items->head->next->data;
    ASSERT_TRUE(strcmp(d->name, "D") == 0, "second leaf in colR is D");
    teardown_graph(g);
    printf("PASS test_trivial_rect_wiring\n");
    return 0;
}

int main(void) {
    int fails = 0;
    fails += test_trivial_rect_wiring();
    if (fails == 0) {
        printf("ALL graph_nav tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d graph_nav test(s) failed\n", fails);
    return 1;
}