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
#include "gui_inst_internal.h"
#include "gui_mod_screen.h"
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

/* The InputState the helpers drive. It has to be the *same* object the tests
 * pass to handlePresetUiInput: an earlier version of this file drove a
 * file-static g_input while calling handlePresetUiInput(e.is, ...), so every
 * input assertion silently ran with no keys pressed and passed vacuously. */
static InputState *g_is = NULL;

static void input_reset(void) {
	if(g_is) {
		memset(g_is->keys, 0, sizeof(g_is->keys));
		g_is->historyIndex = 0;
	}
}

/* Press `km` for exactly one frame: isPressed with wasPressed clear, which is
 * exactly what isKeyJustPressed() tests for. */
static void tap(KeyMapping km) {
	if(!g_is) {
		return;
	}
	g_is->keys[km].isPressed = true;
	g_is->keys[km].wasPressed = false;
}

/* Hold `km`: pressed now and already pressed last frame, i.e. isKeyHeld. */
static void hold(KeyMapping km) {
	if(!g_is) {
		return;
	}
	g_is->keys[km].isPressed = true;
	g_is->keys[km].wasPressed = true;
}

/* End the frame: everything becomes "was", so a tap is consumed exactly once
 * and the next tap on the same key registers as a new edge. */
static void end_frame(void) {
	if(!g_is) {
		return;
	}
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		g_is->keys[i].wasPressed = g_is->keys[i].isPressed;
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
#define TEST_PRESET_NAME "test-preset"

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
	loadSample(e->sp, "sine", buf, 24, SPECTRAX_ENGINE_RATE, TEST_SAMPLE);
	free(buf);

	e->wtp = createWavetablePool();
	ASSERT_TRUE(e->wtp != NULL, "createWavetablePool");

	initPresetBank(&e->pb);
	Preset p;
	initDefaultFmPreset(&p);
	/* Give it a name: initDefaultFmPreset leaves it empty, and the
	 * overwrite-confirm path only triggers for a name the bank already
	 * holds. The test asserts PRESET_EXISTS from guiSavePreset. */
	snprintf(p.name, sizeof(p.name), "%s", TEST_PRESET_NAME);
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
	/* Publish it so the tap/hold helpers drive the same object the tests
	 * hand to handlePresetUiInput. */
	g_is = e->is;
	input_reset();

	/* No window needed. createInstGraph does reach createModStripGuiNode, but
	 * initModStrip is now pure bookkeeping -- the ping/pong render textures
	 * are allocated lazily by ensureModStripLoaded() on first use -- so graph
	 * construction is pure data again and this test runs headless like every
	 * other GUI module test (TESTING.md I41). */
	e->selected = 0;
	createInstrumentGui(e->vm, &e->selected, 0);
	ASSERT_TRUE(getInstrumentGui() != NULL, "createInstrumentGui");
	return 0;
}

static void free_env(Env *e) {
	g_is = NULL;
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

/* Make the instrument genuinely dirty.
 *
 * Setting loaded.dirty alone does nothing: isInstrumentDirty() returns false
 * whenever loaded.name is empty, on the reasonable grounds that an instrument
 * with no on-disk baseline has no unsaved edits to lose. Getting this wrong
 * makes every dirty-path assertion pass vacuously, which it did on the first
 * attempt (TESTING.md I45). */
static void make_dirty(Instrument *i, const char *name) {
	if(!name || !name[0]) {
		fprintf(stderr, "FAIL: make_dirty needs a non-empty name\n");
		exit(1);
	}
	memset(i->loaded.name, 0, sizeof(i->loaded.name));
	snprintf(i->loaded.name, sizeof(i->loaded.name), "%s", name);
	i->loaded.dirty = true;
}

static void make_clean(Instrument *i) {
	i->loaded.dirty = false;
}

/* ---- the modal state machines ------------------------------------------ */

/* Find a node by name anywhere under `n`, or NULL. The modal bodies tag their
 * buttons with stable names ("OVERWRITE_YES", "DIRTY_SAVE", "LOADLIST_BACK"...)
 * precisely so they can be identified; using that here means the test drives
 * the same nodes the app does rather than a parallel set of fixtures. */
static GuiNode *find_node_named(GuiNode *n, const char *name) {
	if(!n || !n->name) {
		return NULL;
	}
	if(strcmp(n->name, name) == 0) {
		return n;
	}
	if(!n->items) {
		return NULL;
	}
	ListElement *e = n->items->head;
	for(int i = 0; i < n->itemCount && e; i++) {
		GuiNode *child = *(GuiNode **)e->data;
		GuiNode *hit = find_node_named(child, name);
		if(hit) {
			return hit;
		}
		e = e->next;
	}
	return NULL;
}

/* Is a layer with this name on the stack? The right question when checking that
 * a *specific* dialog closed: DISCARD deliberately proceeds to open the load
 * list behind the dialog, so "no modal is active" is true but misleading. */
static bool layer_named(const char *name) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return false;
	}
	for(int i = 0; i < ig->overlayLayers.count; i++) {
		const char *n = ig->overlayLayers.layers[i].name;
		if(n && strcmp(n, name) == 0) {
			return true;
		}
	}
	return false;
}

