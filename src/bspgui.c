//BSP GUI
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "raylib.h"

#define MAX_NODE_CHILDREN 32
#define SCREEN_W 800
#define SCREEN_H 600

typedef struct GuiNode GuiNode;
typedef struct ListElement ListElement;

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

typedef enum {
    GO_ADD,
    GO_REMOVE,
    GO_SWAP,
    GO_INSERT,
    graphOperationCount
} GraphOperation;

typedef struct {
    void* draw;
    int x;
    int y;
    int w;
    int h;
} Drawable;

typedef struct {
    Drawable base;
    int selected;
} Button;

struct ListElement {
    ListElement* prev;
    ListElement* next;
    int val;
};

typedef struct {
    struct ListElement* head;
    struct ListElement* tail;
    int count;
} List;

struct GuiNode {
    GuiNode* container;
    GuiNode* items[MAX_NODE_CHILDREN];

    int itemCount;
    List* itemWeights;
    ListElement* weightRef;
    int totalItemWeights;
    DrawableAlignment drawableAlignment;
    NodeAlignment nodeAlignment;
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

List* createList(){
    List* l = (List*)malloc(sizeof(List));
    l->head = NULL;
    l->tail = NULL;
    l->count = 0;
    return l;
}

ListElement* createListElement(int val){
    ListElement* le = (ListElement*)malloc(sizeof(ListElement));
    if(!le) return NULL;
    le->val = val;
    le->next = NULL;
    le->prev = NULL;
    return le;
}

ListElement* listAppend(List* l, int val)
{
    if(!l) return NULL;
    
    ListElement *newElement = createListElement(val);
    printf("count: %i\n", l->count);
    if(l->count == 0){
        l->head = newElement;
        l->tail = newElement;

    } else {
        l->tail->next = newElement;
        newElement->prev = l->tail;
        l->tail = newElement;
    }
    l->count++;
    return newElement;
}

ListElement* listPush(List* l, int val){
    if(!l) return NULL;
    
    ListElement* newElement = createListElement(val);
    
    if(l->count == 0){
        l->head = newElement;
        l->tail = newElement;
    } else {
        l->head->prev = newElement;
        newElement->next = l->head;
        l->head = newElement;
    }
    l->count++;
    return newElement;
}

ListElement* listInsert(List* l, int index, int val){
    if (!l || index < 0 || index > l->count) return NULL;

    ListElement* newElement = createListElement(val);
    if (!newElement) return NULL;

    if (index == 0 || index == l->count) {
        listAppend(l, val);
    } else {
        ListElement* current = l->head;
        for (int i = 0; i < index - 1; i++) {
            current = current->next;
        }
        newElement->next = current->next;
        newElement->prev = current;
        current->next->prev = newElement;
        current->next = newElement;
    }

    l->count++;
    return newElement;
}

void listRemove(List* l, int index) {
    if (!l || index < 0 || index >= l->count) return;

    ListElement* toRemove = NULL;

    if (index == 0) {
        toRemove = l->head;
        l->head = l->head->next;
        if (l->head) l->head->prev = NULL;
        if (l->count == 1) l->tail = NULL;
    } else if (index == l->count - 1) {
        toRemove = l->tail;
        l->tail = l->tail->prev;
        if (l->tail) l->tail->next = NULL;
    } else {
        ListElement* current = l->head;
        for (int i = 0; i < index; i++) {
            current = current->next;
        }
        toRemove = current;
        current->prev->next = current->next;
        current->next->prev = current->prev;
    }

    free(toRemove);
    l->count--;
}

void freeList(List* l) {
    if (!l) return;

    ListElement* current = l->head;
    while (current) {
        ListElement* next = current->next;
        free(current);
        current = next;
    }

    l->head = NULL;
    l->tail = NULL;
    l->count = 0;
}

GuiNode* createGuiNode(int x, int y, int w, int h, int padding, DrawableAlignment da, NodeAlignment na, Color c, const char* name, int selected){
    GuiNode* gn = (GuiNode*)malloc(sizeof(GuiNode));
    if(!gn) return NULL;

    gn->container = NULL;
    gn->x = x;
    gn->y = y;
    gn->w = w;
    gn->h = h;
    gn->name = malloc(sizeof(char) * strlen(name));
    if(gn->name == NULL){
        printf("ERR: createGuiNode: malloc fail\n");
        free(gn);
        return NULL;
    }
    strcpy(gn->name, name);
    gn->padding = padding;
    gn->weightRef = NULL;
    gn->selected = selected;
    gn->itemWeights = createList(128);
    gn->totalItemWeights = 0;
    gn->itemCount = 0;
    gn->c = c;
    gn->drawableAlignment = da;
    gn->nodeAlignment = na;
    return gn;
}

void reflowCoordinates(GuiNode* n){
    if(!n) return;
    if(n->itemCount <= 0) return;

    int y_scalar = 0;
    int x_scalar = 0;
    int weightsAccumlator = 0;

    for(int i = 0; i < n->itemCount; i++){
        printf(". ");
        n->items[i]->x = n->x + n->padding;
        n->items[i]->y = n->y + n->padding;
        n->items[i]->w = n->w - n->padding * 2;
        n->items[i]->h = n->h - n->padding * 2;

        printf("reflowed coords: %s (x:%i, y:%i, w:%i, h:%i)\n",
            n->items[i]->name,
            n->items[i]->x,
            n->items[i]->y,
            n->items[i]->w,
            n->items[i]->h
        );
        switch(n->nodeAlignment){
            case na_vertical:
                y_scalar = (n->h / n->totalItemWeights);
                n->items[i]->y = n->y + (y_scalar * weightsAccumlator) + (n->padding);
                n->items[i]->h = (y_scalar * n->items[i]->weightRef->val) - (n->padding * 2);
                break;
            case na_horizontal:
                x_scalar = (n->w / n->totalItemWeights);
                n->items[i]->x = n->x + (x_scalar * weightsAccumlator) + (n->padding);
                n->items[i]->w = (x_scalar * n->items[i]->weightRef->val) - (n->padding * 2);
                break;
            default:
                break;
        }

        weightsAccumlator += n->items[i]->weightRef->val;
        reflowCoordinates(n->items[i]);
    }

    printf("\n");
}

void appendItem(GuiNode* parent, GuiNode* child, int weight){
    if(parent->itemCount >= MAX_NODE_CHILDREN) return;
    parent->items[parent->itemCount] = child;
    child->container = parent;
    parent->itemCount++;
    parent->totalItemWeights += weight;
    child->x = parent->x + parent->padding;
    child->y = parent->y + parent->padding;
    child->w = parent->w - parent->padding*2;
    child->h = parent->h - parent->padding*2;
    child->weightRef = listAppend(parent->itemWeights, weight);
    reflowCoordinates(parent);
}

void drawBSP(GuiNode* cont){
    
    if(cont->selected){
        DrawRectangleLinesEx((Rectangle){cont->x, cont->y, cont->w, cont->h}, 2, WHITE);
    } else {
        DrawRectangleLinesEx((Rectangle){cont->x, cont->y, cont->w, cont->h}, 2, cont->c);
    }
    DrawRectangleLinesEx((Rectangle){cont->x + cont->padding, cont->y+ cont->padding, cont->w - cont->padding*2, cont->h- cont->padding*2}, 2, (Color){cont->c.r/2, cont->c.g/2, cont->c.b/2, cont->c.a});
    DrawText(cont->name, cont->x + cont->padding, cont->y+ cont->padding, 12, WHITE);

    if(cont->itemCount > 0){
        for(int i = 0; i < cont->itemCount; i++){
            drawBSP(cont->items[i]);
        }
    }
    
}

Graph* createGraph(){
    Graph* g = (Graph*)malloc(sizeof(Graph));
    g->root = createGuiNode(0,0, SCREEN_W, SCREEN_H, 4, da_center, na_vertical, RED, "root", 0);
    g->selected = NULL;
}

void navigateGraph(GuiNode* selected, int keymapping){
    switch (keymapping){
        case KM_UP:
            switch (selected->container->nodeAlignment){
                case na_horizontal:

                    break;
                case na_vertical:
                    
                    break;
            }
            break;
        case KM_DOWN:
            break;
        case KM_LEFT:
            break;
        case KM_RIGHT:
            break;
        default:
            break;
    }
}

int main(void){
	InitWindow(SCREEN_W, SCREEN_H, "BSP Test");
    Graph* graph = createGraph();

    GuiNode* ca = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_horizontal, GREEN, "ca", 0);
    GuiNode* cb = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_vertical, BLUE, "cb", 0);

    GuiNode* ca1 = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_horizontal, PURPLE, "ca1", 0);
    GuiNode* ca2 = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_horizontal, ORANGE, "ca2", 0);
    GuiNode* ca3 = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_horizontal, MAGENTA, "ca3", 1);
    graph->selected = ca3;

    GuiNode* cb1 = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_horizontal, MAROON, "cb1", 0);
    GuiNode* cb2 = createGuiNode(0,0,SCREEN_W, SCREEN_H, 6, da_center, na_horizontal, PINK, "cb2", 0);

    
    appendItem(graph->root, ca, 3);
    appendItem(graph->root, cb, 2);
    appendItem(ca, ca1, 1);
    appendItem(ca, ca2, 1);
    appendItem(ca, ca3, 1);
    appendItem(cb, cb1, 1);
    appendItem(cb, cb2, 1);
    reflowCoordinates(graph->root);
    
    InputState* is = createInputState(INPUT_TYPE_KEYBOARD);
	
    BeginDrawing();
    ClearBackground(BLACK);

    while(!WindowShouldClose()){
        BeginDrawing();
        drawBSP(graph->root);
        if(isKeyJustPressed(is, KM_UP)){

        }
        EndDrawing();
    }

    EndDrawing();
    CloseWindow();
}