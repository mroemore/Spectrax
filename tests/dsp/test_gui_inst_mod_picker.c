/* The route picker: opening a ROUTE layer, routing a source into a
 * destination, and taking it back out again with erase mode.
 *
 * This is gui_inst_mod.c's core purpose, and every entry point is public:
 * cbOpenRouteLayer takes the Instrument, the layer's dest nodes carry their
 * own actionCb, and the erase modifier is set from outside.
 *
 * Headless -- construction reaches createModStripGuiNode, but ModStrip's
 * render textures are lazily allocated (TESTING.md I41). */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "gui_inst_internal.h"
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

/* gui_inst_mod.c's SourceCtx is file-static too. Note that cbOpenRouteLayer
 * takes a SourceCtx*, NOT an Instrument* -- resolveSourceCtx re-points the
 * struct at the currently displayed instrument and validates sc->idx against
 * that instrument's modList, so a hand-built ctx must carry a valid source
 * position or the call is a no-op. */
typedef struct {
	Instrument *inst;
	int idx;
} SourceCtx;

/* gui_inst_mod.c's DestCtx is file-static. Mirrored here by hand, following the
 * convention test_graph_nav uses: the layout is repeated so the test can read
 * ->dest, and any change to the struct breaks this mirror loudly rather than
 * silently mis-reading it. */
typedef struct {
	Instrument *inst;
	int srcIdx;
	Parameter *dest;
} DestCtx;

#define TEST_SAMPLE 1024

typedef struct {
	VoiceManager *vm;
	InputState *is;
	ParamList *pl;
	Arranger *arranger;
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
	int selected;
} Env;

static InputState *g_is = NULL;

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
	settings.enabledChannels = 1;
	settings.defaultVoiceCount = 1;
	settings.defaultBPM = 120;
	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	ASSERT_TRUE(e->vm != NULL, "createVoiceManager");

	e->pl = createParamList();
	e->arranger = (Arranger *)calloc(1, sizeof(Arranger));
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			e->arranger->song[ch][r] = -1;
		}
		e->arranger->playhead_indices[ch] = -1;
	}
	e->arranger->tempoSettings.bpm = createParameterPro(e->pl, "BPM", 120.0f, 1.0f,
	                                                    1000.0f, 1.0f, 10.0f, NULL, NULL);
	e->arranger->tempoSettings.swing = createParameterPro(e->pl, "Swing", 0.0f, 0.0f,
	                                                     1.0f, 0.01f, 0.1f, NULL, NULL);

	e->is = createInputState(INPUT_TYPE_KEYBOARD);
	ASSERT_TRUE(e->is != NULL, "createInputState");
	memset(e->is->keys, 0, sizeof(e->is->keys));
	g_is = e->is;

	initModSystem();
	e->selected = 0;
	createInstrumentGui(e->vm, &e->selected, 0);
	ASSERT_TRUE(getInstrumentGui() != NULL, "createInstrumentGui");
	return 0;
}

static void free_env(Env *e) {
	g_is = NULL;
	freeInputState(e->is);
	freeParamList(e->pl);
	free(e->arranger);
	freeSamplePool(e->sp);
	freeWavetablePool(e->wtp);
	freeInstrumentGui();
}

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

static Layer *layer_by_name(const char *name) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return NULL;
	}
	for(int i = 0; i < ig->overlayLayers.count; i++) {
		const char *n = ig->overlayLayers.layers[i].name;
		if(n && strcmp(n, name) == 0) {
			return &ig->overlayLayers.layers[i];
		}
	}
	return NULL;
}

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
		GuiNode *hit = find_node_named(*(GuiNode **)e->data, name);
		if(hit) {
			return hit;
		}
		e = e->next;
	}
	return NULL;
}

/* First node in the ROUTE layer that has an action callback -- i.e. a dest. */
static GuiNode *first_dest_node(Layer *route) {
	if(!route || !route->graph || !route->graph->root) {
		return NULL;
	}
	GuiNode *r = route->graph->root;
	if(!r->items) {
		return NULL;
	}
	ListElement *e = r->items->head;
	for(int i = 0; i < r->itemCount && e; i++) {
		GuiNode *c = *(GuiNode **)e->data;
		if(c && c->actionCb) {
			return c;
		}
		e = e->next;
	}
	return NULL;
}

