#ifndef SPECTRAX_GUI_MOD_SCREEN_H
#define SPECTRAX_GUI_MOD_SCREEN_H
#include "graph_gui.h"
#include "voice.h"

/* Build the modulation screen's graph for one instrument. The caller owns
 * the returned graph and frees it with freeGuiNode(graph->root). */
Graph *createModScreenGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected);

/* The selected source's index within the instrument's source list, or -1. */
int modScreenSelectedSource(void);
void modScreenSetSelectedSource(int idx);
#endif
