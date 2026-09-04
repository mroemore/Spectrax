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
 * Config coupling: the app splits config (~/.config/spectrax, cfg.json +
 * clr.json) from data (assets/song/presets). The harness runs from bin/
 * without main()'s --data-dir/--config-dir plumbing, so initApplication
 * resolves the config dir itself — which lands on ~/.config/spectrax when
 * it exists. Fixtures assert against default settings; if the user's home
 * config ever diverges from the defaults, the fixtures must account for it.
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
/* raylib and X11 both typedef `Font` and `Drawable`; rename X11's away
 * (same trick as nav_harness). Used by shotX11 (SHOT script verb) to
 * capture the presented window under Xvfb/llvmpipe where raylib's
 * TakeScreenshot reads back a blank frame. */
#define Font _X11_Font
#define Drawable _X11_Drawable
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef Drawable
#undef Font

#include "main.h"
#include "gui.h"
#include "gui_layer.h"
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
	SOP_ASSERT_RATIO1,   /* FM op0 ratio baseValue (as int) == N */
	SOP_ASSERT_LOADLISTACTIVE, /* g_loadListActive (guiIsLoadListActive) == N */
	SOP_ASSERT_SAVEDFLASH,     /* Task 5: selected PresetNameGuiNode's savedFlashUntil
	                            * is strictly in the future (currentFrameIndex() < it).
	                            * ==1 right after a successful save, ==0 once the
	                            * ~30-frame flash window has expired. */
	SOP_ASSERT_TOPLAYER_SELECTED, /* Task 7: topmost overlay layer's graph
	                            * selected->name == "NAME". Falls back to the
	                            * instrument graph when no layer is active. */
	SOP_SET_SCENE,      /* Task 7: assign appState->currentScene = N (0=ARRANGER, 1=INSTRUMENT) */
	SOP_ASSERT_SCENE,   /* Task 7: appState->currentScene == N */
	SOP_ASSERT_CHIP_EXPANDED, /* Task 7: isChipExpanded(CH) == N (0|1) */
	SOP_SELECT_NAMED,        /* Task 7: changeGraphSelection(getSelectedInstGraph(), findSelectableByName(...)) */
	SOP_ASSERT_CHIP_LABEL,    /* Task 7: strcmp(arranger->label[CH], name) == 0 */
	SOP_ASSERT_CHIP_COLOUR,   /* Task 7: arranger->labelColourIdx[CH] == N */
	SOP_ASSERT_VOICE_TYPE,    /* Task 7: inst->voiceType == N (matches VOICE_TYPE_* enum) */
	SOP_ASSERT_VOICE_COUNT,   /* Task 7: roundf(inst->voiceCountParam->baseValue) == N */
	SOP_SHOT,          /* capture the window to a PNG (X11 XGetImage) */
	SOP_REPORT,        /* Task 4: print a GuiNode's rect (x y w h) to stdout */
	SOP_QUIT
} ScriptOpKind;

