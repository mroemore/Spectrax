#ifndef SPECTRAX_GUI_MOD_SCREEN_H
#define SPECTRAX_GUI_MOD_SCREEN_H
#include <stdbool.h>
#include "graph_gui.h"
#include "voice.h"

/* Build the modulation screen's graph for one instrument. The caller owns
 * the returned graph and frees it with
 * freeGuiNode(graph->root); free(graph); */
Graph *createModScreenGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected);

/* The selected source's index within the instrument's source list, or -1.
 * The pattern sources share the one grouped row, so they are not
 * selectable sources: setting one of them is refused and the selection
 * stays where it was.
 *
 * The selection is screen state, not graph state: it is read when the
 * screen's graph is BUILT. So set the selection BEFORE opening the
 * screen -- openModScreen() builds the graph from the selection as it
 * stands then, and the detail pane follows the source that was
 * selected at that moment. */
int modScreenSelectedSource(void);
void modScreenSetSelectedSource(int idx);

/* The screen as a layer over the instrument screen.
 *
 * The screen is a full-graph layer pushed onto the instrument GUI's
 * overlay stack, so the layer stack routes the input to it while it is
 * up and the instrument screen's own input does not fire. The layer is
 * named "MOD", which is distinct from the layer stack's other special
 * cases ("ROUTE", "ROUTELINES"). */

/* True while the screen's layer is up. */
bool modScreenIsOpen(void);

/* Build the screen's graph for the selected instrument and push it as a
 * layer over the instrument screen.
 *
 * The graph is built from the selection as it stands at this moment, so
 * a caller that wants the screen to open on a particular source sets
 * the selection first (modScreenSetSelectedSource) and opens second. */
void openModScreen(void);

/* Pop the screen's layer and free it. The layer stack's own SELECT
 * gesture (the universal back) also closes the screen. */
void closeModScreen(void);
#endif
