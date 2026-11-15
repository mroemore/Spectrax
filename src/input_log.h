#ifndef INPUT_LOG_H
#define INPUT_LOG_H

#include <stdbool.h>

#include "input.h"

/* Input-mapping diagnostic.
 *
 * Writes "input_log.txt" in the working directory, with the three layers of
 * every press side by side so a physical button can be followed from the
 * kernel into the app:
 *
 *   evdev    the raw event from /dev/input/event*: device name, code
 *            (BTN_SOUTH, ABS_HAT0X, ...) and value
 *   raylib   what the DRM backend reports: gamepad slot/button/axis, key
 *   command  which KeyMapping the app resolved it to (KEY_NAMES)
 *
 * The mapping is assigned from this log, so it deliberately records the raw
 * layer as well: raylib translates only BTN_* codes and only the ones in its
 * linuxToRaylibMap table, and a button missing from that table (a D-pad
 * reported as ABS_HAT0X/Y, say) never reaches the app at all -- visible in the
 * evdev lines and nowhere else.
 *
 * All calls are cheap and best-effort: with no writable log file or no
 * /dev/input access the app runs unchanged. */

/* Opens the log and the input devices. False if the log could not be written;
 * the caller keeps running either way. */
bool inputDiagStart(void);

/* Once per frame, after updateInputState(). Drains the evdev devices and logs
 * raylib's view (transitions only). `now` is the frame's clock, the same one
 * the quit gesture uses. */
void inputDiagPoll(double now);

/* Once per frame, after updateInputState(): logs which mapped commands fired.
 * Reads the press state directly, so it never consumes the just-pressed edge
 * the app is about to act on. */
void inputDiagLogCommands(InputState *state);

/* The exit gesture taken from the kernel codes (BTN_SELECT+BTN_START) instead
 * of the app's mapping, so a wrong mapping cannot trap the app on the device.
 * Same QUIT_HOLD_SECONDS hold as the mapped gesture. */
bool inputDiagRawExitHeld(double now);

void inputDiagStop(void);

/* --- pure helpers, exposed for the tests ------------------------------- */

/* +1 on press, -1 on release, 0 while unchanged. *prev is updated. */
int inputDiagEdge(bool down, bool *prev);

/* Axis deadband: log a stick only when it actually moved. */
#define INPUT_DIAG_AXIS_EPSILON 0.25f
bool inputDiagAxisChanged(float prev, float now);

/* Readable name for a linux input code (BTN_SELECT, KEY_VOLUMEUP, ABS_HAT0X);
 * "0x%03x" for anything not in the table. Returns a rotating static buffer. */
const char *inputDiagKeyName(int code);

#endif
