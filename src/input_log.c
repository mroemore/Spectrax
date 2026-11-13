#include "input_log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "raylib.h"

/* One /dev/input/event* we are watching. The name is the kernel's
 * EVIOCGNAME: it is what distinguishes the pad from the volume keys, and it
 * goes in every line so two devices pressing at once stay readable. */
#define MAX_DIAG_DEVICES 8

typedef struct {
	int fd;
	char path[32];
	char name[64];
} DiagDevice;

static FILE *logFile;
static DiagDevice devices[MAX_DIAG_DEVICES];
static int deviceCount;

/* Last value logged per linux code, so only transitions reach the file. */
static int keyState[KEY_CNT];
static int absValue[ABS_CNT];
static int absMin[ABS_CNT];
static int absMax[ABS_CNT];

/* raylib's view, same transition rule. Button/axis counts are raylib 5.5's. */
#define RAYLIB_BUTTONS 18
#define RAYLIB_AXES 6
#define RAYLIB_KEYS 512
static bool raylibButtonPrev[POLLED_GAMEPADS][RAYLIB_BUTTONS];
static float raylibAxisPrev[POLLED_GAMEPADS][RAYLIB_AXES];
static bool raylibKeyPrev[RAYLIB_KEYS];

static double rawQuitSince = -1.0;

static void logLine(const char *fmt, ...) {
	if(!logFile) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	vfprintf(logFile, fmt, args);
	va_end(args);
	/* Flushed per line: the device may be killed or power-cut mid-session and
	 * the log is the whole point of the run. */
	fflush(logFile);
}

/* --- names -------------------------------------------------------------- */

/* The codes a handheld actually emits, plus the ABS codes a D-pad hides in.
 * Everything else falls back to its hex value, which is still enough to map a
 * button. Kept as a table because linux/input-event-codes.h has no names. */
static const struct {
	int code;
	const char *name;
} inputCodeNames[] = {
	{ BTN_SOUTH, "BTN_SOUTH" },
	{ BTN_EAST, "BTN_EAST" },
	{ BTN_NORTH, "BTN_NORTH" },
	{ BTN_WEST, "BTN_WEST" },
	{ BTN_TL, "BTN_TL" },
	{ BTN_TR, "BTN_TR" },
	{ BTN_TL2, "BTN_TL2" },
	{ BTN_TR2, "BTN_TR2" },
	{ BTN_SELECT, "BTN_SELECT" },
	{ BTN_START, "BTN_START" },
	{ BTN_MODE, "BTN_MODE" },
	{ BTN_THUMBL, "BTN_THUMBL" },
	{ BTN_THUMBR, "BTN_THUMBR" },
	{ BTN_DPAD_UP, "BTN_DPAD_UP" },
	{ BTN_DPAD_DOWN, "BTN_DPAD_DOWN" },
	{ BTN_DPAD_LEFT, "BTN_DPAD_LEFT" },
	{ BTN_DPAD_RIGHT, "BTN_DPAD_RIGHT" },
	{ BTN_TRIGGER_HAPPY1, "BTN_TRIGGER_HAPPY1" },
	{ BTN_TRIGGER_HAPPY2, "BTN_TRIGGER_HAPPY2" },
	{ BTN_TRIGGER_HAPPY3, "BTN_TRIGGER_HAPPY3" },
	{ BTN_TRIGGER_HAPPY4, "BTN_TRIGGER_HAPPY4" },
	{ KEY_ESC, "KEY_ESC" },
	{ KEY_ENTER, "KEY_ENTER" },
	{ KEY_SPACE, "KEY_SPACE" },
	{ KEY_LEFT, "KEY_LEFT" },
	{ KEY_RIGHT, "KEY_RIGHT" },
	{ KEY_UP, "KEY_UP" },
	{ KEY_DOWN, "KEY_DOWN" },
	{ KEY_LEFTCTRL, "KEY_LEFTCTRL" },
	{ KEY_LEFTSHIFT, "KEY_LEFTSHIFT" },
	{ KEY_LEFTALT, "KEY_LEFTALT" },
	{ KEY_TAB, "KEY_TAB" },
	{ KEY_BACKSPACE, "KEY_BACKSPACE" },
	{ KEY_VOLUMEUP, "KEY_VOLUMEUP" },
	{ KEY_VOLUMEDOWN, "KEY_VOLUMEDOWN" },
	{ KEY_MUTE, "KEY_MUTE" },
	{ KEY_POWER, "KEY_POWER" },
	{ KEY_HOME, "KEY_HOME" },
	{ KEY_BACK, "KEY_BACK" },
	{ KEY_MENU, "KEY_MENU" },
	{ ABS_X, "ABS_X" },
	{ ABS_Y, "ABS_Y" },
	{ ABS_Z, "ABS_Z" },
	{ ABS_RX, "ABS_RX" },
	{ ABS_RY, "ABS_RY" },
	{ ABS_RZ, "ABS_RZ" },
	{ ABS_HAT0X, "ABS_HAT0X" },
	{ ABS_HAT0Y, "ABS_HAT0Y" },
	{ ABS_HAT1X, "ABS_HAT1X" },
	{ ABS_HAT1Y, "ABS_HAT1Y" },
};

