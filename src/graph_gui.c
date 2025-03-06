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

GuiNode* createGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, Color c, const char* name, bool selectable, bool selected){
    GuiNode* gn = (GuiNode*)malloc(sizeof(GuiNode));
    if(!gn) return NULL;
    if(x < 0 || x > SCREEN_W || y < 0 || y > SCREEN_H || w < 0 || w > SCREEN_W || h < 0 || h > SCREEN_H){
        printf("error: invalid GuiNode dimensions. [x:%i y:%i w:%i h:%i ]\n", x, y, w, h);
        free(gn);
        return NULL;
    }
    gn->container = NULL;
    gn->x = x;
    gn->y = y;
    gn->w = w;
    gn->h = h;
    gn->resizeable = true;
    gn->items = createList(16);
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
    gn->selectable = selectable;
    gn->itemWeights = createList(16);
    gn->totalItemWeights = 0;
    gn->itemCount = 0;
    gn->hasSelectableItems = false;
    gn->c = c;
    if(na >= nodeAlignmentCount){
        printf("error: invalid alignment %i", na);
        free(gn);
        return NULL;
    }
    gn->nodeAlignment = na;
    return gn;
}

GuiNode* createInputGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, Color c, const char* name, bool selected, OnPressCallback callback, Parameter* p){
    GuiNode* gn = createGuiNode(x,y,w,h, padding, na, c, name, 1, selected);
    if(gn == NULL){
        printf("InputGuiNode error, could not create.");
        return NULL;
    }
    gn->callback = callback;
    gn->p = p;
    return gn;
}

GuiNode* createBlankGuiNode(){
    GuiNode* gn = createGuiNode(0,0,0,0,0, na_horizontal, WHITE, "blank", 0, 0);
    if(gn == NULL){
        printf("BlankGuiNode error, could not create.");
        return NULL;
    }
    return gn;
}

void appendFMInstControlNode(Graph* g, GuiNode* container, char* name, int weight, bool selected, Instrument *inst){
    GuiNode* btnwrap = createGuiNode(0,0,100,100, 5, na_vertical, RED, "bwrp1", 0,0);

    GuiNode* btnrow1 = createGuiNode(0,0,100,100, 5, na_horizontal, ORANGE, "br1", 0,0);
    GuiNode* btnrow2 = createGuiNode(0,0,100,100, 5, na_horizontal, ORANGE, "br2", 0,0);

    GuiNode* rat1 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b1", 0, incParameterBaseValue, inst->ops[0]->ratio);
    GuiNode* fb1 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b2", 0, incParameterBaseValue, inst->ops[0]->feedbackAmount);
    GuiNode* rat2 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b3", 0, incParameterBaseValue, inst->ops[1]->ratio);
    GuiNode* fb2 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b4", 0, incParameterBaseValue, inst->ops[1]->feedbackAmount);
    GuiNode* rat3 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b5", 0, incParameterBaseValue, inst->ops[2]->ratio);
    GuiNode* fb3 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b6", 0, incParameterBaseValue, inst->ops[2]->feedbackAmount);
    GuiNode* rat4 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b7", 0, incParameterBaseValue, inst->ops[3]->ratio);
    GuiNode* fb4 = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b8", 0, incParameterBaseValue, inst->ops[3]->feedbackAmount);
    GuiNode* alg = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b9", 0, incParameterBaseValue, inst->selectedAlgorithm);

    if(selected){ g->selected = rat1;}

    GuiNode* sp1 = createBlankGuiNode();
    GuiNode* sp2 = createBlankGuiNode();
    GuiNode* sp3 = createBlankGuiNode();
    GuiNode* sp4 = createBlankGuiNode();
    GuiNode* sp5 = createBlankGuiNode();

    appendItem(btnrow1,rat1,2);
    appendItem(btnrow1,fb1,2);
    appendItem(btnrow1,sp1,1);
    appendItem(btnrow1,rat2,2);
    appendItem(btnrow1,fb2,2);
    appendItem(btnrow1,sp2,1);
    appendItem(btnrow1,sp3,2);

    appendItem(btnrow2,rat3,2);
    appendItem(btnrow2,fb3,2);
    appendItem(btnrow2,sp4,1);
    appendItem(btnrow2,rat4,2);
    appendItem(btnrow2,fb4,2);
    appendItem(btnrow2,sp5,1);
    appendItem(btnrow2,alg,2);

    appendItem(btnwrap,btnrow1,1);
    appendItem(btnwrap,btnrow2,1);

    appendItem(container,btnwrap,weight);
}

