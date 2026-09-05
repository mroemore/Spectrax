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
	l->passive = false;
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

void setLayerPassive(Layer *layer, bool passive) {
	if(!layer) {
		return;
	}
	layer->passive = passive;
}

Layer *findLayerByName(const LayerStack *stack, const char *name) {
	if(!stack || !name) {
		return NULL;
	}
	for(int i = stack->count - 1; i >= 0; i--) {
		Layer *l = &stack->layers[i];
		if(l->name && strcmp(l->name, name) == 0) {
			return l;
		}
	}
	return NULL;
}

Layer *topNonPassiveLayer(const LayerStack *stack) {
	if(!stack || stack->count == 0) {
		return NULL;
	}
	for(int i = stack->count - 1; i >= 0; i--) {
		if(!stack->layers[i].passive) {
			return &stack->layers[i];
		}
	}
	return NULL;
}

Layer *popTopNonPassiveLayer(LayerStack *stack) {
	if(!stack || stack->count == 0) {
		return NULL;
	}
	for(int i = stack->count - 1; i >= 0; i--) {
		Layer *l = &stack->layers[i];
		if(l->passive) {
			continue;
		}
		/* Found the top non-passive entry. Move everything above it
		 * down by one slot (these are always passive by construction:
		 * ROUTELINES), then shrink the count. */
		Layer *out = (Layer *)malloc(sizeof(Layer));
		*out = *l;
		for(int j = i; j < stack->count - 1; j++) {
			stack->layers[j] = stack->layers[j + 1];
		}
		stack->count--;
		return out;
	}
	return NULL;
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

void layerStackInputLayer(LayerStack *stack, int index, InputState *is) {
	if(!stack || index < 0 || index >= stack->count) {
		return;
	}
	Layer *l = &stack->layers[index];
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
	/* KM_SELECT pops the topmost non-passive layer — universal "back /
	 * cancel" for modal overlays. The picker bookkeeping reset runs
	 * here so cancel and confirm-on-dest both end up with a clean
	 * state (the gradient overlay re-syncs next frame). */
	if(isKeyJustPressed(is, KM_SELECT) && stack->count > 0) {
		Layer *popped = popTopNonPassiveLayer(stack);
		if(popped && popped->name && strcmp(popped->name, "ROUTE") == 0) {
			extern int g_routePickerCount;
			if(g_routePickerCount > 0) {
				g_routePickerCount--;
			}
			extern bool g_routeErase;
			g_routeErase = false;
		}
		free(popped);
		return;
	}
	/* KM_EDIT (z) activates the selected node's action callback. */
	if(isKeyJustPressed(is, KM_EDIT) && g->selected && g->selected->actionCb) {
		g->selected->actionCb(g->selected->actionCtx);
	}
}

void layerStackInput(LayerStack *stack, InputState *is) {
	if(!stack || stack->count == 0) {
		return;
	}
	/* Spec #3 + Spec #6: passive layers (ROUTELINES) are visually
	 * present but must NOT capture the input pipeline. Walk past them
	 * to the first non-passive layer and feed input there instead. */
	Layer *l = topNonPassiveLayer(stack);
	if(!l) {
		return;
	}
	int idx = (int)(l - stack->layers);
	layerStackInputLayer(stack, idx, is);
}