/* Find a node by name in the base graph or in ANY overlay layer. Modal bodies
 * (the load list, the confirm dialogs) are layers, not part of the
 * instrument's own graph, so a search that only looks at the base graph misses
 * them -- which is how this test first failed. */
static GuiNode *find_named_anywhere(const char *name) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return NULL;
	}
	Graph *sel_graph = getSelectedInstGraph();
	if(sel_graph) {
		GuiNode *n = find_node_named(sel_graph->root, name);
		if(n) {
			return n;
		}
	}
	for(int i = 0; i < ig->overlayLayers.count; i++) {
		Layer *l = &ig->overlayLayers.layers[i];
		if(!l->graph || !l->graph->root) {
			continue;
		}
		GuiNode *n = find_node_named(l->graph->root, name);
		if(n) {
			return n;
		}
	}
	return NULL;
}

/* Fire a named button's action callback, as the input layer would. */
static int fire_named_button(const char *name) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return 1;
	}
	for(int i = 0; i < ig->overlayLayers.count; i++) {
		GuiNode *n = find_node_named(ig->overlayLayers.layers[i].graph->root, name);
		if(n && n->actionCb) {
			n->actionCb(n->actionCtx);
			return 0;
		}
	}
	return 1;
}

static int test_overwrite_modal(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst(&e);
	InputState *is = e.is;

	/* Saving under a name already in the bank must report PRESET_EXISTS and
	 * open the confirm modal rather than silently clobbering the slot. */
	/* initDefaultFmPreset leaves the preset's name empty, and an empty
	 * loaded.name makes isInstrumentDirty() report false by design -- so this
	 * needs a real name or every dirty assertion below is vacuous. */
	const char *existing = e.pb.patches[0].name;
	PresetFileResult r = guiSavePreset(i, existing);
	ASSERT_TRUE(r == PRESET_EXISTS, "saving over an existing name returns PRESET_EXISTS");
	ASSERT_TRUE(instrumentLayerModalActive(), "overwrite modal counts as a modal");
	/* NO cancels outright, so each button gets its own freshly opened
	 * modal -- firing them back to back was the test's bug, not the
	 * code's: the first press pops the layer the second one lives in. */
	ASSERT_EQ(fire_named_button("OVERWRITE_NO"), 0);
	ASSERT_TRUE(!instrumentLayerModalActive(), "OVERWRITE_NO dismisses the modal");
	ASSERT_TRUE(guiSavePreset(i, existing) == PRESET_EXISTS, "reopen");
	ASSERT_EQ(fire_named_button("OVERWRITE_YES"), 0);
	ASSERT_TRUE(!instrumentLayerModalActive(), "OVERWRITE_YES dismisses the modal");

	/* LEFT/RIGHT toggle the choice; any other key is swallowed rather
	 * than reaching the scene underneath.
	 *
	 * Each key needs its own end_frame(): isKeyJustPressed means "pressed
	 * now, not last frame", so without the settle a second tap would still
	 * see the *first* key as just-pressed and the handler would toggle on
	 * LEFT forever without ever reaching the key under test. */
	for(int step = 0; step < 2; step++) {
		r = guiSavePreset(i, existing);
		ASSERT_TRUE(r == PRESET_EXISTS, "reopening the modal");

		tap(KM_LEFT);
		ASSERT_TRUE(handlePresetUiInput(is, i), "the modal consumes LEFT");
		end_frame();
		input_reset();

		tap(KM_RIGHT);
		ASSERT_TRUE(handlePresetUiInput(is, i), "the modal consumes RIGHT");
		end_frame();
		input_reset();

		tap(KM_RIGHT);
		ASSERT_TRUE(handlePresetUiInput(is, i), "the modal consumes RIGHT again");
		end_frame();
		input_reset();

		/* A key the modal does not use must still be consumed -- otherwise a
		 * stray press behind the modal would change an instrument parameter. */
		tap(KM_UP);
		ASSERT_TRUE(handlePresetUiInput(is, i), "an unused key is still swallowed");
		end_frame();
		input_reset();

		/* SELECT dismisses without overwriting (the same as choosing NO). */
		tap(KM_SELECT);
		ASSERT_TRUE(handlePresetUiInput(is, i), "SELECT dismisses the modal");
		ASSERT_TRUE(!instrumentLayerModalActive(),
		            "the modal layer is popped, not just the state flag");
		input_reset();
	}

	/* START on the default (NO) choice closes without overwriting. */
	r = guiSavePreset(i, existing);
	ASSERT_TRUE(r == PRESET_EXISTS, "reopening the modal again");
	input_reset();
	tap(KM_START);
	ASSERT_TRUE(handlePresetUiInput(is, i), "START on NO dismisses the modal");
	end_frame();
	input_reset();
	ASSERT_TRUE(!instrumentLayerModalActive(), "modal is gone after START/NO");

	/* START on YES actually overwrites and clears the dirty bit. */
	make_dirty(i, existing);
	ASSERT_TRUE(isInstrumentDirty(i), "make_dirty actually makes it dirty");
	r = guiSavePreset(i, existing);
	ASSERT_TRUE(r == PRESET_EXISTS, "reopening the modal for the overwrite path");
	input_reset();
	tap(KM_LEFT);      /* -> YES */
	handlePresetUiInput(is, i);
	end_frame();
	input_reset();
	tap(KM_START);
	handlePresetUiInput(is, i);
	end_frame();
	input_reset();
	ASSERT_TRUE(!instrumentLayerModalActive(), "modal is gone after START/YES");
	ASSERT_TRUE(!isInstrumentDirty(i), "a successful overwrite clears the dirty bit");

	/* A NULL instrument or name is rejected rather than dereferenced. */
	ASSERT_TRUE(guiSavePreset(NULL, "x") == PRESET_ERROR_FORMAT, "NULL instrument");
	ASSERT_TRUE(guiSavePreset(i, NULL) == PRESET_ERROR_FORMAT, "NULL name");

	free_env(&e);
	printf("PASS test_overwrite_modal\n");
	return 0;
}

