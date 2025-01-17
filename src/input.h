#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include "raylib.h"

#define MAX_INPUT_HISTORY 128

typedef enum {
    INPUT_TYPE_KEYBOARD,
    INPUT_TYPE_GAMEPAD
} InputDeviceType;

typedef enum {
	KM_LEFT,
	KM_RIGHT,
	KM_UP,
	KM_DOWN,
	KM_SELECT,
	KM_START,
	KM_EDIT,
	KM_FUNCTION,
	KM_NAV_LEFT,
	KM_NAV_RIGHT,
	KEY_MAPPING_COUNT
} KeyMapping;

static const int KEYBOARD_MAP[] = {
	KEY_LEFT,
	KEY_RIGHT,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT_SHIFT,
	KEY_ENTER,
	KEY_Z,
	KEY_X,
	KEY_Q,
	KEY_W
};

static const char* KEY_NAMES[] = {
	"LEFT",
	"RIGHT",
	"UP",
	"DOWN",
	"SELECT",
	"START",
	"EDIT",
	"FUNC",
	"SCN<",
	"SCN>"
};

static const int GAMEPAD_MAP[] = {
	GAMEPAD_BUTTON_LEFT_FACE_LEFT,
	GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
	GAMEPAD_BUTTON_LEFT_FACE_UP,
	GAMEPAD_BUTTON_LEFT_FACE_DOWN,
	GAMEPAD_BUTTON_MIDDLE_LEFT,
	GAMEPAD_BUTTON_MIDDLE_RIGHT,
	GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
	GAMEPAD_BUTTON_RIGHT_FACE_LEFT,
	GAMEPAD_BUTTON_LEFT_TRIGGER_1,
	GAMEPAD_BUTTON_RIGHT_TRIGGER_1
};

typedef struct {
    bool isPressed;
    bool wasPressed;
} KeyState;

typedef struct {
    KeyState keys[KEY_MAPPING_COUNT];
    InputDeviceType deviceType;
    const int* currentMap;
	int* inputHistory;
	int historyIndex;
} InputState;

typedef struct {
	int thing;
} AppStateData;

typedef void (*SceneInputHandler)(InputState* input, AppStateData* data);

InputState* createInputState(InputDeviceType type);
bool isKeyHeld(InputState* state, KeyMapping keyCode);
bool isKeyJustPressed(InputState* state, KeyMapping keyCode);
void updateInputState(InputState* state);
int getMappedKeyCode(InputState* state, KeyMapping key);

#endif