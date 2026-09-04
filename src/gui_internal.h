#ifndef GUI_INTERNAL_H
#define GUI_INTERNAL_H

/* Cross-module internals for the gui split. Anything here is shared between
 * gui_core.c and the per-screen modules but is NOT public API (use gui.h). */

#include "gui.h"
#include "graph_gui.h"
#include "modsystem.h"

/* shared state (defined in gui_core.c) */
extern ColourScheme cs;
extern Font pixelFont;
extern Font symbolFont;
extern Font textFont;
extern Texture2D dial;

/* core node helpers shared by the screen modules (defined in gui_core.c) */
void drawColourRectangle(int x, int y, int w, int h, float roundness, float line_w, bool highlighted);
void drawWrapperNode(void *self);
void drawValueDisplay(int x, int y, int w, int h, char *text);
void drawRotatedDial(int x, int y, int w, int h, int radius, int startAngle, int offsetAngle);
GuiNode *createDialGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback cb, Parameter *p);
GuiNode *createActionBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, ActionCallback cb, void *ctx);
void drawActionBtnGuiNode(void *self);
void drawDiscreteDialGuiNode(void *self);
const char *voiceTypeTag(VoiceType t);

/* shared screen state (owners noted) */
extern Arranger *g_arranger;        /* gui_arranger.c */
extern MixRing *arrangerMixRing;    /* gui_instrument.c */
extern const Color chipPalette[8];  /* gui_instrument.c */
void drawSampleWaveLinesNode(void *self);       /* gui_instrument.c */
void drawSampleWavePolylineNode(void *self);    /* gui_instrument.c */

/* per-screen draw entry points (called from DrawGUI in gui_core.c) */
void gui_arranger_draw(void);
void gui_pattern_draw(void);
void gui_instrument_draw(void);

#endif
