#include <stdio.h>
#include "input.h"

/* Multi-key binding regression: every KeyMapping must have a primary key,
 * the vim hjkl secondaries must be bound to the four arrows, and every
 * row must be 0-terminated so the poll loop stops cleanly. */

static int rowHas(const int row[MAX_KEYS_PER_MAPPING], int key) {
	for(int k = 0; k < MAX_KEYS_PER_MAPPING; k++) {
		if(row[k] == 0) {
			break;
		}
		if(row[k] == key) {
			return 1;
		}
	}
	return 0;
}

static int rowTerminated(const int row[MAX_KEYS_PER_MAPPING]) {
	/* Once we hit a 0, the rest must also be 0. */
	int seenZero = 0;
	for(int k = 0; k < MAX_KEYS_PER_MAPPING; k++) {
		if(row[k] == 0) {
			seenZero = 1;
			continue;
		}
		if(seenZero) {
			return 0;
		}
	}
	return 1;
}

int main(void) {
	/* Every keyboard mapping needs at least a primary key. */
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		if(KEYBOARD_MAP[i][0] == 0) {
			printf("FAIL keyboard mapping %d has no primary key\n", i);
			return 1;
		}
		if(!rowTerminated(KEYBOARD_MAP[i])) {
			printf("FAIL keyboard mapping %d row not 0-terminated\n", i);
			return 1;
		}
	}
	/* Primaries unchanged (the arrow defaults must survive). */
	if(KEYBOARD_MAP[KM_LEFT][0] != KEY_LEFT || KEYBOARD_MAP[KM_RIGHT][0] != KEY_RIGHT ||
	   KEYBOARD_MAP[KM_UP][0] != KEY_UP || KEYBOARD_MAP[KM_DOWN][0] != KEY_DOWN) {
		printf("FAIL arrow primaries changed\n");
		return 1;
	}
	/* Vim hjkl as secondary arrows. */
	if(!rowHas(KEYBOARD_MAP[KM_LEFT], KEY_H) ||
	   !rowHas(KEYBOARD_MAP[KM_DOWN], KEY_J) ||
	   !rowHas(KEYBOARD_MAP[KM_UP], KEY_K) ||
	   !rowHas(KEYBOARD_MAP[KM_RIGHT], KEY_L)) {
		printf("FAIL vim hjkl secondary arrows missing\n");
		return 1;
	}
	/* Gamepad rows are all either a real button or cleanly empty. */
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		if(!rowTerminated(GAMEPAD_MAP[i])) {
			printf("FAIL gamepad mapping %d row not 0-terminated\n", i);
			return 1;
		}
	}
	printf("OK input multi-key\n");
	return 0;
}