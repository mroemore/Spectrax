#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include "sequencer.h"
#include "modsystem.h"
#include "raylib.h"
#include "input.h"

#define MAX_GRAPH_HISTORY 25
#define SCREEN_W 800
#define SCREEN_H 600
#define MAX_BUTTON_ROWS 64
#define MAX_BUTTON_COLS 64
#define MAX_BUTTON_CONTAINER_ROWS 64
#define MAX_BUTTON_CONTAINER_COLS 64
#define OSCILLOSCOPE_HISTORY 1024

typedef enum {
	GLOBAL,
	SCENE_ARRANGER,
	SCENE_PATTERN,
	SCENE_INSTRUMENT,
	SCENE_COUNT
} Scene;

typedef void (*DrawCallback)(void* self);
typedef void (*OnPressCallback)(Parameter* parameter, float value);
typedef void (*CallbackApplicator)(void* self, float value);




typedef struct {
	Color backgroundColor; //17, 7, 8
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
	DrawCallback draw;
	OnPressCallback onPress;
} Drawable;

typedef struct {
	Drawable base;
	Sequencer* sequencer;
	PatternList* pattern_list;
	int *selected_pattern_index;
	int *selected_note_index;
	int x;
	int y;
	int pad_w;
	int pad_h;
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
	int x;
	int y;
	int w;
	int h;
	int padding;
	int margin;
	int history_size;
	int history[MAX_GRAPH_HISTORY];	
} GraphGui;

typedef struct {
	Drawable** drawables;
	size_t size;
	size_t capacity;
} DrawableList;

typedef struct {
	Drawable base;
	int x;
	int y;
	int w;
	int h;
	int grid_padding;
	int border_size;
	Color cellColour;
	Color textColour;
	Arranger* arranger;
	PatternList* patternList;
} ArrangerGui;

typedef struct {
	Drawable base;
	Arranger* arranger;
	int x;
	int y;
	int w;
	int h;
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
	int x;
	int y;
	int *playing;
	int *tempo;
	Arranger *arranger;
} TransportGui;

typedef struct {
	Drawable base;
	int x;
	int y;
	int w;
	int h;
	Envelope* env;
	int* graphData;
} EnvelopeGui;

typedef struct {
	Drawable base;
	int x;
	int y;
	int w;
	int h;
	int selected;
	CallbackApplicator applyCallback;
	Parameter* parameter;
	Color backgroundColour;
	Color selectedColour;
	Color textColour;
	char* buttonText;
} ButtonGui;

typedef struct {
	ButtonGui* buttonRefs[MAX_BUTTON_ROWS][MAX_BUTTON_COLS];
	int rowCount;
	int columnCount[MAX_BUTTON_CONTAINER_COLS];
	int selectedRow;
	int selectedColumn;
} InputContainer;

typedef struct {
	InputContainer* containerRefs[MAX_BUTTON_CONTAINER_ROWS][MAX_BUTTON_CONTAINER_COLS];
	int rowCount;
	int columnCount[MAX_BUTTON_CONTAINER_COLS];
	int selectedRow;
	int selectedColumn;
} ContainerGroup;

typedef struct {
	ContainerGroup* envInputs;
	EnvelopeGui* envelopeGui;
} EnvelopeContainer;

typedef struct {
	Drawable base;
	int x;
	int y;
	InputState* inputState;
} InputsGui;

void initDefaultColourScheme(ColourScheme* colourScheme);
void setColourScheme(ColourScheme* colourScheme);
Color** getColorSchemeAsPointerArray();
ColourScheme* getColourScheme();

DrawableList* create_drawable_list();
void free_drawable_list(DrawableList *list);
void add_drawable(Drawable* drawable, int scene);
void removeDrawable(Drawable* drawable, int scene);

TransportGui *createTransportGui(int *playing, Arranger *arranger, int x, int y);
SequencerGui *createSequencerGui(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedNote, int x, int y);
GraphGui* createGraphGui(float* target, char* name, float min, float max, int x, int y, int h, int size);
ArrangerGui* createArrangerGui(Arranger *arranger, PatternList *patternList, int x, int y);
SongMinimapGui* createSongMinimapGui(Arranger *arranger, int *songIndex, int x, int y);
EnvelopeGui* createEnvelopeGui(Envelope* env, int x, int y, int w, int h);
InputsGui* createInputsGui(InputState* inputState, int x, int y);

ContainerGroup* createContainerGroup();
InputContainer* createInputContainer();
ContainerGroup* createModMappingGroup(ParamList* paramList, Mod* mod, int scene, int x, int y);
ButtonGui* createButtonGui(int x, int y, int w, int h, char* text, Parameter* param, void* callback);
void addContainerToGroup(ContainerGroup* cg, InputContainer* ic, int row, int col);
void containerGroupNavigate(ContainerGroup* cg, int rowInc, int colInc);
void removeButtonFromContainer(ButtonGui* btnGui, InputContainer* btnCont, Scene scene);
void addButtonToContainer(ButtonGui* btnGui, InputContainer* btnCont, int row, int col);
ButtonGui* getSelectedInput(ContainerGroup* cg);
void drawButtonGui(void* self);
void applyButtonCallback(void* self, float value);

void clearBg();
void drawTransportGui(void *self);
void drawSequencerGui(void* self);
void drawGraphGui(void* self);
void drawArrangerGui(void *self);
void drawSongMinimapGui(void *self);
void drawEnvelopeGui(void *self);
void updateGraphGui(GraphGui* graphGui);
void InitGUI(void);
void DrawGUI(int currentScene);
void CleanupGUI(void);
void drawInputsGui(void* self);
#endif // GUI_H