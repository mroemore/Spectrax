/* gui_mod_screen.c -- the modulation screen's graph and its layer.
 *
 * The screen is two panes: the source list on the left, the selected
 * source's detail on the right. This module builds the graph and owns
 * the screen's layer -- the full-graph overlay the screen is pushed as
 * over the instrument screen. The route picker is a later task of the
 * phase. The graph's shape:
 *
 *   root (vertical)
 *     MODHEADER (horizontal, weight 2)
 *       MODBACK   the back button
 *       MODTITLE  the instrument's name
 *     MODBODY (horizontal, weight 18)
 *       MODLIST   the source list (a scroll container, 40px rows)
 *         MODSRC_<i>   one row per source
 *         MODSRC_PTN   the four pattern tracks, grouped into one row
 *       MODDETAIL (vertical, weight 3)
 *         the selected source's own dials
 *         MODROUTES   the selected source's routes
 *           MODROUTE_<i>   one row per route
 *           MOD_ADDRUTE    the "+ ROUTE" button
 *
 * The rows carry no actions yet: the route picker lands with the next
 * task, so every button but the back button is inert (a NULL callback)
 * and the cursor still moves over them. The back button closes the
 * screen.
 *
 * The screen is a layer: openModScreen() builds the graph and pushes it
 * as a "MOD" layer over the instrument screen, closeModScreen() pops
 * it. The layer stack's own SELECT gesture pops it too, so the screen
 * has two ways back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph_gui.h"
#include "gui.h"
#include "gui_internal.h"
#include "gui_inst_internal.h"
#include "gui_layer.h"
#include "gui_mod_screen.h"
#include "gui_style.h"
#include "modsystem.h"
#include "voice.h"

/* The source list's rows are 40px tall, like the instrument screen's
 * mod-source rows. */
#define MOD_ROW_H 40

/* The header's title and the source rows' names. */
#define MOD_TITLE_LEN 64
#define MOD_NAME_LEN  32

/* The header's back button: closes the screen. The layer stack pops a
 * layer on the SELECT back gesture too, so the button is the second
 * way out -- the one the user aims at. */
static void cbModScreenBack(void *ctx);

/* The "+ ROUTE" button's action: opens the destination picker over the
 * screen. Defined below, with the picker. */
static void cbModScreenOpenPicker(void *ctx);

/* The "CLEAR ALL" button's action: opens the shared confirmation. */
static void cbModScreenClearAll(void *ctx);

/* The source list's ADD and DEL, and the detail pane's TYPE control.
 * Defined below, with the picker. */
static void cbModScreenAddSource(void *ctx);
static void cbModScreenDeleteSource(void *ctx);
static void cbModScreenCycleType(void *ctx);

/* The source at a source position (0-based over the non-attenuator
 * mods), or NULL. */
static Mod *modSourceAt(Instrument *inst, int pos) {
	if(!inst || !inst->modList) {
		return NULL;
	}
	int mi = modIndexAt(inst->modList, pos);
	return (mi >= 0) ? inst->modList->mods[mi] : NULL;
}

/* A source the screen can show: the attenuators are connection-internal
 * and the pattern sources share the one grouped row, so neither has a
 * source row of its own. */
static bool isSelectableSource(Mod *m) {
	return m && m->type != MT_ATTEN && m->type != MT_PATTERN;
}

/* The first source the screen can show, or -1 when it has none. */
static int firstSelectableSource(Instrument *inst) {
	int count = inst ? modSourceCount(inst->modList) : 0;
	for(int i = 0; i < count; i++) {
		if(isSelectableSource(modSourceAt(inst, i))) {
			return i;
		}
	}
	return -1;
}

/* The selected source's index within the instrument's source list, or -1
 * when no source is selected. The selection is screen state: it outlives
 * the graph, so a rebuild does not throw the user back to the first
 * source. */
static int g_modScreenSource = -1;

/* The instrument the screen is showing. The selection is validated
 * against it, and the screen holds one graph at a time (the selected
 * instrument's), so the pointer stays live for as long as the screen
 * does. */
