#ifndef GRAPH_GUI_H
#define GRAPH_GUI_H

#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "settings.h"
#include "dstruct.h"
#include "raylib.h"
#include "input.h"

typedef void (*DrawCallback)(void* self);

typedef struct GuiNode GuiNode;

typedef enum {
    da_left,
    da_right,
    da_center,
    da_spread,
    drawableAlignmentCount
} DrawableAlignment;

typedef enum {
    na_horizontal,
    na_vertical,
    nodeAlignmentCount
} NodeAlignment;

enum {
    LT_INT,
    LT_FLOAT,
    LT_CHAR,
    LT_COUNT
};

void drawList(List* dl);

struct GuiNode {
    GuiNode* container;
    List* items;

    int itemCount;
    List* itemWeights;
    ListElement* weightRef;
    ListElement* itemListRef;
    int totalItemWeights;
    int drawableAlignment;
    int nodeAlignment;
    Color c;
    char* name;
    int selected;
    int x;
    int y;
    int w;
    int h;
    int padding;
};

typedef struct {
    GuiNode* root;
    GuiNode* selected;
} Graph;

GuiNode* createGuiNode(int x, int y, int w, int h, int padding, DrawableAlignment da, NodeAlignment na, Color c, const char* name, int selected);
void reflowCoordinates(GuiNode* n);
void appendItem(GuiNode* parent, GuiNode* child, int weight);
void drawNode(GuiNode* cont);
Graph* createGraph();
void navigateGraph(Graph* g, int keymapping);

int findClosestLeafLeft(Graph* g, int alignment);
int findClosestLeafRight(Graph* g, int alignment);
void selectTailLeaf(Graph* g, GuiNode* current);
void selectHeadLeaf(Graph* g, GuiNode* current);
int selectRelative(Graph* g, GuiNode* selected, int direction);

#endif
