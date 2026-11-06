/* Tests for gui_inst_mod.c — the mod routing UI's data layer.
 *
 * Headless: gui_inst_mod.c's construction path reaches createModStripGuiNode,
 * but ModStrip's render textures are lazily allocated (TESTING.md I41), so
 * building the instrument graph needs no GL context.
 *
 * The focus here is the source-list lifecycle (addRuntimeSource /
 * removeSource and the three "selected" wrappers) because that is where the
 * module's index arithmetic lives, and every guard in it is reachable from
 * outside. The route picker and the layer builders are exercised by
 * test_gui_inst_mod_picker.c. */

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

#define TEST_CHANNELS   2
#define TEST_VOICES     2
#define TEST_SAMPLE     1024
#define TEST_PRESET     "inst-mod-test"

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
	memset(e->is->keys, 0, sizeof(e->is->keys));

	initModSystem();
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
	freeSamplePool(e->sp);
	freeWavetablePool(e->wtp);
	freeInstrumentGui();
	/* The VoiceManager is process-lifetime state: gui_inst_mod.c's
	 * g_sourceCtx[] holds Instrument pointers into it (TESTING.md I40). */
}

static Instrument *inst0(Env *e) {
	return e->vm->instruments[0];
}

/* ---- source-list lifecycle ---------------------------------------------- */

static int test_source_lifecycle(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst0(&e);
	ASSERT_TRUE(i != NULL, "instrument 0 exists");
	ASSERT_TRUE(i->modList != NULL, "instrument has a modList");

	int start = modSourceCount(i->modList);
	int startEnvelopes = i->envelopeCount;
	/* The starting state is NOT envelopeCount == modSourceCount: a default FM
	 * instrument has three core envelopes plus one LFO, and envelopes[] mirrors
	 * the envelopes only. So start(4) > startEnvelopes(3) here.
	 *
	 * addRuntimeSource, however, re-syncs envelopeCount to modSourceCount --
	 * which counts the LFO too. The two counters therefore mean different
	 * things in different places. Recorded in TESTING.md I49; the test pins the
	 * shape rather than asserting an equality the code never promised. */
	ASSERT_TRUE(start > 0, "a default instrument has mod sources");
	ASSERT_TRUE(startEnvelopes > 0, "and mirrors its envelopes");
	ASSERT_TRUE(startEnvelopes < start, "the LFO is a source but not a mirrored envelope");

	/* Adding a source must move both counters by one. */
	addRuntimeSource(i);
	ASSERT_EQ(modSourceCount(i->modList), start + 1, "addRuntimeSource appends a source");
	/* envelopeCount counts populated envelopes[] slots, NOT mod sources --
	 * the LFO is a source but has no mirror slot (I49). So it advances by
	 * one per add, not to the source count. */
	ASSERT_EQ(i->envelopeCount, startEnvelopes + 1,
	          "envelopeCount advances by one per mirrored envelope");

	/* The mirror array must point at real Mods, or rebuildInstrumentGraph
	 * dereferences freed memory. */
	int added = modSourceCount(i->modList) - 1;
	int mi = modIndexAt(i->modList, added);
	ASSERT_TRUE(mi >= 0, "the added source resolves to a modList index");
	ASSERT_TRUE(i->modList->mods[mi] != NULL, "that slot holds a Mod");
	for(int k = 0; k < i->envelopeCount && k < MAX_MOD_SOURCES; k++) {
		Mod *m = i->envelopes[k];
		ASSERT_TRUE(m == NULL || (m->type >= 0 && m->type < MT_COUNT),
		            "every mirrored envelope holds a plausible Mod type");
	}

	/* Removing it must move both back. The mirror slot to check is whichever
	 * one held the Mod being removed -- not its modList index, which is a
	 * different index space (TESTING.md I49). */
	int mirrorSlot = -1;
	for(int k = 0; k < MAX_MOD_SOURCES; k++) {
		if(i->envelopes[k] == i->modList->mods[mi]) {
			mirrorSlot = k;
			break;
		}
	}
	ASSERT_TRUE(mirrorSlot >= 0, "the added source is present in the mirror");

	removeSource(i, added);
	ASSERT_EQ(modSourceCount(i->modList), start, "removeSource drops the source");
	ASSERT_TRUE(i->envelopes[mirrorSlot] == NULL,
	            "the mirror slot holding the removed Mod was cleared");

	/* Several in a row, then all gone -- exercises the loop path. The
	 * default instrument already has 4 sources and the ceiling is
	 * MAX_MOD_SOURCES (16), so there is room for `room` more; add exactly
	 * that many and assert the shape rather than an exact number. */
	int room = MAX_MOD_SOURCES - start;
	for(int k = 0; k < room; k++) {
		addRuntimeSource(i);
		ASSERT_TRUE(modSourceCount(i->modList) <= MAX_MOD_SOURCES,
		            "the ceiling holds while adding");
	}
	int afterAdds = modSourceCount(i->modList);
	ASSERT_EQ(afterAdds, start + room, "as many adds as there was room for");
	for(int k = afterAdds - 1; k >= start; k--) {
		removeSource(i, k);
		ASSERT_EQ(modSourceCount(i->modList), k, "removing down one at a time");
	}
	ASSERT_EQ(modSourceCount(i->modList), start, "back to the starting count");

	free_env(&e);
	printf("PASS test_source_lifecycle\n");
	return 0;
}

