#ifndef APPSTATE_H
#define APPSTATE_H

#include "gui.h"
#include "input.h"
#include "notes.h"

typedef struct {
	int currentPattern;
	int selectedPattern;
	int selectedStep;
	int selectedArrangerCell[2];
	Scene currentScene;
	int lastUsedNote[NOTE_INFO_SIZE];
	InputState *inputState;
} ApplicationState;

void initApplicationState();
ApplicationState *createApplicationState();
void incrementScene(ApplicationState *appState);
void decrementScene(ApplicationState *appState);

#endif