static int test_load_list(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst(&e);
	InputState *is = e.is;

	ASSERT_TRUE(!guiIsLoadListActive(), "list starts closed");

	/* The LOAD control's callback opens it (or, on a dirty instrument, the
	 * dirty-confirm modal instead). A clean instrument must open the list. */
	make_clean(i);
	ASSERT_TRUE(!isInstrumentDirty(i), "a fresh instrument is not dirty");
	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "instrument graph");
	GuiNode *load = find_named_anywhere("LOAD");
	if(load != NULL && load->actionCb) {
		load->actionCb(load->actionCtx);
	}
	if(guiIsLoadListActive()) {
		ASSERT_TRUE(find_named_anywhere("LOADLIST_BACK") != NULL,
		            "the list has a back button");
		/* Navigation inside the list must not crash and must stay in range. */
		for(int k = 0; k < 8; k++) {
			input_reset();
			tap(KM_DOWN);
			handlePresetUiInput(is, i);
			input_reset();
			tap(KM_UP);
			handlePresetUiInput(is, i);
		}
		input_reset();
		tap(KM_SELECT);
		handlePresetUiInput(is, i);
		input_reset();
		tap(KM_START);
		handlePresetUiInput(is, i);
	}

	free_env(&e);
	printf("PASS test_load_list\n");
	return 0;
}

