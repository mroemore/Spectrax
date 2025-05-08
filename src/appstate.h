#ifndef APPSTATE_H
#define APPSTATE_H

#include "input.h"
#include "settings.h"
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

void setCurrentPattern(void *self, void *patternID);
void setSelectedPattern(void *self, void *patternID);
void setSelectedStep(void *self, void *step);
void setSelectedArrangerCell(void *self, void *cellCoordinates);
void setLastUsedNote(void *self, void *octave);

#endif
