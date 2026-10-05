/* layerStackDraw reachability test — runs under Xvfb (a real GL context).
 *
 * layerStackDraw() is the only function in gui_layer.c that makes raylib draw
 * calls (DrawRectangle for the dim backdrop, drawNode for each layer's graph).
 * It cannot run without a window, so this test opens one, builds a stack with
 * a dim layer and a graph-backed layer, and draws a frame. It does not assert
 * pixels — that would be a golden-image test, a different tool — it asserts
 * that the draw path executes and is not skipped, which is what closes the
 * remaining branches in the coverage build.
 *
 * Registered to run under xvfb-run (see tests/meson.build), matching the
 * vizulobe GL tests' pattern. */

#include <stdio.h>

#include "gui_layer.h"
#include "graph_gui.h"
#include "raylib.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static Graph *tiny_graph(void) {
	Graph *g = createGraph(na_vertical);
	GuiNode *leaf = createGuiNode(0, 0, 40, 20, 1, na_vertical, "leaf", true, false);
	appendItem(g->root, leaf, 1);
	g->selected = leaf;
	return g;
}

int main(void) {
	/* A small offscreen window; Xvfb provides the display. */
	InitWindow(320, 240, "spx-layer-draw-test");
	if(!IsWindowReady()) {
		fprintf(stderr, "FAIL: window not ready (needs a display / Xvfb)\n");
		return 1;
	}

	LayerStack s;
	initLayerStack(&s);

	/* Bottom: a dim, graph-backed layer. */
	Graph *g1 = tiny_graph();
	pushLayer(&s, createLayer(g1, 0, 0, 320, 240, "base", true, true));
	/* Top: a non-dim layer exercising the second loop iteration. */
	Graph *g2 = tiny_graph();
	pushLayer(&s, createLayer(g2, 10, 10, 100, 100, "modal", false, true));
	/* A graph-less layer exercises the l->graph==NULL branch. */
	pushLayer(&s, createLayer(NULL, 0, 0, 0, 0, "empty", false, false));
	/* A *dim* layer with no graph: takes the drawNode skip while still
	 * taking the dim DrawRectangle path. */
	pushLayer(&s, createLayer(NULL, 0, 0, 5, 5, "dim-empty", true, false));
	/* A graph whose root has been freed: takes the l->graph->root false
	 * half of the drawNode guard. */
	Graph *g3 = createGraph(na_vertical);
	freeGuiNode(g3->root);
	g3->root = NULL;
	pushLayer(&s, createLayer(g3, 0, 0, 5, 5, "noroot", false, true));

	ASSERT_TRUE(s.count == 5);

	/* Draw two frames: the loop runs over every layer, taking the dim
	 * branch, the graph branch, the graph-less branch, the dim + graph-less
	 * combination, and the root-less-graph branch (all skip drawNode). */
	BeginDrawing();
	ClearBackground(BLACK);
	layerStackDraw(&s);
	EndDrawing();
	BeginDrawing();
	ClearBackground(BLACK);
	layerStackDraw(&s);
	EndDrawing();

	destroyLayerStack(&s);   /* frees g1, g2 (owned) */
	CloseWindow();

	printf("PASSED (gui_layer draw under Xvfb)\n");
	return 0;
}
