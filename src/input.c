#include <stdio.h>
#include <stdlib.h>
#include "input.h"

InputState *createInputState(InputDeviceType type) {
	InputState *state = (InputState *)malloc(sizeof(InputState));
	if(!state) return NULL;
	state->deviceType = type;
	state->currentMap = (type == INPUT_TYPE_KEYBOARD) ? KEYBOARD_MAP : GAMEPAD_MAP;
	state->inputHistory = (int *)malloc(MAX_INPUT_HISTORY * sizeof(int));
	if(!state->inputHistory) {
		free(state);
		return NULL;
	}
	for(int i = 0; i < MAX_INPUT_HISTORY; i++) {
		state->inputHistory[i] = 0;
	}
	state->historyIndex = 0;
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		state->keys[i].isPressed = false;
		state->keys[i].wasPressed = false;
	}
	return state;
}

void updateInputState(InputState *state) {
	// printf("updating inputs...\n");
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		// printf("%i, ", i);
		state->keys[i].wasPressed = state->keys[i].isPressed;
		state->keys[i].isPressed = IsKeyDown(state->currentMap[i]);
	}
	// printf("\n");
}

void addToHistory(InputState *state, KeyMapping keyCode) {
	state->inputHistory[state->historyIndex] = keyCode;
	state->historyIndex++;
	if(state->historyIndex > MAX_INPUT_HISTORY) {
		state->historyIndex = 0;
	}
}

bool isKeyHeld(InputState *state, KeyMapping keyCode) {
	return state->keys[keyCode].isPressed;
}

bool isKeyJustPressed(InputState *state, KeyMapping keyCode) {
	if(state->keys[keyCode].isPressed && !state->keys[keyCode].wasPressed) addToHistory(state, keyCode);
	return state->keys[keyCode].isPressed && !state->keys[keyCode].wasPressed;
}

int getMappedKeyCode(InputState *state, KeyMapping key) {
	return state->currentMap[key];
}
