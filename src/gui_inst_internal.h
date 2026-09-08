#ifndef GUI_INST_INTERNAL_H
#define GUI_INST_INTERNAL_H

/* Cross-module internals for the instrument-screen split: the shell
 * (gui_instrument.c) builds the graph and calls into the per-type row
 * builders and the mod-source UI. Anything here is private to the
 * gui_inst_* modules; public API lives in gui.h. */

#include "gui.h"

/* shell state shared with the gui_inst_* modules */
extern InstrumentGui *igui;

/* per-voiceType control rows */
void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendSampleInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendBlepInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);

/* mod-source container + strip (gui_inst_mod) */
void appendModSourceEntry(Graph *g, GuiNode *container, Instrument *inst, int idx, int weight, bool selected);
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
/* visibility helper: true when a dial is inside its scroll container's
 * viewport (route/ghost lines must skip dests scrolled out, whose uint16
 * y wraps to a huge value). Exposed for the regression test. */
bool dialVisibleInViewport(GuiNode *dial);
/* boot-path source-ctx refresh (see gui_inst_mod.c) — called once from
 * createInstrumentGui after igui is assigned */
void guiInstRefreshSourceCtx(void);
/* test hook: which source's routes the ROUTELINES overlay currently shows */
int guiRouteLinesSource(void);
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

/* Probe-only: drive the picker exactly once against the first
 * routable dial of the selected instrument. Returns true if the route
 * was applied. Gated behind g_probeRoute from main.c (zero effect
 * when --probe-route is absent; the picker state machine itself is
 * unchanged). */
int probePickFirstRoute(void);

#endif
