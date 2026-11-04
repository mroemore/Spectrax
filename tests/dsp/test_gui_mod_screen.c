/* Tests for gui_mod_screen.c — the modulation screen's graph.
 *
 * The screen is a graph: the source list on the left, the selected
 * source's detail on the right. This suite builds the graph for an FM
 * instrument and asserts its shape — the list's rows, the detail pane,
 * the route list — plus the screen's selected source.
 *
 * Headless: the graph is built without drawing and no build-path
 * callback touches a GL context, so no window is needed.
 *
 * The fixture is the voice manager's channel-0 instrument: the voice
 * manager builds every channel with init_instrument and then applies
 * the bank's patch, so channel 0 is the default FM patch — the core
 * envelopes, the LFO and the four grouped pattern tracks — plus one
 * runtime source added the way the instrument screen's ADD button adds
 * it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "gui_inst_internal.h"
#include "gui_layer.h"
#include "gui_mod_screen.h"
#include "graph_gui.h"
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

#define ASSERT_EQ(actual, expected, ...) do { \
	long _a = (long)(actual); \
	long _e = (long)(expected); \
	if(_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: expected %ld, got %ld: ", \
		        __FILE__, __LINE__, _e, _a); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
		return 1; \
	} \
} while (0)

#define TEST_CHANNELS 1
#define TEST_VOICES   1
#define TEST_SAMPLE   1024
#define TEST_PRESET   "inst-mod-screen-test"

/* The screen's rows are 40px tall, like the instrument screen's
 * mod-source rows. */
#define MOD_ROW_H 40

typedef struct {
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
	VoiceManager *vm;
	Instrument *inst;
	InputState *is;
	int selected;
} Env;

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
	snprintf(p.name, sizeof(p.name), "%s", TEST_PRESET);
	addPresetToBank(&e->pb, p);

	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = TEST_CHANNELS;
	settings.defaultVoiceCount = TEST_VOICES;
	settings.defaultBPM = 120;

	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	ASSERT_TRUE(e->vm != NULL, "createVoiceManager");
	e->inst = e->vm->instruments[0];
	ASSERT_TRUE(e->inst != NULL, "the channel's instrument");

	e->is = createInputState(INPUT_TYPE_KEYBOARD);
	ASSERT_TRUE(e->is != NULL, "createInputState");

	/* The screen is a layer on the instrument GUI's overlay stack, so
	 * the GUI has to exist for the layer tests. The instrument GUI's
	 * graphs are built from the same voice manager, and the mod-strip
	 * render textures are allocated lazily, so this is headless. */
	e->selected = 0;
	createInstrumentGui(e->vm, &e->selected, 0);
	ASSERT_TRUE(getInstrumentGui() != NULL, "createInstrumentGui");
	return 0;
}

static void free_env(Env *e) {
	if(!e) {
		return;
	}
	freeInputState(e->is);
	freeInstrumentGui();
	freeVoiceManager(e->vm);
	freeSamplePool(e->sp);
	freeWavetablePool(e->wtp);
	/* The VoiceManager does not own the instruments (freeInstrument is
	 * static in voice.c), so the channel's instrument would leak. It is
	 * the app's own process-lifetime state; the leak backlog is
	 * recorded in TESTING.md I19. */
}

/* The source at a source position (0-based over the non-attenuator
 * mods), or NULL. */
static Mod *sourceAt(Instrument *inst, int srcPos) {
	if(!inst || !inst->modList) {
		return NULL;
	}
	int mi = modIndexAt(inst->modList, srcPos);
	return (mi >= 0) ? inst->modList->mods[mi] : NULL;
}

/* Walk the tree for a node by name. The production one in
 * gui_inst_sample.c is file-static, so the test carries its own. */
static GuiNode *findGuiNodeNamed(GuiNode *n, const char *name) {
	if(!n) {
		return NULL;
	}
	if(n->name && strcmp(n->name, name) == 0) {
		return n;
	}
	ListElement *le = n->items ? n->items->head : NULL;
	for(; le; le = le->next) {
		GuiNode *child = *(GuiNode **)le->data;
		GuiNode *match = findGuiNodeNamed(child, name);
		if(match) {
			return match;
		}
	}
	return NULL;
}

