#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include "sample.h"
#include "sequencer.h"
#include "modsystem.h"
#include "settings.h"
#include "raylib.h"
#include "input.h"
#include "graph_gui.h"
#include "voice.h"
#include "vizfx.h"

typedef struct {
	int x;
	int y;
	int w;
	int h;
} Shape;

typedef struct {
	Color backgroundColor; // 17, 7, 8
	Color secondaryFontColour;
	Color fontColour;
	Color outlineColour;
	Color defaultCell;
	Color blankCell;
	Color highlightedCell;
	Color selectedCell;
	Color reddish;
} ColourScheme;

typedef struct {
	Texture2D sheet;
	int spriteCount;
	int spriteW;
	int spriteH;
	float scale;
	Vector2 origin;
	Rectangle spriteSize;
} SpriteSheet;

SpriteSheet *createSpriteSheet(char *imagePath, int sprite_w, int sprite_h);
void drawSprite(SpriteSheet *spriteSheet, int index, int x, int y, int w, int h);

typedef struct {
	DrawCallback draw;
	int enabled;
	OnPressCallback onPress;
} Drawable;

typedef struct {
	Drawable base;
	Arranger *arranger;
	Shape shape;
	int padding;
	int maxMapLength;
	int *songIndex;
	Color defaultCellColour;
	Color blankCellColour;
	Color selectedCellColour;
	Color playingCellColour;
} SongMinimapGui;

typedef struct {
	Graph *instrumentScreenGraphs[MAX_SEQUENCER_CHANNELS];
	int instrumentCount;
	int *selectedInstrument;
	Shape shape;
} InstrumentGui;

void createArrangerGraph(Arranger *a, PatternList *pl);
void navigateArrangerGraph(int keymapping);
void arrangerGraphControlInput(int keymapping);
void createPatternGraph(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedStep);
void navigatePatternGraph(int keymapping);
void rebuildPatternGraph();
void setSongMinimapGui(SongMinimapGui *smg);
void setPatternBufferScroller(BufferScroller *bs);
void drawStepGuiNode(void *self);
void createInstrumentGui(VoiceManager *vm, int *selectedInstrument, int scene);
Graph *getSelectedInstGraph();

void initDefaultColourScheme(ColourScheme *colourScheme);
void setColourScheme(ColourScheme *colourScheme);
Color **getColorSchemeAsPointerArray();
ColourScheme *getColourScheme();

SongMinimapGui *createSongMinimapGui(Arranger *arranger, int *songIndex, int x, int y);

typedef struct {
	GuiNode base;
	Arranger *arranger;
	PatternList *patternList;
	int grid_padding;
	int iconx;
	int icony;
	int border_size;
} ArrangerGuiNode;

typedef struct {
	GuiNode base;
	Instrument *instrument;
	Parameter *loopStart;
	Parameter *loopEnd;
	Color bgColour;
	Color wfColour;
	Color wfAltColour;
	Image wfImage;
} SampleWaveformGuiNode;

typedef struct {
	GuiNode base;
	ModStrip strip;
} ModStripGuiNode;

GuiNode *createBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback callback, Parameter *p);
void printArrGraph();
SampleWaveformGuiNode *createSampleWaveformGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Instrument *inst, Parameter *loopStart, Parameter *loopEnd);
void drawSampleWaveformGuiNode(void *self);
ArrangerGuiNode *createArrangerGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Arranger *arranger, PatternList *patternList);
bool navigateArrangerGuiNode(void *self, int keymapping);
void drawRotatedDial(int x, int y, int w, int h, int radius, int startAngle, int offsetAngle);
void drawValueDisplay(int x, int y, int w, int h, char *text);
void drawColourRectangle(int x, int y, int w, int h, float roundness, float line_w, bool highlighted);
void drawDialGuiNode(void *self);
void drawBtnGuiNode(void *self);
void drawWrapperNode(void *self);
void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendSampleInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendBlepInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendADEnvControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Envelope *env);
void appendBlankNode(GuiNode *container, int weight);
Graph *createInstGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected);

void clearBg();
void drawArrangerGuiNode(void *self);
void drawSongMinimapGui(void *self);
void InitGUI(void);
void DrawGUI(int currentScene);
#endif // GUI_H