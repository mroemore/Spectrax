#include "graph_gui.h"

//helpers
static Vector2 getCenter(Rectangle r){
    return (Vector2){r.x + r.width/2, r.y + r.height/2};
}

static float linearDistance(Vector2 a, Vector2 b){
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

static float angleBetweenVector2(Vector2 v1, Vector2 v2) {
    double dot = v1.x * v2.x + v1.y * v2.y;

    double mag1 = sqrt(v1.x * v1.x + v1.y * v1.y);
    double mag2 = sqrt(v2.x * v2.x + v2.y * v2.y);

    double cosTheta = dot / (mag1 * mag2);

    cosTheta = fmax(-1.0, fmin(1.0, cosTheta));

    return acos(cosTheta);
}

// Box* createBox(int x, int y, int w, int h, Color c, int selected){
//     Box* b = (Box*)malloc(sizeof(Box));
//     if(!b){
//         printf("ERR: createBox: malloc fail.\n");
//     }
//     b->base.draw = drawBox;
//     b->base.bounds = (Rectangle){x,y,w,h};
//     b->c_selected = c;
//     b->c_default = GREEN;
//     b->selected = selected;

//     return b;
// }

// void drawBox(void* self){
//     Box* b = (Box*)self;
//     if(b->selected){
//         DrawRectangleLinesEx(b->base.bounds, 2, b->c_selected);
//     } else {
//         DrawRectangleLinesEx(b->base.bounds, 2, b->c_default);
//     }
// }

// void drawList(List* dl){
//     if(dl->count <= 0){
//         return;
//     }
//     ListElement* current = dl->head;
//     for(int i = 0; i < dl->count; i++){
//         printf("%i, ", i);
//         Drawable* d = (Drawable*)current->data;
//         d->draw(d);
//         current = current->next;
//     }

//     printf("\n");
// }

GuiNode* createGuiNode(int x, int y, int w, int h, int padding, DrawableAlignment da, NodeAlignment na, Color c, const char* name, int selected){
    GuiNode* gn = (GuiNode*)malloc(sizeof(GuiNode));
    if(!gn) return NULL;

    gn->container = NULL;
    gn->x = x;
    gn->y = y;
    gn->w = w;
    gn->h = h;
    gn->items = createList(128);
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
    printf("REFLOW! \n");
    if(!n) return;
    if(n->itemCount <= 0) return;

    int y_scalar = 0;
    int x_scalar = 0;
    int weightsAccumlator = 0;

    ListElement* current = n->items->head;
    for(int i = 0; i < n->itemCount; i++){
        printf(". ");
        GuiNode* cn = *(GuiNode**)current->data;
        printf("hello\n");
        printf("node weight: %i\n", *(int*)n->itemWeights->head->data);
        printf("node weight: %i\n", *(int*)cn->weightRef->data);
        printf("coords before reflow: %s (x:%i, y:%i, w:%i, h:%i)\n",
            cn->name,
            cn->x,
            cn->y,
            cn->w,
            cn->h
        );
        
        cn->x = n->x + n->padding;
        cn->y = n->y + n->padding;
        cn->w = n->w - n->padding * 2;
        cn->h = n->h - n->padding * 2;

        
        switch(n->nodeAlignment){
            case na_vertical:
                y_scalar = (n->h / n->totalItemWeights);
                cn->y = n->y + (y_scalar * weightsAccumlator) + (n->padding);
                cn->h = (y_scalar * (*(int*)cn->weightRef->data)) - (n->padding * 2);
                break;
            case na_horizontal:
                x_scalar = (n->w / n->totalItemWeights);
                cn->x = n->x + (x_scalar * weightsAccumlator) + (n->padding);
                cn->w = (x_scalar * (*(int*)cn->weightRef->data)) - (n->padding * 2);
                break;
        }

        weightsAccumlator += *(int*)cn->weightRef->data;
        reflowCoordinates(cn);
        current = current->next;
    }

    printf("\n");
}

void appendItem(GuiNode* parent, GuiNode* child, int weight){
    if(parent->itemCount >= MAX_NODE_CHILDREN) return;
    child->container = parent;    
    child->x = parent->x + parent->padding;
    child->y = parent->y + parent->padding;
    child->w = parent->w - parent->padding * 2;
    child->h = parent->h - parent->padding * 2;
    child->weightRef = appendToList(parent->itemWeights, (const void*)&weight, sizeof(int), 0);
    child->itemListRef = appendToList(parent->items, (const void*)&child, sizeof(GuiNode*), 0);
    parent->itemCount++;
    parent->totalItemWeights += weight;
    printf("%s weightref: %i\n", child->name, *(int*)child->weightRef->data);
    printf("weightref: %i\n", *(int*)parent->itemWeights->head->data);
    reflowCoordinates(parent);
}

void drawNode(GuiNode* cont){
    if(cont->selected){
        DrawRectangleLinesEx((Rectangle){cont->x, cont->y, cont->w, cont->h}, 2, WHITE);
    } else {
        DrawRectangleLinesEx((Rectangle){cont->x, cont->y, cont->w, cont->h}, 2, cont->c);
    }
    DrawRectangleLinesEx((Rectangle){cont->x + cont->padding, cont->y+ cont->padding, cont->w - cont->padding*2, cont->h- cont->padding*2}, 2, (Color){cont->c.r/2, cont->c.g/2, cont->c.b/2, cont->c.a});
    DrawText(cont->name, cont->x + cont->padding, cont->y+ cont->padding, 12, WHITE);

    if(cont->itemCount > 0){
        ListElement* current = cont->items->head;
        for(int i = 0; i < cont->itemCount; i++){
            GuiNode* gn = *(GuiNode**)current->data;
            drawBSP(gn);
            current = current->next;
        }
    }
    
}

Graph* createGraph(){
    Graph* g = (Graph*)malloc(sizeof(Graph));
    g->root = createGuiNode(0,0, SCREEN_W, SCREEN_H, 4, da_center, na_horizontal, RED, "root", 0);
    g->selected = NULL;
}

void navigateGraph(Graph* g, int keymapping){
    int success = 0;
    switch (g->selected->container->nodeAlignment){
        case na_horizontal:
            switch (keymapping){
                case KM_UP:
                    success = findClosestLeafLeft(g, na_horizontal);
                    printf("na_horizontal KM_UP result: %i\n", success);
                    break;
                case KM_DOWN:
                    success = findClosestLeafRight(g, na_horizontal);
                    printf("na_horizontal KM_DOWN result: %i\n", success);
                    break;
                case KM_LEFT:
                    success = selectRelative(g, g->selected, -1);
                        printf("na_horizontal KM_EFT result1: %i\n", success);
                    if(!success){
                        success = findClosestLeafRight(g, na_vertical);
                        printf("na_horizontal KM_EFT result2: %i\n", success);
                    }
                    break;
                case KM_RIGHT:
                    success = selectRelative(g, g->selected, 1);
                        printf("na_horizontal KM_RIGHT result1: %i\n", success);
                    if(!success){
                        success = findClosestLeafLeft(g, na_vertical);
                        printf("na_horizontal KM_RIGHT result2: %i\n", success);
                    }
                    break;
            }
            break;
        case na_vertical:
                switch (keymapping){
                    case KM_UP:
                        success = selectRelative(g, g->selected, -1);
                            printf("na_vertical KM_UP result1: %i\n", success);
                        if(!success){
                            success = findClosestLeafLeft(g, na_vertical);
                            printf("na_vertical KM_UP result2: %i\n", success);
                        }
                        break;
                    case KM_DOWN:
                        success = selectRelative(g, g->selected, 1);
                            printf("na_vertical KM_DOWN result1: %i\n", success);
                        if(!success){
                            success = findClosestLeafRight(g, na_vertical);
                            printf("na_vertical KM_DOWN result2: %i\n", success);
                        }
                        break;
                    case KM_LEFT:
                        success = findClosestLeafLeft(g, na_vertical);
                        printf("na_vertical KM_LEFT result: %i\n", success);
                        break;
                    case KM_RIGHT:
                        success = findClosestLeafRight(g, na_vertical);
                        printf("na_vertical KM_RIGHT result: %i\n", success);
                        break;
                }
            break;
    }
    
}

int findClosestLeafLeft(Graph* g, int alignment){
    if(g->selected->container == NULL) return 0;

    ListElement* previousNode = g->selected->container->itemListRef->prev;
    if(g->selected->container->nodeAlignment != alignment){
        previousNode = NULL;
    }
    GuiNode* current = NULL;
        
    if(previousNode != NULL){
        printf("PrevNode\n");

        current = *(GuiNode**)previousNode->data;
        selectTailLeaf(g, current);
        
        return 1;
    } else {
        current = g->selected->container;
        printf("Traversing upwards\n");

        if(current->container != NULL){
            current = current->container;
            while(current->container != NULL){ 
                if(current->container->nodeAlignment != alignment){
                    current = current->container;
                    printf(" NO: %s, %i", current->name, current->nodeAlignment);
                } else {
                    break;
                }
                printf("(%s) -> ", current->name);
            }
            printf("(%s)\n", current->name);
        }

        if(current == NULL){
            return 0;
        } else {
            selectTailLeaf(g, current);
            return 1;
        }
    }
}

int findClosestLeafRight(Graph* g, int alignment){

    if(g->selected->container == NULL) return 0;

    ListElement* previousNode = g->selected->container->itemListRef->next;
    if(g->selected->container->nodeAlignment != alignment){
        previousNode = NULL;
    }
    GuiNode* current = NULL;

    if(previousNode != NULL){
        current = *(GuiNode**)previousNode->data;

        printf("NextNode\n");
        selectHeadLeaf(g, current);
        
        return 1;
    } else {
        current = g->selected->container;
        printf("Traversing upwards\n");

        if(current->container != NULL){
            current = current->container;
            while(current->container != NULL){ 
                if(current->container->nodeAlignment != alignment){
                    current = current->container;
                    printf(" NO ");
                } else {
                    break;
                }
                printf("(%s) -> ", current->name);
            }
            printf("(%s)\n", current->name);
        }

        if(current == NULL){
            return 0;
        } else {
            selectHeadLeaf(g, current);
            return 1;
        }
    }
}

void selectTailLeaf(Graph* g, GuiNode* current) {
    if (g->selected != NULL) {
        g->selected->selected = 0;
    }
    printf("tail nav: \n");
    while (current->items->tail != NULL) {
        current = *(GuiNode**)current->items->tail->data;
        printf(" -> (%s)", current->name);
    }
    printf("\n");
    g->selected = current;
    g->selected->selected = 1;  
}

void selectHeadLeaf(Graph* g, GuiNode* current) {
    if (g->selected != NULL) {
        g->selected->selected = 0;
    }
    printf("head nav: \n");
    while (current->items->head != NULL) {
        current = *(GuiNode**)current->items->head->data;
        printf(" -> (%s)", current->name);
    }
    printf("\n");
    g->selected = current;
    g->selected->selected = 1;
}

int selectRelative(Graph* g, GuiNode* selected, int direction){
    switch(direction){
        case -1:
            if(selected->itemListRef->prev != NULL){
                GuiNode* previous = *(GuiNode**)selected->itemListRef->prev->data;
                selectTailLeaf(g, previous);
                
                return 1;
            } else {
                return 0;
            }
            break;
        case 1:
            if(selected->itemListRef->next != NULL){
                GuiNode* next = *(GuiNode**)selected->itemListRef->next->data;
                selectHeadLeaf(g, next);
                return 1;
            } else {
                return 0;
            }
            break;
    }
}