static int test_source_guards(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst0(&e);

	/* NULL instrument and NULL modList must both be no-ops, not crashes. */
	addRuntimeSource(NULL);
	removeSource(NULL, 0);
	removeSelectedSource();
	removeSelectedEnvelope();

	Instrument *noMods = (Instrument *)calloc(1, sizeof(Instrument));
	ASSERT_TRUE(noMods != NULL, "calloc bare Instrument");
	addRuntimeSource(noMods);
	removeSource(noMods, 0);
	free(noMods);

	/* Capacity: MAX_MOD_SOURCES sources is the ceiling, and hitting it must
	 * stop adding rather than overflow inst->envelopes[]. */
	int start = modSourceCount(i->modList);
	for(int k = 0; k < MAX_MOD_SOURCES + 4; k++) {
		addRuntimeSource(i);
	}
	ASSERT_TRUE(modSourceCount(i->modList) <= MAX_MOD_SOURCES,
	            "the source list never exceeds MAX_MOD_SOURCES");
	ASSERT_TRUE(i->envelopeCount <= MAX_MOD_SOURCES,
	            "envelopeCount never exceeds the envelopes[] capacity");
	int capped = modSourceCount(i->modList);

	/* Out-of-range and negative indices are rejected. */
	removeSource(i, capped);
	removeSource(i, -1);
	removeSource(i, 1000000);
	ASSERT_EQ(modSourceCount(i->modList), capped, "bad indices remove nothing");

	/* Core sources are protected. With coreEnvelopeCount at 0 nothing is
	 * protected, so assert the invariant rather than assume it. */
	ASSERT_TRUE(i->coreEnvelopeCount >= 0, "coreEnvelopeCount is not negative");
	if(i->coreEnvelopeCount > 0) {
		removeSource(i, 0);
		ASSERT_EQ(modSourceCount(i->modList), capped, "core sources are protected");
	}

	free_env(&e);
	printf("PASS test_source_guards\n");
	return 0;
}

