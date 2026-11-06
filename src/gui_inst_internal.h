#ifndef GUI_INST_INTERNAL_H
#define GUI_INST_INTERNAL_H

/* Cross-module internals for the instrument-screen split: the shell
 * (gui_instrument.c) builds the graph and calls into the per-type row
 * builders and the mod-source UI. Anything here is private to the
 * gui_inst_* modules; public API lives in gui.h. The modulation screen
 * (gui_mod_screen.c) shares the mod-source helpers below with the
 * instrument screen's mod-source UI. */

#include <stdio.h>
#include "gui.h"
#include "gui_internal.h"
#include "gui_style.h"

/* A connection is "from src" when its source IS src, or when it routes
 * through src's attenuator (MT_ATTEN with input == src). Line drawing,
 * route matching and erase/toggle detection must follow the attenuator.
 * Shared: the instrument screen's mod-source UI and the modulation
 * screen both match a route back to its source. */
static inline bool connFromSource(ModConnection *c, Mod *src) {
	if(!c || !src) {
		return false;
	}
	if(c->source == src) {
		return true;
	}
	return c->source && c->source->type == MT_ATTEN && c->source->data.atten.input == src;
}

/* The source at a source position (0-based over the instrument's mod
 * sources), or NULL. The position is the index space the modulation
 * screen's selection uses (modScreenSetSelectedSource), so the line on
 * the instrument screen and the screen itself agree on what "source 2"
 * means. Shared: the modulation screen's source list and the
 * instrument screen's modulation line. */
static inline Mod *modSourceAtPos(const Instrument *inst, int pos) {
	if(!inst || !inst->modList) {
		return NULL;
	}
	int mi = modIndexAt(inst->modList, pos);
	return (mi >= 0) ? inst->modList->mods[mi] : NULL;
}

/* The display tag for a source's type. Shared with the modulation
 * screen, whose source rows show the tag next to the source's name. */
static inline const char *modTypeTag(ModType t) {
	switch(t) {
		case MT_LFO: return "LFO";
		case MT_RND: return "RND";
		case MT_PATTERN: return "PTN";
		case MT_ATTEN: return "ATTN";
		default:     return "ENV";
	}
}

/* The pattern track's shape, abbreviated for the row's sublabel (the
 * length+shape summary). Kept short so it fits next to the track's
 * name without overflowing the cell. */
static inline const char *patternShapeAbbrev(int shape) {
	switch(shape) {
		case SH_HOLD:   return "HLD";
		case SH_LINEAR: return "LIN";
		case SH_SLEW:   return "SLEW";
		case SH_CURVE:  return "CUR";
		default:        return "?";
	}
}

/* A pattern track's summary for a row's sublabel: its length and its
 * shape ("L4 HLD"), or "--" when the track has no pattern mod.
 * Shared: the instrument screen's pattern row and the modulation
 * screen's pattern row must show the same summary. */
static inline void patternTrackSummary(Mod *m, char *buf, size_t len) {
	if(!buf || len == 0) {
		return;
	}
	if(m && m->type == MT_PATTERN) {
		PatternState *p = &m->data.pattern;
		int trackLen = p->length ? getParameterValueAsInt(p->length) : p->stepCount;
		int shape = p->shape ? getParameterValueAsInt(p->shape) : SH_HOLD;
		snprintf(buf, len, "L%d %s", trackLen, patternShapeAbbrev(shape));
	} else {
		snprintf(buf, len, "--");
	}
}

/* An envelope source's own stage dials: one box per stage, holding the
 * stage's rate dial ("ATK" / "DEC") and its small curve dial ("CRV").
 * The box is the app's styled wrapper (drawWrapperNode), so both
 * screens show the same widget.
 *
 * `selected` marks the first stage's rate dial as the cursor's target,
 * the way the instrument screen's source row does. The stage boxes are
 * appended with weight 8: the instrument screen's row layout overrides
 * it, and the modulation screen's detail pane lays them out by it.
 *
 * Shared: the instrument screen's mod-source row and the modulation
 * screen's detail pane. */
