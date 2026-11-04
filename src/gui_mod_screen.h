#ifndef SPECTRAX_GUI_MOD_SCREEN_H
#define SPECTRAX_GUI_MOD_SCREEN_H
#include "graph_gui.h"
#include "voice.h"

/* Build the modulation screen's graph for one instrument. The caller owns
 * the returned graph and frees it with
 * freeGuiNode(graph->root); free(graph); */
Graph *createModScreenGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected);

/* The selected source's index within the instrument's source list, or -1.
 * The pattern sources share the one grouped row, so they are not
 * selectable sources: setting one of them is refused and the selection
 * stays where it was. */
int modScreenSelectedSource(void);
void modScreenSetSelectedSource(int idx);
#endif
