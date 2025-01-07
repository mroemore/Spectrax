#include <stdlib.h>
#include <stdio.h>
#include "raylib.h" // Include Raylib header

#define MAX_IC_SIZE 64
#define MAX_CONTAINERS 512
#define MAX_CONTAINER_CHILDREN 64

typedef void (*DrawCallback)(void* self);

typedef struct {
    DrawCallback draw;
} Drawable;

typedef struct {
    Drawable base;
    int x;
    int y;
    int selected;
} InputGui;

typedef struct InputContainer {
    InputGui* inputGui[MAX_IC_SIZE];
    int inputGuiCount;
    int selectedInputIndex;
    struct InputContainer** children;
    int childCount;
    int selectedChildIndex;
    struct InputContainer* parent;
    struct ContainerSearchIndex* index;
    InputGui* selectedInputGui;
} InputContainer;

typedef struct ContainerSearchIndex {
    int visited;
    int id;
    struct InputContainer* c;
    struct ContainerSearchIndex* next;
    struct ContainerSearchIndex* prev;
} ContainerSearchIndex;

typedef struct {
    ContainerSearchIndex* head;
    ContainerSearchIndex* tail;
    int containerCount;
    InputContainer* selected;
} ContainerMap;

// Function prototypes
void drawInputGui(void* self);
InputGui* createInputGui(int x, int y);
void addInputGuiToContainer(InputContainer* ic, InputGui* ig);
InputContainer* createInputContainer(ContainerMap* cm);
ContainerMap* createContainerMap();
void addChildToContainer(InputContainer* parent, InputContainer* child);
void removeChildFromContainer(InputContainer* parent, InputContainer* child);
void removeGuiFromContainer(InputContainer* ic, InputGui* ig);
void addContainerToMap(ContainerMap* cm, InputContainer* ic);
void freeContainer(InputContainer* ic);
InputGui* getFirstInputGui(InputContainer* ic);
InputGui* getLastInputGui(InputContainer* ic);
void selectInputGui(InputContainer* ic, InputGui* ig);
void setSelectedInputGui(InputContainer* ic, InputGui* ig);
void navigateContainer(InputContainer* root, int rowDelta, int colDelta);
void drawContainer(InputContainer* ic); // New function to draw the container

// Draw function for InputGui
void drawInputGui(void* self) {
    InputGui* ig = (InputGui*)self;
    if (ig->selected) {
        DrawRectangle(ig->x, ig->y, 5, 5, GREEN); // Draw green if selected
    } else {
        DrawRectangle(ig->x, ig->y, 5, 5, RED); // Draw red if not selected
    }
}

// Function to draw all InputGui elements in a container
void drawContainer(InputContainer* ic) {
    if (!ic) return;

    // Draw all InputGui elements in the container
    for (int i = 0; i < ic->inputGuiCount; i++) {
        if (ic->inputGui[i]) {
            ic->inputGui[i]->base.draw(ic->inputGui[i]);
        }
    }

    // Recursively draw child containers
    for (int i = 0; i < ic->childCount; i++) {
        if (ic->children[i]) {
            drawContainer(ic->children[i]);
        }
    }
}

InputGui* createInputGui(int x, int y) {
    InputGui* ig = (InputGui*)malloc(sizeof(InputGui));
    if (!ig) return NULL;
    ig->base.draw = drawInputGui;
    ig->x = x;
    ig->y = y;
    ig->selected = 0;
    return ig;
}

void addInputGuiToContainer(InputContainer* ic, InputGui* ig) {
    if (!ic || !ig || ic->inputGuiCount >= MAX_IC_SIZE) {
        return;
    }
    ic->inputGui[ic->inputGuiCount] = ig;
    ic->inputGuiCount++;
}

InputContainer* createInputContainer(ContainerMap* cm) {
    InputContainer* ic = (InputContainer*)malloc(sizeof(InputContainer));
    if (!ic) {
        return NULL;
    }

    ic->children = (InputContainer**)malloc(sizeof(InputContainer*) * MAX_CONTAINER_CHILDREN);
    if (!ic->children) {
        free(ic);
        return NULL;
    }

    for (int i = 0; i < MAX_CONTAINER_CHILDREN; i++) {
        ic->children[i] = NULL;
    }

    ic->childCount = 0;
    ic->selectedChildIndex = 0;
    ic->selectedInputIndex = 0;
    ic->parent = NULL;
    ic->selectedInputGui = NULL;
    addContainerToMap(cm, ic);

    return ic;
}

