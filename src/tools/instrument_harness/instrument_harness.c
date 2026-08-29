/*
 * instrument_harness -- boots straight to the FM instrument screen for
 * manual and scripted (Xvfb) UI testing of the dynamic mod-source
 * add/remove/rewire controls.
 *
 * The harness deliberately re-uses everything that lives in main.c:
 *   - initApplication() sets up the GUI, VoiceManager, Arranger,
 *     PatternList, loads s1.sng, creates the instrument graph and
 *     the arranger/pattern graphs.
 *   - initModSystem() registers the mod-source factory.
 *
 * We avoid PortAudio: this is a GUI-only harness, no audio stream.
 *
 * Linking trick: main.c compiles with -DSPECTRAX_HARNESS so its
 * own main() is excluded, leaving initApplication + helper symbols
 * available to the harness.
 *
 * Run from the bin/ directory (the same cwd the app expects),
 * so that resources/ and data/ resolve correctly:
 *
 *   cd bin && ../src/tools/instrument_harness/instrument_harness
 *
 * Scripted mode (Task 15):
 *
 *   instrument_harness --script <fixture.txt>
 *
 * Drives the same input handler the interactive loop uses, but
 * synthesises key events directly into the InputState instead of
 * relying on raylib. raylib does NOT receive keyboard events under
 * Xvfb/llvmpipe, so we must inject ourselves. See loadScript() /
 * stepScript() / runScriptEventInjection() for the engine.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "raylib.h"

#include "main.h"
#include "gui.h"
#include "graph_gui.h"
#include "input.h"
#include "appstate.h"
#include "sequencer.h"
#include "voice.h"
#include "modsystem.h"
#include "settings.h"

#include "io/preset_io.h"

/* The preset save dir is hardcoded relative to cwd (see
 * src/gui.c::guiSavePreset / cbConfirmOverwrite) — the harness
 * runs from bin/ via run_scripted.sh, so this maps to
 * bin/data/instrument_presets/ on disk. */
#define HARNESS_PRESET_DIR "data/instrument_presets/"

#define MAX_SCRIPT_STEPS 512
#define MAX_SCRIPT_LINE 256
#define MAX_SCRIPT_TOKENS 16

/* --- Script engine ------------------------------------------------------ */

typedef enum {
	SOP_KEY,             /* one-frame keypress: ADD, REMOVE, LEFT/RIGHT/UP/DOWN */
	SOP_EDIT_ARROW,      /* hold EDIT + arrow for one frame, then release */
	SOP_FRAMES,          /* idle for N frames (all keys released) */
	SOP_ASSERT_ENVCOUNT, /* inst->envelopeCount == N */
	SOP_ASSERT_MODULATORS, /* ops[op]->{fb|rat|lvl}->modulator_count == N */
	SOP_ASSERT_SELECTED, /* currentGraph->selected->name == "NAME" */
	SOP_ASSERT_PRESET,   /* presetBank has a patch whose name matches "NAME" */
	SOP_ASSERT_FILE,     /* "data/instrument_presets/<sanitized>" exists on disk */
	SOP_ASSERT_PRESETCOUNT, /* presetBank->presetCount == N */
	SOP_ASSERT_ALGO,     /* FM selectedAlgorithm baseValue (as int) == N */
	SOP_ASSERT_LOADLISTACTIVE, /* g_loadListActive (guiIsLoadListActive) == N */
	SOP_ASSERT_SAVEDFLASH,     /* Task 5: selected PresetNameGuiNode's savedFlashUntil
	                            * is strictly in the future (currentFrameIndex() < it).
	                            * ==1 right after a successful save, ==0 once the
	                            * ~30-frame flash window has expired. */
	SOP_QUIT
} ScriptOpKind;