/* A container's direct child with the given name. The list's rows and
 * the route list's rows are the container's direct children, so this is
 * what says "the list HOLDS the row". */
static GuiNode *findChildNamed(GuiNode *parent, const char *name) {
	if(!parent || !parent->items) {
		return NULL;
	}
	ListElement *le = parent->items->head;
	for(int i = 0; i < parent->itemCount && le; i++, le = le->next) {
		GuiNode *child = *(GuiNode **)le->data;
		if(child && child->name && strcmp(child->name, name) == 0) {
			return child;
		}
	}
	return NULL;
}

/* How many of the container's direct children start with `prefix`. */
static int countChildrenWithPrefix(GuiNode *parent, const char *prefix) {
	if(!parent || !parent->items || !prefix) {
		return 0;
	}
	size_t len = strlen(prefix);
	int n = 0;
	ListElement *le = parent->items->head;
	for(int i = 0; i < parent->itemCount && le; i++, le = le->next) {
		GuiNode *child = *(GuiNode **)le->data;
		if(child && child->name && strncmp(child->name, prefix, len) == 0) {
			n++;
		}
	}
	return n;
}

/* ---- the graph's shape ------------------------------------------------ */

static int test_graph_shape(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;
	/* One runtime source, the way the instrument screen's ADD button
	 * adds it. */
	addRuntimeSource(inst);
	int count = modSourceCount(inst->modList);
	ASSERT_TRUE(count > 0, "the instrument has sources");

	/* The screen's selection is screen state that outlives a rebuild:
	 * start from "nothing selected" so the build picks the first
	 * source. */
	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	ASSERT_TRUE(g->root != NULL, "the graph has a root");

	GuiNode *list = findGuiNodeNamed(g->root, "MODLIST");
	ASSERT_TRUE(list != NULL, "the source list exists");
	GuiNode *detail = findGuiNodeNamed(g->root, "MODDETAIL");
	ASSERT_TRUE(detail != NULL, "the detail pane exists");
	GuiNode *routes = findChildNamed(detail, "MODROUTES");
	ASSERT_TRUE(routes != NULL, "the detail pane holds the route list");
	GuiNode *header = findGuiNodeNamed(g->root, "MODHEADER");
	ASSERT_TRUE(header != NULL, "the header exists");
	ASSERT_TRUE(findChildNamed(header, "MODBACK") != NULL, "the header holds the back button");

	/* One row per source, plus the grouped pattern row. */
	int expectedRows = 0;
	for(int i = 0; i < count; i++) {
		Mod *m = sourceAt(inst, i);
		if(!m || m->type == MT_PATTERN) {
			continue;
		}
		expectedRows++;
		char rowName[32];
		snprintf(rowName, sizeof(rowName), "MODSRC_%d", i);
		ASSERT_TRUE(findChildNamed(list, rowName) != NULL,
		            "the list holds a row for the source");
	}
	ASSERT_TRUE(findChildNamed(list, "MODSRC_PTN") != NULL,
	            "the list holds the grouped pattern row");
	ASSERT_EQ(countChildrenWithPrefix(list, "MODSRC_"), expectedRows + 1,
	          "one row per source plus the pattern row");

	/* The screen opens on the first source. */
	ASSERT_EQ(modScreenSelectedSource(), 0, "the screen selects the first source");

	freeGuiNode(g->root);
	free(g);
	free_env(&e);
	printf("PASS test_graph_shape (%d sources, %d rows)\n", count, expectedRows + 1);
	return 0;
}

/* ---- the route list ---------------------------------------------------- */

