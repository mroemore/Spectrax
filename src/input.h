#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include "raylib.h"

#define MAX_INPUT_HISTORY 128

/* How many physical keys may back a single KeyMapping command. Each
 * mapping's row is 0-terminated; the poll ORs every key in the row, so
 * any one of them produces the command (e.g. arrow keys + vim hjkl). */
#define MAX_KEYS_PER_MAPPING 4

/* The three or four gamepad slots raylib can expose; poll them all rather
 * than assuming the one pad has index 0 (the handheld's own pad may not be
 * first after other evdev devices are enumerated). */
#define POLLED_GAMEPADS 4

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
	KM_MOD_EXTRA,
	KM_ADD,
	KM_REMOVE,
	KM_CTRL,
	KM_SHIFT,
	KM_EQUAL,
	KM_MINUS,
	KEY_MAPPING_COUNT
} KeyMapping;

static const int KEYBOARD_MAP[KEY_MAPPING_COUNT][MAX_KEYS_PER_MAPPING] = {
	{ KEY_LEFT,  KEY_H, 0, 0 },
	{ KEY_RIGHT, KEY_L, 0, 0 },
	{ KEY_UP,    KEY_K, 0, 0 },
	{ KEY_DOWN,  KEY_J, 0, 0 },
	{ KEY_LEFT_SHIFT, 0, 0, 0 },
	{ KEY_ENTER, 0, 0, 0 },
	{ KEY_Z, 0, 0, 0 },
	{ KEY_X, 0, 0, 0 },
	{ KEY_Q, 0, 0, 0 },
	{ KEY_W, 0, 0, 0 },
	{ KEY_LEFT_CONTROL, 0, 0, 0 },
	{ KEY_SEMICOLON, 0, 0, 0 },
	{ KEY_APOSTROPHE, 0, 0, 0 },
	{ KEY_LEFT_CONTROL, 0, 0, 0 },
	{ KEY_LEFT_SHIFT, 0, 0, 0 },
	{ KEY_EQUAL, 0, 0, 0 },
	{ KEY_MINUS, 0, 0, 0 }
};

static const char *KEY_NAMES[] = {
	"LEFT",
	"RIGHT",
	"UP",
	"DOWN",
	"SELECT",
	"START",
	"EDIT",
	"FUNC",
	"SCN<",
	"SCN>",
	"MOD",
	"ADD",
	"DEL",
	"CTRL",
	"SHIFT",
	"=",
	"-"
};

static const int GAMEPAD_MAP[KEY_MAPPING_COUNT][MAX_KEYS_PER_MAPPING] = {
	{ GAMEPAD_BUTTON_LEFT_FACE_LEFT, 0, 0, 0 },
	{ GAMEPAD_BUTTON_LEFT_FACE_RIGHT, 0, 0, 0 },
	{ GAMEPAD_BUTTON_LEFT_FACE_UP, 0, 0, 0 },
	{ GAMEPAD_BUTTON_LEFT_FACE_DOWN, 0, 0, 0 },
	{ GAMEPAD_BUTTON_MIDDLE_LEFT, 0, 0, 0 },
	{ GAMEPAD_BUTTON_MIDDLE_RIGHT, 0, 0, 0 },
	{ GAMEPAD_BUTTON_RIGHT_FACE_DOWN, 0, 0, 0 },
	{ GAMEPAD_BUTTON_RIGHT_FACE_LEFT, 0, 0, 0 },
	{ GAMEPAD_BUTTON_LEFT_TRIGGER_1, 0, 0, 0 },
	{ GAMEPAD_BUTTON_RIGHT_TRIGGER_1, 0, 0, 0 },
	{ GAMEPAD_BUTTON_LEFT_TRIGGER_2, 0, 0, 0 },
	/* KM_ADD was RIGHT_TRIGGER_1 but collided with KM_NAV_RIGHT —
	 * moved to RIGHT_FACE_UP (PS3 Triangle / Xbox Y), unused elsewhere. */
	{ GAMEPAD_BUTTON_RIGHT_FACE_UP, 0, 0, 0 },
	{ GAMEPAD_BUTTON_MIDDLE, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 }
};

typedef struct {
	bool isPressed;
	bool wasPressed;
} KeyState;

typedef struct {
	KeyState keys[KEY_MAPPING_COUNT];
	InputDeviceType deviceType;
	const int (*currentMap)[MAX_KEYS_PER_MAPPING];
	int *inputHistory;
	int historyIndex;
} InputState;

typedef struct {
	int thing;
} AppStateData;

typedef void (*SceneInputHandler)(InputState *input, AppStateData *data);

/* Native-DRM builds have no window manager, so there is no window close
 * button to leave the app with. SELECT+START held this long is the exit
 * gesture: a hold rather than a tap, because START on its own is the
 * play/stop (and commit) key. */
#define QUIT_HOLD_SECONDS 1.5

/* True once select+start have been held together for QUIT_HOLD_SECONDS.
 * *heldSince is the caller's sentinel (-1.0 when not holding) and is reset by
 * any release, so the gesture must be deliberate. */
bool updateQuitHold(bool selectHeld, bool startHeld, double now, double *heldSince);

InputState *createInputState(InputDeviceType type);
void freeInputState(InputState *state);
bool isKeyHeld(InputState *state, KeyMapping keyCode);
bool isKeyJustPressed(InputState *state, KeyMapping keyCode);
void updateInputState(InputState *state);
int getMappedKeyCode(InputState *state, KeyMapping key);

#endif
