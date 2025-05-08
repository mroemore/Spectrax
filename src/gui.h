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

#define MAX_GRAPH_HISTORY 25
#define MAX_BUTTON_ROWS 64
#define MAX_BUTTON_COLS 64
#define MAX_BUTTON_CONTAINER_ROWS 64
#define MAX_BUTTON_CONTAINER_COLS 64
#define OSCILLOSCOPE_HISTORY 1024

typedef void (*CallbackApplicator)(void *self, float value);

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
	Shape shape;
	int updateIndex;
	float data[OSCILLOSCOPE_HISTORY];
	Color *backgroundColour;
	Color *waveformColour;
	Color *lineColour;
} OscilloscopeGui;

typedef struct {
	Drawable base;
	Sequencer *sequencer;
	PatternList *pattern_list;
	int *selected_pattern_index;
	int *selected_note_index;
	Shape shape;
	int padding;
	int border_size;
	int pads_per_col;
	Color outline_colour;
	Color playing_fill_colour;
	Color default_fill_colour;
} SequencerGui;

typedef struct {
	Drawable base;
	float *target;
	char *name;
	int index;
	float min;
	float max;
	Shape shape;
	int padding;
	int margin;
	int history_size;
	int history[MAX_GRAPH_HISTORY];
} GraphGui;

typedef struct {
	Drawable **drawables;
	size_t size;
	size_t capacity;
} DrawableList;

typedef struct {
	Drawable base;
	Shape shape;
	int iconx;
	int icony;
	int grid_padding;
	int border_size;
	Color cellColour;
	Color textColour;
	Arranger *arranger;
	PatternList *patternList;
} ArrangerGui;

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
	Drawable base;
	Shape shape;
	SpriteSheet *icons;
	int *playing;
	int *tempo;
	Arranger *arranger;
} TransportGui;

typedef struct {
	Drawable base;
	Shape shape;
	Envelope *env;
	int *graphData;
} EnvelopeGui;

typedef struct {
	Drawable base;
	Shape shape;
	int selected;
	CallbackApplicator applyCallback;
	Parameter *parameter;
	Color backgroundColour;
	Color selectedColour;
	Color textColour;
	char *buttonText;
} ButtonGui;

typedef struct {
	Drawable base;
	Shape shape;
	Parameter *algorithm;
	Color backgroundColour;
	Color graphColour;
} AlgoGraphGui;

typedef struct {
	ButtonGui *buttonRefs[MAX_BUTTON_ROWS][MAX_BUTTON_COLS];
	Drawable *otherDrawables[MAX_BUTTON_ROWS];
	Shape containerBounds;
	int otherDrawableCount;
	int inputCount;
	int rowCount;
	int inputPadding;
	int columnCount[MAX_BUTTON_CONTAINER_COLS];
	int selectedRow;
	int selectedColumn;
} InputContainer;

typedef struct {
	InputContainer *containerRefs[MAX_BUTTON_CONTAINER_ROWS][MAX_BUTTON_CONTAINER_COLS];
	int rowCount;
	int columnCount[MAX_BUTTON_CONTAINER_COLS];
	int selectedRow;
	int selectedColumn;
} ContainerGroup;

typedef struct {
	InputContainer *envInputs;
	EnvelopeGui *envelopeGui;
} EnvelopeContainer;

typedef struct {
	Graph *instrumentScreenGraphs[MAX_SEQUENCER_CHANNELS];
	int instrumentCount;
	int *selectedInstrument;
	Shape shape;
} InstrumentGui;

typedef struct {
	Graph *instrumentScreenGraphs[MAX_SEQUENCER_CHANNELS];
	int instrumentCount;
	int *selectedInstrument;
	Shape shape;
} ArrangerGraph;

