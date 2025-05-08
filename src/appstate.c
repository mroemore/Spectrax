#include "appstate.h"
#include "gui.h"
#include "input.h"

ApplicationState *createApplicationState() {
	ApplicationState *as = (ApplicationState *)malloc(sizeof(ApplicationState));
	if(!as) {
		printf("ERROR: could not allocate memory for ApplicationState.\n");
		return NULL;
	}
	as->selectedPattern = -1;
	as->selectedArrangerCell[0] = 0;
	as->selectedArrangerCell[1] = 0;
	as->selectedStep = 0;
	as->currentScene = SCENE_ARRANGER;
	as->lastUsedNote[0] = C;
	as->lastUsedNote[1] = 3;
	as->inputState = createInputState(INPUT_TYPE_KEYBOARD);
	if(!as->inputState) {
		printf("ERROR: couldn't create input state.\n");
		return NULL;
	}
	return as;
}

void incrementScene(ApplicationState *appState) {
	if(appState->currentScene < SCENE_COUNT - 1) {
		if(appState->selectedPattern != -1) {
			appState->currentScene++;
			// printf("\n\nSCENE: %i\n\n", appState->currentScene);
		}
	}
}

void decrementScene(ApplicationState *appState) {
	// printf("\n\n\nDECREMENT SCENE\n\n\n");
	if(appState->currentScene > 1) {
		appState->currentScene--;
	}
}

void setCurrentPattern(void *self, void *patternID) {
	ApplicationState *as = (ApplicationState *)self;
	as->currentPattern = *(int *)patternID;
}
void setSelectedPattern(void *self, void *patternID) {
	ApplicationState *as = (ApplicationState *)self;
	as->selectedPattern = *(int *)patternID;
	// printf("\n\n Pattern: %i \n\n", as->selectedPattern);
}
void setSelectedStep(void *self, void *step) {
	ApplicationState *as = (ApplicationState *)self;
	as->selectedStep = *(int *)step;
}

void setSelectedArrangerCell(void *self, void *cellCoordinates) {
	ApplicationState *as = (ApplicationState *)self;
	as->selectedArrangerCell[0] = ((int *)cellCoordinates)[0];
	as->selectedArrangerCell[1] = ((int *)cellCoordinates)[1];
	// printf("\n\nSELECTED: %i, %i \n\n", as->selectedArrangerCell[0], as->selectedArrangerCell[1]);
}
void setLastUsedNote(void *self, void *noteArray) {
	ApplicationState *as = (ApplicationState *)self;
	as->lastUsedNote[0] = ((int *)noteArray)[0];
	as->lastUsedNote[1] = ((int *)noteArray)[1];
	// printf("\n\n NOTE: %i, %i \n\n", as->lastUsedNote[0], as->lastUsedNote[1]);
}