static int test_route_rows(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;
	Mod *src = sourceAt(inst, 0);
	ASSERT_TRUE(src != NULL, "the first source exists");
	/* The FM voice seeds a route from its first envelope into the
	 * voice's gain driver. Drop it so the source starts unconnected —
	 * the screen's empty route list is the state under test. */
	ASSERT_TRUE(removeModulation(inst->paramList, inst->modList, inst->gain, src),
	          "the seeded gain route is removed");

	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	GuiNode *detail = findGuiNodeNamed(g->root, "MODDETAIL");
	ASSERT_TRUE(detail != NULL, "the detail pane exists");
	GuiNode *routes = findChildNamed(detail, "MODROUTES");
	ASSERT_TRUE(routes != NULL, "the detail pane holds the route list");
	ASSERT_EQ(countChildrenWithPrefix(routes, "MODROUTE_"), 0,
	          "an unconnected source has no route rows");

	/* Route the source into a destination: the route list grows a row
	 * for it, showing the destination's name. */
	ASSERT_TRUE(addModulation(inst->paramList, inst->modList, src, inst->pitch, 1.0f, MO_ADD),
	          "the source is routed into the pitch driver");
	freeGuiNode(g->root);
	free(g);

	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	detail = findGuiNodeNamed(g->root, "MODDETAIL");
	routes = findChildNamed(detail, "MODROUTES");
	ASSERT_TRUE(routes != NULL, "the route list exists");
	ASSERT_EQ(countChildrenWithPrefix(routes, "MODROUTE_"), 1,
	          "the route list holds one row for the route");
	GuiNode *row = findChildNamed(routes, "MODROUTE_0");
	ASSERT_TRUE(row != NULL, "the route row is named MODROUTE_0");
	ASSERT_TRUE(row->text && strcmp(row->text, "pitch") == 0,
	            "the route row shows the destination's name");
	ASSERT_TRUE(row->sublabel && row->sublabel[0] != '\0',
	            "the route row shows the route's amount");

	freeGuiNode(g->root);
	free(g);
	free_env(&e);
	printf("PASS test_route_rows\n");
	return 0;
}

/* ---- the selected source ----------------------------------------------- */

static int test_selection_state(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;

	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	/* The setter validates an index against the screen's instrument,
	 * which the build is what tells it: build first, then select. */
	modScreenSetSelectedSource(2);
	ASSERT_EQ(modScreenSelectedSource(), 2, "the selection is settable");
	freeGuiNode(g->root);
	free(g);

	/* A rebuild keeps the selection. */
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	ASSERT_EQ(modScreenSelectedSource(), 2, "a rebuild keeps the selection");
	/* The selected source's own dials are in the detail pane: an
	 * envelope's ATK/DEC/CRV stages. */
	GuiNode *detail = findGuiNodeNamed(g->root, "MODDETAIL");
	ASSERT_TRUE(detail != NULL, "the detail pane exists");
	ASSERT_TRUE(findGuiNodeNamed(detail, "ATK") != NULL,
	            "the detail pane holds the envelope's ATK dial");
	ASSERT_TRUE(findGuiNodeNamed(detail, "DEC") != NULL,
	            "the detail pane holds the envelope's DEC dial");
	freeGuiNode(g->root);
	free(g);

	/* An explicit "none" reads back as none, and the next build falls
	 * back to the first source. */
	modScreenSetSelectedSource(-1);
	ASSERT_EQ(modScreenSelectedSource(), -1, "no selection reads back as -1");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	ASSERT_EQ(modScreenSelectedSource(), 0, "no selection falls back to the first source");
	freeGuiNode(g->root);
	free(g);

	free_env(&e);
	printf("PASS test_selection_state\n");
	return 0;
}

/* ---- robustness -------------------------------------------------------- */

static int test_null_instrument(void) {
	/* A missing instrument must not crash the builder: the screen comes
	 * up empty rather than dereferencing nothing. */
	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(NULL, NULL, 0, true);
	ASSERT_TRUE(g != NULL, "a NULL instrument still yields a graph");
	ASSERT_TRUE(g->root != NULL, "the graph has a root");
	ASSERT_TRUE(findGuiNodeNamed(g->root, "MODLIST") != NULL, "the source list exists");
	ASSERT_TRUE(findGuiNodeNamed(g->root, "MODDETAIL") != NULL, "the detail pane exists");
	ASSERT_EQ(modScreenSelectedSource(), -1, "no instrument means no selected source");
	freeGuiNode(g->root);
	free(g);
	printf("PASS test_null_instrument\n");
	return 0;
}

/* ---- the selected source's own dials ------------------------------------ */