void createArrangerGraph(Arranger *a, PatternList *pl);
void navigateArrangerGraph(int keymapping);
void arrangerGraphControlInput(int keymapping);
void createInstrumentGui(VoiceManager *vm, int *selectedInstrument, int scene);
Graph *getSelectedInstGraph();
EnvelopeContainer *createADEnvelopeContainer(Envelope *env, int x, int y, int w, int h, int scene, int enabled);
EnvelopeContainer *createADSREnvelopeContainer(Envelope *env, int x, int y, int w, int h, int scene, int enabled);
void freeEnvelopeContainer(EnvelopeContainer *ec);
ContainerGroup *createInstrumentModulationGui(Instrument *inst, int x, int y, int contW, int contH, int scene, int enabled);
InputContainer *createFmParamsContainer(Instrument *inst, int x, int y, int w, int h, int scene, int enabled);
InputContainer *createBlepParamsContainer(Instrument *inst, int x, int y, int w, int h, int scene, int enabled);
InputContainer *createSampleParamsContainer(Instrument *inst, int x, int y, int w, int h, int scene, int enabled);

typedef struct {
	Drawable base;
	int x;
	int y;
	InputState *inputState;
} InputsGui;

InputsGui *createInputsGui(InputState *inputState, int x, int y);
void drawInputsGui(void *self);
EnvelopeContainer *createEnvelopeContainer(Envelope *env, int x, int y, int w, int h);

void initDefaultColourScheme(ColourScheme *colourScheme);
void setColourScheme(ColourScheme *colourScheme);
Color **getColorSchemeAsPointerArray();
ColourScheme *getColourScheme();

DrawableList *create_drawable_list();
void free_drawable_list(DrawableList *list);
void add_drawable(Drawable *drawable, int scene);
void removeDrawable(Drawable *drawable, int scene);

// TransportGui *createTransportGui(int *playing, Arranger *arranger, int x, int y);
SequencerGui *createSequencerGui(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedNote, int x, int y);
GraphGui *createGraphGui(float *target, char *name, float min, float max, int x, int y, int h, int size);
ArrangerGui *createArrangerGui(Arranger *arranger, PatternList *patternList);
SongMinimapGui *createSongMinimapGui(Arranger *arranger, int *songIndex, int x, int y);
EnvelopeGui *createEnvelopeGui(Envelope *env, int x, int y, int w, int h);
OscilloscopeGui *createOscilloscopeGui(int x, int y, int w, int h);
AlgoGraphGui *createAlgoGraphGui(Parameter *algorithm, int x, int y, int w, int h);

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
Graph *createInstGraph(Instrument *inst, bool selected);

ContainerGroup *createContainerGroup();
InputContainer *createInputContainer();
ContainerGroup *createModMappingGroup(ParamList *paramList, Mod *mod, int x, int y, int scene, int enabled);
ButtonGui *createButtonGui(int x, int y, int w, int h, char *text, Parameter *param, void *callback);
void addContainerToGroup(ContainerGroup *cg, InputContainer *ic, int row, int col);
void removeContainerGroup(ContainerGroup *cg, int scene);
void containerGroupNavigate(ContainerGroup *cg, int rowInc, int colInc);
void removeButtonFromContainer(ButtonGui *btnGui, InputContainer *btnCont, Scene scene);
void addButtonToContainer(ButtonGui *btnGui, InputContainer *btnCont, int row, int col, int scene, int enabled);
void addDrawableToContainer(InputContainer *ic, Drawable *d);
ButtonGui *getSelectedInput(ContainerGroup *cg);
void drawButtonGui(void *self);
void applyButtonCallback(void *self, float value);

void clearBg();
void drawOscilloscopeGui(void *self);
void drawTransportGui(void *self);
void drawSequencerGui(void *self);
void drawGraphGui(void *self);
void drawArrangerGui(void *self);
void drawArrangerGuiNode(void *self);
void drawSongMinimapGui(void *self);
void drawEnvelopeGui(void *self);
void drawAlgoGraphGui(void *self);
void updateOscilloscopeGui(OscilloscopeGui *og, float *data, int length);
void updateGraphGui(GraphGui *graphGui);
void InitGUI(void);
void DrawGUI(int currentScene);
void CleanupGUI(void);
#endif // GUI_H