static Instrument *g_modScreenInst = NULL;

/* The instrument the screen is showing, or NULL.
 *
 * The cached pointer can DANGLE: changing the voice type frees the old
 * instrument, and the screen's pointer would still name it. So the
 * screen never uses the cache on its own -- it asks the instrument screen
 * which instrument is selected now, and forgets the cache when the two
 * disagree. */
static Instrument *modScreenInstrument(void) {
	Instrument *current = getSelectedInstInstrument();
	if(current != g_modScreenInst) {
		g_modScreenInst = current;
		g_modScreenSource = -1;
	}
	return g_modScreenInst;
}

int modScreenSelectedSource(void) {
	return g_modScreenSource;
}

void modScreenSetSelectedSource(int idx) {
	if(idx < 0) {
		g_modScreenSource = -1;
		return;
	}
	/* The pattern sources share the one grouped row, so they are not
	 * selectable sources: an index naming one is refused and the
	 * selection stays where it was. */
	modScreenInstrument();
	if(g_modScreenInst && !isSelectableSource(modSourceAt(g_modScreenInst, idx))) {
		return;
	}
	g_modScreenSource = idx;
}

/* The header's title: the instrument's name. An instrument that has
 * never been saved carries no preset name, so the voice type and the
 * channel stand in ("FM1"). */
static void modScreenTitle(char *buf, size_t len, Instrument *inst, int channel) {
	if(inst && inst->loaded.name[0]) {
		snprintf(buf, len, "%s", inst->loaded.name);
		return;
	}
	snprintf(buf, len, "%s%d", voiceTypeTag(inst ? inst->voiceType : VOICE_TYPE_FM), channel + 1);
}

/* The context a source row carries: the source POSITION it names. The
 * row's action selects that source. */
typedef struct {
	Instrument *inst;
	int pos;
} ModSrcCtx;

/* The source rows' contexts. The screen's graph is rebuilt whenever the
 * selection changes, so the contexts are rebuilt with it -- but they
 * have to outlive the graph they are attached to, so they are file
 * static.
 *
 * They are sized MAX_MODS, not MAX_MOD_SOURCES: the source POSITIONS
 * run over the whole mod list, and a preset may carry more sources
 * than MAX_MOD_SOURCES (the pattern tracks are re-seeded on top of the
 * preset's own). */
static ModSrcCtx g_modSrcCtx[MAX_MODS];

/* A row's action: select that source and rebuild the screen, so the
 * detail pane follows. */
static void cbModScreenSelectSource(void *ctx) {
	ModSrcCtx *sc = (ModSrcCtx *)ctx;
	if(!sc || !sc->inst) {
		return;
	}
	modScreenSetSelectedSource(sc->pos);
	refreshModScreen();
}

/* One row in the source list: a button named "MODSRC_<i>" showing the
 * source's name and its type tag. The attenuators are connection
 * internal — they never appear as rows — and the pattern sources are
 * grouped into the one MODSRC_PTN row. */
static GuiNode *appendSourceRow(GuiNode *container, Instrument *inst, int idx) {
	Mod *mod = modSourceAt(inst, idx);
	if(!isSelectableSource(mod)) {
		return NULL;
	}
	char name[MOD_NAME_LEN];
	snprintf(name, sizeof(name), "MODSRC_%d", idx);
	g_modSrcCtx[idx].inst = inst;
	g_modSrcCtx[idx].pos = idx;
	GuiNode *btn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, name, 0,
	                                      cbModScreenSelectSource, &g_modSrcCtx[idx]);
	if(!btn) {
		return NULL;
	}
	guiNodeSetText(btn, mod->name);
	guiNodeSetSublabel(btn, modTypeTag(mod->type));
	appendItem(container, btn, 1);
	return btn;
}

/* The grouped pattern row: the four fixed pattern tracks share one row,
 * with a button per track. Built like appendPatternTrackRow in
 * gui_inst_mod.c — the same grouping, the same per-track summary — but
 * the buttons carry the track's name instead of a ROUTE action. */
