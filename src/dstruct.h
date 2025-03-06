#ifndef DSTRUCT_H
#define DSTRUCT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"

#define MAX_NODE_CHILDREN 32

typedef struct ListElement ListElement;
typedef struct Node Node;
typedef struct GuiNode GuiNode;

enum {
    D_UP,
    D_DOWN,
    D_LEFT,
    D_RIGHT,
    D_COUNT
};

struct ListElement {
    int type;
    ListElement* prev;
    ListElement* next;
    void* data;
    size_t dataSize;
    void* (*getValue)(ListElement* element);
};

void* assignGetter(int type);

void* getFloatValue(ListElement* element);
void* getIntValue(ListElement* element);


typedef struct {
    ListElement* head;
    ListElement* tail;
    int count;
    int capacity;
    ListElement* pool;
    ListElement* freeList;
}List;

typedef struct {
    List l;

} IntList;

IntList* createIntList();

List* createList(int capacity);
ListElement* allocateElement(List* list);
void freeElement(List* list, ListElement* element);

ListElement* appendToList(List* list, const void* data, size_t dataSize, int type);
void removeFromList(List* list, ListElement* element);

void replaceElement(List* list, ListElement* replacement, ListElement* toReplace);
void swapAdjacent(List* list, ListElement* replacement, ListElement* toReplace);

void swapListElements(List* list, ListElement* a, ListElement* b);

void freeList(List* list);

//ListElement* listAppend(List* l, ListElement* newElement);
//ListElement* listPush(List* l, ListElement* newElement);
//ListElement* listInsert(List* l, int index, ListElement* newElement);
//void listRemoveElement(List* l, ListElement* newElement);
//void listRemoveByIndex(List* l, int index);

//void freeList(List* l);

struct Node {
    Node* conns[D_COUNT];
    void* data;
    size_t dataSize;
};

Node* createNode(void* data, size_t dataSize);
Node* buildGraph(List* l);





#endif