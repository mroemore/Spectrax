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
#include "gui_layer.h"
#include "theme.h"

typedef struct {
	int x;
	int y;
	int w;
	int h;
} Shape;

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
	/* Task 7: overlay layer stack. Every modal (overwrite, dirty-confirm,
	 * load-list) lives as a layer on top of the instrument's underlying
	 * graph. The stack starts empty; pushLayer() to open a modal, popLayer()
	 * to dismiss. layerStackInput() routes the next keypress to the topmost
	 * layer's graph; layerStackDraw() draws all layers bottom-up. */
	LayerStack overlayLayers;
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
ColourScheme *getColourScheme();

/* Font config (Task 9): copied into gui.c file-scope gFontConfig. Call
 * BEFORE InitGUI so the window/raylib context exists when fonts load. */
void setFontConfig(const FontConfig *cfg);
FontConfig *getFontConfig(void);

/* Theme-loaded flag: once set, InitGUI() leaves the colour scheme alone
 * (no default-init) and uses gFontConfig for pixelFont. */
void markThemeLoaded(void);
bool isThemeLoaded(void);

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

/* Task 3: per-instrument chip row above the arranger grid. Drawn for
 * each enabled channel; bg colour comes from the per-channel label
 * palette index, content shows the type tag, voice count, label, and
 * current patch + voice-active state. The draw early-returns when vm
 * or inst is NULL so the nav sanity test in tests/dsp/test_graph_nav.c
 * can build chips with NULL refs without crashing. */
typedef struct {
	GuiNode base;
	struct VoiceManager *vm;
	int channel;
	Arranger *arranger;
	bool expanded;
	int swatchFocus;
	/* Task 5: cursor for the 8-char label edit. Cursor is the index of
	 * the slot being edited; 0..strlen(label) inclusive (so the user can
	 * insert a char immediately after the current end). Bounded to 7 by
	 * the label cap (8 chars + NUL). Mirrors the preset-name node's
	 * cursor model (preset node has its own copy intentionally; see
	 * PresetNameGuiNode.cursor — the fixtures depend on it). */
	int cursor;
} InstChipGuiNode;

GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, struct VoiceManager *vm, int channel, Arranger *arranger);
bool isInstChipNode(const GuiNode *n);

/* Task 4: chip input routing. getSelectedChipChannel() returns the
 * channel of the currently-selected chip in agui (or -1 if the
 * selection isn't a chip). expandChip() sets the chip's `expanded`
 * flag by channel; used by EDIT+UP to reveal the expanded chip
 * content (Task 5+ will render the expanded state). isChipExpanded()
 * reads the current flag so the EDIT+UP handler can toggle. The chip
 * node pointers are registered in createArrangerGraph into a file-static
 * g_chipNodes[] array in gui.c, so expandChip indexes directly rather
 * than walking the agui tree (which is fragile across Task 3+). */
int getSelectedChipChannel(void);

/* Task 7: selected GuiNode in the arranger graph (or NULL). Used by
 * the scripted harness so chip-flow fixtures can compare against the
 * arranger side (chipr, chip, arr) rather than the instrument graph. */
GuiNode *getArrangerSelectedNode(void);
bool isChipExpanded(int channel);
void expandChip(int channel, bool expanded);

/* Task 5: route the expanded chip's input. When KM_EDIT is NOT held:
 * LEFT/RIGHT move the swatch focus + write labelColourIdx live. When
 * KM_EDIT IS held: LEFT/RIGHT move the cursor (bounded to strlen),
 * UP/DOWN cycle the char under the cursor through NAME_CHARS. Bare
 * KM_EDIT / KM_SELECT / KM_START collapse (expandChip(channel, false)).
 * Returns nothing — the caller (main.c SCENE_ARRANGER KM_EDIT branch)
 * is responsible for routing when this function should fire. */
void handleExpandedChipInput(int channel, int km, bool editHeld);

/* Task 5: pure helpers used by handleExpandedChipInput + unit-tested
 * in tests/dsp/test_mod_voice.c. The cursor never indexes past the
 * current NUL (so the user can extend the label by moving right), but
 * is also capped at maxLen so a corrupt strlen can never read past
 * the buffer. dir is -1 (left) or +1 (right); anything else is a
 * no-op so the function is forgiving in the input layer. */
void chipLabelCursorMove(int *cursor, const char *label, int maxLen, int dir);

/* Task 5: cycle the char at *slot through the arcade char table.
 * dir is -1 or +1. The slot is overwritten with the new char. The
 * function never inserts a NUL into the table (NAME_CHARS does not
 * contain one), so calling this on a slot that already holds the
 * NUL terminator is safe — it lands on 'A' (the first char) or the
 * last char depending on direction. The slot is left untouched if
 * it already points past the table. */