typedef struct {
	ScriptOpKind op;
	int lineno;          /* source line for error reporting */
	int frames;          /* for SOP_FRAMES, and per-step subframe counter */
	union {
		KeyMapping key; /* for SOP_KEY */
		KeyMapping arrow; /* for SOP_EDIT_ARROW */
		int n;          /* for SOP_FRAMES, SOP_ASSERT_ENVCOUNT, SOP_ASSERT_PRESETCOUNT,
		                 * SOP_ASSERT_ALGO; and N for modulators */
		int op;         /* for SOP_ASSERT_MODULATORS: operator index 0..3 */
		int kind;       /* for SOP_ASSERT_MODULATORS: 0=fb 1=rat 2=lvl */
	} a;
	union {
		int n;          /* secondary int (modulator_count) */
	} b;
	char name[64];       /* for SOP_ASSERT_SELECTED, SOP_ASSERT_PRESET, SOP_ASSERT_FILE */
} ScriptStep;

typedef struct {
	ScriptStep steps[MAX_SCRIPT_STEPS];
	int count;
} Script;

typedef enum {
	SCRIPT_INACTIVE,
	SCRIPT_RUNNING,
	SCRIPT_PASS,
	SCRIPT_FAIL
} ScriptState;

static Script g_script;
static ScriptState g_scriptState = SCRIPT_INACTIVE;
static bool g_scriptMode = false;
static int g_scriptStepIdx = 0;        /* index of step currently being processed */
static int g_scriptSubframe = 0;       /* 0..N for multi-frame steps (e.g. EDIT_ARROW = 3 subframes) */
static char g_scriptFailMsg[256] = "";

/* ----- Tokeniser ------------------------------------------------------- *
 * Tiny per-line tokenizer. Returns number of tokens (0 = blank/comment).
 * Strips leading whitespace and trailing newline. Supports '#' line comments.
 */
/* Replace every `==`, `(`, `)`, `,` with a space-padded copy so the
 * whitespace tokenizer can split them as their own tokens. In-place
 * rewrite is safe because each replacement only grows the gap between
 * tokens, never shrinks existing content ahead of the write head. */
static void normalizeDelimiters(char *line) {
	size_t n = strlen(line);
	/* worst case: every char becomes two (e.g. "," -> " , ") */
	char buf[MAX_SCRIPT_LINE];
	size_t bi = 0;
	for(size_t i = 0; i < n && bi + 3 < sizeof(buf); i++) {
		char c = line[i];
		if((c == '=' && i + 1 < n && line[i + 1] == '=') ||
		   c == '(' || c == ')' || c == ',') {
			if(bi > 0 && buf[bi - 1] != ' ') {
				buf[bi++] = ' ';
			}
			if(c == '=') {
				buf[bi++] = '=';
				buf[bi++] = '=';
				i++; /* consume the second '=' */
			} else {
				buf[bi++] = c;
			}
			buf[bi++] = ' ';
		} else {
			buf[bi++] = c;
		}
	}
	buf[bi] = '\0';
	strcpy(line, buf);
}

static int tokenizeLine(char *line, char *tokens[], int maxTokens) {
	/* strip trailing newline */
	size_t n = strlen(line);
	while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
		line[--n] = '\0';
	}
	/* Normalise `==` `(`, `)`, `,` to whitespace-delimited tokens. */
	normalizeDelimiters(line);
	/* skip leading whitespace */
	char *p = line;
	while(*p == ' ' || *p == '\t') {
		p++;
	}
	/* comment-only or blank */
	if(*p == '\0' || *p == '#') {
		return 0;
	}
	int count = 0;
	while(*p && count < maxTokens) {
		tokens[count++] = p;
		while(*p && *p != ' ' && *p != '\t') {
			p++;
		}
		if(*p) {
			*p = '\0';
			p++;
			while(*p == ' ' || *p == '\t') {
				p++;
			}
		}
	}
	return count;
}

/* ----- Key name <-> KeyMapping ---------------------------------------- */