const char *inputDiagKeyName(int code) {
	/* A ring of buffers: one log line can carry two names, and two calls in
	 * one expression must not both point at the same static string. */
	static char fallbacks[4][16];
	static unsigned next;
	for(size_t i = 0; i < sizeof(inputCodeNames) / sizeof(inputCodeNames[0]); i++) {
		if(inputCodeNames[i].code == code) {
			return inputCodeNames[i].name;
		}
	}
	char *buf = fallbacks[next];
	next = (next + 1) % 4;
	snprintf(buf, sizeof(fallbacks[0]), "0x%03x", code);
	return buf;
}

static const char *raylibButtonName(int button) {
	switch(button) {
		case GAMEPAD_BUTTON_LEFT_FACE_UP: return "LEFT_FACE_UP";
		case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return "LEFT_FACE_RIGHT";
		case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return "LEFT_FACE_DOWN";
		case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return "LEFT_FACE_LEFT";
		case GAMEPAD_BUTTON_RIGHT_FACE_UP: return "RIGHT_FACE_UP";
		case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "RIGHT_FACE_RIGHT";
		case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "RIGHT_FACE_DOWN";
		case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "RIGHT_FACE_LEFT";
		case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "LEFT_TRIGGER_1";
		case GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "LEFT_TRIGGER_2";
		case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "RIGHT_TRIGGER_1";
		case GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "RIGHT_TRIGGER_2";
		case GAMEPAD_BUTTON_MIDDLE_LEFT: return "MIDDLE_LEFT";
		case GAMEPAD_BUTTON_MIDDLE: return "MIDDLE";
		case GAMEPAD_BUTTON_MIDDLE_RIGHT: return "MIDDLE_RIGHT";
		case GAMEPAD_BUTTON_LEFT_THUMB: return "LEFT_THUMB";
		case GAMEPAD_BUTTON_RIGHT_THUMB: return "RIGHT_THUMB";
		default: return "?";
	}
}

/* --- pure helpers ------------------------------------------------------- */

int inputDiagEdge(bool down, bool *prev) {
	if(down == *prev) {
		return 0;
	}
	*prev = down;
	return down ? 1 : -1;
}

bool inputDiagAxisChanged(float prev, float now) {
	float delta = now - prev;
	if(delta < 0.0f) {
		delta = -delta;
	}
	return delta >= INPUT_DIAG_AXIS_EPSILON;
}

/* --- devices ------------------------------------------------------------ */

#define BITS_PER_LONG (8 * (int)sizeof(unsigned long))
#define BIT_WORD(n) ((n) / BITS_PER_LONG)
#define BIT_TEST(bits, n) (((bits)[BIT_WORD(n)] >> ((n) % BITS_PER_LONG)) & 1UL)