static int test_dirty_confirm(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst(&e);
	InputState *is = e.is;

	/* With edits in flight, the LOAD control must NOT yank the instrument --
	 * it pushes the dirty-confirm modal instead. */
	make_dirty(i, "edited");
	ASSERT_TRUE(isInstrumentDirty(i), "edits in flight set the dirty bit");
	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "instrument graph");
	GuiNode *load = find_named_anywhere("LOAD");
	if(load != NULL && load->actionCb) {
		load->actionCb(load->actionCtx);
	}
	/* Hard assertions, not `if (modal) { ... }`: a conditional block silently
	 * skips when the dialog fails to open, so the test goes green having tested
	 * nothing. That is exactly how this case passed vacuously once. */
	ASSERT_TRUE(layer_named("DIRTY_CONFIRM"),
	            "a dirty instrument must push the confirm dialog, not open the list");
	ASSERT_TRUE(!guiIsLoadListActive(), "and must not open the load list");
	{
		ASSERT_EQ(fire_named_button("DIRTY_DISCARD"), 0);
		ASSERT_TRUE(!isInstrumentDirty(i), "DISCARD drops the unsaved edits");

		make_dirty(i, "edited");
		load->actionCb(load->actionCtx);
		ASSERT_EQ(fire_named_button("DIRTY_CANCEL"), 0);
		ASSERT_TRUE(isInstrumentDirty(i), "CANCEL keeps the edits");
		ASSERT_TRUE(!layer_named("DIRTY_CONFIRM"), "CANCEL closes the dialog");
		/* DISCARD deliberately proceeds into the load list, so a layer being up
		 * here is correct. What must NOT survive is the legacy overwrite
		 * handler: with the dialog gone and g_modalState still set, it would
		 * swallow every keypress with nothing on screen, and START would
		 * overwrite the preset. Close the load list, then prove the screen is
		 * genuinely idle. */
		ASSERT_EQ(fire_named_button("LOADLIST_BACK"), 0);
		ASSERT_TRUE(!instrumentLayerModalActive(),
		            "no modal left swallowing input after CANCEL");
	}

	free_env(&e);
	printf("PASS test_dirty_confirm\n");
	return 0;
}

/* The sampler's character panel: the rows that bind the character
 * chain's parameters. They are the controls the landed DSP deserves --
 * without them the ten parameters that define the sampler's character
 * exist and are unreachable. */