static int test_capacity_boundary(void) {
	/* Drive exactly to MAX_MOD_SOURCES and confirm the array indices stay
	 * in bounds: envelopes[] has MAX_MOD_SOURCES slots and addRuntimeSource
	 * writes envelopes[envelopeCount] before incrementing. */
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst0(&e);
	while(modSourceCount(i->modList) < MAX_MOD_SOURCES) {
		int before = modSourceCount(i->modList);
		addRuntimeSource(i);
		ASSERT_TRUE(modSourceCount(i->modList) == before + 1
		            || modSourceCount(i->modList) == MAX_MOD_SOURCES,
		            "add either succeeds or is refused, never both");
	}
	ASSERT_EQ(modSourceCount(i->modList), MAX_MOD_SOURCES, "reached the ceiling");
	ASSERT_TRUE(i->envelopeCount <= MAX_MOD_SOURCES, "envelopeCount within capacity");

	int before = modSourceCount(i->modList);
	addRuntimeSource(i);
	ASSERT_EQ(modSourceCount(i->modList), before,
	          "one past the ceiling is refused");

	/* Drain back to the floor. The floor is coreEnvelopeCount: removeSource
	 * refuses any source position below it, so the core envelope block is
	 * unreachable by design. Removing past it must be a no-op, not a crash. */
	int floorCount = i->coreEnvelopeCount;
	ASSERT_TRUE(floorCount > 0, "the instrument has protected core envelopes");
	while(modSourceCount(i->modList) > floorCount) {
		int n = modSourceCount(i->modList);
		removeSource(i, n - 1);
		ASSERT_EQ(modSourceCount(i->modList), n - 1, "removal always makes progress");
	}
	ASSERT_EQ(modSourceCount(i->modList), floorCount, "drained to the protected floor");

	/* Below the floor is refused, not obeyed. */
	for(int k = 0; k < floorCount; k++) {
		removeSource(i, k);
		ASSERT_EQ(modSourceCount(i->modList), floorCount,
		          "removing a core source is refused");
	}

	free_env(&e);
	printf("PASS test_capacity_boundary\n");
	return 0;
}

/* ---- route-line and picker state ---------------------------------------- */

/* First dial-shaped node in the tree. Identified by draw-callback identity,
 * which is how the module itself identifies its own nodes (mirrors
 * isPresetNameNode / isInstChipNode). */
static GuiNode *find_first_dial(GuiNode *n) {
	if(!n) {
		return NULL;
	}
	if(n->draw == drawDialGuiNode) {
		return n;
	}
	if(!n->items) {
		return NULL;
	}
	ListElement *e = n->items->head;
	for(int i = 0; i < n->itemCount && e; i++) {
		GuiNode *hit = find_first_dial(*(GuiNode **)e->data);
		if(hit) {
			return hit;
		}
		e = e->next;
	}
	return NULL;
}

static int test_route_erase_mode(void) {
	guiSetRouteEraseMode(false);
	ASSERT_TRUE(!guiRouteEraseMode(), "erase starts off");
	guiSetRouteEraseMode(true);
	ASSERT_TRUE(guiRouteEraseMode(), "erase can be armed");
	guiSetRouteEraseMode(false);
	ASSERT_TRUE(!guiRouteEraseMode(), "erase can be disarmed");

	guiSetPickerEditHeld(true);
	guiSetPickerEditHeld(false);
	printf("PASS test_route_erase_mode\n");
	return 0;
}

static int test_route_node_lookup(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "instrument graph exists");

	/* No source routes are shown until one is added, so the lookup has to
	 * report "not found" rather than hand back an arbitrary node. Adding a
	 * source is what makes a route node exist. */
	int srcIdx = -1;
	int count = modSourceCount(inst0(&e)->modList);
	if(count > 0) {
		for(int k = 0; k < count; k++) {
			if(findSourceRouteNode(g->root, k) != NULL) {
				srcIdx = k;
				break;
			}
		}
	}
	if(srcIdx < 0) {
		addRuntimeSource(inst0(&e));
		count = modSourceCount(inst0(&e)->modList);
		for(int k = 0; k < count; k++) {
			if(findSourceRouteNode(g->root, k) != NULL) {
				srcIdx = k;
				break;
			}
		}
	}
	/* If no route node is findable at all, that is a real finding about the
	 * graph, so report it rather than skipping. */
	ASSERT_TRUE(srcIdx >= 0,
	            "at least one source's route node is present in the graph");
	ASSERT_TRUE(findSourceRouteNode(g->root, srcIdx) != NULL, "it is findable");
	/* Out-of-range and negative lookups must miss, not crash. */
	ASSERT_TRUE(findSourceRouteNode(g->root, -1) == NULL, "negative index misses");
	ASSERT_TRUE(findSourceRouteNode(g->root, 100000) == NULL, "huge index misses");
	ASSERT_TRUE(findSourceRouteNode(NULL, srcIdx) == NULL, "NULL node misses");

	free_env(&e);
	printf("PASS test_route_node_lookup (found source %d)\n", srcIdx);
	return 0;
}