static void appendPatternRow(GuiNode *container, Instrument *inst) {
	if(!inst || !inst->modList) {
		return;
	}
	int firstPat = inst->coreEnvelopeCount - PATTERN_TRACKS;
	if(firstPat < 0 || modIndexAt(inst->modList, firstPat) < 0) {
		return;
	}
	GuiNode *row = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODSRC_PTN", 0, 0);
	if(!row) {
		return;
	}
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		int mi = modIndexAt(inst->modList, firstPat + t);
		Mod *m = (mi >= 0) ? inst->modList->mods[mi] : NULL;
		char tag[MOD_NAME_LEN];
		snprintf(tag, sizeof(tag), "MODPTN_%d", t + 1);
		GuiNode *btn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, tag, 0, NULL, NULL);
		if(!btn) {
			continue;
		}
		guiNodeSetText(btn, (m && m->name) ? m->name : tag);
		char sub[24];
		patternTrackSummary(m, sub, sizeof(sub));
		guiNodeSetSublabel(btn, sub);
		appendItem(row, btn, 1);
	}
	appendItem(container, row, 1);
}

/* A dial on the instrument's own parameter. The Parameters are the
 * source's own, so an edit here edits the source. */
static void appendDial(GuiNode *container, const char *name, Parameter *p, int weight) {
	if(!container || !p) {
		return;
	}
	GuiNode *dial = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, name, 0, incParameterBaseValue, p);
	if(!dial) {
		return;
	}
	appendItem(container, dial, weight);
}

/* The selected source's own parameter dials: an envelope's ATK/DEC/CRV
 * stages, an LFO's or a random source's RATE/SHAPE. The instrument
 * screen shows the same dials in its source rows; here they are the
 * selected source's detail. */
static void appendSourceDials(GuiNode *container, Mod *mod) {
	if(!container || !mod) {
		return;
	}
	switch(mod->type) {
		case MT_ENV: {
			/* The envelope's stage boxes are shared with the instrument
			 * screen (appendEnvStageDials in gui_inst_internal.h), so
			 * both screens show the same widget. */
			appendEnvStageDials(container, mod, false);
			break;
		}
		case MT_LFO: {
			LfoState *l = &mod->data.lfo;
			appendDial(container, "RATE", l->rate, 8);
			appendDial(container, "SHAPE", l->shape, 8);
			break;
		}
		case MT_RND: {
			RndState *r = &mod->data.rnd;
			appendDial(container, "RATE", r->rate, 8);
			appendDial(container, "SHAPE", r->shape, 8);
			break;
		}
		default:
			break;
	}
}

/* The selected source's routes: one row per route, showing the
 * destination's name and the route's amount. The routes are the
 * instrument's parameters' modulator lists, filtered by the same test
 * the instrument screen uses (connFromSource), so a route that runs
 * through the source's attenuator counts as the source's route. */
static void appendRouteRows(GuiNode *container, Instrument *inst, Mod *src) {
	if(!container || !inst || !inst->paramList || !src) {
		return;
	}
	int idx = 0;
	for(int i = 0; i < inst->paramList->count; i++) {
		Parameter *dest = inst->paramList->params[i];
		if(!dest) {
			continue;
		}
		for(ModConnection *c = dest->modulators; c; c = c->next) {
			if(!connFromSource(c, src)) {
				continue;
			}
			char name[MOD_NAME_LEN];
			snprintf(name, sizeof(name), "MODROUTE_%d", idx);
			GuiNode *row = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, name, 0, NULL, NULL);
			if(!row) {
				continue;
			}
			guiNodeSetText(row, dest->name);
			char amount[24];
			snprintf(amount, sizeof(amount), "%.2f", c->amount ? getParameterValue(c->amount) : 1.0f);
			guiNodeSetSublabel(row, amount);
			appendItem(container, row, 1);
			idx++;
		}
	}
	/* The "+ ROUTE" button: it opens the destination picker, a plain
	 * list of the instrument's routable parameters. Its name stays out
	 * of the "MODROUTE_<i>" row namespace, so counting the rows by
	 * prefix counts the routes and nothing else. */
	GuiNode *add = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MOD_ADDRUTE", 0, cbModScreenOpenPicker, NULL);
	if(add) {
		guiNodeSetText(add, "+ ROUTE");
		appendItem(container, add, 1);
	}
	/* The "CLEAR ALL" button: it opens the shared YES/NO confirmation,
	 * and confirming removes every route of this source. Its name is
	 * out of the "MODROUTE_<i>" namespace too, so it is not counted as
	 * a route. */
	GuiNode *clear = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MOD_CLEARALL", 0, cbModScreenClearAll, NULL);
	if(clear) {
		guiNodeSetText(clear, "CLEAR ALL");
		appendItem(container, clear, 1);
	}
}

