#include "graph_gui.h"
#include "dstruct.h"
#include "modsystem.h"
#include <string.h>

bool initGuiNode(GuiNode *gn, int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selectable, bool selected) {
	bool success = true;
	if(x < 0 || x > SCREEN_W || y < 0 || y > SCREEN_H || w < 0 || w > SCREEN_W || h < 0 || h > SCREEN_H) {
		printf("error: invalid GuiNode dimensions. [x:%i y:%i w:%i h:%i ]\n", x, y, w, h);
		return false;
	}
	gn->container = NULL;
	gn->x = x;
	gn->y = y;
	gn->w = w;
	gn->h = h;
	gn->resizeable = true;
	gn->items = createList(16);
	gn->itemWeights = createList(16);
	const char *defaultName = "unnamed";
	const char *actualName = (name && strlen(name) > 0) ? name : defaultName;
	gn->name = malloc(strlen(actualName) + 1);
	if(!gn->name) {
		freeList(gn->items);
		freeList(gn->itemWeights);
		return false;
	}
	strcpy(gn->name, actualName);
	// printf("creation name: %s\n", gn->name);

	gn->padding = padding;
	gn->weightRef = NULL;
	gn->selected = selected;
	gn->selectable = selectable;
	gn->totalItemWeights = 0;
	gn->itemCount = 0;
	gn->drawable = 0;
	gn->hasSelectableItems = false;
	if(na >= nodeAlignmentCount) {
		printf("error: invalid alignment %i", na);
		success = false;
	}
	gn->nodeAlignment = na;
	gn->customNav = NULL;
	printf("NA: %i, ", gn->nodeAlignment);
	return success;
}

GuiNode *createGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selectable, bool selected) {
	GuiNode *gn = malloc(sizeof(GuiNode));
	if(!gn) {
		printf("error: Graphnode malloc failed.\n");
		return NULL;
	}

	if(!initGuiNode(gn, x, y, w, h, padding, na, name, selectable, selected)) {
		free(gn);
		printf("\n\n\nerror: Graphnode init failed.\n\n\n");

		return NULL;
	}

	return gn;
}

GuiNode *createBlankGuiNode() {
	return createNamedBlankGuiNode("blank");
}

GuiNode *createNamedBlankGuiNode(char *name) {
	GuiNode *gn = createGuiNode(0, 0, 0, 0, 0, na_horizontal, name, 0, 0);
	if(!gn) {
		printf("BlankGuiNode error, could not create.");
	}
	return gn;
}

void freeGuiNode(GuiNode *gn) {
	if(gn->itemCount > 0) {
		ListElement *current = gn->items->head;
		for(int i = 0; i < gn->itemCount; i++) {
			GuiNode *cn = *(GuiNode **)current->data;
			freeGuiNode(cn);
			current = current->next;
		}
	}

	free(gn);
}

void printGraph(GuiNode *root, int depth) {
	if(root == NULL) {
		printf("\nNULL NODE!\n");
		return;
	}
	for(int tabs = 0; tabs <= depth; tabs++) {
		printf("\t");
	}
	bool hasCB = root->callback != NULL;
	int weight = -1;
	if(root->weightRef != NULL) {
		weight = *(int *)root->weightRef->data;
	}
	char alignment[6];
	if(root->nodeAlignment == na_vertical) {
		strcpy(alignment, "VERTI");
	} else {
		strcpy(alignment, "HORIZ");
	}

	printf("'%s'<%s> - [ ItmC: %i, HasSelItems: %i, Selectable:%i, IsSelected:%i, Drw: %i, CB: %i, Wgt: %i]\n", root->name, alignment, root->itemCount, root->hasSelectableItems, root->selectable, root->selected, root->drawable, hasCB, weight);
	if(root->itemCount > 0) {
		ListElement *current = root->items->head;
		for(int i = 0; i < root->itemCount; i++) {
			GuiNode *cn = *(GuiNode **)current->data;
			printGraph(cn, depth + 1);
			current = current->next;
		}
	}
}