static int parseKeyName(const char *s, KeyMapping *out) {
	if(strcmp(s, "ADD") == 0) { *out = KM_ADD; return 1; }
	if(strcmp(s, "REMOVE") == 0) { *out = KM_REMOVE; return 1; }
	if(strcmp(s, "LEFT") == 0) { *out = KM_LEFT; return 1; }
	if(strcmp(s, "RIGHT") == 0) { *out = KM_RIGHT; return 1; }
	if(strcmp(s, "UP") == 0) { *out = KM_UP; return 1; }
	if(strcmp(s, "DOWN") == 0) { *out = KM_DOWN; return 1; }
	if(strcmp(s, "SELECT") == 0) { *out = KM_SELECT; return 1; }
	if(strcmp(s, "START") == 0) { *out = KM_START; return 1; }
	if(strcmp(s, "EDIT") == 0) { *out = KM_EDIT; return 1; }
	if(strcmp(s, "FUNCTION") == 0) { *out = KM_FUNCTION; return 1; }
	return 0;
}

/* ----- Script parser --------------------------------------------------- *
 * One ScriptStep per line. Each line is either:
 *   ADD | REMOVE | LEFT | RIGHT | UP | DOWN
 *   EDIT <LEFT|RIGHT|UP|DOWN>
 *   FRAMES <N>
 *   ASSERT envcount==<N>
 *   ASSERT modulators==(<op>,<kind>,<N>)
 *   ASSERT selected==<NAME>
 *   ASSERT preset==<NAME>           -- presetBank has patch named NAME
 *   ASSERT file==<NAME>             -- data/instrument_presets/<san NAME>.ipb exists
 *   ASSERT presetCount==<N>         -- presetBank->presetCount == N
 *   ASSERT algo==<N>                -- FM selectedAlgorithm baseValue == N
 *   QUIT
 * On any syntax error, set g_scriptState=SCRIPT_FAIL with a message and
 * let the main loop bail.
 */
static void failScript(int lineno, const char *fmt, ...) {
	va_list ap;
	char body[sizeof(g_scriptFailMsg) - 32];
	va_start(ap, fmt);
	vsnprintf(body, sizeof(body), fmt, ap);
	va_end(ap);
	if(lineno > 0) {
		snprintf(g_scriptFailMsg, sizeof(g_scriptFailMsg), "line %d: %s", lineno, body);
	} else {
		snprintf(g_scriptFailMsg, sizeof(g_scriptFailMsg), "%s", body);
	}
	g_scriptState = SCRIPT_FAIL;
}