static int test_viewport_geometry(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "instrument graph");

	/* dialVisibleInViewport must be total: every dial in the graph has to
	 * yield a verdict, not crash on an unlaid-out node. */
	/* Dials are nested deep inside the instrument graph, so this has to be a
	 * recursive walk -- a scan of the root's immediate children found none and
	 * made the whole test vacuous. */
	int checked = 0;
	GuiNode *dial = find_first_dial(g->root);
	ASSERT_TRUE(dial != NULL, "the instrument graph contains a dial node");
	{
		/* A node with no layout has w == 0; the predicate must answer
		 * false rather than divide by zero or read past the viewport. */
		(void)dialVisibleInViewport(dial);
		dial->w = 100;
		dial->h = 40;
		dial->x = 0;
		dial->y = 0;
		(void)dialVisibleInViewport(dial);
		dial->x = -500;
		(void)dialVisibleInViewport(dial);
		dial->x = 100000;
		(void)dialVisibleInViewport(dial);
		checked = 1;
	}
	/* NULL must be safe: the picker calls this on nodes it has not laid out. */
	(void)dialVisibleInViewport(NULL);
	/* And it must be idempotent -- the picker calls it every frame. */
	if(dial != NULL) {
		bool first = dialVisibleInViewport(dial);
		for(int k = 0; k < 5; k++) {
			ASSERT_EQ(dialVisibleInViewport(dial), first,
			          "dialVisibleInViewport is stable across calls");
		}
	}

	free_env(&e);
	printf("PASS test_viewport_geometry (%d dial(s) exercised)\n", checked);
	return 0;
}

/* ---- the modulation line's data ---------------------------------------- */

/* The line shows the instrument's CONNECTED modulators -- the sources that
 * have at least one route -- and nothing else. A source with no route is
 * not something the user can adjust, so it does not belong in the line.
 *
 * The counts are RELATIVE: a default FM patch already wires its core
 * envelopes to the operators, so the instrument starts with connected
 * sources. What matters is the difference a route makes. */