void reflowCoordinates(GuiNode *n) {
	if(!n) {
		return;
	}
	if(n->itemCount <= 0) {
		return;
	}

	int y_scalar = 0;
	int x_scalar = 0;
	int weightsAccumlator = 0;

	ListElement *current = n->items->head;
	for(int i = 0; i < n->itemCount; i++) {
		GuiNode *cn = *(GuiNode **)current->data;

		cn->x = n->x + n->padding;
		cn->y = n->y + n->padding;
		cn->w = n->w - n->padding * 2;
		cn->h = n->h - n->padding * 2;

		switch(n->nodeAlignment) {
			case na_vertical:
				y_scalar = (n->h / n->totalItemWeights);
				cn->y = n->y + (y_scalar * weightsAccumlator) + (n->padding);
				cn->h =
				  (y_scalar * (*(int *)cn->weightRef->data)) - (n->padding * 2);
				break;
			case na_horizontal:
				x_scalar = (n->w / n->totalItemWeights);
				cn->x = n->x + (x_scalar * weightsAccumlator) + (n->padding);
				cn->w = (x_scalar * (*(int *)cn->weightRef->data)) - (n->padding * 2);
				break;
		}

		weightsAccumlator += *(int *)cn->weightRef->data;
		reflowCoordinates(cn);
		current = current->next;
	}
}

void appendItem(GuiNode *parent, GuiNode *child, int weight) {
	if(parent->itemCount >= MAX_NODE_CHILDREN) {
		return;
	}
	child->container = parent;
	child->x = parent->x + parent->padding;
	child->y = parent->y + parent->padding;
	child->w = parent->w - parent->padding * 2;
	child->h = parent->h - parent->padding * 2;
	child->weightRef =
	  appendToList(parent->itemWeights, (const void *)&weight, sizeof(int), 0);
	child->itemListRef =
	  appendToList(parent->items, (const void *)&child, sizeof(GuiNode *), 0);
	parent->itemCount++;
	parent->totalItemWeights += weight;
	if(child->selectable || child->hasSelectableItems) {
		parent->hasSelectableItems = true;
	}
	reflowCoordinates(parent);
}

void drawNode(GuiNode *cont) {
#ifdef DEBUG_GRAPH_DRAW
	if(cont->selected) {
		DrawRectangleLinesEx((Rectangle){ cont->x, cont->y, cont->w, cont->h }, 4, WHITE);
	} else if(cont->selectable) {
		DrawRectangleLinesEx((Rectangle){ cont->x, cont->y, cont->w, cont->h }, 2, RED);
	} else if(cont->hasSelectableItems) {
		DrawRectangleLinesEx((Rectangle){ cont->x, cont->y, cont->w, cont->h }, 2, YELLOW);
	} else if(cont->drawable) {
		DrawRectangleLinesEx((Rectangle){ cont->x, cont->y, cont->w, cont->h }, 2, ORANGE);
	} else {
		DrawRectangleLinesEx((Rectangle){ cont->x, cont->y, cont->w, cont->h }, 2, BROWN);
	}
	DrawRectangleLinesEx(
	  (Rectangle){ cont->x + cont->padding, cont->y + cont->padding, cont->w - cont->padding * 2, cont->h - cont->padding * 2 },
	  2,
	  GRAY);
	DrawText(cont->name, cont->x + cont->padding, cont->y + cont->padding, 12, WHITE);
#endif
	if(cont->drawable) {
		cont->draw(cont);
	}
	if(cont->itemCount > 0) {
		ListElement *current = cont->items->head;
		for(int i = 0; i < cont->itemCount; i++) {
			GuiNode *gn = *(GuiNode **)current->data;
			drawNode(gn);
			current = current->next;
		}
	}
}

Graph *createGraph(NodeAlignment na) {
	Graph *g = (Graph *)malloc(sizeof(Graph));
	g->root = createGuiNode(0, 0, SCREEN_W, SCREEN_H, 0, na, "root", false, false);
	g->selected = NULL;
	return g;
}