static void parseScript(const char *path) {
	FILE *fp = fopen(path, "r");
	if(!fp) {
		failScript(0, "could not open script file: %s", path);
		return;
	}
	char line[MAX_SCRIPT_LINE];
	char *tokens[MAX_SCRIPT_TOKENS];
	int lineno = 0;
	while(fgets(line, sizeof(line), fp)) {
		lineno++;
		int nt = tokenizeLine(line, tokens, MAX_SCRIPT_TOKENS);
		if(nt == 0) {
			continue;
		}
		if(g_script.count >= MAX_SCRIPT_STEPS) {
			fclose(fp);
			failScript(lineno, "script exceeds %d step limit", MAX_SCRIPT_STEPS);
			return;
		}
		ScriptStep *s = &g_script.steps[g_script.count];
		memset(s, 0, sizeof(*s));
		s->lineno = lineno;

		const char *op = tokens[0];
		if(strcmp(op, "ADD") == 0) {
			s->op = SOP_KEY; s->a.key = KM_ADD; s->frames = 1;
		} else if(strcmp(op, "REMOVE") == 0) {
			s->op = SOP_KEY; s->a.key = KM_REMOVE; s->frames = 1;
		} else if(strcmp(op, "LEFT") == 0) {
			s->op = SOP_KEY; s->a.key = KM_LEFT; s->frames = 1;
		} else if(strcmp(op, "RIGHT") == 0) {
			s->op = SOP_KEY; s->a.key = KM_RIGHT; s->frames = 1;
		} else if(strcmp(op, "UP") == 0) {
			s->op = SOP_KEY; s->a.key = KM_UP; s->frames = 1;
		} else if(strcmp(op, "DOWN") == 0) {
			s->op = SOP_KEY; s->a.key = KM_DOWN; s->frames = 1;
		} else if(strcmp(op, "SELECT") == 0) {
			s->op = SOP_KEY; s->a.key = KM_SELECT; s->frames = 1;
		} else if(strcmp(op, "START") == 0) {
			s->op = SOP_KEY; s->a.key = KM_START; s->frames = 1;
		} else if(strcmp(op, "FUNCTION") == 0) {
			s->op = SOP_KEY; s->a.key = KM_FUNCTION; s->frames = 1;
		} else if(strcmp(op, "EDIT") == 0) {
			if(nt < 2) {
				/* Task 4: standalone EDIT injects a one-frame KM_EDIT press so
				 * fixtures can explicitly toggle edit mode on the name node.
				 * Falls through to the g_script.count++ at the end of the
				 * parser loop (no `continue` -- that would skip the step
				 * registration). */
				s->op = SOP_KEY; s->a.key = KM_EDIT; s->frames = 1;
			} else {
				KeyMapping arrow;
				if(!parseKeyName(tokens[1], &arrow) ||
				   (arrow != KM_LEFT && arrow != KM_RIGHT && arrow != KM_UP && arrow != KM_DOWN)) {
					fclose(fp);
					failScript(lineno, "EDIT requires LEFT|RIGHT|UP|DOWN, got '%s'", tokens[1]);
					return;
				}
				s->op = SOP_EDIT_ARROW; s->a.arrow = arrow; s->frames = 3;
			}
		} else if(strcmp(op, "FRAMES") == 0) {
			if(nt < 2) {
				fclose(fp);
				failScript(lineno, "FRAMES requires N");
				return;
			}
			int n = atoi(tokens[1]);
			if(n < 1) {
				fclose(fp);
				failScript(lineno, "FRAMES N must be >= 1");
				return;
			}
			s->op = SOP_FRAMES; s->a.n = n; s->frames = n;
		} else if(strcmp(op, "ASSERT") == 0) {
			if(nt < 2) {
				fclose(fp);
				failScript(lineno, "ASSERT requires a target");
				return;
			}
			if(strcmp(tokens[1], "envcount") == 0) {
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT envcount==<N>");
					return;
				}
				s->op = SOP_ASSERT_ENVCOUNT; s->a.n = atoi(tokens[3]);
			} else if(strcmp(tokens[1], "modulators") == 0) {
				/* ASSERT modulators == ( <op> , <kind> , <N> ) */
				if(nt < 9 || strcmp(tokens[2], "==") != 0 ||
				   strcmp(tokens[3], "(") != 0 ||
				   strcmp(tokens[5], ",") != 0 ||
				   strcmp(tokens[7], ",") != 0 ||
				   strcmp(tokens[9], ")") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT modulators==(<op>,<kind>,<N>)");
					return;
				}
				int opIdx = atoi(tokens[4]);
				int kind = atoi(tokens[6]);
				int count = atoi(tokens[8]);
				if(opIdx < 0 || opIdx >= MAX_FM_OPERATORS) {
					fclose(fp);
					failScript(lineno, "ASSERT modulators op %d out of range", opIdx);
					return;
				}
				if(kind < 0 || kind > 2) {
					fclose(fp);
					failScript(lineno, "ASSERT modulators kind %d out of range", kind);
					return;
				}
				s->op = SOP_ASSERT_MODULATORS;
				s->a.op = opIdx;
				s->a.kind = kind;
				s->b.n = count;
			} else if(strcmp(tokens[1], "selected") == 0) {
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT selected==<NAME>");
					return;
				}
				s->op = SOP_ASSERT_SELECTED;
				strncpy(s->name, tokens[3], sizeof(s->name) - 1);
				s->name[sizeof(s->name) - 1] = '\0';
			} else if(strcmp(tokens[1], "preset") == 0) {
				/* ASSERT preset == <NAME> : presetBank has a patch whose
				 * name matches NAME exactly (strcmp, same convention as
				 * presetNameExists in io/preset_io.c). */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT preset==<NAME>");
					return;
				}
				s->op = SOP_ASSERT_PRESET;
				strncpy(s->name, tokens[3], sizeof(s->name) - 1);
				s->name[sizeof(s->name) - 1] = '\0';
			} else if(strcmp(tokens[1], "file") == 0) {
				/* ASSERT file == <NAME> : on-disk existence of
				 * "data/instrument_presets/<sanitized NAME>.ipb".
				 * NAME is the user-facing preset name; sanitize
				 * mirrors preset_io.c::sanitizePresetFilename. */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT file==<NAME>");
					return;
				}
				s->op = SOP_ASSERT_FILE;
				strncpy(s->name, tokens[3], sizeof(s->name) - 1);
				s->name[sizeof(s->name) - 1] = '\0';
			} else if(strcmp(tokens[1], "presetCount") == 0) {
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT presetCount==<N>");
					return;
				}
				s->op = SOP_ASSERT_PRESETCOUNT;
				s->a.n = atoi(tokens[3]);
			} else if(strcmp(tokens[1], "algo") == 0) {
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT algo==<N>");
					return;
				}
				s->op = SOP_ASSERT_ALGO;
				s->a.n = atoi(tokens[3]);
			} else if(strcmp(tokens[1], "loadListActive") == 0) {
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT loadListActive==<N>");
					return;
				}
				s->op = SOP_ASSERT_LOADLISTACTIVE;
				s->a.n = atoi(tokens[3]);
			} else if(strcmp(tokens[1], "savedFlash") == 0) {
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT savedFlash==<0|1>");
					return;
				}
				s->op = SOP_ASSERT_SAVEDFLASH;
				s->a.n = atoi(tokens[3]);
			} else {
				fclose(fp);
				failScript(lineno, "ASSERT target '%s' unknown (envcount|modulators|selected|preset|file|presetCount|algo|loadListActive|savedFlash)", tokens[1]);
				return;
			}
		} else if(strcmp(op, "QUIT") == 0) {
			s->op = SOP_QUIT;
		} else {
			fclose(fp);
			failScript(lineno, "unknown opcode '%s'", op);
			return;
		}
		g_script.count++;
	}
	fclose(fp);
}