static int test_source_dials(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;
	/* The FM patch's LFO is the fourth source. */
	int lfoPos = -1;
	for(int i = 0; i < modSourceCount(inst->modList); i++) {
		Mod *m = sourceAt(inst, i);
		if(m && m->type == MT_LFO) {
			lfoPos = i;
			break;
		}
	}
	ASSERT_TRUE(lfoPos >= 0, "the patch has an LFO source");

	/* Build first: the setter validates an index against the screen's
	 * instrument, which the build is what tells it. */
	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	modScreenSetSelectedSource(lfoPos);
	ASSERT_EQ(modScreenSelectedSource(), lfoPos, "the LFO is selected");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	GuiNode *detail = findGuiNodeNamed(g->root, "MODDETAIL");
	ASSERT_TRUE(detail != NULL, "the detail pane exists");
	/* An LFO's own dials are its RATE and its SHAPE. */
	ASSERT_TRUE(findGuiNodeNamed(detail, "RATE") != NULL,
	            "the detail pane holds the LFO's RATE dial");
	ASSERT_TRUE(findGuiNodeNamed(detail, "SHAPE") != NULL,
	            "the detail pane holds the LFO's SHAPE dial");
	freeGuiNode(g->root);
	free(g);

	/* A random source shows the same pair of dials. */
	Mod *lfo = sourceAt(inst, lfoPos);
	ASSERT_TRUE(lfo != NULL, "the LFO source exists");
	ASSERT_TRUE(changeModType(inst->modList, lfo, MT_RND, inst->paramList),
	          "the LFO becomes a random source");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	detail = findGuiNodeNamed(g->root, "MODDETAIL");
	ASSERT_TRUE(detail != NULL, "the detail pane exists");
	ASSERT_TRUE(findGuiNodeNamed(detail, "RATE") != NULL,
	            "the detail pane holds the random source's RATE dial");
	ASSERT_TRUE(findGuiNodeNamed(detail, "SHAPE") != NULL,
	            "the detail pane holds the random source's SHAPE dial");
	freeGuiNode(g->root);
	free(g);

	free_env(&e);
	printf("PASS test_source_dials\n");
	return 0;
}

/* ---- the header --------------------------------------------------------- */

static int test_header(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;

	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	GuiNode *title = findGuiNodeNamed(g->root, "MODTITLE");
	ASSERT_TRUE(title != NULL, "the header holds the title");
	ASSERT_TRUE(title->text && strcmp(title->text, TEST_PRESET) == 0,
	            "the title shows the instrument's name");
	freeGuiNode(g->root);
	free(g);

	/* An instrument that has never been saved has no preset name, so
	 * the voice type and the channel stand in. */
	inst->loaded.name[0] = '\0';
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	title = findGuiNodeNamed(g->root, "MODTITLE");
	ASSERT_TRUE(title != NULL, "the header holds the title");
	ASSERT_TRUE(title->text && strcmp(title->text, "FM1") == 0,
	            "the title falls back to the voice type and the channel");
	freeGuiNode(g->root);
	free(g);

	free_env(&e);
	printf("PASS test_header\n");
	return 0;
}

/* ---- the graph's focus -------------------------------------------------- */

static int test_focus(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;

	/* The focused screen's cursor starts on the selected source's row. */
	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	ASSERT_TRUE(g->selected != NULL, "the focused screen has a cursor");
	ASSERT_TRUE(g->selected && g->selected->name
	            && strcmp(g->selected->name, "MODSRC_0") == 0,
	            "the cursor starts on the selected source's row");
	freeGuiNode(g->root);
	free(g);

	/* A screen that is not the focused one carries no cursor. */
	g = createModScreenGraph(inst, e.vm, 0, false);
	ASSERT_TRUE(g != NULL, "the graph is built");
	ASSERT_TRUE(g->selected == NULL, "an unfocused screen has no cursor");
	freeGuiNode(g->root);
	free(g);

	/* A selection past the end of the source list is refused: the
	 * selection stays where it was. */
	modScreenSetSelectedSource(9999);
	ASSERT_EQ(modScreenSelectedSource(), 0, "an index past the end is refused");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	ASSERT_EQ(modScreenSelectedSource(), 0, "the selection is still the first source");
	freeGuiNode(g->root);
	free(g);

	/* A selection whose source is gone falls back to the first source
	 * the screen can show. */
	addRuntimeSource(inst);
	int runtimePos = modSourceCount(inst->modList) - 1;
	modScreenSetSelectedSource(runtimePos);
	ASSERT_EQ(modScreenSelectedSource(), runtimePos, "the runtime source is selected");
	ASSERT_TRUE(removeSource(inst, runtimePos), "the runtime source is removed");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	ASSERT_EQ(modScreenSelectedSource(), 0, "a selection whose source is gone falls back");
	ASSERT_TRUE(g->selected != NULL, "the screen still has a cursor");
	freeGuiNode(g->root);
	free(g);

	free_env(&e);
	printf("PASS test_focus\n");
	return 0;
}