void navigateGraph(Graph *g, int keymapping) {
	printf("----------\nNEW INPUT: %s\n----------\n\n", KEY_NAMES[keymapping]);
	if(g->selected->customNav) {
		bool navSuccess = g->selected->customNav(g->selected, keymapping);
		printf("CUSTOM! %i\n", navSuccess);
		if(navSuccess) {
			return;
		}
	}
	GuiNode *cont = g->selected->container;
	// printf("selected: %s\n", g->selected);
	//  printf("cont: %s\n", cont->name);
	GuiNode *r;
	switch(cont->nodeAlignment) {
		case na_vertical:
			switch(keymapping) {
				case KM_UP:
					navAdjacent(g, g->selected, na_vertical, true, false);
					break;
				case KM_DOWN:
					navAdjacent(g, g->selected, na_vertical, false, true);
					break;
				case KM_LEFT:
					r = searchUpwardsByAlignment(g->selected, na_horizontal, true);
					// r = getAdjacentNode(r, true);
					if(r != NULL) {
						selectLeaf(g, r, false);
					}
					break;
				case KM_RIGHT:
					r = searchUpwardsByAlignment(g->selected, na_horizontal, false);
					// r = getAdjacentNode(r, false);
					if(r != NULL) {
						selectLeaf(g, r, true);
					}
					break;
			}
			break;
		case na_horizontal:
			switch(keymapping) {
				case KM_UP:
					r = searchUpwardsByAlignment(g->selected, na_vertical, true);
					if(r != NULL) {
						printf("using adjacent node [ %s ]\n", r->name);
						selectLeaf(g, r, false);
					} else {
						printf("parents prev is null\n");
					}
					break;
				case KM_DOWN:
					r = searchUpwardsByAlignment(g->selected, na_vertical, false);
					if(r != NULL) {
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

bool selectLeaf(Graph *g, GuiNode *n, bool head) {
	printf("[SL] leaf search... \n");
	if(n == NULL) {
		printf("[SL] leaf null... \n");
		return false;
	}
	if(n->selectable) {
		printf("[SL] picking leaf... \n");

		changeGraphSelection(g, n);
		return true;
	} else if(n->hasSelectableItems) {
		printf("[SL] iterating items... \n");
		ListElement *l;
		if(head) {
			l = n->items->head;
		} else {
			l = n->items->tail;
		}
		if(l != NULL) {
			n = *(GuiNode **)l->data;
		} else {
			n = NULL;
		}
		while(n != NULL) {
			if(n != NULL) {
				printf("[SL] trying [ %s ]... ", n->name);
				if(selectLeaf(g, n, head)) {
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

GuiNode *searchUpwardsByAlignment(GuiNode *n, NodeAlignment na, bool prev) {
	printf("searching up...\n");

	GuiNode *result = NULL;
	n = n->container;
	while(n->container != NULL) {
		if(n != NULL) {
			printf("[SU] trying [ %s ], ", n->name);
		}

		if(n->container->nodeAlignment == na) {
			printf("\n[SU]found correct alignment. CONTAINER: [ %s ] RETURN: [ %s ]\n", n->container->name, n->name);
			GuiNode *adjacent = getAdjacentNode(n, prev);
			while(adjacent != NULL) {
				if(adjacent->hasSelectableItems || adjacent->selectable) {
					result = adjacent;
					break;
				} else {
					printf("adjacent node ['%s'] has no selectable items and is not selectable.\n", adjacent->name);
				}
				adjacent = getAdjacentNode(adjacent, prev);
			}
			if(result != NULL) {
				break;
			} else {
				printf("no adjacent node within container.\n");
			}
		}
		n = n->container;
	}
	if(n == NULL) {
		printf("no container of alignment [%i] found with adjacent item in "
		       "direction [%i]",
		       na,
		       prev);
	}
	return result;
}

GuiNode *getAdjacentNode(GuiNode *c, bool prev) {
	ListElement *l;
	if(prev) {
		printf("[%s] PREV ", c->name);
		l = c->itemListRef->prev;
	} else {
		printf("[%s] NEXT ", c->name);
		l = c->itemListRef->next;
	}
	if(l == NULL) {
		printf("adj was null, returning [%s]\n", c->name);
		c = NULL;
	} else {
		c = *(GuiNode **)l->data;
		printf("adj [%s] found.\n", c->name);
	}
	return c;
}

GuiNode *selectAdjacent(Graph *g, GuiNode *c, bool prev) {
	printf("selecting...\n");
	GuiNode *result = getAdjacentNode(c, prev);
	while(result != NULL) {
		if(result->selectable) {
			printf("found adjacent selectable: %s\n", result->name);
			changeGraphSelection(g, result);
			break;
		}
		if(result->hasSelectableItems) {
			selectLeaf(g, result, prev);
			return g->selected;
		}
		result = getAdjacentNode(result, prev);
	}

	return result;
}

void changeGraphSelection(Graph *g, GuiNode *new) {
	printf("de-selecting: %s \n", g->selected->name);

	g->selected->selected = 0;
	g->selected = new;
	g->selected->selected = 1;
	printf("selected: %s \n", g->selected->name);
}

void navAdjacent(Graph *g, GuiNode *n, NodeAlignment na, bool prev, bool head) {
	GuiNode *res = selectAdjacent(g, g->selected, prev);
	if(res == NULL) {
		res = searchUpwardsByAlignment(g->selected, na, prev);
		if(res != NULL) {
			// if(na_horizontal != na)res = res->container; //fix this hack
			// res = getAdjacentNode(res, prev);
			if(selectLeaf(g, res, head)) {
				printf("gotit.\n");
			} else {
				printf("leaf selection fail.\n");
			}
		} else {
			printf("upnav fail.\n");
		}
	} else {
		printf("adjacent nav fail.\n");
	}
}