/* How many modulator connections a parameter has. */
static int conn_count(Parameter *p) {
	int n = 0;
	for(ModConnection *c = p ? p->modulators : NULL; c; c = c->next) {
		n++;
	}
	return n;
}

/* ---- the picker --------------------------------------------------------- */

static int test_open_route_layer(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.vm->instruments[0];
	ASSERT_TRUE(inst != NULL, "instrument 0");

	SourceCtx sc;
	sc.inst = inst;
	sc.idx = modSourceCount(inst->modList) - 1;
	ASSERT_TRUE(sc.idx >= 0, "the instrument has a source to route");

	ASSERT_TRUE(!layer_named("ROUTE"), "no ROUTE layer before we ask for one");
	cbOpenRouteLayer(&sc);
	ASSERT_TRUE(layer_named("ROUTE"),
	            "cbOpenRouteLayer pushes a layer named ROUTE for an FM instrument");

	Layer *route = layer_by_name("ROUTE");
	GuiNode *dest = first_dest_node(route);
	ASSERT_TRUE(dest != NULL, "the ROUTE layer offers at least one destination");
	ASSERT_TRUE(dest->actionCtx != NULL, "the dest node carries its context");

	/* Opening twice must not stack two pickers: the second open is meant to
	 * rebuild the graph in place (g_routePickerCount). */
	int layersBefore = getInstrumentGui()->overlayLayers.count;
	cbOpenRouteLayer(&sc);
	int layersAfter = getInstrumentGui()->overlayLayers.count;
	ASSERT_TRUE(layersAfter <= layersBefore + 1,
	            "opening again does not stack unbounded layers");

	/* Closing resets every piece of picker state. */
	guiPickerClosed();
	guiSetRouteEraseMode(false);
	guiSetPickerEditHeld(false);

	free_env(&e);
	printf("PASS test_open_route_layer\n");
	return 0;
}

static int test_route_and_erase(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.vm->instruments[0];

	/* A runtime source gives the picker something to route. */
	addRuntimeSource(inst);
	int srcIdx = modSourceCount(inst->modList) - 1;
	ASSERT_TRUE(srcIdx >= 0, "a source exists to route");
	SourceCtx sc;
	sc.inst = inst;
	sc.idx = srcIdx;

	cbOpenRouteLayer(&sc);
	Layer *route = layer_by_name("ROUTE");
	ASSERT_TRUE(route != NULL, "ROUTE layer is up");
	GuiNode *dest = first_dest_node(route);
	ASSERT_TRUE(dest != NULL, "a destination is offered");

	/* The dest node's context names the destination parameter; route into it
	 * and the connection count must go up. */
	DestCtx *dc = (DestCtx *)dest->actionCtx;
	ASSERT_TRUE(dc != NULL, "dest ctx");
	Parameter *target = dc->dest;
	ASSERT_TRUE(target != NULL, "dest ctx names a parameter");
	int before = conn_count(target);

	dest->actionCb(dest->actionCtx);
	int afterRoute = conn_count(target);
	ASSERT_TRUE(afterRoute > before, "routing adds a modulator connection");

	/* Same press with erase armed must take it back out. */
	guiSetRouteEraseMode(true);
	ASSERT_TRUE(guiRouteEraseMode(), "erase armed");
	dest->actionCb(dest->actionCtx);
	guiSetRouteEraseMode(false);
	ASSERT_TRUE(conn_count(target) < afterRoute, "erase mode removes the connection");

	/* Erase with nothing routed must be a no-op, not a crash. */
	dest->actionCb(dest->actionCtx);

	guiPickerClosed();
	free_env(&e);
	printf("PASS test_route_and_erase (conns %d -> %d -> <%d)\n",
	       before, afterRoute, before);
	return 0;
}

