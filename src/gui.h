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

struct VoiceManager;

typedef struct {
	Graph *instrumentScreenGraphs[MAX_SEQUENCER_CHANNELS];
	int instrumentCount;
	int *selectedInstrument;
	Shape shape;
	struct VoiceManager *vm;
} InstrumentGui;

void createArrangerGraph(Arranger *a, PatternList *pl);
void navigateArrangerGraph(int keymapping);
void arrangerGraphControlInput(int keymapping);
void createPatternGraph(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedStep);
void navigatePatternGraph(int keymapping);
void rebuildPatternGraph();
void setSongMinimapGui(SongMinimapGui *smg);
void setPatternBufferScroller(BufferScroller *bs);
void setArrangerMixRing(MixRing *r);
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

typedef struct {
	GuiNode base;
	Instrument *inst;
	char name[33];
	int cursor;
	bool editing;
} PresetNameGuiNode;

GuiNode *createDialGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback cb, Parameter *p);
GuiNode *createActionBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, ActionCallback cb, void *ctx);
void drawActionBtnGuiNode(void *self);
void printArrGraph();
GuiNode *createPresetNameGuiNode(int x, int y, int w, int h, Instrument *inst, bool selected);
bool isPresetNameNode(GuiNode *n);

/* Task 9: scrollable preset file list. Identified by draw fn pointer
 * via isPresetLoadListNode (same pattern as the name node). The list
 * contents live in a static g_loadList state populated by guiOpenLoadList. */
GuiNode *createPresetLoadListNode(int x, int y, int w, int h);
bool isPresetLoadListNode(GuiNode *n);

/* Task 7: name-entry input handler. Returns true if it consumed the input.
 * Called at the top of the instrument input path in main.c and the harness. */
bool handlePresetUiInput(InputState *is, Instrument *inst);

/* Task 7: SAVE flow entry point. Saves the instrument as a preset under
 * `name` in the user-presets dir. On PRESET_EXISTS this opens the
 * overwrite modal (Task 8) and the actual overwrite save happens later
 * once the user confirms. */
void guiSavePreset(Instrument *inst, const char *name);

/* Task 7: LOAD flow entry point. Task 9 will replace the stub with the
 * load-list UI. For now it just flips a flag. */
void guiOpenLoadList(void);

/* Task 8: overwrite-modal state + API. The modal is the only modal in
 * the app (per the brief), so we keep a tiny enum and a single pending
 * name slot rather than a generalised modal system. */
typedef enum {
	MODAL_NONE,
	MODAL_CONFIRM_OVERWRITE
} ModalState;

/* Open the overwrite modal for `name`. Called by guiSavePreset when the
 * underlying save returns PRESET_EXISTS. The caller passes the live
 * Instrument* through handlePresetUiInput so the modal can re-attempt the
 * save on confirm. */
void guiSetOverwritePending(const char *name);

/* True while any modal is open. Callers in main.c / the harness can use
 * this to short-circuit scene navigation while the modal is up. */
bool guiIsModalOpen(void);

/* True while the preset load-list (Task 9) is open. The scripted harness
 * uses this to assert that the LOAD button activated the list and that
 * START in the list closed it. */
bool guiIsLoadListActive(void);

/* Drawn from DrawGUI's SCENE_INSTRUMENT case while g_modalState != MODAL_NONE. */
void drawPresetModal(void);
SampleWaveformGuiNode *createSampleWaveformGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Instrument *inst, Parameter *loopStart, Parameter *loopEnd);
void drawSampleWaveformGuiNode(void *self);
ArrangerGuiNode *createArrangerGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Arranger *arranger, PatternList *patternList);
bool navigateArrangerGuiNode(void *self, int keymapping);
void drawRotatedDial(int x, int y, int w, int h, int radius, int startAngle, int offsetAngle);
void drawValueDisplay(int x, int y, int w, int h, char *text);
void drawColourRectangle(int x, int y, int w, int h, float roundness, float line_w, bool highlighted);
void drawDialGuiNode(void *self);
void drawWrapperNode(void *self);
void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendSampleInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendBlepInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst);
void appendADEnvControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Envelope *env);
void appendBlankNode(GuiNode *container, int weight);
Graph *createInstGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected);
void addRuntimeEnvelope(Instrument *inst);
void removeRuntimeEnvelope(Instrument *inst, int envIndex);
void rebuildInstrumentGraph(void);
void removeSelectedEnvelope(void);
Instrument *getSelectedInstInstrument(void);

void clearBg();
void drawArrangerGuiNode(void *self);
void drawSongMinimapGui(void *self);
void InitGUI(void);
void DrawGUI(int currentScene);
#endif // GUI_H