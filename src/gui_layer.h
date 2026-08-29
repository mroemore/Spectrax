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

#endif /* SPECTRAX_GUI_LAYER_H */