/* ---- the pattern sources are not selectable sources -------------------- */

static int test_pattern_selection(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.inst;
	int count = modSourceCount(inst->modList);
	int patternPos = -1;
	for(int i = 0; i < count; i++) {
		Mod *m = sourceAt(inst, i);
		if(m && m->type == MT_PATTERN) {
			patternPos = i;
			break;
		}
	}
	ASSERT_TRUE(patternPos >= 0, "the instrument has pattern sources");

	/* The screen learns its instrument when it is built. */
	modScreenSetSelectedSource(-1);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	ASSERT_TRUE(g->selected != NULL, "the screen has a cursor");
	freeGuiNode(g->root);
	free(g);

	/* The pattern sources share the one grouped row, so they are not
	 * selectable sources: the setter refuses one and the selection
	 * stays where it was. */
	modScreenSetSelectedSource(0);
	ASSERT_EQ(modScreenSelectedSource(), 0, "the first source is selected");
	modScreenSetSelectedSource(patternPos);
	ASSERT_EQ(modScreenSelectedSource(), 0, "a pattern source is refused");

	/* A source that BECOMES a pattern source leaves the selection
	 * pointing at nothing the screen can show: the build falls back to
	 * a source it can show, so the screen always has a cursor. */
	Mod *first = sourceAt(inst, 0);
	ASSERT_TRUE(first != NULL, "the first source exists");
	ASSERT_TRUE(changeModType(inst->modList, first, MT_PATTERN, inst->paramList),
	          "the first source becomes a pattern source");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	ASSERT_TRUE(g->selected != NULL, "the screen still has a cursor");
	ASSERT_TRUE(g->selected && g->selected->name
	            && strcmp(g->selected->name, "MODSRC_1") == 0,
	            "the cursor fell back to a source the screen can show");
	ASSERT_EQ(modScreenSelectedSource(), 1, "the selection fell back too");
	freeGuiNode(g->root);
	free(g);

	free_env(&e);
	printf("PASS test_pattern_selection\n");
	return 0;
}

/* ---- the screen as a layer ------------------------------------------------ */

/* The screen is a layer on the instrument screen's overlay stack: opening
 * it pushes the layer, closing it pops the layer, and the layer stack's
 * own SELECT gesture closes it too (the universal back gesture). */

static int test_layer_lifecycle(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();
	ASSERT_TRUE(ig != NULL, "the instrument GUI exists");
	ASSERT_TRUE(layerStackIsEmpty(&ig->overlayLayers), "the layer stack starts empty");
	ASSERT_TRUE(!modScreenIsOpen(), "the screen starts closed");

	openModScreen();
	ASSERT_TRUE(modScreenIsOpen(), "the screen is open");
	ASSERT_TRUE(!layerStackIsEmpty(&ig->overlayLayers), "the screen pushed a layer");
	Layer *top = topLayer(&ig->overlayLayers);
	ASSERT_TRUE(top != NULL, "a layer is on top of the stack");
	ASSERT_TRUE(top->name && strcmp(top->name, "MOD") == 0, "the layer is named MOD");
	ASSERT_TRUE(top->dim, "the layer dims the instrument screen behind it");
	ASSERT_TRUE(top->ownsGraph, "the layer owns the screen's graph");
	ASSERT_TRUE(top->graph && top->graph->root, "the layer carries the screen's graph");
	ASSERT_TRUE(findGuiNodeNamed(top->graph->root, "MODLIST") != NULL,
	            "the layer's graph is the modulation screen's");

	closeModScreen();
	ASSERT_TRUE(!modScreenIsOpen(), "the screen is closed");
	ASSERT_TRUE(layerStackIsEmpty(&ig->overlayLayers), "the layer stack is empty again");

	free_env(&e);
	printf("PASS test_layer_lifecycle\n");
	return 0;
}

