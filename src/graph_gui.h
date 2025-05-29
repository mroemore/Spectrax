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

#define MAX_GRAPH_ITEMS sizeof(uint8_t)

typedef void (*DrawCallback)(void *self);
typedef void (*OnPressCallback)(Parameter *parameter, float value);
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
	Parameter *p;
	uint8_t itemCount;
	List *itemWeights;
	ListElement *weightRef;
	ListElement *itemListRef;
	uint32_t totalItemWeights;
	char *name;
	bool selectable;
	bool hasSelectableItems;
	bool selected;
	bool resizeable;
	bool drawable;
	bool navOverride;
	uint8_t nodeAlignment;
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	uint16_t padding;
	CustomNavFunc customNav;
};

typedef struct {
	GuiNode *root;
	GuiNode *selected;
} Graph;

bool initGuiNode(GuiNode *gn, int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selectable, bool selected);
GuiNode *createGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selectable, bool selected);
GuiNode *createBlankGuiNode();
GuiNode *createNamedBlankGuiNode(char *name);
void freeGuiNode(GuiNode *gn);
void printGraph(GuiNode *root, int depth);

void reflowCoordinates(GuiNode *n);
void appendItem(GuiNode *parent, GuiNode *child, int weight);
void drawNode(GuiNode *cont);
Graph *createGraph(NodeAlignment na);

void navigateGraph(Graph *g, int keymapping);
bool selectLeaf(Graph *g, GuiNode *n, bool head);
GuiNode *searchUpwardsByAlignment(GuiNode *n, NodeAlignment na, bool prev);
GuiNode *getAdjacentNode(GuiNode *c, bool prev);
GuiNode *selectAdjacent(Graph *g, GuiNode *c, bool prev);
void changeGraphSelection(Graph *g, GuiNode *new);
void navAdjacent(Graph *g, GuiNode *n, NodeAlignment na, bool prev, bool head);

#endif