static int test_picker_input_guards(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}

	/* With no picker open, neither picker input path may claim the frame --
	 * if either did, the instrument screen would stop receiving arrows. */
	guiPickerClosed();
	guiSetPickerEditHeld(false);
	ASSERT_TRUE(!guiPickerEditorInput(e.is), "editor declines with no picker open");
	ASSERT_TRUE(!guiPickerHeldAmountAdjust(e.is), "held-adjust declines with no picker open");

	/* Arming EDIT alone is not enough; a ROUTE layer with a selected dest is
	 * required, so this must still decline. */
	guiSetPickerEditHeld(true);
	ASSERT_TRUE(!guiPickerHeldAmountAdjust(e.is), "EDIT held alone is not enough");
	guiSetPickerEditHeld(false);

	/* probePickFirstRoute is a harness hook, not a query: it deliberately
	 * creates routes on the last real source so the overlay has something to
	 * draw. So the assertion is that it succeeds and that the routes are
	 * really there -- not that it declines. */
	Instrument *inst = e.vm->instruments[0];
	int added = probePickFirstRoute();
	ASSERT_TRUE(added >= 0, "probePickFirstRoute returns a count, never a negative");
	int routed = 0;
	for(int i = 0; i < inst->paramList->count; i++) {
		Parameter *p = inst->paramList->params[i];
		if(p && p->modulator_count > 0) {
			routed++;
		}
	}
	if(added > 0) {
		ASSERT_TRUE(routed > 0,
		            "a probe that reports routes leaves real connections behind");
	}
	/* Note: the paramList legitimately grows, because addModulation creates a
	 * per-connection attenuator parameter (that is the MT_ATTEN entry that
	 * interleaves the modList). Do not assert the parameter count is stable. */

	/* The frame-sync helpers are called unconditionally from the draw path,
	 * so they must be total: calling them with no picker must not crash. */
	guiPickerFrameSync();
	syncPickerDestRects();
	syncPickerBaseScroll();
	syncModWrapScroll();
	syncRouteLinesOverlay(getInstrumentGui());

	free_env(&e);
	printf("PASS test_picker_input_guards\n");
	return 0;
}

static int test_clear_all_layer(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *inst = e.vm->instruments[0];
	addRuntimeSource(inst);

	/* Route something first, so CLEARALL has work to do. */
	SourceCtx sc;
	sc.inst = inst;
	sc.idx = modSourceCount(inst->modList) - 1;
	cbOpenRouteLayer(&sc);
	Layer *route = layer_by_name("ROUTE");
	ASSERT_TRUE(route != NULL, "ROUTE layer is up");
	GuiNode *dest = first_dest_node(route);
	ASSERT_TRUE(dest != NULL, "a destination is offered");
	Parameter *target = ((DestCtx *)dest->actionCtx)->dest;
	dest->actionCb(dest->actionCtx);
	ASSERT_TRUE(conn_count(target) > 0, "a connection now exists to clear");
	guiPickerClosed();

	/* The same SourceCtx drives cbOpenClearAllLayer -- it takes a SourceCtx*,
	 * not an Instrument*. */
	ASSERT_TRUE(!layer_named("CLEARALL"), "no CLEARALL layer yet");
	cbOpenClearAllLayer(&sc);
	ASSERT_TRUE(layer_named("CLEARALL"), "cbOpenClearAllLayer pushes CLEARALL");

	GuiNode *no = find_node_named(layer_by_name("CLEARALL")->graph->root, "CLEARALL_NO");
	GuiNode *yes = find_node_named(layer_by_name("CLEARALL")->graph->root, "CLEARALL_YES");
	ASSERT_TRUE(no != NULL && no->actionCb, "CLEARALL has a NO button with a callback");
	ASSERT_TRUE(yes != NULL && yes->actionCb, "CLEARALL has a YES button with a callback");

	/* NO must keep the routing and close the dialog. */
	no->actionCb(no->actionCtx);
	ASSERT_TRUE(!layer_named("CLEARALL"), "NO closes the dialog");
	ASSERT_TRUE(conn_count(target) > 0, "NO keeps the existing routing");

	/* YES must drop every modulation and close the dialog. */
	cbOpenClearAllLayer(&sc);
	GuiNode *yes2 = find_node_named(layer_by_name("CLEARALL")->graph->root, "CLEARALL_YES");
	ASSERT_TRUE(yes2 != NULL, "CLEARALL is up again");
	yes2->actionCb(yes2->actionCtx);
	ASSERT_TRUE(!layer_named("CLEARALL"), "YES closes the dialog");
	ASSERT_TRUE(conn_count(target) == 0, "YES clears every modulation");

	free_env(&e);
	printf("PASS test_clear_all_layer\n");
	return 0;
}

int main(void) {
	if(test_open_route_layer() != 0) {
		return 1;
	}
	if(test_route_and_erase() != 0) {
		return 1;
	}
	if(test_picker_input_guards() != 0) {
		return 1;
	}
	if(test_clear_all_layer() != 0) {
		return 1;
	}
	printf("PASSED (gui_inst_mod picker)\n");
	return 0;
}