/* The screen's own BACK button closes the screen, through the layer
 * stack's input path (EDIT fires the selected node's action). */
static int test_back_button_closes(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();
	openModScreen();
	Layer *top = topLayer(&ig->overlayLayers);
	ASSERT_TRUE(top != NULL, "the screen pushed a layer");
	GuiNode *back = findGuiNodeNamed(top->graph->root, "MODBACK");
	ASSERT_TRUE(back != NULL, "the header holds the back button");
	ASSERT_TRUE(back->actionCb != NULL, "the back button carries an action");
	changeGraphSelection(top->graph, back);

	/* EDIT fires the action: the layer pops and the screen is closed. */
	e.is->keys[KM_EDIT].isPressed = true;
	e.is->keys[KM_EDIT].wasPressed = false;
	layerStackInputLayer(&ig->overlayLayers, 0, e.is);
	ASSERT_TRUE(!modScreenIsOpen(), "the back button closed the screen");
	ASSERT_TRUE(layerStackIsEmpty(&ig->overlayLayers), "the layer stack is empty again");

	free_env(&e);
	printf("PASS test_back_button_closes\n");
	return 0;
}

/* SELECT is the universal back gesture: the layer stack pops the
 * screen's layer, and the screen knows it is closed afterwards. */
static int test_select_closes(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();
	openModScreen();
	ASSERT_TRUE(modScreenIsOpen(), "the screen is open");

	e.is->keys[KM_SELECT].isPressed = true;
	e.is->keys[KM_SELECT].wasPressed = false;
	layerStackInputLayer(&ig->overlayLayers, 0, e.is);
	ASSERT_TRUE(!modScreenIsOpen(), "SELECT closed the screen");
	ASSERT_TRUE(layerStackIsEmpty(&ig->overlayLayers), "the layer stack is empty again");

	free_env(&e);
	printf("PASS test_select_closes\n");
	return 0;
}

/* The screen's graph is built from the selection as it stands when the
 * screen opens: set the selection first, open second, and the screen
 * shows that source. This is the contract Task 4 relies on when it
 * opens the screen focused on the modulator the user picked. */
static int test_selection_is_read_at_open(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();

	/* Pick a source that is not the first one. */
	int lfoPos = -1;
	for(int i = 0; i < modSourceCount(e.inst->modList); i++) {
		Mod *m = sourceAt(e.inst, i);
		if(m && m->type == MT_LFO) {
			lfoPos = i;
			break;
		}
	}
	ASSERT_TRUE(lfoPos >= 0, "the patch has an LFO source");
	ASSERT_TRUE(lfoPos != 0, "the LFO is not the first source");

	modScreenSetSelectedSource(lfoPos);
	ASSERT_EQ(modScreenSelectedSource(), lfoPos, "the LFO is selected");

	openModScreen();
	Layer *top = topLayer(&ig->overlayLayers);
	ASSERT_TRUE(top != NULL && top->graph != NULL, "the screen pushed a layer");
	ASSERT_EQ(modScreenSelectedSource(), lfoPos,
	          "the screen kept the selection it was opened with");
	/* The detail pane shows the LFO's own dials -- the source that was
	 * selected when the screen opened. */
	GuiNode *detail = findGuiNodeNamed(top->graph->root, "MODDETAIL");
	ASSERT_TRUE(detail != NULL, "the detail pane exists");
	ASSERT_TRUE(findGuiNodeNamed(detail, "RATE") != NULL,
	            "the detail pane holds the LFO's RATE dial");

	closeModScreen();
	free_env(&e);
	printf("PASS test_selection_is_read_at_open\n");
	return 0;
}

/* The screen is modal: while its layer is up, the instrument screen's own
 * input is captured by the layer, so the instrument screen's channel
 * cycling and page navigation do not fire. That is what makes the layer
 * the whole mechanism -- no change to main.c is needed.
 *
 * handlePresetUiInput is the instrument screen's input entry point: it
 * routes the whole stream to the topmost non-passive layer and returns
 * true, which stops the rest of the instrument screen's input. */
