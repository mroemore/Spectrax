/* gui_mod_screen.c -- the modulation screen's graph.
 *
 * The screen is two panes: the source list on the left, the selected
 * source's detail on the right. This module builds the graph only — the
 * screen's layer, its input handling and its route picker are the later
 * tasks of the phase. The graph's shape:
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
 * The rows carry no actions yet: the screen is a graph until the layer
 * and the picker land, so every button here is inert (a NULL callback)
 * and the cursor still moves over them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph_gui.h"
#include "gui.h"
#include "gui_internal.h"
#include "gui_inst_internal.h"
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

/* The selected source's index within the instrument's source list, or -1
 * when no source is selected. The selection is screen state: it outlives
 * the graph, so a rebuild does not throw the user back to the first
 * source. */
static int g_modScreenSource = -1;

int modScreenSelectedSource(void) {
	return g_modScreenSource;
}

void modScreenSetSelectedSource(int idx) {
	g_modScreenSource = (idx < 0) ? -1 : idx;
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

/* One row in the source list: a button named "MODSRC_<i>" showing the
 * source's name and its type tag. The attenuators are connection
 * internal — they never appear as rows — and the pattern sources are
 * grouped into the one MODSRC_PTN row. */
static GuiNode *appendSourceRow(GuiNode *container, Instrument *inst, int idx) {
	Mod *mod = NULL;
	if(inst && inst->modList) {
		int mi = modIndexAt(inst->modList, idx);
		mod = (mi >= 0) ? inst->modList->mods[mi] : NULL;
	}
	if(!mod || mod->type == MT_ATTEN || mod->type == MT_PATTERN) {
		return NULL;
	}
	char name[MOD_NAME_LEN];
	snprintf(name, sizeof(name), "MODSRC_%d", idx);
	GuiNode *btn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, name, 0, NULL, NULL);
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
		if(m && m->type == MT_PATTERN) {
			PatternState *p = &m->data.pattern;
			int len = p->length ? getParameterValueAsInt(p->length) : p->stepCount;
			int shape = p->shape ? getParameterValueAsInt(p->shape) : SH_HOLD;
			snprintf(sub, sizeof(sub), "L%d %s", len, patternShapeAbbrev(shape));
		} else {
			snprintf(sub, sizeof(sub), "--");
		}
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
			EnvState *e = &mod->data.env;
			static const char *stageName[2] = {
				"ATK",
				"DEC",
			};
			for(int s = 0; s < 2; s++) {
				/* Each stage keeps its rate dial and its curve dial
				 * together, the way the instrument screen's env
				 * stage box does. */
				GuiNode *stage = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "ENV_STAGE", 0, 0);
				if(!stage) {
					continue;
				}
				appendDial(stage, stageName[s], e->stages[s].duration, 2);
				appendDial(stage, "CRV", e->stages[s].curvature, 1);
				applyLayout(stage, "env-stage");
				appendItem(container, stage, 2);
			}
			break;
		}
		case MT_LFO: {
			LfoState *l = &mod->data.lfo;
			appendDial(container, "RATE", l->rate, 2);
			appendDial(container, "SHAPE", l->shape, 2);
			break;
		}
		case MT_RND: {
			RndState *r = &mod->data.rnd;
			appendDial(container, "RATE", r->rate, 2);
			appendDial(container, "SHAPE", r->shape, 2);
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
	/* The "+ ROUTE" button: the destination picker is a later task, so
	 * the button carries no action yet. Its name stays out of the
	 * "MODROUTE_<i>" row namespace, so counting the rows by prefix
	 * counts the routes and nothing else. */
	GuiNode *add = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MOD_ADDRUTE", 0, NULL, NULL);
	if(add) {
		guiNodeSetText(add, "+ ROUTE");
		appendItem(container, add, 1);
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

	/* The selected source: the screen keeps its selection across a
	 * rebuild, so a rebuild does not throw the user back to the first
	 * source. A selection that no longer exists falls back to the first
	 * source (or to none, when the instrument has no sources). */
	int count = inst ? modSourceCount(inst->modList) : 0;
	int src = g_modScreenSource;
	if(src < 0 || src >= count) {
		src = count > 0 ? 0 : -1;
	}
	g_modScreenSource = src;

	/* The header: the back button and the instrument's name. */
	GuiNode *header = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "MODHEADER", 0, 0);
	if(!header) {
		freeGuiNode(g->root);
		free(g);
		return NULL;
	}
	GuiNode *back = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODBACK", 0, NULL, NULL);
	if(back) {
		guiNodeSetText(back, "BACK");
		appendItem(header, back, 1);
	}
	char title[MOD_TITLE_LEN];
	modScreenTitle(title, sizeof(title), inst, channel);
	GuiNode *titleNode = createTextGuiNode(0, 0, 100, 100, title, "MODTITLE");
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
	Mod *sel = NULL;
	if(src >= 0 && inst && inst->modList) {
		int mi = modIndexAt(inst->modList, src);
		sel = (mi >= 0) ? inst->modList->mods[mi] : NULL;
	}
	appendSourceDials(detail, sel);
	GuiNode *routes = createGuiNode(0, 0, 100, 100, 2, na_vertical, "MODROUTES", 0, 0);
	if(routes) {
		appendRouteRows(routes, inst, sel);
		appendItem(detail, routes, 8);
	}

	/* The cursor starts on the selected source's row: the screen opens
	 * on the list, where every interaction begins. */
	if(selected && selectedRow) {
		changeGraphSelection(g, selectedRow);
	}
	return g;
}
