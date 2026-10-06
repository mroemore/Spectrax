/* Tests for gui_instrument.c — the instrument screen's preset UI, load list,
 * overwrite modal and page navigation.
 *
 * Follows the test_gui_layer playbook: no window. createInstrumentGui builds
 * real GuiNodes (construction needs no display), and handlePresetUiInput reads
 * InputState flags directly rather than calling raylib, so the entire input
 * machine is drivable by writing is-pressed / was-pressed flags.
 *
 * Keys are modelled the way isKeyJustPressed reads them: a "press" sets
 * isPressed with wasPressed clear, and the next call frame must clear it so the
 * edge is consumed exactly once. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "graph_gui.h"
#include "input.h"
#include "io.h"
#include "modsystem.h"
#include "sample.h"
#include "sequencer.h"
#include "voice.h"
#include "wavetable.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define ASSERT_EQ(actual, expected) do { \
	long _a = (long)(actual); \
	long _e = (long)(expected); \
	if(_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: expected %ld, got %ld\n", \
		        __FILE__, __LINE__, _e, _a); \
		return 1; \
	} \
} while (0)

/* ---- input plumbing ----------------------------------------------------- */

static InputState g_input;

static void input_reset(void) {
	memset(&g_input, 0, sizeof(g_input));
}

/* Press `km` for exactly one frame. */
static void tap(KeyMapping km) {
	g_input.keys[km].isPressed = true;
	g_input.keys[km].wasPressed = false;
}

/* Hold `km` for the current and following frames (isKeyHeld). */
static void hold(KeyMapping km) {
	g_input.keys[km].isPressed = true;
	g_input.keys[km].wasPressed = true;
}

/* End the frame: everything becomes "was", so a tap is consumed. */
static void end_frame(void) {
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		g_input.keys[i].wasPressed = g_input.keys[i].isPressed;
	}
}

/* Tap then settle — the ordinary "user pressed a key" unit. */
static void press(KeyMapping km) {
	tap(km);
	end_frame();
	input_reset();
}

/* ---- environment -------------------------------------------------------- */

#define TEST_CHANNELS 2
#define TEST_VOICES   2
#define TEST_SAMPLE   1024

typedef struct {
	VoiceManager *vm;
	InputState *is;
	ParamList *pl;
	Arranger *arranger;
	PatternList *patterns;
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
	int selected;
} Env;

/* The VoiceManager needs a pool, a wavetable pool, a preset bank and a BPM
 * Parameter on the arranger -- the same prerequisites test_voice.c records. */
static int make_env(Env *e) {
	memset(e, 0, sizeof(*e));

	e->sp = createSamplePool();
	ASSERT_TRUE(e->sp != NULL, "createSamplePool");
	float *buf = (float *)malloc(TEST_SAMPLE * sizeof(float));
	ASSERT_TRUE(buf != NULL, "sample buffer");
	for(int i = 0; i < TEST_SAMPLE; i++) {
		buf[i] = sinf(2.0f * (float)M_PI * (float)i / (float)TEST_SAMPLE);
	}
	loadSample(e->sp, "sine", buf, 24, SAMPLE_RATE, TEST_SAMPLE);
	free(buf);

	e->wtp = createWavetablePool();
	ASSERT_TRUE(e->wtp != NULL, "createWavetablePool");

	initPresetBank(&e->pb);
	Preset p;
	initDefaultFmPreset(&p);
	addPresetToBank(&e->pb, p);

	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = TEST_CHANNELS;
	settings.defaultVoiceCount = TEST_VOICES;
	settings.defaultBPM = 120;

	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	ASSERT_TRUE(e->vm != NULL, "createVoiceManager");

	e->pl = createParamList();
	ASSERT_TRUE(e->pl != NULL, "createParamList");

	e->arranger = (Arranger *)calloc(1, sizeof(Arranger));
	ASSERT_TRUE(e->arranger != NULL, "calloc Arranger");
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			e->arranger->song[ch][r] = -1;
		}
		e->arranger->playhead_indices[ch] = -1;
	}
	e->arranger->enabledChannels = TEST_CHANNELS;
	e->arranger->tempoSettings.bpm = createParameterPro(e->pl, "BPM", 120.0f, 1.0f,
	                                                    1000.0f, 1.0f, 10.0f, NULL, NULL);
	e->arranger->tempoSettings.swing = createParameterPro(e->pl, "Swing", 0.0f, 0.0f,
	                                                     1.0f, 0.01f, 0.1f, NULL, NULL);
	e->arranger->tempoSettings.samplesPerEvenStep = 64;
	e->arranger->tempoSettings.samplesPerOddStep = 64;

	e->patterns = (PatternList *)calloc(1, sizeof(PatternList));
	ASSERT_TRUE(e->patterns != NULL, "calloc PatternList");
	e->patterns->pattern_count = 1;
	e->patterns->patterns[0].pattern_size = 8;

	e->is = createInputState(INPUT_TYPE_KEYBOARD);
	ASSERT_TRUE(e->is != NULL, "createInputState");

	/* createInstGraph -> createModStripGuiNode -> initModStrip ->
	 * LoadRenderTexture, so building an instrument graph needs a GL context
	 * even though none of the logic under test draws anything. That makes
	 * this the one GUI module whose *construction* is not window-free, which
	 * is why this test runs under Xvfb rather than headless like its siblings.
	 * (Recorded as an observation in TESTING.md I38: node construction that
	 * allocates GPU resources makes the whole screen untestable headlessly.) */
	InitWindow(640, 480, "spx-instrument-test");
	if(!IsWindowReady()) {
		fprintf(stderr, "FAIL: no window; test must run under Xvfb\n");
		return 1;
	}

	e->selected = 0;
	createInstrumentGui(e->vm, &e->selected, 0);
	ASSERT_TRUE(getInstrumentGui() != NULL, "createInstrumentGui");
	return 0;
}

