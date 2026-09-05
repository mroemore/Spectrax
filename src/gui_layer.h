/* Task 7: Layer primitive.
 *
 * A Layer is a full Graph (selectable + navable + drawable) plus the
 * frame/rectangle on the screen it should be drawn within. Layers are
 * stacked; the topmost layer captures input first, and drawing is
 * bottom-up. The classic use case is overlay modals: instead of
 * hand-rolling `g_modalState` + a dedicated draw fn, push a layer
 * with a 2-button graph and let the normal graph machinery do the
 * navigation, selection, and drawing. The layer automatically pops
 * itself when the user picks a terminal action (handled by the layer's
 * action callback, not the layer system itself).
 *
 * A layer can either own its Graph (frees it on destroy) or borrow
 * one. The instrument's main screen will eventually live as the
 * bottom layer; modals are pushed on top and own their graphs. */
#ifndef SPECTRAX_GUI_LAYER_H
#define SPECTRAX_GUI_LAYER_H

#include <stdbool.h>
#include "graph_gui.h"
#include "input.h"

typedef struct Layer {
	Graph *graph;
	int x;
	int y;
	int w;
	int h;
	char *name;
	bool dim;
	bool ownsGraph;
	/* Spec #3 (route picker): passive layers draw and read selection from
	 * the underlying instrument graph for arrow input but do NOT capture
	 * the input pipeline themselves. Used by ROUTELINES so it can sit on
	 * top of ROUTE while keeping ROUTE modal. Toggle via setLayerPassive. */
	bool passive;
} Layer;

typedef struct LayerStack {
	Layer *layers;
	int count;
	int capacity;
} LayerStack;

/* Lifecycle */
void initLayerStack(LayerStack *stack);
void destroyLayerStack(LayerStack *stack);

/* Build a layer. The caller passes in a fully-constructed Graph. If
 * `ownsGraph` is true, destroyLayer() will free both the graph's
 * root (via freeGuiNode) and the graph itself. `name` is duplicated;
 * caller retains ownership. The dim flag toggles a translucent
 * backdrop so lower layers are still visible but recede. */
Layer *createLayer(Graph *graph, int x, int y, int w, int h, const char *name, bool dim, bool ownsGraph);
void destroyLayer(Layer *layer);

/* Push / pop */
void pushLayer(LayerStack *stack, Layer *layer);
Layer *popLayer(LayerStack *stack); /* removes + returns the top, caller frees */
Layer *topLayer(const LayerStack *stack); /* peek, does not modify */
bool layerStackIsEmpty(const LayerStack *stack);

/* I/O */
void layerStackDraw(const LayerStack *stack);
void layerStackInput(LayerStack *stack, InputState *is);
/* Mark an already-pushed layer as passive. The layerStackInput path treats
 * passive layers as transparent: it skips KM_EDIT / KM_SELECT pop logic
 * for them and lets input flow through to the next non-passive layer. The
 * picker driver uses this so ROUTELINES can ride on top of ROUTE without
 * stealing arrow keys or stealing EDIT. */
void setLayerPassive(Layer *layer, bool passive);
/* Find the layer whose name matches (linear scan; layers are tiny). NULL
 * if not found. Used by syncRouteLinesOverlay to update the existing
 * ROUTELINES entry in place when it must re-route the source after a
 * graph rebuild rather than push a fresh layer each time. */
Layer *findLayerByName(const LayerStack *stack, const char *name);
/* Iterate the stack from top down and return the first layer whose
 * `passive` flag is false. Returns NULL if every layer is passive (e.g.
 * only the ROUTELINES overlay is up). Used by the picker driver to skip
 * arrow routing into a picker layer that's been temporarily eclipsed by
 * a passive overlay. */
Layer *topNonPassiveLayer(const LayerStack *stack);
/* Remove the top non-passive layer from the stack and return a heap-
 * allocated copy. Caller frees. The picker driver calls this from
 * layerStackInput on KM_SELECT so the universal "back" gesture peels
 * the modal layer off without also yanking a passive ROUTELINES
 * overlay above it. */
Layer *popTopNonPassiveLayer(LayerStack *stack);

#endif /* SPECTRAX_GUI_LAYER_H */