static int test_sampler_character_panel(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	/* The panel is the SAMPLE instrument's. */
	ASSERT_TRUE(setInstrumentVoiceType(e.vm, 0, VOICE_TYPE_SAMPLE),
	            "the instrument becomes a SAMPLE");
	rebuildInstrumentGraph();

	Instrument *i = e.vm->instruments[0];
	ASSERT_TRUE(i != NULL, "the instrument exists");
	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "the instrument screen has a graph");

	/* R_3: the bake's shape. R_4: the filter and the tuning. */
	static const char *names[] = {
		"PROFILE", "LAW", "INTERP", "BITS", "RATE",
		"FILTER", "CUTOFF", "RES", "TUNE", "ROOT"
	};
	GuiNode *dials[10];
	for(int k = 0; k < 10; k++) {
		dials[k] = find_node_named(g->root, names[k]);
		ASSERT_TRUE(dials[k] != NULL, "the panel carries the control");
	}

	/* PROFILE, LAW, INTERP and FILTER are DISCRETE dials: they pick
	 * from a fixed set, so they draw as steppers. */
	GuiNode *discrete[] = { dials[0], dials[1], dials[2], dials[5] };
	for(int k = 0; k < 4; k++) {
		ASSERT_TRUE(discrete[k]->draw == drawDiscreteDialGuiNode,
		            "the profile, law, interpolation and filter dials are discrete");
	}

	/* The dials are bound to the chain's own parameters, so turning them
	 * changes the character and not a copy of it. */
	ASSERT_TRUE(dials[0]->p == i->id.sampler.profile, "PROFILE binds the profile parameter");
	ASSERT_TRUE(dials[1]->p == i->id.sampler.law, "LAW binds the law parameter");
	ASSERT_TRUE(dials[2]->p == i->id.sampler.interp, "INTERP binds the interpolation parameter");
	ASSERT_TRUE(dials[3]->p == i->id.sampler.bitDepth, "BITS binds the bit depth parameter");
	ASSERT_TRUE(dials[4]->p == i->id.sampler.sampleRate, "RATE binds the sample rate parameter");
	ASSERT_TRUE(dials[5]->p == i->id.sampler.filter, "FILTER binds the filter parameter");
	ASSERT_TRUE(dials[6]->p == i->id.sampler.cutoff, "CUTOFF binds the cutoff parameter");
	ASSERT_TRUE(dials[7]->p == i->id.sampler.res, "RES binds the resonance parameter");
	ASSERT_TRUE(dials[8]->p == i->id.sampler.tune, "TUNE binds the tuning parameter");
	ASSERT_TRUE(dials[9]->p == i->id.sampler.root, "ROOT binds the root parameter");

	/* And the panel's rows are laid out in the order the design gives:
	 * R_1, R_2, R_3 (the character), R_4 (the filter and tuning), R_5
	 * (the waveform). */
	GuiNode *r3 = find_node_named(g->root, "R_3");
	GuiNode *r4 = find_node_named(g->root, "R_4");
	GuiNode *r5 = find_node_named(g->root, "R_5");
	ASSERT_TRUE(r3 != NULL, "the panel has a character row");
	ASSERT_TRUE(r4 != NULL, "the panel has a filter row");
	ASSERT_TRUE(r5 != NULL, "the panel has the waveform row");
	ASSERT_TRUE(r3->y < r4->y, "the character row is above the filter row");
	ASSERT_TRUE(r4->y < r5->y, "the filter row is above the waveform");

	free_env(&e);
	printf("PASS test_sampler_character_panel\n");
	return 0;
}

/* Task 8: the waveform's points. START and END are BUTTONS that carry
 * the point's value, and the waveform holds the points model. */
static int test_waveform_points(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	ASSERT_TRUE(setInstrumentVoiceType(e.vm, 0, VOICE_TYPE_SAMPLE),
	            "the instrument becomes a SAMPLE");
	rebuildInstrumentGraph();

	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "the instrument screen has a graph");

	/* START and END are buttons now, not dials: they carry the point's
	 * value, because the point is what FUNCTION acts on. */
	GuiNode *start = find_node_named(g->root, "START");
	GuiNode *end = find_node_named(g->root, "END");
	ASSERT_TRUE(start != NULL, "the panel carries a START button");
	ASSERT_TRUE(end != NULL, "the panel carries an END button");
	ASSERT_TRUE(start->actionCb != NULL, "the START button acts");
	ASSERT_TRUE(end->actionCb != NULL, "the END button acts");
	ASSERT_TRUE(start->text && strstr(start->text, "START") != NULL,
	            "the START button shows its name");
	ASSERT_TRUE(start->text && strchr(start->text, '.') != NULL,
	            "the START button shows the point's value");

	/* The waveform holds the points model. */
	GuiNode *wfr = find_node_named(g->root, "WFRM");
	ASSERT_TRUE(wfr != NULL, "the panel carries the waveform");
	SampleWaveformGuiNode *sw = (SampleWaveformGuiNode *)wfr;
	ASSERT_TRUE(sw->points.length > 0, "the points know the sample's length");
	ASSERT_TRUE(sw->points.cursor.exists, "the cursor exists");
	ASSERT_TRUE(sw->points.cursor.selected, "the cursor is the selected point");

	/* The END button selects its point: the point is created at the
	 * cursor, and the cursor jumps to it. */
	end->actionCb(end->actionCtx);
	ASSERT_TRUE(sw->points.loopEnd.exists, "the loop end now exists");
	ASSERT_TRUE(sw->points.loopEnd.selected, "the loop end is the selected point");

	free_env(&e);
	printf("PASS test_waveform_points\n");
	return 0;
}

/* The voice types that cannot make a sound are not offered: GRAIN and
 * SPECTRAL cannot today, so the type selector skips them. */
