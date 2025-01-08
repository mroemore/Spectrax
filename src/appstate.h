#ifndef APPSTATE_H
#define APPSTATE_H

#include "gui.h" 
#include "input.h" 

typedef struct {
	int currentPattern;
	int selectedPattern;
	int selectedStep;
	int selectedArrangerCell[2];
	Scene currentScene;
	InputState* inputState;
} ApplicationState;

void initApplicationState();
ApplicationState* createApplicationState();
void incrementScene();
void decrementScene();

#endif