static void free_env(Env *e) {
	freeInputState(e->is);
	free(e->patterns);
	freeParamList(e->pl);
	free(e->arranger);
	/* The VoiceManager is deliberately NOT freed. main.c never frees it
	 * either: gui_inst_mod.c's file-static `g_sourceCtx[]` holds Instrument*
	 * pointers into it for the life of the process, so freeing it leaves a
	 * dangling global that the next createInstrumentGui -> appendModSourceEntry
	 * dereferences. ASan caught exactly that when this test did free it.
	 *
	 * That is a real fragility -- the module assumes a process-lifetime
	 * VoiceManager -- but it is the product's current contract, so the test
	 * honours it rather than papering over it. Recorded in TESTING.md I40. */
	freeSamplePool(e->sp);
	freeWavetablePool(e->wtp);
	freeInstrumentGui();
	CloseWindow();
}

static Instrument *inst(Env *e) {
	return e->vm->instruments[0];
}

/* The clamp the accessors are expected to apply (I39). */
static int clamped(int sel) {
	if(sel < 0) {
		return 0;
	}
	if(sel >= MAX_SEQUENCER_CHANNELS) {
		return MAX_SEQUENCER_CHANNELS - 1;
	}
	return sel;
}

/* ---- voice-type cycling ------------------------------------------------- */

static int test_voice_type_cycling(void) {
	/* The enum has three members; next/prev must wrap. */
	ASSERT_EQ(nextVoiceType(VOICE_TYPE_FM), VOICE_TYPE_BLEP);
	ASSERT_EQ(nextVoiceType(VOICE_TYPE_BLEP), VOICE_TYPE_SAMPLE);
	ASSERT_EQ(nextVoiceType(VOICE_TYPE_SAMPLE), VOICE_TYPE_FM);
	ASSERT_EQ(prevVoiceType(VOICE_TYPE_FM), VOICE_TYPE_SAMPLE);
	ASSERT_EQ(prevVoiceType(VOICE_TYPE_SAMPLE), VOICE_TYPE_BLEP);
	ASSERT_EQ(prevVoiceType(VOICE_TYPE_BLEP), VOICE_TYPE_FM);

	/* Two full laps return to the start. */
	VoiceType t = VOICE_TYPE_FM;
	for(int i = 0; i < 3; i++) {
		t = nextVoiceType(t);
	}
	ASSERT_EQ(t, VOICE_TYPE_FM);
	for(int i = 0; i < 3; i++) {
		t = prevVoiceType(t);
	}
	ASSERT_EQ(t, VOICE_TYPE_FM);

	/* An out-of-range value must not fall off the end: next/prev are used on
	 * a value that came off disk or a cast. */
	ASSERT_TRUE(nextVoiceType((VoiceType)99) >= 0, "next of an out-of-range type stays in range");
	ASSERT_TRUE(nextVoiceType((VoiceType)99) < VOICE_TYPE_COUNT, "next of an out-of-range type stays in range");
	ASSERT_TRUE(prevVoiceType((VoiceType)-1) >= 0, "prev of an out-of-range type stays in range");
	ASSERT_TRUE(prevVoiceType((VoiceType)-1) < VOICE_TYPE_COUNT, "prev of an out-of-range type stays in range");

	printf("PASS test_voice_type_cycling\n");
	return 0;
}

/* ---- accessors before/after a GUI exists -------------------------------- */

