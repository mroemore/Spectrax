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
	return 0;
}

static void free_env(Env *e) {
	if(!e) {
		return;
	}
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

	modScreenSetSelectedSource(2);
	ASSERT_EQ(modScreenSelectedSource(), 2, "the selection is settable");
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
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

	/* A selection that no longer exists falls back to the first
	 * source; an explicit "none" reads back as none. */
	modScreenSetSelectedSource(-1);
	ASSERT_EQ(modScreenSelectedSource(), -1, "no selection reads back as -1");
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
	ASSERT_EQ(modScreenSelectedSource(), 0, "a stale selection falls back to the first source");
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

	modScreenSetSelectedSource(lfoPos);
	Graph *g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "createModScreenGraph returns a graph");
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

	/* A selection past the end of the source list falls back to the
	 * first source. */
	modScreenSetSelectedSource(9999);
	g = createModScreenGraph(inst, e.vm, 0, true);
	ASSERT_TRUE(g != NULL, "the graph is rebuilt");
	ASSERT_EQ(modScreenSelectedSource(), 0, "a stale selection falls back to the first source");
	freeGuiNode(g->root);
	free(g);

	free_env(&e);
	printf("PASS test_focus\n");
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
	if(test_null_instrument() != 0) {
		return 1;
	}
	printf("PASSED (gui_mod_screen)\n");
	return 0;
}
