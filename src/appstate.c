#include "appstate.h"
#include "gui.h"
#include "input.h"


ApplicationState* createApplicationState(){
	ApplicationState* as = (ApplicationState*)malloc(sizeof(ApplicationState));
	as->selectedPattern = -1;
	as->selectedArrangerCell[0] = 0;
	as->selectedArrangerCell[1] = 0;
	as->selectedStep = 0;
	as->currentScene = SCENE_ARRANGER;
	as->inputState = createInputState(INPUT_TYPE_KEYBOARD);
	return as;
}

void incrementScene(ApplicationState* appState){
	if(appState->currentScene < SCENE_COUNT - 1){
		if(appState->selectedPattern != -1){
			appState->currentScene++;
			printf("\n\nSCENE: %i\n\n", appState->currentScene);
		}
	}	
}

void decrementScene(ApplicationState* appState){
	printf("\n\n\nDECREMENT SCENE\n\n\n");
	if(appState->currentScene > 1){
		appState->currentScene--;
	}
}