/* ----- Per-step synthetic input --------------------------------------- */

/* Set a single KeyMapping's flags (wasPressed first, then isPressed) so
 * the handler's isKeyJustPressed reads correctly on this frame. */
static void injectKey(InputState *state, KeyMapping k, bool press, bool heldPrev) {
	state->keys[k].wasPressed = heldPrev;
	state->keys[k].isPressed = press;
}

/* Run an assert. Populates g_scriptFailMsg + sets g_scriptState on fail. */
static void runAssertEnvcount(int lineno, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	int got = (inst ? inst->envelopeCount : -1);
	if(got != expected) {
		failScript(lineno, "ASSERT envcount==%d failed: got %d", expected, got);
	}
}

static void runAssertModulators(int lineno, int opIdx, int kind, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	if(!inst) {
		failScript(lineno, "ASSERT modulators: no instrument");
		return;
	}
	if(opIdx < 0 || opIdx >= MAX_FM_OPERATORS) {
		failScript(lineno, "ASSERT modulators: op %d out of range", opIdx);
		return;
	}
	Operator *o = inst->id.fm.ops[opIdx];
	if(!o) {
		failScript(lineno, "ASSERT modulators: op %d is NULL", opIdx);
		return;
	}
	Parameter *p = NULL;
	switch(kind) {
		case 0: p = o->feedbackAmount; break;
		case 1: p = o->ratio; break;
		case 2: p = o->level; break;
	}
	if(!p) {
		failScript(lineno, "ASSERT modulators: target param NULL");
		return;
	}
	if(p->modulator_count != expected) {
		failScript(lineno, "ASSERT modulators==(%d,%d,%d) failed: got %d",
		           opIdx, kind, expected, p->modulator_count);
	}
}

static void runAssertSelected(int lineno, const char *expected) {
	Graph *g = getSelectedInstGraph();
	const char *got = (g && g->selected && g->selected->name) ? g->selected->name : "(null)";
	if(strcmp(got, expected) != 0) {
		failScript(lineno, "ASSERT selected==%s failed: got %s", expected, got);
	}
}

