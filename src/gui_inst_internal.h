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