static int test_layer_captures_instrument_input(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();
	openModScreen();
	ASSERT_TRUE(modScreenIsOpen(), "the screen is open");

	/* The instrument screen's global gestures are gated on "no modal
	 * layer is up" (instrumentLayerModalActive). The screen's layer is
	 * a non-passive layer, so the gate is closed while it is up. */
	ASSERT_TRUE(instrumentLayerModalActive(),
	            "the screen's layer counts as a modal layer");

	/* The instrument screen's input goes to the layer, and the
	 * instrument screen reports it as consumed. */
	Graph *instGraph = getSelectedInstGraph();
	ASSERT_TRUE(instGraph != NULL, "the instrument screen has a graph");
	GuiNode *before = instGraph->selected;

	e.is->keys[KM_DOWN].isPressed = true;
	e.is->keys[KM_DOWN].wasPressed = false;
	bool consumed = handlePresetUiInput(e.is, e.inst);
	e.is->keys[KM_DOWN].isPressed = false;

	ASSERT_TRUE(consumed, "the instrument screen's input was consumed by the layer");
	ASSERT_TRUE(instGraph->selected == before,
	            "the instrument screen's cursor did not move");

	/* The layer's own graph took the arrow instead. */
	Layer *top = topLayer(&ig->overlayLayers);
	ASSERT_TRUE(top != NULL && top->graph != NULL, "the screen's layer is up");
	ASSERT_TRUE(top->graph->selected != NULL, "the screen's graph has a cursor");

	free_env(&e);
	printf("PASS test_layer_captures_instrument_input\n");
	return 0;
}

/* A button on the instrument screen reaches the screen: the button
 * carries the callback, the instrument screen's action dispatch fires it
 * on EDIT, and the screen opens as a layer. (Task 4 wires the
 * modulation line to the same callback.) */
static int test_button_reaches_open_mod_screen(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	InstrumentGui *ig = getInstrumentGui();
	Graph *instGraph = getSelectedInstGraph();
	ASSERT_TRUE(instGraph != NULL, "the instrument screen has a graph");
	ASSERT_TRUE(instGraph->selected != NULL, "the instrument screen has a cursor");

	/* The button carries the callback, the way the instrument screen's
	 * buttons carry theirs. */
	instGraph->selected->actionCb = cbOpenModScreen;
	instGraph->selected->actionCtx = NULL;

	e.is->keys[KM_EDIT].isPressed = true;
	e.is->keys[KM_EDIT].wasPressed = false;
	bool consumed = handlePresetUiInput(e.is, e.inst);
	e.is->keys[KM_EDIT].isPressed = false;

	ASSERT_TRUE(consumed, "the instrument screen's input was consumed");
	ASSERT_TRUE(modScreenIsOpen(), "the button opened the modulation screen");
	Layer *top = topLayer(&ig->overlayLayers);
	ASSERT_TRUE(top != NULL, "the screen pushed a layer");
	ASSERT_TRUE(top->name && strcmp(top->name, "MOD") == 0,
	            "the screen pushed the MOD layer");

	free_env(&e);
	printf("PASS test_button_reaches_open_mod_screen\n");
	return 0;
}

int main(void) {
	if(test_graph_shape() != 0) {
		return 1;
	}
	if(test_route_rows() != 0) {
		return 1;
	}
	if(test_selection_state() != 0) {
		return 1;
	}
	if(test_source_dials() != 0) {
		return 1;
	}
	if(test_header() != 0) {
		return 1;
	}
	if(test_focus() != 0) {
		return 1;
	}
	if(test_pattern_selection() != 0) {
		return 1;
	}
	if(test_null_instrument() != 0) {
		return 1;
	}
	if(test_layer_lifecycle() != 0) {
		return 1;
	}
	if(test_back_button_closes() != 0) {
		return 1;
	}
	if(test_select_closes() != 0) {
		return 1;
	}
	if(test_layer_captures_instrument_input() != 0) {
		return 1;
	}
	if(test_selection_is_read_at_open() != 0) {
		return 1;
	}
	if(test_button_reaches_open_mod_screen() != 0) {
		return 1;
	}
	printf("PASSED (gui_mod_screen)\n");
	return 0;
}
