/* Task 7: Layer primitive implementation. See gui_layer.h for the
 * design rationale. The short version: a Layer wraps a Graph (which
 * already has its own draw + nav + selection) and a frame. The
 * LayerStack is a dynamic array of Layers; push to bring a modal
 * on top, pop to dismiss it. The instrument input pipeline routes
 * arrow keys to the topmost layer's graph first; if that doesn't
 * consume the input (e.g. the layer is empty), it falls through
 * to the underlying instrument graph.
 *
 * Design notes for anyone debugging this later:
 *  - The graph inside a layer is fully self-contained. Its root
 *    takes the full screen (per createGraph) but the action nodes
 *    inside use absolute screen coords. The layer's x/y/w/h is
 *    stored for any caller that wants a "backdrop dim" rect.
 *  - layerStackInput only handles UP/DOWN/LEFT/RIGHT and START.
 *    UP/DOWN/LEFT/RIGHT are routed via navigateGraphRefined. START
 *    fires the selected node's actionCb if it has one. The "is the
 *    layer done" decision is the responsibility of the actionCb
 *    itself — it pops its own layer.
 *  - No background-thread / cross-thread issues. All pushes happen
 *    on the input handler thread, and the draw side just iterates. */
#include "gui_layer.h"

#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "graph_gui.h"
#include "gui.h"
#include "theme.h"

void initLayerStack(LayerStack *stack) {
	stack->layers = NULL;
	stack->count = 0;
	stack->capacity = 0;
}

void destroyLayerStack(LayerStack *stack) {
	if(!stack) {
		return;
	}
	for(int i = 0; i < stack->count; i++) {
		destroyLayer(&stack->layers[i]);
	}
	free(stack->layers);
	stack->layers = NULL;
	stack->count = 0;
	stack->capacity = 0;
}

Layer *createLayer(Graph *graph, int x, int y, int w, int h, const char *name, bool dim, bool ownsGraph) {
	Layer *l = (Layer *)malloc(sizeof(Layer));
	l->graph = graph;
	l->x = x;
	l->y = y;
	l->w = w;
	l->h = h;
	l->name = name ? strdup(name) : NULL;
	l->dim = dim;
	l->ownsGraph = ownsGraph;
	return l;
}

void destroyLayer(Layer *layer) {
	if(!layer) {
		return;
	}
	if(layer->ownsGraph && layer->graph) {
		if(layer->graph->root) {
			freeGuiNode(layer->graph->root);
		}
		free(layer->graph);
		layer->graph = NULL;
	}
	if(layer->name) {
		free(layer->name);
		layer->name = NULL;
	}
	free(layer);
}

void pushLayer(LayerStack *stack, Layer *layer) {
	if(!stack || !layer) {
		return;
	}
	if(stack->count >= stack->capacity) {
		int newCap = stack->capacity == 0 ? 4 : stack->capacity * 2;
		Layer *nl = (Layer *)realloc(stack->layers, sizeof(Layer) * (size_t)newCap);
		if(!nl) {
			return;
		}
		stack->layers = nl;
		stack->capacity = newCap;
	}
	stack->layers[stack->count++] = *layer;
	/* Ownership transferred — don't let the caller free the Layer. */
	free(layer);
}

Layer *popLayer(LayerStack *stack) {
	if(!stack || stack->count == 0) {
		return NULL;
	}
	Layer *out = (Layer *)malloc(sizeof(Layer));
	*out = stack->layers[--stack->count];
	return out;
}

Layer *topLayer(const LayerStack *stack) {
	if(!stack || stack->count == 0) {
		return NULL;
	}
	return &stack->layers[stack->count - 1];
}

bool layerStackIsEmpty(const LayerStack *stack) {
	return !stack || stack->count == 0;
}

void layerStackDraw(const LayerStack *stack) {
	if(!stack) {
		return;
	}
	for(int i = 0; i < stack->count; i++) {
		const Layer *l = &stack->layers[i];
		if(l->dim) {
			/* Dim everything underneath by tinting the layer's frame
			 * region with a translucent black. We pick a fairly heavy
			 * alpha so the lower graph recedes but is still readable
			 * for visual continuity. */
			DrawRectangle(l->x, l->y, l->w, l->h, getColourScheme()->layerDim);
		}
		if(l->graph && l->graph->root) {
			drawNode(l->graph->root);
		}
	}
}

void layerStackInput(LayerStack *stack, InputState *is) {
	if(!stack || stack->count == 0) {
		return;
	}
	Layer *l = topLayer(stack);
	if(!l || !l->graph) {
		return;
	}
	Graph *g = l->graph;
	if(isKeyJustPressed(is, KM_UP)) {
		navigateGraphRefined(g, KM_UP);
	}
	if(isKeyJustPressed(is, KM_DOWN)) {
		navigateGraphRefined(g, KM_DOWN);
	}
	if(isKeyJustPressed(is, KM_LEFT)) {
		navigateGraphRefined(g, KM_LEFT);
	}
	if(isKeyJustPressed(is, KM_RIGHT)) {
		navigateGraphRefined(g, KM_RIGHT);
	}
	/* KM_SELECT pops the top layer — the universal "back / cancel" gesture
	 * for modal overlays (overwrite confirm, dirty discard, load list).
	 * Individual layer action callbacks handle their own positive paths
	 * via KM_START. If a layer needs to override SELECT, it should set its
	 * own actionCb on the selected node and the caller's input handler
	 * will run before this generic pop. */
	if(isKeyJustPressed(is, KM_SELECT) && stack->count > 0) {
		popLayer(stack);
		return;
	}
	/* KM_EDIT (z) activates the selected node's action callback. Action
	 * buttons inside overlay layers (load-list entries, overwrite YES/NO,
	 * dirty-confirm DISCARD/SAVE/CANCEL) all live as `actionCb` on their
	 * GuiNode; KM_EDIT fires them, matching the base graph's button
	 * activation model. If the selected node has no actionCb, this is
	 * a no-op (e.g. when the layer's selection lands on a header). */
	if(isKeyJustPressed(is, KM_EDIT) && g->selected && g->selected->actionCb) {
		g->selected->actionCb(g->selected->actionCtx);
	}
}