typedef struct {
	ScriptOpKind op;
	int lineno;          /* source line for error reporting */
	int frames;          /* for SOP_FRAMES, and per-step subframe counter */
	int opIdx;           /* for SOP_ASSERT_MODULATORS: operator index 0..3 */
	int kind;            /* for SOP_ASSERT_MODULATORS: 0=fb 1=rat 2=lvl */
	union {
		KeyMapping key; /* for SOP_KEY */
		KeyMapping arrow; /* for SOP_EDIT_ARROW */
		int n;          /* for SOP_FRAMES, SOP_ASSERT_ENVCOUNT, SOP_ASSERT_PRESETCOUNT,
		                 * SOP_ASSERT_ALGO, chipChannel, labelColourIdx, voiceType;
		                 * and N for modulators */
	} a;
	union {
		int n;          /* secondary int (modulator_count), voiceCount, expanded-flag 0|1, scene index */
	} b;
	char name[64];       /* for SOP_ASSERT_SELECTED, SOP_ASSERT_PRESET, SOP_ASSERT_FILE,
	                      * SOP_ASSERT_CHIP_LABEL */
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
 *   ADD | REMOVE | LEFT | RIGHT | UP | DOWN | SELECT | START | FUNCTION | EDIT
 *   EDIT <LEFT|RIGHT|UP|DOWN>
 *   FRAMES <N>
 *   SCENE <N>                       -- appState->currentScene = N (0=ARRANGER, 1=INSTRUMENT)
 *   ASSERT envcount==<N>
 *   ASSERT modulators==(<op>,<kind>,<N>)
 *   ASSERT selected==<NAME>
 *   ASSERT topLayerSelected==<NAME>
 *   ASSERT preset==<NAME>           -- presetBank has patch named NAME
 *   ASSERT file==<NAME>             -- data/instrument_presets/<san NAME>.ipb exists
 *   ASSERT presetCount==<N>         -- presetBank->presetCount == N
 *   ASSERT algo==<N>                -- FM selectedAlgorithm baseValue == N
 *   ASSERT ratio1==<N>              -- FM op0 ratio baseValue (as int)
 *   ASSERT loadListActive==<0|1>
 *   ASSERT savedFlash==<0|1>
 *   ASSERT scene==<N>               -- appState->currentScene == N
 *   ASSERT chipExpanded==<CH> <0|1> -- isChipExpanded(CH) == 0|1
 *   ASSERT chipLabel==<CH> "<TXT>"  -- strcmp(arranger->label[CH], TXT) == 0
 *   ASSERT chipColour==<CH> <N>     -- arranger->labelColourIdx[CH] == N
 *   ASSERT voiceType==<SAMPLE|FM|BLEP|GRAIN|SPECTRAL>
 *                                   -- inst->voiceType == VOICE_TYPE_<...>
 *   ASSERT voiceCount==<N>          -- roundf(inst->voiceCountParam->baseValue) == N
 *   SHOT <path>
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
		} else if(strcmp(op, "SCENE") == 0) {
			/* SCENE <N>: assign appState->currentScene = N. 0=ARRANGER,
			 * 1=INSTRUMENT. Effects are observed next frame (input
			 * dispatcher switches on scene at the start of each
			 * handleInstrumentInput). One-frame step — assignment takes
			 * effect immediately so the frame the next keypress is on
			 * already routes correctly. */
			if(nt < 2) {
				fclose(fp);
				failScript(lineno, "SCENE requires N (0=ARRANGER, 1=INSTRUMENT)");
				return;
			}
			int n = atoi(tokens[1]);
			Scene target;
			if(n == 0) {
				target = SCENE_ARRANGER;
			} else if(n == 1) {
				target = SCENE_INSTRUMENT;
			} else {
				fclose(fp);
				failScript(lineno, "SCENE N must be 0 (ARRANGER) or 1 (INSTRUMENT)");
				return;
			}
			s->op = SOP_SET_SCENE; s->b.n = (int)target; s->frames = 1;
		} else if(strcmp(op, "JUMP") == 0) {
			/* JUMP <name>: deterministic navigation aid. The plain SELECT
			 * verb collides with the KM_SELECT key parser — rename to
			 * JUMP. Searches the instrument graph for a selectable node
			 * whose `name` matches and reassigns selection. */
			if(nt < 2) {
				fclose(fp);
				failScript(lineno, "JUMP requires a node name");
				return;
			}
			s->op = SOP_SELECT_NAMED; s->frames = 1;
			strncpy(s->name, tokens[1], sizeof(s->name) - 1);
			s->name[sizeof(s->name) - 1] = '\0';
		} else if(strcmp(op, "SHOT") == 0) {
			/* SHOT <path>: capture the window to a PNG via X11. */
			if(nt < 2) {
				fclose(fp);
				failScript(lineno, "SHOT requires a path");
				return;
			}
			s->op = SOP_SHOT; s->frames = 1;
			strncpy(s->name, tokens[1], sizeof(s->name) - 1);
			s->name[sizeof(s->name) - 1] = '\0';
		} else if(strcmp(op, "REPORT") == 0) {
			/* REPORT <name>: Task 4 size verification. Print the rect
			 * (x y w h) of the GuiNode whose name matches — works for
			 * any node (selectable or wrapper). Used to confirm the
			 * META row is ~35px after the meta-row rework, and that
			 * the preset row matches the AD-env row heights. */
			if(nt < 2) {
				fclose(fp);
				failScript(lineno, "REPORT requires a node name");
				return;
			}
			s->op = SOP_REPORT; s->frames = 1;
			strncpy(s->name, tokens[1], sizeof(s->name) - 1);
			s->name[sizeof(s->name) - 1] = '\0';
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
				s->opIdx = opIdx;
				s->kind = kind;
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
			} else if(strcmp(tokens[1], "topLayerSelected") == 0) {
				/* ASSERT topLayerSelected==<NAME>: like selected, but reads
				 * the topmost overlay layer's graph (for modal navigation).
				 * Falls back to the instrument graph if no layer is active. */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT topLayerSelected==<NAME>");
					return;
				}
				s->op = SOP_ASSERT_TOPLAYER_SELECTED;
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
			} else if(strcmp(tokens[1], "ratio1") == 0) {
				/* ASSERT ratio1==<N>: FM op0 ratio baseValue (as int). */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT ratio1==<N>");
					return;
				}
				s->op = SOP_ASSERT_RATIO1;
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
			} else if(strcmp(tokens[1], "scene") == 0) {
				/* ASSERT scene == <N> : appState->currentScene matches. */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT scene==<N>");
					return;
				}
				s->op = SOP_ASSERT_SCENE;
				s->a.n = atoi(tokens[3]);
			} else if(strcmp(tokens[1], "chipExpanded") == 0) {
				/* ASSERT chipExpanded == <CH> <0|1> : isChipExpanded(CH) == 0|1. */
				if(nt < 5 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT chipExpanded==<CH> <0|1>");
					return;
				}
				int ch = atoi(tokens[3]);
				if(ch < 0 || ch >= MAX_SEQUENCER_CHANNELS) {
					fclose(fp);
					failScript(lineno, "ASSERT chipExpanded CH %d out of range", ch);
					return;
				}
				int flag = atoi(tokens[4]);
				s->op = SOP_ASSERT_CHIP_EXPANDED;
				s->a.n = ch;
				s->b.n = flag;
			} else if(strcmp(tokens[1], "chipLabel") == 0) {
				/* ASSERT chipLabel == <CH> "<TEXT>" : arranger->label[CH] matches. */
				if(nt < 5 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT chipLabel==<CH> \"<TEXT>\"");
					return;
				}
				int ch = atoi(tokens[3]);
				if(ch < 0 || ch >= MAX_SEQUENCER_CHANNELS) {
					fclose(fp);
					failScript(lineno, "ASSERT chipLabel CH %d out of range", ch);
					return;
				}
				/* The whitespace tokenizer leaves the optional quotes
				 * around tokens[4] ("B" stays as one token). Strip them
				 * so the C-string compared in runAssertChipLabel doesn't
				 * have straggling leading/trailing '"' characters. */
				const char *t = tokens[4];
				size_t tlen = strlen(t);
				if(tlen >= 2 && t[0] == '"' && t[tlen - 1] == '"') {
					t++;
					tlen -= 2;
				}
				s->op = SOP_ASSERT_CHIP_LABEL;
				s->a.n = ch;
				if(tlen >= sizeof(s->name)) tlen = sizeof(s->name) - 1;
				memcpy(s->name, t, tlen);
				s->name[tlen] = '\0';
			} else if(strcmp(tokens[1], "chipColour") == 0) {
				/* ASSERT chipColour == <CH> <N> : arranger->labelColourIdx[CH] == N. */
				if(nt < 5 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT chipColour==<CH> <N>");
					return;
				}
				int ch = atoi(tokens[3]);
				if(ch < 0 || ch >= MAX_SEQUENCER_CHANNELS) {
					fclose(fp);
					failScript(lineno, "ASSERT chipColour CH %d out of range", ch);
					return;
				}
				s->op = SOP_ASSERT_CHIP_COLOUR;
				s->a.n = ch;
				s->b.n = atoi(tokens[4]);
			} else if(strcmp(tokens[1], "voiceType") == 0) {
				/* ASSERT voiceType == <SAMPLE|FM|BLEP|GRAIN|SPECTRAL> :
				 * inst->voiceType matches VOICE_TYPE_<...>. */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT voiceType==<SAMPLE|FM|BLEP|GRAIN|SPECTRAL>");
					return;
				}
				int vt;
				if(strcmp(tokens[3], "SAMPLE") == 0) {
					vt = VOICE_TYPE_SAMPLE;
				} else if(strcmp(tokens[3], "FM") == 0) {
					vt = VOICE_TYPE_FM;
				} else if(strcmp(tokens[3], "BLEP") == 0) {
					vt = VOICE_TYPE_BLEP;
				} else if(strcmp(tokens[3], "GRAIN") == 0) {
					vt = VOICE_TYPE_GRAIN;
				} else if(strcmp(tokens[3], "SPECTRAL") == 0) {
					vt = VOICE_TYPE_SPECTRAL;
				} else {
					fclose(fp);
					failScript(lineno, "ASSERT voiceType '%s' unknown (SAMPLE|FM|BLEP|GRAIN|SPECTRAL)", tokens[3]);
					return;
				}
				s->op = SOP_ASSERT_VOICE_TYPE;
				s->a.n = vt;
			} else if(strcmp(tokens[1], "voiceCount") == 0) {
				/* ASSERT voiceCount == <N> :
				 * roundf(inst->voiceCountParam->baseValue) == N. */
				if(nt < 4 || strcmp(tokens[2], "==") != 0) {
					fclose(fp);
					failScript(lineno, "ASSERT voiceCount==<N>");
					return;
				}
				s->op = SOP_ASSERT_VOICE_COUNT;
				s->a.n = atoi(tokens[3]);
			} else {
				fclose(fp);
				failScript(lineno, "ASSERT target '%s' unknown (envcount|modulators|selected|preset|file|presetCount|algo|loadListActive|savedFlash|scene|chipExpanded|chipLabel|chipColour|voiceType|voiceCount)", tokens[1]);
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

/* Task 7: file-static pointer to the live ApplicationState installed by
 * runScripted at entry. Used by runAssertSelected() (scene-aware) and
 * the new scene/chip/voice asserts so we don't have to thread paTestData
 * through every helper signature. */
static ApplicationState *s_scriptAppState = NULL;
static paTestData *s_scriptData = NULL;

static void runAssertSelected(int lineno, const char *expected) {
	/* Task 7: scene-aware — when we're in SCENE_ARRANGER the 'selected'
	 * the fixture cares about lives in the arranger graph (chip row vs
	 * chip vs grid). In SCENE_INSTRUMENT it lives in the instrument
	 * graph as before. */
	const char *got;
	if(s_scriptAppState && (int)s_scriptAppState->currentScene == (int)SCENE_ARRANGER) {
		GuiNode *sel = getArrangerSelectedNode();
		got = (sel && sel->name) ? sel->name : "(null)";
	} else {
		Graph *g = getSelectedInstGraph();
		got = (g && g->selected && g->selected->name) ? g->selected->name : "(null)";
	}
	if(strcmp(got, expected) != 0) {
		failScript(lineno, "ASSERT selected==%s failed: got %s", expected, got);
	}
}

/* Task 7: assert the topmost overlay layer's graph selected->name.
 * Falls back to the instrument graph if no layer is active. */
static void runAssertTopLayerSelected(int lineno, const char *expected) {
	InstrumentGui *ig = getInstrumentGui();
	Graph *g = NULL;
	if(ig && ig->overlayLayers.count > 0) {
		Layer *top = &ig->overlayLayers.layers[ig->overlayLayers.count - 1];
		g = top->graph;
	}
	if(!g) {
		g = getSelectedInstGraph();
	}
	const char *got = (g && g->selected && g->selected->name) ? g->selected->name : "(null)";
	if(strcmp(got, expected) != 0) {
		failScript(lineno, "ASSERT topLayerSelected==%s failed: got %s", expected, got);
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

/* Task 8: assert FM op0 ratio baseValue (as int) == N. Proves a param
 * edit persisted through a save/load round-trip. */
static void runAssertRatio1(int lineno, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	if(!inst || inst->voiceType != VOICE_TYPE_FM || !inst->id.fm.ops[0] || !inst->id.fm.ops[0]->ratio) {
		failScript(lineno, "ASSERT ratio1==%d: no FM instrument or no op0 ratio", expected);
		return;
	}
	int got = (int)inst->id.fm.ops[0]->ratio->baseValue;
	if(got != expected) {
		failScript(lineno, "ASSERT ratio1==%d failed: got %d", expected, got);
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

/* Task 7: scene assertion. The fixture uses SCENE 0/1 to switch
 * between ARRANGER and INSTRUMENT; this checks the assignment stuck.
 * Reads via the file-static s_scriptAppState pointer set in runScripted
 * entry (see runScripted) so we don't have to thread the pointer
 * through every assert helper. */
static void runAssertScene(int lineno, int expected) {
	if(!s_scriptAppState) {
		failScript(lineno, "ASSERT scene==%d: no script state", expected);
		return;
	}
	/* Translate the Scene enum (SCENE_ARRANGER=1, SCENE_INSTRUMENT=3)
	 * back to the user-facing 0/1 form the fixture uses. */
	int gotEnum = (int)s_scriptAppState->currentScene;
	int got;
	if(gotEnum == (int)SCENE_ARRANGER) {
		got = 0;
	} else if(gotEnum == (int)SCENE_INSTRUMENT) {
		got = 1;
	} else {
		got = gotEnum;
	}
	if(got != expected) {
		failScript(lineno, "ASSERT scene==%d failed: got %d", expected, got);
	}
}

/* Task 7: assert isChipExpanded(CH) == expected (0 collapsed, 1
 * expanded). Reads through the public isChipExpanded() getter. */
static void runAssertChipExpanded(int lineno, int channel, int expected) {
	int got = isChipExpanded(channel) ? 1 : 0;
	if(got != expected) {
		failScript(lineno, "ASSERT chipExpanded==%d %d failed: got %d",
		           channel, expected, got);
	}
}

/* Task 7: deep-search the graph root for the first selectable node whose
 * `name` matches `want` (exact match). Returns the GuiNode* or NULL.
 * Mirrors the static collectSelectables in graph_gui.c but stops early
 * when a match is found. */
static GuiNode *findSelectableByName(GuiNode *root, const char *want) {
	if(!root || !want) {
		return NULL;
	}
	if(root->selectable && root->name && strcmp(root->name, want) == 0) {
		return root;
	}
	if(!root->items) {
		return NULL;
	}
	ListElement *l = root->items->head;
	while(l != NULL) {
		GuiNode *child = *(GuiNode **)l->data;
		GuiNode *match = findSelectableByName(child, want);
		if(match) {
			return match;
		}
		l = l->next;
	}
	return NULL;
}

/* Task 4: like findSelectableByName but matches any GuiNode regardless
 * of the `selectable` flag — needed to inspect wrapper rows like the
 * "META" or "presetwrappa" containers whose `selectable` is false.
 * Recurses the same way and stops at the first name match. */
static GuiNode *findNodeByName(GuiNode *root, const char *want) {
	if(!root || !want) {
		return NULL;
	}
	if(root->name && strcmp(root->name, want) == 0) {
		return root;
	}
	if(!root->items) {
		return NULL;
	}
	ListElement *l = root->items->head;
	while(l != NULL) {
		GuiNode *child = *(GuiNode **)l->data;
		GuiNode *match = findNodeByName(child, want);
		if(match) {
			return match;
		}
		l = l->next;
	}
	return NULL;
}

/* Task 7: assert arranger->label[CH] matches the (8-char max) string
 * recorded in s->name. Reads through s_scriptData->arranger — the
 * arranger struct is reachable via the paTestData pointer cached at
 * the top of runScripted. */
static void runAssertChipLabel(int lineno, int channel, const char *expected) {
	if(!s_scriptData || !s_scriptData->arranger) {
		failScript(lineno, "ASSERT chipLabel==%d \"%s\": arranger NULL", channel, expected);
		return;
	}
	const char *got = s_scriptData->arranger->label[channel];
	if(strcmp(got, expected) != 0) {
		failScript(lineno, "ASSERT chipLabel==%d \"%s\" failed: got \"%s\"",
		           channel, expected, got);
	}
}

/* Task 7: assert arranger->labelColourIdx[CH] == expected. Index 0..7. */
static void runAssertChipColour(int lineno, int channel, int expected) {
	if(!s_scriptData || !s_scriptData->arranger) {
		failScript(lineno, "ASSERT chipColour==%d %d: arranger NULL", channel, expected);
		return;
	}
	int got = s_scriptData->arranger->labelColourIdx[channel];
	if(got != expected) {
		failScript(lineno, "ASSERT chipColour==%d %d failed: got %d",
		           channel, expected, got);
	}
}

/* Task 7: assert inst->voiceType == expected (one of VOICE_TYPE_*).
 * Compared against an enum int cast. */
static void runAssertVoiceType(int lineno, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	if(!inst) {
		failScript(lineno, "ASSERT voiceType==%d: no instrument", expected);
		return;
	}
	int got = (int)inst->voiceType;
	if(got != expected) {
		failScript(lineno, "ASSERT voiceType==%d failed: got %d", expected, got);
	}
}

/* Task 7: assert roundf(inst->voiceCountParam->baseValue) == expected.
 * The voice-count param is a float in the model; the harness rounds it
 * so the fixture reads back an int. */
static void runAssertVoiceCount(int lineno, int expected) {
	Instrument *inst = getSelectedInstInstrument();
	if(!inst || !inst->voiceCountParam) {
		failScript(lineno, "ASSERT voiceCount==%d: no instrument/param", expected);
		return;
	}
	int got = (int)roundf(inst->voiceCountParam->baseValue);
	if(got != expected) {
		failScript(lineno, "ASSERT voiceCount==%d failed: got %d", expected, got);
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
	(void)state;
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
		case SOP_SET_SCENE:
			/* Idempotent on subframe 0. The scene switch happens
			 * BEFORE handleInstrumentInput runs that frame, so the
			 * new scene is in effect for the very first dispatch. */
			if(subframe == 0 && s_scriptAppState) {
				s_scriptAppState->currentScene = (Scene)s->b.n;
			}
			break;
		case SOP_FRAMES:
		case SOP_QUIT:
		case SOP_ASSERT_ENVCOUNT:
		case SOP_ASSERT_MODULATORS:
		case SOP_ASSERT_SELECTED:
		case SOP_ASSERT_TOPLAYER_SELECTED:
		case SOP_ASSERT_PRESET:
		case SOP_ASSERT_FILE:
		case SOP_ASSERT_PRESETCOUNT:
		case SOP_ASSERT_ALGO:
		case SOP_ASSERT_RATIO1:
		case SOP_ASSERT_LOADLISTACTIVE:
		case SOP_ASSERT_SAVEDFLASH:
		case SOP_ASSERT_SCENE:
		case SOP_ASSERT_CHIP_EXPANDED:
		case SOP_ASSERT_CHIP_LABEL:
		case SOP_ASSERT_CHIP_COLOUR:
		case SOP_ASSERT_VOICE_TYPE:
		case SOP_ASSERT_VOICE_COUNT:
		case SOP_SHOT:
		case SOP_REPORT:
		default:
			break;
	}
}

/* Process one scripted "assert" immediately. Asserts evaluate the
 * current graph state right after the previous handler run. They
 * take one frame to execute (no key injection). */
static int shotX11(const char *path);
static void processScriptAssert(const ScriptStep *s) {
	switch(s->op) {
		case SOP_ASSERT_ENVCOUNT:
			runAssertEnvcount(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_MODULATORS:
			runAssertModulators(s->lineno, s->opIdx, s->kind, s->b.n);
			break;
		case SOP_ASSERT_SELECTED:
			runAssertSelected(s->lineno, s->name);
			break;
		case SOP_ASSERT_TOPLAYER_SELECTED:
			runAssertTopLayerSelected(s->lineno, s->name);
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
		case SOP_ASSERT_RATIO1:
			runAssertRatio1(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_LOADLISTACTIVE:
			runAssertLoadListActive(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_SAVEDFLASH:
			runAssertSavedFlash(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_SCENE:
			runAssertScene(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_CHIP_EXPANDED:
			runAssertChipExpanded(s->lineno, s->a.n, s->b.n);
			break;
		case SOP_ASSERT_CHIP_LABEL:
			runAssertChipLabel(s->lineno, s->a.n, s->name);
			break;
		case SOP_ASSERT_CHIP_COLOUR:
			runAssertChipColour(s->lineno, s->a.n, s->b.n);
			break;
		case SOP_ASSERT_VOICE_TYPE:
			runAssertVoiceType(s->lineno, s->a.n);
			break;
		case SOP_ASSERT_VOICE_COUNT:
			runAssertVoiceCount(s->lineno, s->a.n);
			break;
		case SOP_SELECT_NAMED: {
			/* Task 7: deterministic navigation — find a selectable by name
			 * in the current instrument graph and reassign selection. */
			Graph *cg = getSelectedInstGraph();
			if(!cg) {
				failScript(s->lineno, "SELECT %s: no instrument graph", s->name);
			} else {
				GuiNode *match = findSelectableByName(cg->root, s->name);
				if(!match) {
					failScript(s->lineno, "JUMP %s: no selectable with that name", s->name);
				} else {
					changeGraphSelection(cg, match);
				}
			}
			break;
		}
		case SOP_SHOT:
			if(shotX11(s->name) != 0) {
				failScript(s->lineno, "SHOT %s: capture failed", s->name);
			}
			break;
		case SOP_REPORT: {
			/* Task 4 size verification — find the named GuiNode (any
			 * depth, selectable or wrapper) and print its rect. Doesn't
			 * fail the script: a missing node is just an empty result so
			 * the harness log makes the size evidence easy to read. */
			Graph *cg = getSelectedInstGraph();
			if(!cg) {
				printf("[REPORT] %s: no instrument graph\n", s->name);
				break;
			}
			GuiNode *match = findNodeByName(cg->root, s->name);
			if(!match) {
				printf("[REPORT] %s: not found\n", s->name);
				break;
			}
			printf("[REPORT] %s: x=%d y=%d w=%d h=%d\n", s->name, match->x, match->y, match->w, match->h);
			break;
		}
		default:
			break;
	}
}

/*
 * raylib TakeScreenshot reads back a blank frame under Xvfb/llvmpipe, so
 * capture the presented window via X11 XGetImage instead (same technique
 * as nav_harness). GetWindowHandle() returns a GLFW struct, not the X
 * Window id -- walk the root's children and match by size.
 */
static int shotX11(const char *path) {
	Display *dpy = XOpenDisplay(NULL);
	if(!dpy) {
		return 1;
	}
	/* Capture the app's window at its CURRENT geometry (the window may
	 * have been resized — the app renders at a fixed 640x480 content
	 * resolution and letterboxes it into the window). Under Xvfb the
	 * harness window is the only visible one. */
	Window root = DefaultRootWindow(dpy);
	Window parent;
	Window *kids = NULL;
	unsigned int n = 0;
	Window win = 0;
	XWindowAttributes a;
	memset(&a, 0, sizeof(a));
	if(XQueryTree(dpy, root, &root, &parent, &kids, &n)) {
		for(unsigned int i = 0; i < n; i++) {
			if(XGetWindowAttributes(dpy, kids[i], &a) && a.map_state == IsViewable) {
				win = kids[i];
				break;
			}
		}
	}
	if(kids) {
		XFree(kids);
	}
	if(!win || a.width <= 0 || a.height <= 0) {
		XCloseDisplay(dpy);
		return 1;
	}
	int W = a.width;
	int H = a.height;
	XImage *img = XGetImage(dpy, win, 0, 0, W, H, AllPlanes, ZPixmap);
	if(!img) {
		XCloseDisplay(dpy);
		return 1;
	}
	int bpp = img->bits_per_pixel / 8;
	unsigned char *pix = malloc((size_t)W * H * 4);
	unsigned char *src = (unsigned char *)img->data;
	for(int i = 0; i < W * H; i++) {
		unsigned char *s = src + (size_t)i * bpp;
		pix[i * 4 + 0] = s[2];
		pix[i * 4 + 1] = s[1];
		pix[i * 4 + 2] = s[0];
		pix[i * 4 + 3] = 255;
	}
	XDestroyImage(img);
	XCloseDisplay(dpy);
	Image ri = { 0 };
	ri.width = W;
	ri.height = H;
	ri.mipmaps = 1;
	ri.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	ri.data = pix;
	bool ok = ExportImage(ri, path);
	free(pix);
	return ok ? 0 : 1;
}

/* ----- Shared input handler (used by both interactive and scripted) --- */

/* Mirrors main.c's SCENE_ARRANGER branch — chip row + expanded chip
 * input routing. Same priority order as the live engine: KM_SELECT +
 * KM_START collapses a chip (when KM_EDIT is not held), then KM_EDIT
 * held dispatches per-chip arrows + swatch + label-edit, and bare
 * arrows navigate the arranger graph (or the swatch when the chip
 * is expanded). */
static void handleArrangerInput(paTestData *data, ApplicationState *appState) {
	(void)data;
	int chForCollapse = getSelectedChipChannel();

	/* Chip-expanded collapse from the global modifiers (mirrors main.c
	 * L302-310): KM_SELECT and KM_START collapse a chip when JUST
	 * pressed AND KM_EDIT is NOT held. */
	if(chForCollapse >= 0 && isChipExpanded(chForCollapse)
			&& !isKeyHeld(appState->inputState, KM_EDIT)) {
		if(isKeyJustPressed(appState->inputState, KM_SELECT)) {
			handleExpandedChipInput(chForCollapse, KM_SELECT, false);
		} else if(isKeyJustPressed(appState->inputState, KM_START)) {
			handleExpandedChipInput(chForCollapse, KM_START, false);
		}
	}

	if(isKeyHeld(appState->inputState, KM_EDIT)) {
		int chipChannel = getSelectedChipChannel();
		if(chipChannel >= 0) {
			if(isChipExpanded(chipChannel)) {
				if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
					handleExpandedChipInput(chipChannel, KM_LEFT, true);
				} else if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
					handleExpandedChipInput(chipChannel, KM_RIGHT, true);
				} else if(isKeyJustPressed(appState->inputState, KM_UP)) {
					handleExpandedChipInput(chipChannel, KM_UP, true);
				} else if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
					handleExpandedChipInput(chipChannel, KM_DOWN, true);
				}
				/* Bare EDIT just-pressed (no arrow) collapses. */
				if(isKeyJustPressed(appState->inputState, KM_EDIT)
						&& !isKeyJustPressed(appState->inputState, KM_LEFT)
						&& !isKeyJustPressed(appState->inputState, KM_RIGHT)
						&& !isKeyJustPressed(appState->inputState, KM_UP)
						&& !isKeyJustPressed(appState->inputState, KM_DOWN)) {
					handleExpandedChipInput(chipChannel, KM_EDIT, false);
				}
			} else {
				/* Task 4: collapsed chip — EDIT+LEFT/RIGHT jumps to the
				 * instrument page (mirrors main.c L361-365). EDIT+UP
				 * toggles the chip's expanded flag. */
				if(isKeyJustPressed(appState->inputState, KM_LEFT) || isKeyJustPressed(appState->inputState, KM_RIGHT)) {
					appState->selectedArrangerCell[0] = chipChannel;
					appState->selectedArrangerCell[1] = data->arranger->selected_y;
					appState->currentScene = SCENE_INSTRUMENT;
				}
				if(isKeyJustPressed(appState->inputState, KM_UP)) {
					expandChip(chipChannel, !isChipExpanded(chipChannel));
				}
			}
		} else {
			/* No chip selected — EDIT+arrows go through the arranger
			 * control-input handler. */
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				arrangerGraphControlInput(KM_LEFT);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				arrangerGraphControlInput(KM_RIGHT);
			}
			if(isKeyJustPressed(appState->inputState, KM_UP)) {
				arrangerGraphControlInput(KM_UP);
			}
			if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
				arrangerGraphControlInput(KM_DOWN);
			}
		}
	} else {
		/* Bare arrow: if a chip IS expanded, it owns horizontal nav
		 * (swatch focus). Otherwise plain graph navigation. */
		int bareCh = getSelectedChipChannel();
		bool chipExpanded = (bareCh >= 0) && isChipExpanded(bareCh);
		if(chipExpanded) {
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				handleExpandedChipInput(bareCh, KM_LEFT, false);
			} else if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				handleExpandedChipInput(bareCh, KM_RIGHT, false);
			}
		} else {
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				navigateArrangerGraph(KM_LEFT);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				navigateArrangerGraph(KM_RIGHT);
			}
		}
		if(isKeyJustPressed(appState->inputState, KM_UP)) {
			navigateArrangerGraph(KM_UP);
		}
		if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
			navigateArrangerGraph(KM_DOWN);
		}
	}
}

static void handleInstrumentInput(paTestData *data, ApplicationState *appState) {
	/* Task 7: scene dispatch. Chip/meta row lives in SCENE_ARRANGER;
	 * the instrument page lives in SCENE_INSTRUMENT. */
	if(appState->currentScene == SCENE_ARRANGER) {
		handleArrangerInput(data, appState);
		return;
	}

	if(isKeyJustPressed(appState->inputState, KM_ADD)) {
		Instrument *inst = getSelectedInstInstrument();
		if(inst && inst->voiceType == VOICE_TYPE_FM) {
			addRuntimeSource(inst);
		}
	}
	if(isKeyJustPressed(appState->inputState, KM_REMOVE)) {
		/* Task 4: the mod-wrap now has a header row at index 0, so
		 * removeSelectedSource's `idx - 1` mapping is correct. */
		removeSelectedSource();
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
			bool firedCallback = false;
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				currentGraph->selected->callback(currentGraph->selected->p, -0.1f);
				firedCallback = true;
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				currentGraph->selected->callback(currentGraph->selected->p, 0.1f);
				firedCallback = true;
			}
			if(isKeyJustPressed(appState->inputState, KM_UP)) {
				currentGraph->selected->callback(currentGraph->selected->p, 2.0f);
				firedCallback = true;
			}
			if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
				currentGraph->selected->callback(currentGraph->selected->p, -2.0f);
				firedCallback = true;
			}
			/* Task 6: dirty tracking. Same semantics as main.c —
			 * a callback that just fired means the live state moved
			 * away from the last loaded/saved snapshot. The flag is
			 * sticky once set; markPresetLoaded is the only thing
			 * that clears it. */
			if(firedCallback) {
				Instrument *editInst = getSelectedInstInstrument();
				if(editInst) {
					editInst->loaded.dirty = true;
				}
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
	RenderTexture2D gfx = createPresentTarget();
	while(!WindowShouldClose()) {
		updateInputState(appState->inputState);
		BeginTextureMode(gfx);
		clearBg();
		handleInstrumentInput(data, appState);
		DrawGUI(appState->currentScene);
		EndTextureMode();
		presentFrame(gfx);
	}
	UnloadRenderTexture(gfx);
	CloseWindow();
}

static void runScripted(paTestData *data, ApplicationState *appState) {
	RenderTexture2D gfx = createPresentTarget();
	/* Task 7: cache the live paTestData pointer so the scene/chip/voice
	 * assert helpers can read arranger->label[CH], arranger->...
	 * and the currently selected Instrument without threading
	 * `data` through every helper signature. */
	s_scriptData = data;
	s_scriptAppState = appState;
	while(!WindowShouldClose() && g_scriptState == SCRIPT_RUNNING) {
		ScriptStep *s = &g_script.steps[g_scriptStepIdx];

		/* In scripted mode we DO NOT call updateInputState(): raylib
		 * sees no keyboard under Xvfb, so updateInputState would just
		 * overwrite our injection with zero. Instead we drive the
		 * InputState directly each frame. */
		clearInjectedKeys(appState->inputState);
		applyScriptEventInjection(appState->inputState, s, g_scriptSubframe);

		BeginTextureMode(gfx);
		clearBg();
		handleInstrumentInput(data, appState);
		DrawGUI(appState->currentScene);
		EndTextureMode();
		presentFrame(gfx);

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
	UnloadRenderTexture(gfx);
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

	paTestData data = { 0 };
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
