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
ModStripGuiNode *createModStripGuiNode(int x, int y, int w, int h, VoiceManager *vm, int channel);
void cbAddModSource(void *ctx);
void syncRouteLinesOverlay(InstrumentGui *ig);

#endif