Graph *createModScreenGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected) {
	/* vm: the voice manager that owns the instrument. The graph itself
	 * needs nothing from it yet — the screen's actions act on the
	 * manager's channel — so it is carried, not read. */
	(void)vm;

	Graph *g = createGraph(na_vertical);
	if(!g || !g->root) {
		return NULL;
	}

	/* The screen shows one instrument at a time, so the selection is
	 * validated against it. */
	g_modScreenInst = inst;

	/* The selected source: the screen keeps its selection across a
	 * rebuild, so a rebuild does not throw the user back to the first
	 * source. A selection that names no source the screen can show (it
	 * is out of range, or it names a pattern source, which share the
	 * one grouped row) falls back to the first source the screen can
	 * show. */
	int count = inst ? modSourceCount(inst->modList) : 0;
	int src = g_modScreenSource;
	if(src < 0 || src >= count || !isSelectableSource(modSourceAt(inst, src))) {
		src = firstSelectableSource(inst);
	}
	g_modScreenSource = src;

	/* The header: the back button and the instrument's name. */
	GuiNode *header = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "MODHEADER", 0, 0);
	if(!header) {
		freeGuiNode(g->root);
		free(g);
		return NULL;
	}
	GuiNode *back = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODBACK", 0, cbModScreenBack, NULL);
	if(back) {
		guiNodeSetText(back, "BACK");
		appendItem(header, back, 1);
	}
	char title[MOD_TITLE_LEN];
	modScreenTitle(title, sizeof(title), inst, channel);
	GuiNode *titleNode = createTextGuiNode(0, 0, 100, 100, title, NULL);
	if(titleNode) {
		/* A text node is created without a name; give it one so the
		 * screen's chrome can be found by name. */
		guiNodeSetName(titleNode, "MODTITLE");
		appendItem(header, titleNode, 4);
	}
	appendItem(g->root, header, 2);

	/* The two panes. */
	GuiNode *body = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "MODBODY", 0, 0);
	GuiNode *list = createScrollContainer(0, 0, 100, 100, MOD_ROW_H, "MODLIST");
	GuiNode *detail = createGuiNode(0, 0, 100, 100, 2, na_vertical, "MODDETAIL", 0, 0);
	if(!body || !list || !detail) {
		if(detail) {
			freeGuiNode(detail);
		}
		if(list) {
			freeGuiNode(list);
		}
		if(body) {
			freeGuiNode(body);
		}
		freeGuiNode(g->root);
		free(g);
		return NULL;
	}
	appendItem(body, list, 2);
	appendItem(body, detail, 3);
	appendItem(g->root, body, 18);

	/* The source list's header: ADD and DEL. The instrument screen's
	 * list carried them, and the design puts them here -- the screen
	 * that shows the sources is where they are acted on. */
	GuiNode *listHdr = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODLISTHDR", 0, 0);
	if(listHdr) {
		GuiNode *add = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODLIST_ADD", 0,
		                                      cbModScreenAddSource, NULL);
		if(add) {
			guiNodeSetText(add, "+ ADD");
			appendItem(listHdr, add, 1);
		}
		GuiNode *del = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODLIST_DEL", 0,
		                                      cbModScreenDeleteSource, NULL);
		if(del) {
			guiNodeSetText(del, "DEL");
			appendItem(listHdr, del, 1);
		}
		appendItem(list, listHdr, 1);
	}

	/* The source list: one row per source, then the grouped pattern
	 * row. */
	GuiNode *selectedRow = NULL;
	for(int i = 0; i < count; i++) {
		GuiNode *row = appendSourceRow(list, inst, i);
		if(row && i == src) {
			selectedRow = row;
		}
	}
	appendPatternRow(list, inst);

	/* The detail pane: the selected source's own dials, then its
	 * routes. */
	Mod *sel = (src >= 0) ? modSourceAt(inst, src) : NULL;
	/* The source's TYPE control, first in the pane. The instrument
	 * screen's source rows carried it, and the rows are gone -- so the
	 * type is changed from here, for the source the screen is showing.
	 */
	if(sel) {
		GuiNode *typeBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MOD_TYPE", 0,
		                                         cbModScreenCycleType, NULL);
		if(typeBtn) {
			guiNodeSetText(typeBtn, modTypeTag(sel->type));
			appendItem(detail, typeBtn, 2);
		}
	}
	appendSourceDials(detail, sel);
	GuiNode *routes = createGuiNode(0, 0, 100, 100, 2, na_vertical, "MODROUTES", 0, 0);
	if(routes) {
		appendRouteRows(routes, inst, sel);
		/* The route list shares the pane with the source's dials: the
		 * dials take a quarter each, the routes the other half. */
		appendItem(detail, routes, 16);
	}

	/* The cursor starts on the selected source's row: the screen opens
	 * on the list, where every interaction begins. */
	if(selected && selectedRow) {
		changeGraphSelection(g, selectedRow);
	}
	return g;
}