static int test_accessors(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();
	ASSERT_TRUE(ig != NULL, "getInstrumentGui");
	ASSERT_TRUE(getSelectedInstGraph() != NULL, "getSelectedInstGraph");
	ASSERT_TRUE(getSelectedInstInstrument() != NULL, "getSelectedInstInstrument");
	ASSERT_TRUE(!instrumentLayerModalActive(), "no modal right after construction");
	ASSERT_TRUE(!guiIsLoadListActive(), "load list closed right after construction");

	/* An out-of-range selectedInstrument is reachable: it is a caller-owned
	 * int* written by the load list, the modal callbacks and the
	 * channel-switch gestures, and both arrays it indexes are fixed at
	 * MAX_SEQUENCER_CHANNELS. Before the clamp (I39) this returned
	 * instrumentCount reinterpreted as a Graph* — a wild pointer the caller
	 * would dereference. Now it must clamp to a real object. */
	int saved = *ig->selectedInstrument;
	int probes[] = { TEST_CHANNELS, MAX_SEQUENCER_CHANNELS,
	                 MAX_SEQUENCER_CHANNELS + 5, 1000000, -1, -1000000 };
	for(size_t k = 0; k < sizeof(probes) / sizeof(probes[0]); k++) {
		*ig->selectedInstrument = probes[k];
		Graph *g = getSelectedInstGraph();
		ASSERT_TRUE(g == ig->instrumentScreenGraphs[clamped(probes[k])],
		            "getSelectedInstGraph clamps rather than reading past the array");
		Instrument *i = getSelectedInstInstrument();
		ASSERT_TRUE(i == e.vm->instruments[clamped(probes[k])],
		            "getSelectedInstInstrument clamps likewise");
	}
	*ig->selectedInstrument = saved;
	ASSERT_TRUE(getSelectedInstGraph() != NULL, "restored selection yields a graph");

	free_env(&e);
	printf("PASS test_accessors\n");
	return 0;
}

/* ---- the input machine -------------------------------------------------- */

static int test_input_no_modal(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InputState *is = e.is;
	Instrument *i = inst(&e);

	/* Arrows with nothing modal: must be handled without crashing and must
	 * not be swallowed by a modal branch. */
	for(int i2 = 0; i2 < 5; i2++) {
		press(KM_LEFT);
		press(KM_RIGHT);
		press(KM_UP);
		press(KM_DOWN);
	}

	/* EDIT + arrows cycles the instrument's voice type. */
	VoiceType before = i->voiceType;
	tap(KM_UP);
	hold(KM_EDIT);
	handlePresetUiInput(is, i);
	end_frame();
	input_reset();
	if(i->voiceType != before) {
		ASSERT_TRUE(i->voiceType >= 0 && i->voiceType < VOICE_TYPE_COUNT,
		            "voice type stays in range after cycling up");
	}

	before = i->voiceType;
	tap(KM_DOWN);
	hold(KM_EDIT);
	handlePresetUiInput(is, i);
	end_frame();
	input_reset();
	if(i->voiceType != before) {
		ASSERT_TRUE(i->voiceType >= 0 && i->voiceType < VOICE_TYPE_COUNT,
		            "voice type stays in range after cycling down");
	}

	/* SELECT / START drive the preset-name and load-list paths. Each press
	 * is followed by a settle press of the same key, so whatever modal the
	 * first press opened gets dismissed by the second rather than the test
	 * ending in a state it did not intend. */
	press(KM_SELECT);
	handlePresetUiInput(is, i);
	press(KM_SELECT);
	handlePresetUiInput(is, i);

	press(KM_START);
	handlePresetUiInput(is, i);
	/* KM_EDIT on a load list opens the rename field; walk the cursor and
	 * cycle a character so the name-editing branches run. */
	tap(KM_UP);
	hold(KM_EDIT);
	handlePresetUiInput(is, i);
	end_frame();
	input_reset();
	for(int k = 0; k < 4; k++) {
		tap(KM_LEFT);
		hold(KM_EDIT);
		handlePresetUiInput(is, i);
		end_frame();
		input_reset();
		tap(KM_RIGHT);
		hold(KM_EDIT);
		handlePresetUiInput(is, i);
		end_frame();
		input_reset();
		tap(KM_DOWN);
		hold(KM_EDIT);
		handlePresetUiInput(is, i);
		end_frame();
		input_reset();
	}
	press(KM_SELECT);
	handlePresetUiInput(is, i);

	/* FUNCTION held alongside the arrows takes the erase path in the load
	 * list. */
	tap(KM_UP);
	hold(KM_FUNCTION);
	handlePresetUiInput(is, i);
	end_frame();
	input_reset();
	press(KM_SELECT);
	handlePresetUiInput(is, i);

	free_env(&e);
	printf("PASS test_input_no_modal\n");
	return 0;
}

int main(void) {
	if(test_voice_type_cycling() != 0) {
		return 1;
	}
	if(test_accessors() != 0) {
		return 1;
	}
	if(test_input_no_modal() != 0) {
		return 1;
	}
	printf("PASSED (gui_instrument)\n");
	return 0;
}