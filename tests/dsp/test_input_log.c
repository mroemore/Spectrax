/* test_input_log.c — the input-mapping diagnostic's pure logic.
 *
 * input_log.c sits between the kernel and the app and writes what each layer
 * said, so the mapping can be assigned from evidence instead of guesses. The
 * file IO and the evdev reads need a device; what is testable here is the
 * decision logic that makes the log readable:
 *
 *   - transition edges, so a held button logs once (a per-frame dump is
 *     unreadable and hides the press the mapping depends on)
 *   - the axis deadband, so a resting stick does not flood the log
 *   - the code names, which are what make "BTN_SELECT(314)" readable
 *
 * Linux only: the names come from linux/input-event-codes.h. */

#include <stdio.h>
#include <string.h>

/* raylib's header first: linux/input-event-codes.h defines KEY_* as macros and
 * would collide with raylib's KEY_* enum members if it came first. */
#include "input_log.h"

#include <linux/input-event-codes.h>

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while(0)

static int test_edges(void) {
	bool prev = false;

	ASSERT_TRUE(inputDiagEdge(true, &prev) == 1, "first press must log");
	ASSERT_TRUE(inputDiagEdge(true, &prev) == 0, "holding must not log again");
	ASSERT_TRUE(inputDiagEdge(true, &prev) == 0, "still holding");
	ASSERT_TRUE(inputDiagEdge(false, &prev) == -1, "release must log");
	ASSERT_TRUE(inputDiagEdge(false, &prev) == 0, "released state is quiet");
	ASSERT_TRUE(inputDiagEdge(true, &prev) == 1, "a second press logs again");

	/* Starting already down: the first poll logs nothing (an initial press
	 * is only a transition if we saw the up state). */
	prev = true;
	ASSERT_TRUE(inputDiagEdge(true, &prev) == 0, "already-down is not a press");

	printf("PASS test_edges\n");
	return 0;
}

static int test_axis_deadband(void) {
	/* A resting stick jitters in the low hundredths; it must stay out of the
	 * log. A real deflection (|v| past the deadband) logs once. */
	ASSERT_TRUE(!inputDiagAxisChanged(0.0f, 0.10f), "small jitter must not log");
	ASSERT_TRUE(!inputDiagAxisChanged(0.0f, -0.20f), "small negative jitter must not log");
	ASSERT_TRUE(inputDiagAxisChanged(0.0f, 1.0f), "full deflection must log");
	ASSERT_TRUE(inputDiagAxisChanged(1.0f, 0.0f), "return to centre must log");
	ASSERT_TRUE(inputDiagAxisChanged(0.9f, 0.0f), "a big move down must log");
	ASSERT_TRUE(!inputDiagAxisChanged(0.90f, 0.95f), "noise around a held value must not log");
	ASSERT_TRUE(!inputDiagAxisChanged(0.0f, 0.0f), "unchanged must not log");

	printf("PASS test_axis_deadband\n");
	return 0;
}

static int test_code_names(void) {
	/* The codes this device actually emits, plus the ABS codes a D-pad can
	 * hide in -- those are the ones the mapping decision is made from. */
	ASSERT_TRUE(strcmp(inputDiagKeyName(BTN_SOUTH), "BTN_SOUTH") == 0, "BTN_SOUTH");
	ASSERT_TRUE(strcmp(inputDiagKeyName(BTN_EAST), "BTN_EAST") == 0, "BTN_EAST");
	ASSERT_TRUE(strcmp(inputDiagKeyName(BTN_SELECT), "BTN_SELECT") == 0, "BTN_SELECT");
	ASSERT_TRUE(strcmp(inputDiagKeyName(BTN_START), "BTN_START") == 0, "BTN_START");
	ASSERT_TRUE(strcmp(inputDiagKeyName(ABS_HAT0X), "ABS_HAT0X") == 0, "ABS_HAT0X");
	ASSERT_TRUE(strcmp(inputDiagKeyName(KEY_VOLUMEUP), "KEY_VOLUMEUP") == 0, "KEY_VOLUMEUP");

	/* Unknown codes keep the number, so an unlisted button is still mappable. */
	const char *unknown = inputDiagKeyName(0x2ff);
	ASSERT_TRUE(strcmp(unknown, "0x2ff") == 0, "unknown code falls back to hex");

	/* Two names in one expression must not alias the same buffer. */
	const char *a = inputDiagKeyName(0x2f0);
	const char *b = inputDiagKeyName(0x2f1);
	ASSERT_TRUE(a != b, "two fallbacks in one line must be distinct buffers");
	ASSERT_TRUE(strcmp(a, "0x2f0") == 0 && strcmp(b, "0x2f1") == 0, "fallbacks keep their values");

	/* The table's first entry wins for aliased codes (BTN_SOUTH == BTN_A). */
	ASSERT_TRUE(strcmp(inputDiagKeyName(BTN_A), "BTN_SOUTH") == 0, "aliased code resolves once");

	printf("PASS test_code_names\n");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_edges();
	rc |= test_axis_deadband();
	rc |= test_code_names();
	if(rc != 0) {
		fprintf(stderr, "FAILED (input log)\n");
		return 1;
	}
	printf("PASSED (input log)\n");
	return 0;
}