/* Walk the in-memory preset bank looking for a patch whose name matches
 * `expected` exactly. Mirrors presetNameExists() from preset_io.c (strcmp
 * so short names don't get spuriously matched against longer ones).
 * Used after a save to prove the bank was updated. */
static void runAssertPreset(int lineno, const char *expected) {
	Instrument *inst = getSelectedInstInstrument();
	if(!inst || !inst->presetBank) {
		failScript(lineno, "ASSERT preset==%s: no instrument/presetBank", expected);
		return;
	}
	PresetBank *pb = inst->presetBank;
	for(int i = 0; i < pb->presetCount; i++) {
		if(strcmp(pb->patches[i].name, expected) == 0) {
			return;
		}
	}
	failScript(lineno, "ASSERT preset==%s failed: not found in bank (count=%d)",
	           expected, pb->presetCount);
}

/* Resolve "data/instrument_presets/<sanitized NAME>.ipb" to an absolute
 * path under the harness cwd (which run_scripted.sh cds into bin/), then
 * check existence with stat(). sanitizePresetFilename() from preset_io.c
 * turns "MY PATCH" into "MY_PATCH.ipb" (space -> underscore). Mirror
 * the convention exactly so the assert agrees with what saveInstrumentAsPreset
 * actually wrote. */
static void runAssertFile(int lineno, const char *expected) {
	char clean[48];
	sanitizePresetFilename(expected, clean, sizeof(clean));
	char path[512];
	snprintf(path, sizeof(path), "%s%s", HARNESS_PRESET_DIR, clean);
	struct stat st;
	if(stat(path, &st) == 0) {
		return;
	}
	failScript(lineno, "ASSERT file==%s failed: %s does not exist", expected, path);
}

static void runAssertPresetCount(int lineno, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	int got = (inst && inst->presetBank) ? inst->presetBank->presetCount : -1;
	if(got != expected) {
		failScript(lineno, "ASSERT presetCount==%d failed: got %d", expected, got);
	}
}

static void runAssertAlgo(int lineno, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	if(!inst || inst->voiceType != VOICE_TYPE_FM || !inst->id.fm.selectedAlgorithm) {
		failScript(lineno, "ASSERT algo==%d: no FM instrument or no algo param", expected);
		return;
	}
	int got = (int)inst->id.fm.selectedAlgorithm->baseValue;
	if(got != expected) {
		failScript(lineno, "ASSERT algo==%d failed: got %d", expected, got);
	}
}

/* Task 5: assert that the selected PresetNameGuiNode's saved flash is
 * active (==1) or inactive (==0). Drives through the public
 * presetNameGuiNodeSavedFlashActive getter so the harness doesn't reach
 * into gui.c private state. Selection must be the PRESET_NAME node. */
static void runAssertSavedFlash(int lineno, int expected) {
	GuiNode *sel = getSelectedInstGraph() ? getSelectedInstGraph()->selected : NULL;
	int got = sel ? (presetNameGuiNodeSavedFlashActive(sel) ? 1 : 0) : -1;
	if(got != expected) {
		failScript(lineno, "ASSERT savedFlash==%d failed: got %d (selection %s)",
		           expected, got, sel ? "present" : "NULL");
	}
}

static void runAssertLoadListActive(int lineno, int expected) {
	int got = guiIsLoadListActive() ? 1 : 0;
	if(got != expected) {
		failScript(lineno, "ASSERT loadListActive==%d failed: got %d", expected, got);
	}
}

/* Zero the keys array; used as the resting state between scripted events. */
static void clearInjectedKeys(InputState *state) {
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		state->keys[i].isPressed = false;
		state->keys[i].wasPressed = false;
	}
}

/* Apply the input state for the current step's current subframe.
 * After this returns the caller invokes the same instrument-input
 * block the interactive loop uses. */