ContainerMap* createContainerMap() {
    ContainerMap* cm = (ContainerMap*)malloc(sizeof(ContainerMap));
    if (!cm) return NULL;
    cm->head = NULL;
    cm->tail = NULL;
    cm->containerCount = 0;
    cm->selected = NULL;
    return cm;
}

void addChildToContainer(InputContainer* parent, InputContainer* child) {
    if (!parent || !child || parent->childCount >= MAX_CONTAINER_CHILDREN) {
        return;
    }
    parent->children[parent->childCount] = child;
    parent->childCount++;
    child->parent = parent;
}

void removeChildFromContainer(InputContainer* parent, InputContainer* child) {
    for (int i = 0; i < parent->childCount; i++) {
        if (parent->children[i] == child) {
            parent->children[i] = NULL;
            break;
        }
    }
}

void removeGuiFromContainer(InputContainer* ic, InputGui* ig) {
    for (int i = 0; i < ic->inputGuiCount; i++) {
        if (ic->inputGui[i] == ig) {
            ic->inputGui[i] = NULL;
            break;
        }
    }
}

void addContainerToMap(ContainerMap* cm, InputContainer* ic) {
    if (!cm || !ic) return;
    ContainerSearchIndex* csi = (ContainerSearchIndex*)malloc(sizeof(ContainerSearchIndex));
    if (!csi) return;
    csi->c = ic;
    csi->next = NULL;
    csi->prev = NULL;
    csi->id = cm->containerCount;
    if (cm->head == NULL) {
        cm->head = csi;
        cm->tail = csi;
    } else {
        cm->tail->next = csi;
        csi->prev = cm->tail;
        cm->tail = csi;
    }
    cm->containerCount++;
}

void freeContainer(InputContainer* ic) {
    if (!ic) return;
    free(ic->children);
    free(ic);
}

InputGui* getFirstInputGui(InputContainer* ic) {
    if (!ic || ic->inputGuiCount == 0) return NULL;
    return ic->inputGui[0];
}

InputGui* getLastInputGui(InputContainer* ic) {
    if (!ic || ic->inputGuiCount == 0) return NULL;
    return ic->inputGui[ic->inputGuiCount - 1];
}

void selectInputGui(InputContainer* ic, InputGui* ig) {
    if (!ic || !ig) return;
    if (ic->selectedInputGui) {
        ic->selectedInputGui->selected = 0;
    }
    ic->selectedInputGui = ig;
    ic->selectedInputGui->selected = 1;
}

void setSelectedInputGui(InputContainer* ic, InputGui* ig) {
    if (!ic || !ig) return;
    if (ic->selectedInputGui) {
        ic->selectedInputGui->selected = 0; // Deselect the current InputGui
    }
    ic->selectedInputGui = ig;
    ic->selectedInputGui->selected = 1; // Select the new InputGui

    // Debug print
    printf("Selected InputGui at (%d, %d)\n", ig->x, ig->y);
}