/* ---- the screen as a layer ---------------------------------------------- */

/* The screen is a full-graph layer pushed over the instrument screen.
 * The layer stack routes the input to the topmost non-passive layer, so
 * pushing the screen is what makes it modal: the instrument screen's
 * own input (its channel cycling and page navigation) does not fire
 * while the screen is up, because the layer consumes the input stream
 * first (handlePresetUiInput routes to the layer and returns true).
 *
 * The layer's name is "MOD" -- distinct from the stack's other named
 * layers ("ROUTE", "ROUTELINES"), so the stack's name-keyed bookkeeping
 * (the route picker's counters) never fires on the modulation screen.
 *
 * The overlay stack owns the Layer structs: pushLayer copies the Layer
 * into the stack's array and frees the caller's copy, and the array
 * moves when the stack grows or an entry is popped. So the pointer
 * below is a cache, refreshed from the stack (by the layer's name)
 * before every use -- it is never held across a push or a pop. That is
 * also how the module notices that the stack popped the screen's layer
 * itself: the SELECT back gesture pops it without telling anybody. */

/* The screen's layer name on the overlay stack. */
#define MOD_LAYER_NAME "MOD"

/* The screen's layer on the instrument GUI's overlay stack, or NULL
 * when the screen is closed.
 *
 * The handle is refreshed from the stack rather than held across a
 * push or a pop, because the stack owns the Layer structs: pushLayer
 * copies the Layer into the stack's array and frees the caller's copy,
 * the array moves when the stack grows, and popping an entry shifts
 * the ones above it down. The stack also pops the screen's layer
 * itself on the SELECT back gesture, without telling this module --
 * so the only way to know whether the screen is still up is to ask
 * the stack. modScreenLayer() does that, and every use of modLayer
 * goes through it first. */
static Layer *modLayer = NULL;

/* Refresh the screen's layer handle from the overlay stack. */
static void modScreenLayer(void) {
	InstrumentGui *ig = getInstrumentGui();
	modLayer = ig ? findLayerByName(&ig->overlayLayers, MOD_LAYER_NAME) : NULL;
}

/* True while the screen's layer is up. */
bool modScreenIsOpen(void) {
	modScreenLayer();
	return modLayer != NULL;
}

/* Open the modulation screen for the selected instrument: build its
 * graph and push it as a layer over the instrument screen. */