static void applyScriptEventInjection(InputState *state, const ScriptStep *s, int subframe) {
	switch(s->op) {
		case SOP_KEY:
			/* single-frame just-pressed */
			injectKey(state, s->a.key, true, false);
			break;
		case SOP_EDIT_ARROW: {
			/* 3 subframes: hold EDIT (no arrow), arrow-just-pressed (EDIT held), release all */
			if(subframe == 0) {
				injectKey(state, KM_EDIT, true, true);
			} else if(subframe == 1) {
				injectKey(state, KM_EDIT, true, true);
				injectKey(state, s->a.arrow, true, false);
			} else {
				injectKey(state, KM_EDIT, false, false);
			}
			break;
		}
		case SOP_FRAMES:
		case SOP_QUIT:
		case SOP_ASSERT_ENVCOUNT:
		case SOP_ASSERT_MODULATORS:
		case SOP_ASSERT_SELECTED:
		default:
			break;
	}
}

/* Process one scripted "assert" immediately. Asserts evaluate the
 * current graph state right after the previous handler run. They
 * take one frame to execute (no key injection). */
static void processScriptAssert(const ScriptStep *s) {
	switch(s->op) {
		case SOP_ASSERT_ENVCOUNT:
			runAssertEnvcount(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_MODULATORS:
			runAssertModulators(s->lineno, s->a.op, s->a.kind, s->b.n);
			break;
		case SOP_ASSERT_SELECTED:
			runAssertSelected(s->lineno, s->name);
			break;
		case SOP_ASSERT_PRESET:
			runAssertPreset(s->lineno, s->name);
			break;
		case SOP_ASSERT_FILE:
			runAssertFile(s->lineno, s->name);
			break;
		case SOP_ASSERT_PRESETCOUNT:
			runAssertPresetCount(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_ALGO:
			runAssertAlgo(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_LOADLISTACTIVE:
			runAssertLoadListActive(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_SAVEDFLASH:
			runAssertSavedFlash(s->lineno, s->a.n);
			break;
		default:
			break;
	}
}

/* ----- Shared input handler (used by both interactive and scripted) --- */

/* Mirrors main.c's SCENE_INSTRUMENT branch. Has the optional arg
 * `data` so the FUNCTION+LEFT/RIGHT arranger cell selector still has
 * access to paTestData. Defensively guards currentGraph->selected NULL. */
static void handleInstrumentInput(paTestData *data, ApplicationState *appState) {
	if(isKeyJustPressed(appState->inputState, KM_ADD)) {
		Instrument *inst = getSelectedInstInstrument();
		if(inst && inst->voiceType == VOICE_TYPE_FM) {
			addRuntimeEnvelope(inst);
		}
	}
	if(isKeyJustPressed(appState->inputState, KM_REMOVE)) {
		removeSelectedEnvelope();
	}
	if(isKeyHeld(appState->inputState, KM_FUNCTION)) {
		if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
			selectArrangerCell(data->arranger, 0, -1, 0);
		}
		if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
			selectArrangerCell(data->arranger, 0, 1, 0);
		}
	}

	Graph *currentGraph = getSelectedInstGraph();
	/* Defensive: after a graph rebuild the selection can briefly be NULL
	 * (e.g. the selected node was freed by freeGuiNode before the new
	 * graph is wired up). Skip navigation / edit callbacks in that case
	 * instead of dereferencing NULL. */
	if(!currentGraph || !currentGraph->selected) {
		return;
	}
	if(handlePresetUiInput(appState->inputState, getSelectedInstInstrument())) {
		return;
	}

	if(isKeyHeld(appState->inputState, KM_EDIT)) {
		/* Task 3: only dispatch the value callback when the selected
		 * node is a real dial. Mirrors the main.c guard so the scripted
		 * fixture (EDIT + DOWN on PRESET_NAME) doesn't crash. */
		if(isSelectedDialNode(currentGraph)) {
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				currentGraph->selected->callback(currentGraph->selected->p, -0.1f);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				currentGraph->selected->callback(currentGraph->selected->p, 0.1f);
			}
			if(isKeyJustPressed(appState->inputState, KM_UP)) {
				currentGraph->selected->callback(currentGraph->selected->p, 2.0f);
			}
			if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
				currentGraph->selected->callback(currentGraph->selected->p, -2.0f);
			}
		}
	} else {
		if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
			navigateGraph(currentGraph, KM_LEFT);
		}
		if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
			navigateGraph(currentGraph, KM_RIGHT);
		}
		if(isKeyJustPressed(appState->inputState, KM_UP)) {
			navigateGraph(currentGraph, KM_UP);
		}
		if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
			navigateGraph(currentGraph, KM_DOWN);
		}
	}
}