void chipLabelCycleCharAt(char *slot, int dir);

/* Task 5: index of `c` in the arcade table, or 0 if absent. Mirrors
 * charIndex() in the preset-name node — the two are kept in sync by
 * definition (identical NAME_CHARS macros). */
int chipLabelCharIndex(char c);

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
	/* Task 5: visual save-feedback flashes. Set in commitPresetName after
	 * guiSavePreset returns. 0 = inactive. Compared against a wall-clock
	 * frame index (currentFrameIndex()); a value strictly greater than the
	 * current frame means the flash is active. */
	int savedFlashUntil;
	int errorFlashUntil;
} PresetNameGuiNode;

GuiNode *createDialGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback cb, Parameter *p);
GuiNode *createActionBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, ActionCallback cb, void *ctx);
void drawActionBtnGuiNode(void *self);
void printArrGraph();

/* Task 3: dispatch guard for KM_EDIT + arrow keys. True only when the
 * currently-selected node is a dial (has the draw fn pointer drawDialGuiNode
 * and a non-NULL OnPressCallback). Used by the SCENE_INSTRUMENT input
 * branches in main.c and the harness to skip the callback dispatch when
 * the selection is anything else (preset name node, action button, load
 * list, etc.). Fixes the `z`+DOWN crash on the preset name node. */
bool isSelectedDialNode(const Graph *g);
GuiNode *createPresetNameGuiNode(int x, int y, int w, int h, Instrument *inst, bool selected);
bool isPresetNameNode(GuiNode *n);

/* Task 5: flash getters used by the scripted harness to assert that the
 * saved-/error-flash fired after a save attempt. Returns true while the
 * flash is still active (savedFlashUntil > currentFrameIndex()).
 * Returns false for any non-PresetNameGuiNode input. */
bool presetNameGuiNodeSavedFlashActive(GuiNode *n);
bool presetNameGuiNodeErrorFlashActive(GuiNode *n);

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
 * once the user confirms.
 *
 * Task 5: returns the PresetFileResult from the underlying save so the
 * caller (commitPresetName) can light a success- or error-flash on the
 * PresetNameGuiNode. PRESET_EXISTS is not propagated as an "error" —
 * the user hasn't confirmed the overwrite yet, so it's neither success
 * nor failure at this point. */
PresetFileResult guiSavePreset(Instrument *inst, const char *name);

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

/* Task 6: open the "discard unsaved changes?" modal. Called from
 * cbOpenLoadList when the user presses LOAD on a dirty instrument
 * (i.e. one whose live state has been edited since the last
 * load/save). Task 7 fills in the actual modal layer; for now the
 * stub is a no-op so the rest of the system still compiles and
 * unit-tests pass. The Task 7 implementation will switch the
 * state machine and dispatch the three-way choice (discard / save /
 * cancel) before falling through to guiOpenLoadList. */
void guiShowDirtyConfirmModal(Instrument *inst);

/* True while any modal is open. Callers in main.c / the harness can use
 * this to short-circuit scene navigation while the modal is up. */
bool guiIsModalOpen(void);

/* True while the preset load-list (Task 9) is open. The scripted harness
 * uses this to assert that the LOAD button activated the list and that
 * START in the list closed it. */
bool guiIsLoadListActive(void);

/* Drawn from DrawGUI's SCENE_INSTRUMENT case while g_modalState != MODAL_NONE. */
void drawPresetModal(void);

/* Task 7: layer-based overlay system. All three modals (overwrite,
 * dirty-confirm, load-list) are built as overlay layers on the
 * instrument's LayerStack. */

void guiBuildOverwriteLayer(InstrumentGui *ig, const char *pendingName);
void guiBuildDirtyConfirmLayer(InstrumentGui *ig, Instrument *inst);
void guiBuildLoadListLayer(InstrumentGui *ig);

/* Pop the topmost overlay layer (if any). Returns true if a layer was
 * popped. */
bool guiPopOverlay(InstrumentGui *ig);
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
void appendMetaControlNode(Graph *g, GuiNode *container, Instrument *inst, VoiceManager *vm, int channel, int weight, bool selected);
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

/* Window-resize support: the app renders its content into a fixed-size
 * render target (640x480) and presents it scaled to the (resizable)
 * window, preserving the base resolution + aspect ratio. */
RenderTexture2D createPresentTarget(void);
void presentFrame(RenderTexture2D gfx);

/* Task 7: layer stack accessors used by main.c, the harness, and the
 * per-modal builders. getInstrumentOverlayLayers() returns the live
 * LayerStack so callers can push/pop. */
LayerStack *getInstrumentOverlayLayers(void);
InstrumentGui *getInstrumentGui(void);
#endif // GUI_H