static void logCapabilities(const DiagDevice *dev) {
	unsigned long keyBits[BIT_WORD(KEY_CNT) + 1];
	unsigned long absBits[BIT_WORD(ABS_CNT) + 1];
	memset(keyBits, 0, sizeof(keyBits));
	memset(absBits, 0, sizeof(absBits));

	logLine("#   keys:");
	int listed = 0;
	if(ioctl(dev->fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
		for(int code = 1; code < KEY_CNT; code++) {
			if(!BIT_TEST(keyBits, code)) {
				continue;
			}
			logLine(" %s", inputDiagKeyName(code));
			if(++listed >= 64) {
				logLine(" ...");
				break;
			}
		}
	}
	if(listed == 0) {
		logLine(" (none)");
	}
	logLine("\n");

	if(ioctl(dev->fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
		return;
	}
	logLine("#   abs:");
	listed = 0;
	struct input_absinfo info;
	for(int code = 0; code < ABS_CNT; code++) {
		if(!BIT_TEST(absBits, code)) {
			continue;
		}
		memset(&info, 0, sizeof(info));
		if(ioctl(dev->fd, EVIOCGABS(code), &info) == 0) {
			absMin[code] = info.minimum;
			absMax[code] = info.maximum;
			/* Start from the current value: a stick resting off-centre must
			 * not be logged as a press on the first frame. */
			absValue[code] = info.value;
		}
		logLine(" %s[%d..%d]", inputDiagKeyName(code), absMin[code], absMax[code]);
		if(++listed >= 16) {
			logLine(" ...");
			break;
		}
	}
	logLine("\n");
}

static void openInputDevices(void) {
	unsigned long keyBits[BIT_WORD(KEY_CNT) + 1];
	DIR *dir = opendir("/dev/input");
	if(!dir) {
		logLine("# no /dev/input: %s\n", strerror(errno));
		return;
	}
	struct dirent *entry;
	while((entry = readdir(dir)) != NULL && deviceCount < MAX_DIAG_DEVICES) {
		if(strncmp(entry->d_name, "event", 5) != 0) {
			continue;
		}
		DiagDevice *dev = &devices[deviceCount];
		memset(dev, 0, sizeof(*dev));
		snprintf(dev->path, sizeof(dev->path), "/dev/input/%s", entry->d_name);
		dev->fd = open(dev->path, O_RDONLY | O_NONBLOCK);
		if(dev->fd < 0) {
			logLine("# %s: %s\n", dev->path, strerror(errno));
			continue;
		}
		if(ioctl(dev->fd, EVIOCGNAME(sizeof(dev->name) - 1), dev->name) < 0) {
			snprintf(dev->name, sizeof(dev->name), "?");
		}
		/* Seed the key state with what is held right now, so a button that
		 * was already down when we opened is not reported as a press. */
		memset(keyBits, 0, sizeof(keyBits));
		if(ioctl(dev->fd, EVIOCGKEY(sizeof(keyBits)), keyBits) >= 0) {
			for(int code = 0; code < KEY_CNT; code++) {
				if(BIT_TEST(keyBits, code)) {
					keyState[code] = 1;
				}
			}
		}
		logLine("# evdev %s \"%s\"\n", dev->path, dev->name);
		logCapabilities(dev);
		deviceCount++;
	}
	closedir(dir);
}

/* --- public API --------------------------------------------------------- */

bool inputDiagStart(void) {
	if(logFile) {
		return true;
	}
	/* Appended, not truncated: a session where a button was missed can be
	 * followed by another run without losing the first, and each run opens
	 * with a wall-clock banner to tell them apart. */
	logFile = fopen("input_log.txt", "a");
	if(!logFile) {
		fprintf(stderr, "input log: cannot write input_log.txt (%s)\n", strerror(errno));
		return false;
	}
	char stamp[32] = "?";
	time_t wall = time(NULL);
	struct tm *tm = localtime(&wall);
	if(tm && strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tm) == 0) {
		snprintf(stamp, sizeof(stamp), "?");
	}
	logLine("# spectrax input log --- run %s ---\n", stamp);
	logLine("# evdev = kernel event, raylib = backend view, command = app KeyMapping\n");
	logLine("# press each physical button once, about a second apart, then hold\n");
	logLine("# SELECT+START for %.1f s to exit\n", (double)QUIT_HOLD_SECONDS);

	deviceCount = 0;
	openInputDevices();

	int pads = 0;
	for(int g = 0; g < POLLED_GAMEPADS; g++) {
		if(!IsGamepadAvailable(g)) {
			continue;
		}
		const char *name = GetGamepadName(g);
		logLine("# raylib pad%d \"%s\" axes=%d\n", g, name ? name : "?", GetGamepadAxisCount(g));
		pads++;
	}
	logLine("# raylib gamepads: %d\n", pads);
	fflush(logFile);
	printf("input log: input_log.txt (press every button, then hold SELECT+START)\n");
	return true;
}

void inputDiagPoll(double now) {
	if(!logFile) {
		return;
	}
	struct input_event ev;
	for(int d = 0; d < deviceCount; d++) {
		DiagDevice *dev = &devices[d];
		while(read(dev->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if(ev.type == EV_KEY) {
				int code = ev.code;
				if(code < 0 || code >= KEY_CNT || ev.value == 2) {
					continue; /* 2 = autorepeat, not a new press */
				}
				if(ev.value == keyState[code]) {
					continue;
				}
				keyState[code] = ev.value;
				logLine("t=%8.3f evdev %s \"%s\" %s(%d) %d\n", now, dev->path, dev->name,
				        inputDiagKeyName(code), code, ev.value);
			} else if(ev.type == EV_ABS) {
				int code = ev.code;
				if(code < 0 || code >= ABS_CNT || ev.value == absValue[code]) {
					continue;
				}
				absValue[code] = ev.value;
				logLine("t=%8.3f evdev %s \"%s\" %s(%d) %d [%d..%d]\n", now, dev->path, dev->name,
				        inputDiagKeyName(code), code, ev.value, absMin[code], absMax[code]);
			}
		}
	}

	for(int g = 0; g < POLLED_GAMEPADS; g++) {
		if(!IsGamepadAvailable(g)) {
			continue;
		}
		const char *name = GetGamepadName(g);
		for(int b = GAMEPAD_BUTTON_LEFT_FACE_UP; b <= GAMEPAD_BUTTON_RIGHT_THUMB; b++) {
			if(inputDiagEdge(IsGamepadButtonDown(g, b), &raylibButtonPrev[g][b])) {
				logLine("t=%8.3f raylib pad%d \"%s\" button %d %s %d\n", now, g, name ? name : "?", b,
				        raylibButtonName(b), raylibButtonPrev[g][b] ? 1 : 0);
			}
		}
		int axes = GetGamepadAxisCount(g);
		if(axes > RAYLIB_AXES) {
			axes = RAYLIB_AXES;
		}
		for(int a = 0; a < axes; a++) {
			float value = GetGamepadAxisMovement(g, a);
			if(!inputDiagAxisChanged(raylibAxisPrev[g][a], value)) {
				continue;
			}
			raylibAxisPrev[g][a] = value;
			logLine("t=%8.3f raylib pad%d \"%s\" axis %d %+.3f\n", now, g, name ? name : "?", a,
			        (double)value);
		}
	}

	for(int key = 1; key < RAYLIB_KEYS; key++) {
		if(inputDiagEdge(IsKeyDown(key), &raylibKeyPrev[key])) {
			logLine("t=%8.3f raylib key %d %d\n", now, key, raylibKeyPrev[key] ? 1 : 0);
		}
	}
}

void inputDiagLogCommands(InputState *state) {
	if(!logFile || !state) {
		return;
	}
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		/* The raw fields, not isKeyJustPressed(): that helper appends to the
		 * input history, and the app's own handling of this same edge must stay
		 * the only consumer. */
		if(!state->keys[i].isPressed || state->keys[i].wasPressed) {
			continue;
		}
		logLine("t=%8.3f command %s\n", GetTime(), KEY_NAMES[i]);
	}
}

bool inputDiagRawExitHeld(double now) {
	/* BTN_SELECT/BTN_START as the kernel reports them: the app's own mapping
	 * may be wrong (that is why this diagnostic exists), and without this the
	 * app could not be left on the device at all. */
	bool select = keyState[BTN_SELECT] != 0;
	bool start = keyState[BTN_START] != 0;
	return updateQuitHold(select, start, now, &rawQuitSince);
}

void inputDiagStop(void) {
	for(int d = 0; d < deviceCount; d++) {
		if(devices[d].fd >= 0) {
			close(devices[d].fd);
			devices[d].fd = -1;
		}
	}
	deviceCount = 0;
	if(logFile) {
		logLine("# end\n");
		fclose(logFile);
		logFile = NULL;
	}
}