void appendADEnvControlNode(Graph* g, GuiNode* container, char* name, int weight, bool selected, Envelope *env){
    GuiNode* envwrap = createGuiNode(0,0,100,100, 5, na_vertical, RED, "envwrp", 0,0);

    GuiNode* ar = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b1", selected, incParameterBaseValue, env->stages[0].duration);
    GuiNode* ac = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b1", 0, incParameterBaseValue, env->stages[0].curvature);
    GuiNode* dr = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b1", 0, incParameterBaseValue, env->stages[1].duration);
    GuiNode* dc = createInputGuiNode(0,0,100,100, 5, na_horizontal, RED, "b1", 0, incParameterBaseValue, env->stages[0].curvature);
    if(selected){ g->selected = ar;}

    GuiNode* sp1 = createBlankGuiNode();

    appendItem(envwrap,ar,2);
    appendItem(envwrap,ac,2);
    appendItem(envwrap,dr,2);
    appendItem(envwrap,dc,2);
    appendItem(envwrap,sp1,1);

    appendItem(container,envwrap,weight);
}

Graph* createFMInstGraph(Instrument* inst, bool selected){
    Graph* fmi = createGraph();
    GuiNode* margin1 = createBlankGuiNode();
    GuiNode* margin2 = createBlankGuiNode();
    GuiNode* pad1 = createBlankGuiNode();
    GuiNode* pad2 = createBlankGuiNode();

    GuiNode* fmwrap = createGuiNode(0,0,100,100, 5, na_vertical, RED, "fm_wrap", 0,0);
    appendItem(fmwrap, pad1,2);
    appendFMInstControlNode(fmi, fmwrap, "fmctrl", 4, selected, inst);
    appendItem(fmwrap, pad2,2);

    GuiNode* modwrap = createGuiNode(0,0,100,100, 5, na_vertical, RED, "mod_wrap", 0,0);
    for(int i = 0; i < inst->envelopeCount; i++){
        appendADEnvControlNode(fmi, modwrap, "mod", 1, false, inst->envelopes[0]);
    }
    appendItem(fmwrap, modwrap,8);
    appendItem(fmi->root, margin1,1);
    appendItem(fmi->root, fmwrap,8);
    appendItem(fmi->root, margin2,1);
    return fmi;
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
    if(child->selectable || child->hasSelectableItems){
        parent->hasSelectableItems = true;
    }
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
            drawNode(gn);
            current = current->next;
        }
    }
    
}

Graph* createGraph(){
    Graph* g = (Graph*)malloc(sizeof(Graph));
    g->root = createGuiNode(0,0, SCREEN_W, SCREEN_H, 4, na_horizontal, RED, "root", false, false);
    g->selected = NULL;
}

void navigateGraph(Graph* g, int keymapping){
    printf("NEW INPUT--------- \n\n\n");
    GuiNode* cont = g->selected->container;
    GuiNode* r;
    switch(cont->nodeAlignment){
        case na_vertical:
            switch (keymapping){
                case KM_UP:
                    navAdjacent(g, g->selected, na_vertical, true, false);
                    break;
                case KM_DOWN:
                    navAdjacent(g, g->selected, na_vertical, false, true);
                    break;
                case KM_LEFT:
                    r = searchUpwardsByAlignment(g->selected, na_horizontal, true);
                    //r = getAdjacentNode(r, true);
                    if(r != NULL){
                        selectLeaf(g, r, false);
                    }
                    break;
                case KM_RIGHT:
                    r = searchUpwardsByAlignment(g->selected, na_horizontal, false);
                    //r = getAdjacentNode(r, false);
                    if(r != NULL){
                        selectLeaf(g, r, true);
                    }
                    break;
            }
            break;
        case na_horizontal:
            switch (keymapping){
                case KM_UP:
                    r = searchUpwardsByAlignment(g->selected, na_vertical, true);
                    if(r != NULL){
                        printf("using adjacent node [ %s ]\n", r->name);
                        selectLeaf(g, r, false);
                    }else {
                        printf("parents prev is null\n");
                    }
                    break;
                case KM_DOWN:
                    r = searchUpwardsByAlignment(g->selected, na_vertical, false);
                    if(r != NULL){
                        printf("using adjacent node [ %s ]\n", r->name);
                        selectLeaf(g, r, true);
                    } else {
                        printf("parents next is null\n");
                    }
                    break;
                case KM_LEFT:
                    navAdjacent(g, g->selected, na_vertical, true, false);
                    break;
                case KM_RIGHT:
                    navAdjacent(g, g->selected, na_vertical, false, true);
                    break;
            }
            break;
    }
}