static int test_voice_type_gating(void) {
	ASSERT_TRUE(voiceTypeIsAvailable(VOICE_TYPE_FM), "FM can make a sound");
	ASSERT_TRUE(voiceTypeIsAvailable(VOICE_TYPE_SAMPLE), "SAMPLE can make a sound");
	ASSERT_TRUE(voiceTypeIsAvailable(VOICE_TYPE_BLEP), "BLEP can make a sound");
	ASSERT_TRUE(!voiceTypeIsAvailable(VOICE_TYPE_GRAIN), "GRAIN cannot");
	ASSERT_TRUE(!voiceTypeIsAvailable(VOICE_TYPE_SPECTRAL), "SPECTRAL cannot");

	/* Cycling the type never lands on an unavailable one, from every
	 * type that is available. */
	VoiceType available[] = { VOICE_TYPE_FM, VOICE_TYPE_SAMPLE, VOICE_TYPE_BLEP };
	for(unsigned i = 0; i < 3; i++) {
		VoiceType t = available[i];
		for(int step = 0; step < 8; step++) {
			t = nextVoiceType(t);
			ASSERT_TRUE(voiceTypeIsAvailable(t), "the cycle lands on an available type");
			t = prevVoiceType(t);
			ASSERT_TRUE(voiceTypeIsAvailable(t), "the cycle lands on an available type");
		}
	}
	printf("PASS test_voice_type_gating\n");
	return 0;
}

/* The waveform's points REACH the instrument.
 *
 * The points model is the screen's own state; the instrument's loop
 * parameters are the source of truth. Every gesture that moves a point
 * must write them, or the loop the voice plays is not the loop on the
 * screen -- which is what the phase's headline interaction is. */
static int test_points_reach_the_instrument(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	ASSERT_TRUE(setInstrumentVoiceType(e.vm, 0, VOICE_TYPE_SAMPLE),
	            "the instrument becomes a SAMPLE");
	rebuildInstrumentGraph();

	Instrument *i = e.vm->instruments[0];
	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "the instrument screen has a graph");
	GuiNode *wfr = find_node_named(g->root, "WFRM");
	ASSERT_TRUE(wfr != NULL, "the panel carries the waveform");
	GuiNode *start = find_node_named(g->root, "START");
	ASSERT_TRUE(start != NULL, "the panel carries the START button");

	/* Select the loop start: the button's action selects its point. */
	start->actionCb(start->actionCtx);
	SampleWaveformGuiNode *sw = (SampleWaveformGuiNode *)wfr;
	ASSERT_TRUE(sw->points.loopStart.selected, "the loop start is the selected point");

	/* Now move the cursor: the selected point follows it, and the
	 * instrument's loop start parameter must follow too. */
	float before = getParameterValue(i->id.sampler.loopStartIndex);
	ASSERT_TRUE(wfr->customNav != NULL, "the waveform takes the arrows");
	wfr->customNav(wfr, KM_RIGHT);
	wfr->customNav(wfr, KM_RIGHT);
	float after = getParameterValue(i->id.sampler.loopStartIndex);
	ASSERT_TRUE(after != before, "moving the cursor moved the instrument's loop start");
	ASSERT_TRUE(after == (float)sw->points.loopStart.frame,
	            "the parameter holds the point's frame");

	/* The crossfade's ceiling follows the LOOP's length, not the
	 * sample's. */
	ASSERT_TRUE(i->id.sampler.crossfade != NULL, "the instrument has a crossfade");
	ASSERT_TRUE(i->id.sampler.crossfade->maxValue > 0.0f, "the crossfade has a ceiling");

	free_env(&e);
	printf("PASS test_points_reach_the_instrument\n");
	return 0;
}

/* The modulation list is gone from the instrument screen: the modulation
 * screen carries the full interface now, and the instrument screen keeps
 * one line for the connected modulators. */
static int test_mod_list_removed(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}

	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "the instrument screen has a graph");

	/* The list and its header are gone. */
	ASSERT_TRUE(find_node_named(g->root, "mod_wrap") == NULL,
	            "the modulation list is gone from the instrument screen");
	ASSERT_TRUE(find_node_named(g->root, "mods_hdr") == NULL,
	            "the modulation list's header is gone");
	ASSERT_TRUE(find_node_named(g->root, "MODS_ADD") == NULL,
	            "the list's ADD button is gone -- the modulation screen has it");

	/* And the line is what remains. */
	ASSERT_TRUE(find_node_named(g->root, "MODLINE") != NULL,
	            "the modulation line remains");

	free_env(&e);
	printf("PASS test_mod_list_removed\n");
	return 0;
}

