#include "dstruct.h"

List* createList(int capacity){
    List* l = (List*)malloc(sizeof(List));
    if(!l){
        printf("ERR: createList: malloc fail.\n");
        return NULL;
    }
    l->head = NULL;
    l->tail = NULL;
    l->count = 0;
    l->capacity = capacity;
    l->pool = (ListElement*)malloc(sizeof(ListElement) * l->capacity);
    if(l->pool == NULL){
        free(l);
        printf("ERR: createList: pool malloc fail.\n");
        return NULL;
    }

    l->freeList = l->pool;
    
    for (int i = 0; i < capacity - 1; i++) {
        l->pool[i].next = &l->pool[i + 1];
        l->pool[i + 1].prev = &l->pool[i];
    }

    l->pool[capacity - 1].next = NULL;
    return l;
}

ListElement* allocateElement(List* list) {
    if (list->freeList == NULL) {
        printf("ERR: allocateElement: No free elements available\n");
        return NULL;
    }

    ListElement* element = list->freeList;
    list->freeList = element->next;

    element->prev = NULL;
    element->next = NULL;
    element->data = NULL;
    element->dataSize = 0;

    return element;
}

void freeElement(List* list, ListElement* element) {
    element->next = list->freeList;
    list->freeList = element;

    if (element->data != NULL) {
        free(element->data);
        element->data = NULL;
    }
}


ListElement* appendToList(List* list, const void* data, size_t dataSize, int type) {
    ListElement* element = allocateElement(list);
    if (element == NULL) {
        fprintf(stderr, "Failed to allocate element\n");
        return NULL;
    }

    element->data = malloc(dataSize);
    if (element->data == NULL) {
        fprintf(stderr, "Memory allocation for data failed\n");
        freeElement(list, element);
        return NULL;
    }
    memcpy(element->data, data, dataSize);
    element->dataSize = dataSize;

    if (list->head == NULL) {
        list->head = element;
        list->tail = element;
    } else {
        element->prev = list->tail;
        list->tail->next = element;
        list->tail = element;
    }
    list->count++;
    return element;
}

void removeFromList(List* list, ListElement* element) {
    if (element->prev != NULL) {
        element->prev->next = element->next;
    } else {
        list->head = element->next;
    }

    if (element->next != NULL) {
        element->next->prev = element->prev;
    } else {
        list->tail = element->prev;
    }

    list->count--;
    freeElement(list, element);
}

void replaceElement(List* list, ListElement* replacement, ListElement* toReplace){
    replacement->next = toReplace->next;
    if(toReplace->next != NULL){
        toReplace->next->prev = replacement;
    } else {
        list->tail = replacement;
    }
    replacement->prev = toReplace->prev;
    if(toReplace->prev != NULL){
        toReplace->prev->next = replacement;
    } else {
        list->head = replacement;
    }
}

void swapAdjacent(List* list, ListElement* replacement, ListElement* toReplace){
    replacement->next = toReplace->next;

    if(toReplace->next != NULL){
        toReplace->next->prev = replacement;
    } else {
        list->tail = replacement;
    }
    if(replacement->prev != NULL){
        replacement->prev->next = toReplace;
    } else {
        list->head = replacement;
    }
    toReplace->prev = replacement->prev;
    replacement->prev = toReplace;
    toReplace->next = replacement;
}

void swapListElements(List* list, ListElement* a, ListElement* b){
    if(a == b) return;
    if(a == NULL || b == NULL) return;

    ListElement aCopy = *a;
    ListElement bCopy = *b;

    if(a->next == b){
        swapAdjacent(list, a, b);
    } else if (b->next == a){
        swapAdjacent(list, b, a);
    } else {
        replaceElement(list, b, &aCopy);
        replaceElement(list, a, &bCopy);
    }
}

void freeList(List* list) {
    ListElement* current = list->head;
    while (current != NULL) {
        ListElement* next = current->next;
        free(current->data);
        current = next;
    }

    free(list->pool);
    free(list);
}

Node* createNode(void* data, size_t dataSize){
    Node* n = (Node*)malloc(sizeof(Node));
    if(!n){
        printf("ERR: createNode: malloc fail.\n");
    }
    for(int i = 0; i < D_COUNT; i++){
        n->conns[i] = NULL;
    }
    n->data = malloc(dataSize);
    n->data = data;
    return n;
}

Node* buildGraph(List* l){
    Node* current;
    
}