void openModScreen(void) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig || !ig->vm) {
		return;
	}
	Instrument *inst = getSelectedInstInstrument();
	if(!inst) {
		return;
	}
	/* Already open: the screen is one layer at a time, so opening it
	 * again is a no-op rather than a second copy stacked over the
	 * first. */
	if(modScreenIsOpen()) {
		return;
	}
	/* The channel the screen shows: the title names the instrument's
	 * voice type and its channel ("FM1"). The instrument carries the
	 * channel it sits on. */
	int channel = (inst->metaChannel >= 0) ? inst->metaChannel : 0;
	Graph *g = createModScreenGraph(inst, ig->vm, channel, true);
	if(!g) {
		return;
	}
	Layer *layer = createLayer(g, 0, 0, SCREEN_W, SCREEN_H, MOD_LAYER_NAME, true, true);
	if(!layer) {
		/* The layer could not be allocated: the graph is ours still,
		 * and the layer would have owned it. */
		freeGuiNode(g->root);
		free(g);
		return;
	}
	/* The stack takes ownership of the layer -- and of the graph and the
	 * name the layer holds -- so the Layer* must not be used after
	 * this. */
	pushLayer(&ig->overlayLayers, layer);
}

/* Close the modulation screen: pop its layer and free the graph and the
 * name the layer owns. */
void closeModScreen(void) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return;
	}
	modScreenLayer();
	if(!modLayer) {
		return;
	}
	/* Only the topmost non-passive layer can be popped. The screen's
	 * layer is that one whenever the screen is the modal on top --
	 * which is the case whenever the screen's own BACK button can be
	 * pressed. Popping a different layer would destroy that modal's
	 * graph, so the top is checked first. */
	if(topNonPassiveLayer(&ig->overlayLayers) != modLayer) {
		return;
	}
	popTopNonPassiveLayerFree(&ig->overlayLayers);
	modLayer = NULL;
}

/* The header's back button: closes the screen. The layer stack pops a
 * layer on the SELECT back gesture too, so the button is the second way
 * out -- the one the user aims at. */
static void cbModScreenBack(void *ctx) {
	(void)ctx;
	closeModScreen();
}

/* ---- the destination picker --------------------------------------------- */

/* The destinations the picker offers: the instrument's routable
 * parameters, in the instrument graph's order, plus the ones that have
 * no dial of their own (the gain, the per-operator level and pitch).
 *
 * The instrument's OWN graph is walked, not the modulation screen's: the
 * screen covers the dials, but what can be routed into is a property of
 * the instrument, and the screen's graph holds the screen's own dials
 * (the source's stage dials), which are not destinations. */
int modScreenRouteDestinations(const Instrument *inst, Parameter **out, int max) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return 0;
	}
	Graph *instGraph = getSelectedInstGraph();
	if(!instGraph || !instGraph->root) {
		return 0;
	}
	Parameter *params[MAX_PARAMS];
	GuiNode *nodes[MAX_PARAMS];
	Rectangle rects[MAX_PARAMS];
	int n = 0;
	collectRoutableDials(instGraph->root, params, nodes, MAX_PARAMS, &n);
	appendNoDialDests((Instrument *)inst, instGraph->root, params, nodes, rects, MAX_PARAMS, &n);
	if(n <= 0) {
		return 0;
	}
	/* Copy out what the caller asked for. The RETURN is the total
	 * number of destinations -- a caller that passes a NULL `out` (or
	 * a max of 0) is asking how many there are. */
	if(out) {
		int copy = (n < max) ? n : max;
		for(int i = 0; i < copy; i++) {
			out[i] = params[i];
		}
	}
	return n;
}

/* One destination's row: the instrument, the source it routes FROM, and
 * the destination it routes INTO. */
typedef struct {
	Instrument *inst;
	Mod *src;
	Parameter *dest;
} ModDestCtx;

/* The picker's rows' contexts. The picker is a layer over the screen, and
 * its graph is freed when the layer is popped -- so the contexts must
 * outlive it, exactly as the instrument screen's picker does. */
static ModDestCtx g_modDestCtx[MAX_PARAMS];