/* The modulation line: the instrument screen's one-line slot that cycles
 * through the connected modulators and opens the modulation screen. */
static int test_mod_line(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = e.vm->instruments[0];
	ASSERT_TRUE(i != NULL, "the instrument exists");

	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "the instrument screen has a graph");

	/* The line must be on the screen. It is the instrument screen's only
	 * entry point into the modulation screen, so without it the screen is
	 * unreachable from the app. */
	GuiNode *line = find_node_named(g->root, "MODLINE");
	ASSERT_TRUE(line != NULL, "the instrument screen carries a MODLINE node");
	ASSERT_TRUE(line->selectable, "the line is selectable");
	ASSERT_TRUE(line->actionCb != NULL, "the line fires an action on EDIT");
	ASSERT_TRUE(line->customNav != NULL, "the line handles the arrow keys itself");

	/* The default FM patch already wires its core envelopes, so the line
	 * has something to show from the start. */
	Mod *conn[64];
	int connected = modConnectedSources(i, conn, 64);
	ASSERT_TRUE(connected > 0, "the FM's default patch wires its envelopes");

	/* The line's own state is the index into the connected sources. */
	ModLineGuiNode *ml = (ModLineGuiNode *)line;
	ASSERT_EQ(ml->index, 0);

	/* LEFT and RIGHT cycle the line's index, and the line keeps it. With
	 * more than one connected source RIGHT must advance it. */
	ASSERT_TRUE(connected >= 2, "the FM patch has at least two connected sources");
	line->customNav(line, KM_RIGHT);
	ASSERT_EQ(ml->index, 1);
	line->customNav(line, KM_LEFT);
	ASSERT_EQ(ml->index, 0);
	/* Wrapping: LEFT from the first source lands on the last. */
	line->customNav(line, KM_LEFT);
	ASSERT_EQ(ml->index, connected - 1);
	line->customNav(line, KM_RIGHT);
	ASSERT_EQ(ml->index, 0);

	/* EDIT on the line opens the modulation screen, focused on the source
	 * the line is showing. The screen's graph is built at open time, so the
	 * selection must be set BEFORE the open -- otherwise the detail pane
	 * shows the previous source. */
	ASSERT_TRUE(!modScreenIsOpen(), "the modulation screen starts closed");
	line->actionCb(line->actionCtx);
	ASSERT_TRUE(modScreenIsOpen(), "EDIT on the line opens the modulation screen");
	int shown = modScreenSelectedSource();
	ASSERT_EQ(shown, 0);

	/* The line shows a different source, and the screen must follow it. */
	closeModScreen();
	line->customNav(line, KM_RIGHT);
	line->actionCb(line->actionCtx);
	int shown2 = modScreenSelectedSource();
	ASSERT_TRUE(shown2 > 0, "the screen opens on the second source");
	ASSERT_TRUE(shown2 != shown, "the screen followed the line");

	/* Close it again, so the test leaves no layer up. */
	closeModScreen();
	ASSERT_TRUE(!modScreenIsOpen(), "the screen closes");

	free_env(&e);
	printf("PASS test_mod_line\n");
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
	if(test_overwrite_modal() != 0) {
		return 1;
	}
	if(test_load_list() != 0) {
		return 1;
	}
	if(test_dirty_confirm() != 0) {
		return 1;
	}
	if(test_mod_line() != 0) {
		return 1;
	}
	if(test_voice_type_gating() != 0) {
		return 1;
	}
	if(test_mod_list_removed() != 0) {
		return 1;
	}
	if(test_sampler_character_panel() != 0) {
		return 1;
	}
	if(test_waveform_points() != 0) {
		return 1;
	}
	if(test_points_reach_the_instrument() != 0) {
		return 1;
	}
	printf("PASSED (gui_instrument)\n");
	return 0;
}