#ifndef GRAPH_GUI_H
#define GRAPH_GUI_H

// #define DEBUG_GRAPH_DRAW

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "settings.h"
#include "sequencer.h"
#include "dstruct.h"
#include "modsystem.h"
#include "raylib.h"
#include "input.h"

typedef void (*DrawCallback)(void *self);
typedef void (*OnPressCallback)(Parameter *parameter, float value);
typedef void (*ActionCallback)(void *ctx);
typedef bool (*CustomNavFunc)(void *self, int keymapping);

typedef struct GuiNode GuiNode;

typedef enum {
	na_horizontal,
	na_vertical,
	nodeAlignmentCount
} NodeAlignment;

void drawList(List *dl);

struct GuiNode {
	GuiNode *container;
	List *items;
	OnPressCallback callback;
	DrawCallback draw;
	/* Called by freeGuiNode before free() so typed wrappers can release
	 * their own resources (render textures, registry slots). NULL for
	 * plain nodes. */
	void (*destructor)(void *self);
	Parameter *p;
	ActionCallback actionCb;
	void *actionCtx;
	uint8_t itemCount;
	List *itemWeights;
	ListElement *weightRef;
	ListElement *itemListRef;
	uint32_t totalItemWeights;
	char *name;
	char *className;
	bool selectable;
	bool hasSelectableItems;
	bool selected;
	bool resizeable;
	bool drawable;
	bool navOverride;
	/* task scroll: when true, this GuiNode is a scroll container —
	 * reflowCoordinates lays children out in fixed-height rows instead
	 * of weight-based bands, and drawNode wraps the children recursion
	 * in BeginScissorMode/EndScissorMode so off-viewport rows are
	 * clipped. Zero-initialised by initGuiNode. */
	bool scrollable;
	uint8_t nodeAlignment;
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	uint16_t padding;
	uint16_t gap;
	CustomNavFunc customNav;
};

typedef struct {
	GuiNode *root;
	GuiNode *selected;
} Graph;

/* task scroll: scrollable viewport container. Children are laid out
 * in fixed-height rows of rowH pixels, starting at sc->base.y +
 * sc->base.padding and stepping by rowH. The base->h is the visible
 * viewport height; the content height = sc->contentH. drawNode wraps
 * the children recursion in a scissor so off-viewport rows are
 * clipped. scrollOffset is shifted by scrollToVisible so a selected
 * descendant stays inside [sc->base.y, sc->base.y + sc->base.h]. */
typedef struct {
	GuiNode base;
	int rowH;
	int scrollOffset;
	int contentH;
} ScrollContainer;

GuiNode *createScrollContainer(int x, int y, int w, int h, int rowH, const char *name);
void scrollToVisible(ScrollContainer *sc, const GuiNode *sel);

bool initGuiNode(GuiNode *gn, int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selectable, bool selected);
GuiNode *createGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selectable, bool selected);
GuiNode *createBlankGuiNode();
GuiNode *createNamedBlankGuiNode(char *name);
void freeGuiNode(GuiNode *gn);
void printGraph(GuiNode *root, int depth);

void guiNodeSetClass(GuiNode *gn, const char *className);

void reflowCoordinates(GuiNode *n);
void appendItem(GuiNode *parent, GuiNode *child, int weight);
void drawNode(GuiNode *cont);
Graph *createGraph(NodeAlignment na);

void navigateGraph(Graph *g, int keymapping);
void navigateGraphRefined(Graph *g, int keymapping);
bool selectLeaf(Graph *g, GuiNode *n, bool head);
GuiNode *searchUpwardsByAlignment(GuiNode *n, NodeAlignment na, bool prev);
GuiNode *getAdjacentNode(GuiNode *c, bool prev);
GuiNode *selectAdjacent(Graph *g, GuiNode *c, bool prev);
void changeGraphSelection(Graph *g, GuiNode *new);
void navAdjacent(Graph *g, GuiNode *n, NodeAlignment na, bool prev, bool head);

#endif