void navigateContainer(InputContainer* root, int rowDelta, int colDelta) {
    if (!root) return;

    InputContainer* currentContainer = root;

    // Vertical navigation (rowDelta)
    if (rowDelta != 0) {
        int newIndex = currentContainer->selectedInputIndex + rowDelta;

        // Check if newIndex is within bounds of the current container's InputGui array
        if (newIndex >= 0 && newIndex < currentContainer->inputGuiCount) {
			printf("row index %i in bounds\n", newIndex);
            // Select the new InputGui
            setSelectedInputGui(currentContainer, currentContainer->inputGui[newIndex]);
            currentContainer->selectedInputIndex = newIndex;
        } else {
            // Move to sibling containers if possible
			printf("row index %i not in bounds\n", newIndex);
			
            if (currentContainer->parent) {
			printf("has parent\n");

                int siblingIndex = currentContainer->parent->selectedChildIndex + (rowDelta > 0 ? 1 : -1);

                if (siblingIndex >= 0 && siblingIndex < currentContainer->parent->childCount) {
                    InputContainer* sibling = currentContainer->parent->children[siblingIndex];
                    if (sibling->inputGuiCount > 0) {
                        // Select the first InputGui in the sibling container
                        setSelectedInputGui(sibling, sibling->inputGui[0]);
                        sibling->selectedInputIndex = 0;
                        currentContainer->parent->selectedChildIndex = siblingIndex;
                    }
                }
            } else {
			printf("has no parent :( \n");

                // Clamp to the first or last InputGui in the current container
                if (newIndex < 0) {
				printf("row index %i not in bounds\n", newIndex);

                    setSelectedInputGui(currentContainer, getFirstInputGui(currentContainer));
                    currentContainer->selectedInputIndex = 0;
                } else {
			printf("row index %i not in bounds !??\n", newIndex);

                    setSelectedInputGui(currentContainer, getLastInputGui(currentContainer));
                    currentContainer->selectedInputIndex = currentContainer->inputGuiCount - 1;
                }
            }
        }
    }

    // Horizontal navigation (colDelta)
    if (colDelta != 0) {
        if (colDelta > 0) {
            // Move to the first InputGui of the first child container
            if (currentContainer->childCount > 0) {
                InputContainer* child = currentContainer->children[0];
                if (child->inputGuiCount > 0) {
                    setSelectedInputGui(child, child->inputGui[0]);
                    child->selectedInputIndex = 0;
                    currentContainer->selectedChildIndex = 0;
                }
            }
        } else {
            // Move to the parent container's selected InputGui
            if (currentContainer->parent) {
                InputContainer* parent = currentContainer->parent;
                if (parent->inputGuiCount > 0) {
                    setSelectedInputGui(parent, parent->inputGui[parent->selectedInputIndex]);
                }
            }
        }
    }
}

int main() {
    // Initialize Raylib
    InitWindow(800, 600, "InputGui Example");
    SetTargetFPS(60);

    // Create containers and InputGuis
    ContainerMap* cm = createContainerMap();
    if (!cm) return 1;

    InputContainer* rootIC = createInputContainer(cm);
    InputContainer* ic2 = createInputContainer(cm);
    InputContainer* ic3 = createInputContainer(cm);

    InputGui* ig1 = createInputGui(100, 100);
    InputGui* ig2 = createInputGui(110, 100);
    InputGui* ig3 = createInputGui(100, 110);
    InputGui* ig4 = createInputGui(110, 110);

    addInputGuiToContainer(ic2, ig1);
    addInputGuiToContainer(ic2, ig2);
    addInputGuiToContainer(ic3, ig3);
    addInputGuiToContainer(ic3, ig4);

    addChildToContainer(rootIC, ic2);
    addChildToContainer(rootIC, ic3);

    // Set initial selection
    setSelectedInputGui(ic2, ig2);
    ic2->selectedInputIndex = 0; // Initialize the selected index

    // Main loop
    while (!WindowShouldClose()) {
        // Handle input (e.g., arrow keys to navigate)
        if (IsKeyPressed(KEY_RIGHT)) {
            printf("Right key pressed\n");
            navigateContainer(ic2, 0, 1); // Move right
        }
        if (IsKeyPressed(KEY_LEFT)) {
            printf("Left key pressed\n");
            navigateContainer(ic2, 0, -1); // Move left
        }
        if (IsKeyPressed(KEY_DOWN)) {
            printf("Down key pressed\n");
            navigateContainer(ic2, 1, 0); // Move down
        }
        if (IsKeyPressed(KEY_UP)) {
            printf("Up key pressed\n");
            navigateContainer(ic2, -1, 0); // Move up
        }

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);
        drawContainer(rootIC); // Draw all InputGuis
        EndDrawing();
    }

    // Cleanup
    freeContainer(ic2);
    freeContainer(ic3);
    freeContainer(rootIC);
    free(cm);

    // Close Raylib
    CloseWindow();

    return 0;
}