/* The picker layer's name. Distinct from the stack's other named layers
 * ("MOD", "ROUTE", "ROUTELINES"), so the stack's name-keyed bookkeeping
 * (the route picker's counters) never fires on it. */
#define MOD_PICKER_LAYER_NAME "MODPICKER"

/* Route the source into the destination and close the picker. */
static void cbModScreenRouteToDest(void *ctx) {
	ModDestCtx *dc = (ModDestCtx *)ctx;
	InstrumentGui *ig = getInstrumentGui();
	if(!ig || !dc || !dc->inst || !dc->src || !dc->dest) {
		return;
	}
	/* Pop the picker first: the route mutates the instrument, which
	 * rebuilds the instrument's graph, and the picker layer's graph
	 * would be freed underneath us. */
	Layer *top = topNonPassiveLayer(&ig->overlayLayers);
	if(top && top->name && strcmp(top->name, MOD_PICKER_LAYER_NAME) == 0) {
		popTopNonPassiveLayerFree(&ig->overlayLayers);
	}
	/* The picker TOGGLES: picking a destination that is already routed
	 * removes the route, and picking one that is not adds it. (The
	 * instrument screen's picker did the same, and without it the route
	 * list would show the destination twice and the modulation would be
	 * applied twice -- addModulation never dedupes, and it leaves a
	 * stray attenuator behind.) */
	bool already = false;
	for(ModConnection *c = dc->dest->modulators; c; c = c->next) {
		if(connFromSource(c, dc->src)) {
			already = true;
			break;
		}
	}
	pthread_mutex_lock(&g_audioLock);
	dc->inst->rebuilding = true;
	if(already) {
		removeModulation(dc->inst->paramList, dc->inst->modList, dc->dest, dc->src);
	} else {
		addModulation(dc->inst->paramList, dc->inst->modList, dc->src, dc->dest, 1.0f, MO_ADD);
	}
	if(dc->inst->vm) {
		rebuildVoicesForInstrument(dc->inst->vm, dc->inst);
	}
	dc->inst->rebuilding = false;
	pthread_mutex_unlock(&g_audioLock);
	rebuildInstrumentGraph();
	/* The screen's route list shows the routes, so it has to be rebuilt
	 * to show the one just added (or removed). */
	refreshModScreen();
}

/* Build the destination picker's graph for the screen's selected source:
 * one action button per destination, named "DEST_<i>", each labelled
 * with the destination's name.
 *
 * It is a PLAIN LIST, not the instrument screen's ghost overlay: that
 * one pinned a button over each routable dial's rectangle, so it lined up
 * with the dials behind it. The modulation screen covers those dials, so
 * there is nothing to point at -- and a list of names says the same thing
 * without the geometry.
 *
 * The caller owns the returned graph and frees it with freeGuiNode(). */
Graph *createRoutePickerGraph(Instrument *inst, int channel) {
	(void)channel;
	if(!inst) {
		return NULL;
	}
	/* The picker routes the screen's SELECTED source, so the screen's
	 * graph has to have been built (it is what validates the
	 * selection against the instrument). */
	modScreenInstrument();
	Mod *src = (g_modScreenInst == inst && g_modScreenSource >= 0)
	           ? modSourceAt(inst, g_modScreenSource)
	           : NULL;
	if(!src) {
		return NULL;
	}
	Parameter *params[MAX_PARAMS];
	int n = modScreenRouteDestinations(inst, params, MAX_PARAMS);
	if(n <= 0) {
		return NULL;
	}
	Graph *g = createGraph(na_vertical);
	if(!g || !g->root) {
		return NULL;
	}
	GuiNode *first = NULL;
	for(int i = 0; i < n; i++) {
		char name[32];
		snprintf(name, sizeof(name), "DEST_%d", i);
		g_modDestCtx[i].inst = inst;
		g_modDestCtx[i].src = src;
		g_modDestCtx[i].dest = params[i];
		GuiNode *btn = createActionBtnGuiNode(0, 0, 100, 100, 1, na_horizontal, name, 0,
		                                      cbModScreenRouteToDest, &g_modDestCtx[i]);
		if(!btn) {
			continue;
		}
		guiNodeSetText(btn, params[i]->name);
		appendItem(g->root, btn, 1);
		if(!first) {
			first = btn;
		}
	}
	/* The cursor starts on the first destination: the list is the
	 * screen's whole interface, so the first row is where an
	 * interaction begins. */
	if(first) {
		changeGraphSelection(g, first);
	}
	return g;
}