static inline void appendEnvStageDials(GuiNode *container, Mod *mod, bool selected) {
	if(!container || !mod || mod->type != MT_ENV) {
		return;
	}
	EnvState *e = &mod->data.env;
	static const struct {
		const char *rateName;
	} stageCfg[2] = {
		{ "ATK" },
		{ "DEC" },
	};
	for(int s = 0; s < 2; s++) {
		GuiNode *stageBox = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "ENV_STAGE", 0, 0);
		if(!stageBox) {
			continue;
		}
		stageBox->drawable = true;
		stageBox->draw = drawWrapperNode;
		GuiNode *rateDial = createDialGuiNode(0, 0, 100, 100, 0,
		                                      na_horizontal,
		                                      stageCfg[s].rateName,
		                                      s == 0 ? selected : 0,
		                                      incParameterBaseValue,
		                                      e->stages[s].duration);
		GuiNode *curveDial = createDialGuiNode(0, 0, 100, 100, 0,
		                                       na_horizontal,
		                                       "CRV", 0,
		                                       incParameterBaseValue,
		                                       e->stages[s].curvature);
		if(rateDial) {
			appendItem(stageBox, rateDial, 2);
		}
		if(curveDial) {
			appendItem(stageBox, curveDial, 1);
		}
		applyLayout(stageBox, "env-stage");
		appendItem(container, stageBox, 8);
	}
}

/* The routable dials in a graph, with the nodes they live on. A routable
 * dial is a node whose draw is a dial's and whose Parameter pointer is
 * set. The count is written through *n.
 *
 * Shared: the instrument screen's route picker and the modulation
 * screen's destination picker both list what can be routed into. */
void collectRoutableDials(GuiNode *node, Parameter **outParams, GuiNode **outNodes, int cap, int *n);

/* The routable destinations that have NO dial of their own -- the
 * instrument's gain and the FM operators' level and pitch. They are
 * appended to the dial list, so a caller that lists destinations gets
 * all of them. `rects` receives a fallback rectangle for each (the
 * picker pins its buttons to them); the modulation screen's plain list
 * ignores the rectangles.
 *
 * Shared: the same two callers. */
void appendNoDialDests(Instrument *inst, GuiNode *instGraphRoot,
                       Parameter **params, GuiNode **nodes, Rectangle *rects,
                       int cap, int *n);

/* shell state shared with the gui_inst_* modules */
extern InstrumentGui *igui;

/* per-voiceType control rows */
void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendSampleInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendBlepInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);

/* mod-source container + strip (gui_inst_mod) */
void appendModSourceEntry(Graph *g, GuiNode *container, Instrument *inst, int idx, int weight, bool selected);
void appendPatternTrackRow(GuiNode *container, Instrument *inst, int weight);
void syncModWrapScroll(void);
void syncPickerDestRects(void);
void syncPickerBaseScroll(void);
void guiPickerFrameSync(void);
void guiPickerUpdateDeferred(void);
void guiPickerEditorDraw(void);
bool guiPickerEditorInput(InputState *is);
bool guiPickerHeldAmountAdjust(InputState *is);
void cbOpenRouteLayer(void *ctx);
void cbOpenClearAllLayer(void *ctx);
/* Open the shared clear-all confirmation for a source position. The
 * modulation screen's CLEAR ALL button and the instrument screen's
 * FUNCTION+EDIT on a ROUTE button share it. */
void openClearAllForSource(Instrument *inst, int srcIdx);
/* Open the shared delete confirmation for a source position. The
 * modulation screen's DEL and the instrument screen's per-row DEL
 * button share it. */
void openDeleteConfirmForSource(Instrument *inst, int srcIdx);
/* Cycle a source's type (ENV -> LFO -> RND -> ENV). The modulation
 * screen's detail pane carries it for the source it is showing. */
void cycleSourceTypeForSource(Instrument *inst, int srcIdx);
/* Task 2: the action that opens the modulation screen (gui_mod_screen.c)
 * as a layer over the instrument screen. A button on the instrument
 * screen carries it, and the instrument screen's action dispatch fires
 * it on EDIT. */
void cbOpenModScreen(void *ctx);

/* ---- the modulation line (Task 4) ---------------------------------------- */

/* The instrument's sources that have at least one route, in source order.
 * These are the sources the modulation line cycles through: a source with
 * no route is not something the user can adjust, so it does not belong on
 * the line.
 *
 * Returns the number of connected sources; writes up to `max` of them
 * into `out`. A NULL `out` counts without writing. */