bool selectLeaf(Graph* g, GuiNode* n, bool head){
    printf("[SL] leaf search... \n");
    if(n == NULL){
    printf("[SL] leaf null... \n");
        return false;
    }
    if(n->selectable){
    printf("[SL] picking leaf... \n");

        changeGraphSelection(g,n);
        return true;
    } else if(n->hasSelectableItems){
    printf("[SL] iterating items... \n");
        ListElement* l;
        if(head){
            l = n->items->head;
        } else {
            l = n->items->tail;
        }
        if(l!=NULL){
            n = *(GuiNode**)l->data;
        } else {
            n = NULL;
        }
        while (n != NULL)
        {
            if(n != NULL){
                printf("[SL] trying [ %s ]... ", n->name);
                if(selectLeaf(g, n, head)){
                    printf("[SL] leaf selectable... \n");
                    return true;
                }
            } else {
                return false;
            }
            n = getAdjacentNode(n, !head);
        }
    } else {
        printf("[SL] leaf not found... \n");
        return false;
    }
}

GuiNode* searchUpwardsByAlignment(GuiNode* n, NodeAlignment na, bool prev){
    printf("searching up...\n");

    GuiNode* result = NULL;
    n = n->container;
    while (n->container != NULL)
    {
        if(n != NULL){
            printf("[SU] trying [ %s ], ", n->name);
        }
        
        if(n->container->nodeAlignment == na){
            printf("\n[SU]found correct alignment. CONTAINER: [ %s ] RETURN: [ %s ]\n", n->container->name, n->name);
            GuiNode* adjacent = getAdjacentNode(n, prev);
            while(adjacent != NULL){
                if(adjacent->hasSelectableItems){
                    result = adjacent;
                    break;
                } else {
                    printf("adjacent node has no selectable items.\n");
                }
                adjacent = getAdjacentNode(adjacent, prev);
            }
            if(result != NULL){
                break;
            } else {
                printf("no adjacent node within container.\n");
            }
            
        }
        n = n->container;
    }
    if(n == NULL){
        printf("no container of alignment [%i] found with adjacent item in direction [%i]", na, prev);
    }
    return result;
}

GuiNode* getAdjacentNode(GuiNode* c, bool prev){
    ListElement* l;
    if(prev){
        l = c->itemListRef->prev;
    } else {
        l = c->itemListRef->next;
    }
    if(l==NULL) {
        printf("adj was null. %i\n", prev);
        c = NULL;
    } else {
        c = *(GuiNode**)l->data;
    }
    return c;
}

GuiNode* selectAdjacent(Graph* g, GuiNode* c, bool prev){
    printf("selecting...\n");
    GuiNode* result = getAdjacentNode(c, prev);
    while (result != NULL)
    {
        if(result->selectable){
            printf("found adjacent selectable: %s\n", result->name);
            changeGraphSelection(g, result);
            break;
        }
        result = getAdjacentNode(result, prev);
    }

    return result;
}

void changeGraphSelection(Graph* g, GuiNode* new){
    printf("de-selecting: %s \n", g->selected->name);

    g->selected->selected = 0;
    g->selected = new;
    g->selected->selected = 1;
    printf("selected: %s \n", g->selected->name);
}

void navAdjacent(Graph* g, GuiNode* n, NodeAlignment na, bool prev, bool head){
    GuiNode* res = selectAdjacent(g, g->selected, prev);
    if(res == NULL){
        res = searchUpwardsByAlignment(g->selected, na, prev);
        if(res != NULL){
            // if(na_horizontal != na)res = res->container; //fix this hack
            // res = getAdjacentNode(res, prev);
            if(selectLeaf(g, res, head)){
                printf("gotit.\n");
            } else {
                printf("leaf selection fail.\n");
            }
        } else {
            printf("upnav fail.\n");
        }
    }else {
        printf("adjacent nav fail.\n");
    }
}