/* ----- Entry points ---------------------------------------------------- */

static void runInteractive(paTestData *data, ApplicationState *appState) {
	while(!WindowShouldClose()) {
		updateInputState(appState->inputState);
		BeginDrawing();
		clearBg();
		handleInstrumentInput(data, appState);
		DrawGUI(appState->currentScene);
		EndDrawing();
	}
	CloseWindow();
}

static void runScripted(paTestData *data, ApplicationState *appState) {
	while(!WindowShouldClose() && g_scriptState == SCRIPT_RUNNING) {
		ScriptStep *s = &g_script.steps[g_scriptStepIdx];

		/* In scripted mode we DO NOT call updateInputState(): raylib
		 * sees no keyboard under Xvfb, so updateInputState would just
		 * overwrite our injection with zero. Instead we drive the
		 * InputState directly each frame. */
		clearInjectedKeys(appState->inputState);
		applyScriptEventInjection(appState->inputState, s, g_scriptSubframe);

		BeginDrawing();
		clearBg();
		handleInstrumentInput(data, appState);
		if(!g_scriptMode) {
			DrawGUI(appState->currentScene);
		}
		EndDrawing();

		g_scriptSubframe++;
		if(g_scriptSubframe >= s->frames) {
			g_scriptSubframe = 0;
			/* Evaluate this step's assert NOW (post-handler), with the
			 * state as it actually is after the input event. */
			processScriptAssert(s);
			if(g_scriptState != SCRIPT_RUNNING) {
				break;
			}
			g_scriptStepIdx++;
			if(s->op == SOP_QUIT) {
				/* Finish on a clean exit. */
				if(g_scriptState == SCRIPT_RUNNING) {
					g_scriptState = SCRIPT_PASS;
				}
				break;
			}
			if(g_scriptStepIdx >= g_script.count) {
				g_scriptState = SCRIPT_PASS;
				break;
			}
		}
		if(g_scriptState == SCRIPT_FAIL) {
			break;
		}
	}
	CloseWindow();
}

/* Light arg parsing: --script <path>. Anything else is interactive. */
static int parseArgs(int argc, char **argv, const char **scriptPath) {
	*scriptPath = NULL;
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
			*scriptPath = argv[i + 1];
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	const char *scriptPath = NULL;
	int scripted = parseArgs(argc, argv, &scriptPath);

	InitGUI();

	paTestData data;
	ApplicationState *appState;
	initApplication(&data, &appState, NULL);
	initModSystem();

	if(appState->selectedPattern < 0 && data.arranger->song[0][0] >= 0) {
		appState->selectedPattern = data.arranger->song[0][0];
	}
	appState->currentScene = SCENE_INSTRUMENT;

	if(scripted) {
		parseScript(scriptPath);
		if(g_scriptState == SCRIPT_FAIL) {
			/* parseScript failed before we ever started. */
			fprintf(stderr, "FAIL script parse error: %s\n", g_scriptFailMsg);
			return 1;
		}
		if(g_script.count == 0) {
			fprintf(stderr, "FAIL empty script\n");
			return 1;
		}
		g_scriptState = SCRIPT_RUNNING;
		g_scriptMode = true;
		runScripted(&data, appState);
		if(g_scriptState == SCRIPT_PASS) {
			fprintf(stdout, "PASS\n");
			return 0;
		}
		fprintf(stderr, "FAIL %s\n", g_scriptFailMsg);
		return 1;
	}

	runInteractive(&data, appState);
	return 0;
}