int modConnectedSources(const Instrument *inst, Mod **out, int max);

/* The routes of one source: the connections whose source is `src` (or
 * which run through src's attenuator -- see connFromSource). Returns the
 * number of routes; writes up to `max` of them into `out`. A NULL `out`
 * counts without writing. */
int modSourceRoutes(const Instrument *inst, Mod *src, ModConnection **out, int max);

/* The modulation line: the instrument screen's one-row slot that cycles
 * through the connected modulators and opens the modulation screen.
 *
 * It is a dial-shaped node: LEFT and RIGHT change the index rather than
 * moving the cursor (the line has no horizontal neighbour to move to), and
 * EDIT opens the modulation screen on the source the line is showing.
 *
 * `index` is the index into the connected-sources list, clamped to
 * `modConnectedSources(...) - 1`. */
typedef struct {
	GuiNode base;
	Instrument *inst;
	int index;
} ModLineGuiNode;

/* Append the line to the instrument screen's layout. */
GuiNode *appendModLineNode(Graph *g, GuiNode *container, Instrument *inst, int weight, bool selected);

/* The line's selected index, for the test hook and the harness probe. */
int modLineIndex(const GuiNode *n);

/* The modulation line's node in a graph, or NULL. */
GuiNode *findModLineNode(GuiNode *root);

/* The source POSITION of the connected modulator the line is showing, or
 * -1 when nothing is connected. The position is the index space the
 * modulation screen's selection uses. */
int modLineSourcePos(const ModLineGuiNode *ml);
ModStripGuiNode *createModStripGuiNode(int x, int y, int w, int h, VoiceManager *vm, int channel);
void cbAddModSource(void *ctx);
void syncRouteLinesOverlay(InstrumentGui *ig);
/* FUNCTION+arrow page navigation: skip to the group/row edge, then wrap
 * to the next group/row. dir is a KM_* code (KM_UP/DOWN/LEFT/RIGHT). */
void instrumentPageNav(int dir);
/* visibility helper: true when a dial is inside its scroll container's
 * viewport (route/ghost lines must skip dests scrolled out, whose uint16
 * y wraps to a huge value). Exposed for the regression test. */
bool dialVisibleInViewport(GuiNode *dial);
/* boot-path source-ctx refresh (see gui_inst_mod.c) — called once from
 * createInstrumentGui after igui is assigned */
void guiInstRefreshSourceCtx(void);
/* test hook: which source's routes the ROUTELINES overlay currently shows */
int guiRouteLinesSource(void);
/* test hook: the selected instrument's waveform view mode (WF_VIEW_*), or
 * -1 when the sampler panel isn't built. */
int guiWaveformViewMode(void);
/* find a source's ROUTE button by source position (its actionCtx is the
 * &g_sourceCtx[idx] slot). Exposed for the route-lines regression test. */
GuiNode *findSourceRouteNode(GuiNode *n, int srcIdx);

/* Spec #3: picker bookkeeping. syncRouteLinesOverlay decides whether
 * to push the gradient overlay based on g_routePickerCount, not on a
 * hover, so the picker and overlay stay in sync across edits. */
extern int g_routePickerCount;

/* Spec #6: erase-mode flag. Toggled by the ROUTE input handler when
 * the user requests destructive routing; cbRouteToDest reads it to
 * pick addModulation vs removeModulationForSource. */
extern bool g_routeErase;
extern bool g_pickerEditHeld;

/* Spec #6 (public toggle). Called from the topmost ROUTE layer's input
 * handler. Also exposed so scripted tests can drive erase without
 * touching internals. */
void guiSetRouteEraseMode(bool on);
void guiSetPickerEditHeld(bool on);
bool guiRouteEraseMode(void);
/* Resets every piece of picker state (atten editor, pending double-tap). Every
 * path that closes a picker must call it. */
void guiPickerClosed(void);

/* Probe-only: drive the picker exactly once against the first
 * routable dial of the selected instrument. Returns true if the route
 * was applied. Gated behind g_probeRoute from main.c (zero effect
 * when --probe-route is absent; the picker state machine itself is
 * unchanged). */
int probePickFirstRoute(void);

#endif