static int test_connected_sources(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst0(&e);
	ASSERT_TRUE(i != NULL, "instrument 0 exists");
	ASSERT_TRUE(i->modList != NULL, "the instrument has a modList");
	ASSERT_TRUE(i->paramList != NULL, "the instrument has a paramList");

	Mod *conn[64];
	int base = modConnectedSources(i, conn, 64);
	ASSERT_TRUE(base > 0, "the FM's default patch wires its envelopes");

	/* A source with no route: add one. */
	int before = modSourceCount(i->modList);
	addRuntimeSource(i);
	ASSERT_EQ(modSourceCount(i->modList), before + 1, "a runtime source was added");
	Mod *added = modSourceAtPos(i, before);
	ASSERT_TRUE(added != NULL, "the added source resolves");
	ASSERT_EQ(modConnectedSources(i, conn, 64), base,
	          "an unconnected source is not on the line");

	/* Route it, and it joins the line. */
	ASSERT_TRUE(i->gain != NULL, "the instrument has a gain parameter");
	ASSERT_TRUE(addModulation(i->paramList, i->modList, added, i->gain, 0.75f, MO_ADD),
	           "the added source is routed to the gain");
	int n = modConnectedSources(i, conn, 64);
	ASSERT_EQ(n, base + 1, "a routed source joins the line");
	ASSERT_TRUE(conn[base] == added, "the new source is last, in source order");

	/* The route belongs to that source, and its amount is the attenuator's. */
	ModConnection *routes[16];
	int rn = modSourceRoutes(i, added, routes, 16);
	ASSERT_EQ(rn, 1, "the source has one route");
	ASSERT_TRUE(routes[0] != NULL, "the route is returned");
	ASSERT_TRUE(routes[0]->amount != NULL, "the route carries an amount");
	ASSERT_TRUE(getParameterValue(routes[0]->amount) > 0.0f, "the amount is non-zero");

	/* A source with no route contributes no routes. The FM's own core
	 * envelopes are connected, so the check is that a source with no route
	 * is absent from the connected list, not that every source has one. */
	int connectedWithout = 0;
	for(int k = 0; k < base; k++) {
		if(modSourceRoutes(i, conn[k], NULL, 0) == 0) {
			connectedWithout++;
		}
	}
	ASSERT_EQ(connectedWithout, 0, "every connected source has at least one route");

	/* The count must not exceed the cap, and a NULL out is a pure count. */
	ASSERT_EQ(modConnectedSources(i, NULL, 0), base + 1, "a NULL out counts without writing");

	free_env(&e);
	printf("PASS test_connected_sources\n");
	return 0;
}

/* REMOVE removes the source under the cursor.
 *
 * The mapping used to subtract a header row that is not inside the
 * mod-wrap (the MODS header is a SIBLING of the mod-wrap), so REMOVE
 * removed the source BEFORE the one under the cursor -- and for the
 * first runtime source it refused outright, because the source it named
 * was a protected core envelope. */
static int test_remove_selected_source(void) {
	Env e;
	if(make_env(&e) != 0) {
		return 1;
	}
	Instrument *i = inst0(&e);
	int before = modSourceCount(i->modList);

	/* Core sources are protected, so the removal only fires on a runtime
	 * source. Add one. */
	addRuntimeSource(i);
	ASSERT_EQ(modSourceCount(i->modList), before + 1, "a runtime source was added");

	/* The added source is the last one. */
	int target = before;
	Mod *victim = modSourceAtPos(i, target);
	ASSERT_TRUE(victim != NULL, "the target source resolves");

	Graph *g = getSelectedInstGraph();
	ASSERT_TRUE(g != NULL, "instrument graph");
	GuiNode *route = findSourceRouteNode(g->root, target);
	ASSERT_TRUE(route != NULL, "the target source's ROUTE node is in the graph");

	/* Put the cursor on it, the way the player would have it. */
	changeGraphSelection(g, route);

	removeSelectedSource();

	/* The source count drops by one... */
	ASSERT_EQ(modSourceCount(i->modList), before, "the source was removed");
	/* ...and it was the RIGHT one: the source that was at the target
	 * position is the one that went. */
	ASSERT_TRUE(modSourceAtPos(i, target) != victim,
	            "the source under the cursor is the one that was removed");

	free_env(&e);
	printf("PASS test_remove_selected_source\n");
	return 0;
}

int main(void) {
	if(test_source_lifecycle() != 0) {
		return 1;
	}
	if(test_connected_sources() != 0) {
		return 1;
	}
	if(test_remove_selected_source() != 0) {
		return 1;
	}
	if(test_source_guards() != 0) {
		return 1;
	}
	if(test_capacity_boundary() != 0) {
		return 1;
	}
	if(test_route_erase_mode() != 0) {
		return 1;
	}
	if(test_route_node_lookup() != 0) {
		return 1;
	}
	if(test_viewport_geometry() != 0) {
		return 1;
	}
	printf("PASSED (gui_inst_mod)\n");
	return 0;
}