/* Rebuild the screen's graph in place, so its route list reflects a
 * change made elsewhere -- a route added by the picker, or the routes the
 * clear-all removed. The layer keeps its identity and its place on the
 * stack; only the graph behind it is replaced. */
void refreshModScreen(void) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig || !ig->vm) {
		return;
	}
	modScreenLayer();
	if(!modLayer) {
		return;
	}
	Instrument *inst = modScreenInstrument();
	if(!inst) {
		return;
	}
	int channel = (inst->metaChannel >= 0) ? inst->metaChannel : 0;
	Graph *g = createModScreenGraph(inst, ig->vm, channel, true);
	if(!g) {
		return;
	}
	/* The layer owns the old graph (it was created with ownsGraph), so
	 * the old one is freed here rather than left to the layer's
	 * destructor. */
	if(modLayer->ownsGraph && modLayer->graph) {
		freeGuiNode(modLayer->graph->root);
		free(modLayer->graph);
	}
	modLayer->graph = g;
}

/* The "+ ROUTE" button on the screen's route list: opens the destination
 * picker as a layer over the screen. */
static void cbModScreenOpenPicker(void *ctx) {
	(void)ctx;
	InstrumentGui *ig = getInstrumentGui();
	if(!ig) {
		return;
	}
	Graph *g = createRoutePickerGraph(modScreenInstrument(), 0);
	if(!g) {
		return;
	}
	Layer *layer = createLayer(g, 0, 0, SCREEN_W, SCREEN_H, MOD_PICKER_LAYER_NAME, true, true);
	if(!layer) {
		freeGuiNode(g->root);
		free(g);
		return;
	}
	pushLayer(&ig->overlayLayers, layer);
}

/* The "CLEAR ALL" button on the screen's route list: opens the shared
 * YES/NO confirmation for the source the screen is showing. */
static void cbModScreenClearAll(void *ctx) {
	(void)ctx;
	if(!modScreenInstrument() || g_modScreenSource < 0) {
		return;
	}
	openClearAllForSource(modScreenInstrument(), g_modScreenSource);
}

/* The source list's ADD: adds a runtime source. The instrument screen's
 * ADD button does the same thing, so the action is shared. */
static void cbModScreenAddSource(void *ctx) {
	(void)ctx;
	Instrument *inst = modScreenInstrument();
	if(!inst) {
		return;
	}
	/* The FM's sources are what the runtime sources extend; the other
	 * voice types have no runtime source list of their own. */
	if(inst->voiceType != VOICE_TYPE_FM) {
		return;
	}
	cbAddModSource(inst);
	/* The added source is the last one, so the screen follows it. */
	int count = modSourceCount(inst->modList);
	if(count > 0) {
		modScreenSetSelectedSource(count - 1);
	}
	refreshModScreen();
}

/* The detail pane's TYPE control: cycles the selected source's type and
 * rebuilds the screen, so its parameters follow the new type. */
static void cbModScreenCycleType(void *ctx) {
	(void)ctx;
	if(!modScreenInstrument() || g_modScreenSource < 0) {
		return;
	}
	cycleSourceTypeForSource(modScreenInstrument(), g_modScreenSource);
	refreshModScreen();
}

/* The source list's DEL: opens the shared delete confirmation for the
 * screen's selected source. */
static void cbModScreenDeleteSource(void *ctx) {
	(void)ctx;
	if(!modScreenInstrument() || g_modScreenSource < 0) {
		return;
	}
	openDeleteConfirmForSource(modScreenInstrument(), g_modScreenSource);
}
