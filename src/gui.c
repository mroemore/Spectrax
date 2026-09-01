#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dstruct.h"
#include "raylib.h"
#include "gui.h"
#include "graph_gui.h"
#include "input.h"
#include "oscillator.h"
#include "sample.h"
#include "sequencer.h"
#include "modsystem.h"
#include "notes.h"
#include "voice.h"
#include "io/preset_io.h"
#include "io.h"

InstrumentGui *igui;
Graph *agui;
/* Task 4: chip node registry. createArrangerGraph populates g_chipNodes[ch]
 * with the InstChipGuiNode for each enabled channel. expandChip() indexes
 * this array directly instead of walking the agui tree (Task 3 nests chips
 * two layers deep: root → gridColumn → chipRow → chip — a fragile walk
 * that would break the moment Task 5+ adds anything else into chipRow).
 * NULL slots (disabled channels / out-of-range) are skipped. */

/* Task 5: shared arcade char table for chip label editing. Defined up
 * here so the chip helpers below can use it; the original definition
 * for the preset-name node lives at line ~1517 and is a no-op
 * re-definition of the same literal (C allows identical #define
 * re-declarations). This keeps both consumers' char cycles in lock-
 * step without forcing the preset-node fixtures to share an
 * implementation. */
#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-. "
static GuiNode *g_chipNodes[MAX_SEQUENCER_CHANNELS];
static Graph *patternGraph;
static SongMinimapGui *smgui;

typedef struct {
	Instrument *inst;
	Envelope *env;
	Parameter *routeIndex;
	Parameter *target; /* current modulation target; NULL = un-routed */
} RouteState;
static RouteState runtimeRoutes[MAX_ENVELOPES];
/* Task 8: the current preset-name node. Set on every graph build
 * (createPresetNameGuiNode). The SAVE action button uses it to commit
 * the instrument under the name currently shown. */
static PresetNameGuiNode *g_presetNameNode;

Font textFont;
Font symbolFont;
Font pixelFont;
ColourScheme cs;
static FontConfig gFontConfig;
static bool gThemeLoaded;
SpriteSheet *instrumentIcons;
Texture2D dial;
Texture2D btnOn;
Texture2D btnOff;

void initCustomFont(Font *f, char *path, int charCount, int width, int height) {
	*f = LoadFontEx(path, width, NULL, charCount);
	if(f->texture.id == 0) {
		printf("Failed to load font: %s\n", path);
	} else {
		printf("Successfully loaded font: %s\n", path);
	}
}

SpriteSheet *createSpriteSheet(char *imagePath, int sprite_w, int sprite_h) {
	SpriteSheet *sh = (SpriteSheet *)malloc(sizeof(SpriteSheet));
	sh->sheet = LoadTexture(imagePath);
	sh->spriteCount = (sh->sheet.width / sprite_w) * (sh->sheet.height / sprite_h);
	sh->scale = 1.0;
	printf("spriteCount %i\n", sh->spriteCount);
	sh->spriteW = sprite_w;
	printf("sW %i\n", sh->spriteW);
	sh->spriteH = sprite_h;
	printf("sH %i\n", sh->spriteH);
	sh->origin = (Vector2){ sh->spriteW / 2.0, sh->spriteH / 2.0 };
	sh->spriteSize = (Rectangle){ 0, 0, sprite_w, sprite_h };
}

void drawSprite(SpriteSheet *spriteSheet, int index, int x, int y, int w, int h) {
	if(!spriteSheet || spriteSheet->spriteCount <= 0) {
		/* Asset failed to load or has zero sprites -- avoid the SIGFPE
		 * from `index % 0` and silently skip the draw. The arranger
		 * scene's instrument icons are decorative; the rest of the UI
		 * (cells, cursor, pattern grid) still renders. */
		return;
	}
	index = index >= spriteSheet->spriteCount ? index % spriteSheet->spriteCount : index;
	spriteSheet->spriteSize.x = index * spriteSheet->spriteW;
	DrawTexturePro(spriteSheet->sheet, spriteSheet->spriteSize, (Rectangle){ x + spriteSheet->spriteW / 2.0, y + spriteSheet->spriteW / 2.0, w, h }, spriteSheet->origin, 0.0, WHITE);
	spriteSheet->spriteSize.x = 0;
}

void initDefaultColourScheme(ColourScheme *colourScheme) {
	colourScheme->backgroundColor = (Color){ 207, 110, 58, 255 };
	colourScheme->fontColour = (Color){ 99, 17, 0, 255 };
	colourScheme->secondaryFontColour = (Color){ 176, 53, 0, 255 };
	colourScheme->outlineColour = (Color){ 219, 148, 103, 255 };
	colourScheme->defaultCell = (Color){ 148, 68, 16, 255 };
	colourScheme->highlightedCell = (Color){ 214, 60, 17, 255 };
	colourScheme->selectedCell = (Color){ 235, 161, 75, 255 };
	colourScheme->blankCell = (Color){ 94, 23, 29, 255 };
	colourScheme->reddish = (Color){ 170, 38, 49, 255 };
	colourScheme->panel = (Color){ 80, 60, 60, 255 };
	colourScheme->panelBorder = (Color){ 10, 0, 0, 255 };
	colourScheme->valueDisplayBg = (Color){ 50, 40, 40, 255 };
	colourScheme->label = (Color){ 200, 180, 180, 255 };
	colourScheme->labelSelected = (Color){ 255, 180, 180, 255 };
	colourScheme->dial = (Color){ 255, 0, 0, 255 };
	colourScheme->valueText = (Color){ 255, 0, 0, 255 };
	colourScheme->vline = (Color){ 60, 255, 150, 255 };
	colourScheme->poly = (Color){ 255, 80, 80, 255 };
	colourScheme->waveformBg = (Color){ 0, 0, 0, 255 };
	colourScheme->waveform = (Color){ 255, 0, 0, 255 };
	colourScheme->waveformAlt = (Color){ 0, 255, 0, 255 };
	colourScheme->sampleBg = (Color){ 60, 10, 10, 255 };
	colourScheme->sampleAltBg = (Color){ 10, 50, 10, 255 };
	colourScheme->sampleBorder = (Color){ 200, 80, 60, 255 };
	colourScheme->stepBorder = (Color){ 80, 20, 20, 255 };
	colourScheme->stepClosed = (Color){ 80, 30, 30, 255 };
	colourScheme->arrangerPlayhead = (Color){ 255, 0, 0, 255 };
	colourScheme->arrangerCellText = (Color){ 200, 180, 180, 255 };
	colourScheme->wrapperBorder = (Color){ 10, 5, 5, 255 };
	colourScheme->modStripLfo = (Color){ 0, 255, 255, 255 };
	colourScheme->modStripEnv = (Color){ 130, 255, 130, 255 };
	colourScheme->modStripRnd = (Color){ 255, 80, 255, 255 };
	colourScheme->modStripOfs = (Color){ 190, 190, 190, 255 };
	colourScheme->modStripDefault = (Color){ 210, 210, 210, 255 };
	colourScheme->layerDim = (Color){ 0, 0, 0, 170 };
	colourScheme->spectrogramPlayhead = (Color){ 255, 0, 0, 255 };
}

void setColourScheme(ColourScheme *colourScheme) {
	cs = *colourScheme;
}

ColourScheme *getColourScheme() {
	return &cs;
}

void setFontConfig(const FontConfig *cfg) {
	if(!cfg) {
		return;
	}
	gFontConfig = *cfg;
}

FontConfig *getFontConfig(void) {
	return &gFontConfig;
}

void markThemeLoaded(void) {
	gThemeLoaded = true;
}

bool isThemeLoaded(void) {
	return gThemeLoaded;
}

void clearBg() {
	ClearBackground(cs.backgroundColor);
}

RenderTexture2D createPresentTarget(void) {
	return LoadRenderTexture(SCREEN_W, SCREEN_H);
}

void presentFrame(RenderTexture2D gfx) {
	BeginDrawing();
	ClearBackground(BLACK);
	int sw = GetScreenWidth();
	int sh = GetScreenHeight();
	float s = fminf((float)sw / SCREEN_W, (float)sh / SCREEN_H);
	int dw = (int)(SCREEN_W * s);
	int dh = (int)(SCREEN_H * s);
	DrawTexturePro(gfx.texture, (Rectangle){ 0, 0, SCREEN_W, -SCREEN_H },
	               (Rectangle){ (sw - dw) / 2, (sh - dh) / 2, dw, dh },
	               (Vector2){ 0, 0 }, 0.0f, WHITE);
	EndDrawing();
}

void InitGUI(void) {
	const int screenWidth = SCREEN_W;
	const int screenHeight = SCREEN_H;

	if(!gThemeLoaded) {
		initDefaultColourScheme(&cs);
	}

	/* Fall back to hardcoded defaults if main() never set gFontConfig.
	 * The harness boots directly into InitGUI without going through the
	 * cfg/theme loader path, so it relies on these fallbacks. */
	if(gFontConfig.path[0] == '\0') {
		strncpy(gFontConfig.path, "resources/fonts/console.ttf", sizeof(gFontConfig.path) - 1);
		gFontConfig.path[sizeof(gFontConfig.path) - 1] = '\0';
		gFontConfig.size = 9;
		gFontConfig.spacing = 0;
	}

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(screenWidth, screenHeight, "Spectrax");
	textFont = LoadFont("resources/fonts/setback.png");
	pixelFont = LoadFontEx(gFontConfig.path, gFontConfig.size, NULL, 255);
	initCustomFont(&symbolFont, "resources/fonts/iconzfin.png", 8, 10, 12);
	instrumentIcons = createSpriteSheet("resources/images/synthicon_sheet.png", 64, 64);
	Image dialimg = LoadImage("resources/images/dial2.png");
	Image btnimg1 = LoadImage("resources/images/btn-on-s.png");
	Image btnimg2 = LoadImage("resources/images/btn-off-s.png");
	dial = LoadTextureFromImage(dialimg);
	btnOn = LoadTextureFromImage(btnimg1);
	btnOff = LoadTextureFromImage(btnimg2);
	UnloadImage(dialimg);
	UnloadImage(btnimg1);
	UnloadImage(btnimg2);
	SetTargetFPS(60);
}

SongMinimapGui *createSongMinimapGui(Arranger *arranger, int *songIndex, int x, int y) {
	SongMinimapGui *minimapGui = (SongMinimapGui *)malloc(sizeof(SongMinimapGui));
	minimapGui->base.draw = drawSongMinimapGui;
	minimapGui->arranger = arranger;
	minimapGui->songIndex = songIndex;
	minimapGui->shape.x = x;
	minimapGui->shape.y = y;
	minimapGui->shape.w = 4;
	minimapGui->shape.h = 4;
	minimapGui->padding = 1;
	minimapGui->maxMapLength = (SCREEN_H - 150) / (minimapGui->shape.h + minimapGui->padding);
	minimapGui->defaultCellColour = cs.defaultCell;
	minimapGui->selectedCellColour = cs.reddish;
	minimapGui->playingCellColour = cs.highlightedCell;
	minimapGui->blankCellColour = cs.blankCell;

	return minimapGui;
}


void createInstrumentGui(VoiceManager *vm, int *selectedInstrument, int scene) {
	InstrumentGui *ig = (InstrumentGui *)malloc(sizeof(InstrumentGui));
	if(!ig) return;
	memset(ig, 0, sizeof(InstrumentGui));
	ig->vm = vm;
	ig->selectedInstrument = selectedInstrument;

	for(int i = 0; i < vm->enabledChannels; i++) {
		bool isSelected = *selectedInstrument == i;
		ig->instrumentScreenGraphs[i] = createInstGraph(vm->instruments[i], vm, i, isSelected);
		ig->instrumentCount++;
	}
	/* Task 7: initialise the overlay layer stack. The stack starts empty;
	 * modals push layers on demand, the instrument input/draw path drains
	 * it when empty. */
	initLayerStack(&ig->overlayLayers);
	igui = ig;
}

Graph *getSelectedInstGraph() {
	return igui->instrumentScreenGraphs[*igui->selectedInstrument];
}

LayerStack *getInstrumentOverlayLayers(void) {
	return igui ? &igui->overlayLayers : NULL;
}

InstrumentGui *getInstrumentGui(void) {
	return igui;
}

static void drawSampleWaveLinesNode(void *self);
static void drawSampleWavePolylineNode(void *self);

void createArrangerGraph(Arranger *a, PatternList *pl) {
	agui = createGraph(na_vertical);
	GuiNode *arrWrap = createGuiNode(0, 0, 100, 100, 5, na_horizontal, "awrap", 0, 0);
	GuiNode *songControls = createGuiNode(0, 0, 100, 100, 5, na_vertical, "song", 0, 0);

	GuiNode *pad0 = createNamedBlankGuiNode("pad00");
	GuiNode *pad1 = createNamedBlankGuiNode("pad01");
	GuiNode *bpm = createDialGuiNode(0, 0, 100, 100, 5, na_vertical, "BPM", false, incParameterBaseValue, a->tempoSettings.bpm);
	GuiNode *swing = createDialGuiNode(0, 0, 100, 100, 5, na_vertical, "Swing", false, incParameterBaseValue, a->tempoSettings.swing);
	GuiNode *pad2 = createNamedBlankGuiNode("pad02");
	appendItem(songControls, pad0, 1);
	appendItem(songControls, bpm, 1);
	appendItem(songControls, swing, 1);
	appendItem(songControls, pad2, 8);

	GuiNode *margin2 = createNamedBlankGuiNode("Marge!");
	ArrangerGuiNode *agn = createArrangerGuiNode(0, 0, SCREEN_W * 0.75, SCREEN_H, 5, na_vertical, "arr", true, a, pl);
	GuiNode *gn = (GuiNode *)agn;
	agui->selected = gn;
	appendItem(agui->root, pad1, 1);

	appendItem(arrWrap, songControls, 1);
	appendItem(arrWrap, margin2, 1);

	/* Task 3: instrument chip row sits above the arranger grid. One chip
	 * per enabled sequencer channel; chipRow is horizontal, the grid
	 * gets the lion's share (weight 8). */
	GuiNode *gridColumn = createGuiNode(0, 0, 100, 100, 2, na_vertical, "gcol", 0, 0);
	gridColumn->drawable = true;
	gridColumn->draw = drawWrapperNode;
	GuiNode *chipRow = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "chipr", 0, 0);
	chipRow->drawable = true;
	chipRow->draw = drawWrapperNode;
	/* Task 4: zero the chip registry so any channel that doesn't get a
	 * chip this build (e.g. enabledChannels shrank between rebuilds)
	 * doesn't keep a stale pointer. */
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		g_chipNodes[ch] = NULL;
	}
	for(int ch = 0; ch < a->enabledChannels; ch++) {
		GuiNode *chip = createInstChipGuiNode(0, 0, 100, 40, false, a->vm, ch, a);
		g_chipNodes[ch] = chip;
		appendItem(chipRow, chip, 1);
	}
	appendItem(gridColumn, chipRow, 1);
	appendItem(gridColumn, gn, 8);
	appendItem(arrWrap, gridColumn, 4);
	appendItem(agui->root, arrWrap, 15);

	GuiNode *demoStack = createGuiNode(0, 0, 100, 100, 0, na_vertical, "demo", 0, 0);
	GuiNode *linesNode = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "vline", 0, 0);
	linesNode->drawable = true;
	linesNode->draw = drawSampleWaveLinesNode;
	GuiNode *polyNode = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "poly", 0, 0);
	polyNode->drawable = true;
	polyNode->draw = drawSampleWavePolylineNode;
	appendItem(demoStack, linesNode, 1);
	appendItem(demoStack, polyNode, 1);
	appendItem(agui->root, demoStack, 5);
}

typedef struct {
	PatternList *pl;
	Sequencer *seq;
	int patternIndex;
	int stepIndex;
	int *selectedStepPtr;
} StepNodeData;

#define PATTERN_STEPS_PER_ROW 4

static StepNodeData stepData[MAX_SEQUENCE_LENGTH];
static GuiNode *stepNodes[MAX_SEQUENCE_LENGTH];
static PatternList *patternPl;
static Sequencer *patternSeq;
static int *patternSelectedPatternPtr;
static int *patternSelectedStepPtr;
static BufferScroller *patternBufferScroller;

void setPatternBufferScroller(BufferScroller *bs) {
	patternBufferScroller = bs;
}

static MixRing *arrangerMixRing;

void setArrangerMixRing(MixRing *r) {
	arrangerMixRing = r;
}

static void drawSampleWaveLinesNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!arrangerMixRing) {
		return;
	}
	drawSampleWaveLines(arrangerMixRing, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
	DrawTextEx(pixelFont, "VLINE", (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, cs.vline);
}

static void drawSampleWavePolylineNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!arrangerMixRing) {
		return;
	}
	drawSampleWavePolyline(arrangerMixRing, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
	DrawTextEx(pixelFont, "POLY", (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, cs.poly);
}

static void drawBufferScrollerNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!patternBufferScroller) {
		return;
	}
	drawBufferScroller(patternBufferScroller, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
}

static GuiNode *createStepNode(PatternList *pl, Sequencer *seq, int patternIndex, int stepIndex, int *selectedStepPtr) {
	GuiNode *step = createGuiNode(0, 0, 50, 50, 4, na_vertical, "step", 1, 0);
	stepData[stepIndex].pl = pl;
	stepData[stepIndex].seq = seq;
	stepData[stepIndex].patternIndex = patternIndex;
	stepData[stepIndex].stepIndex = stepIndex;
	stepData[stepIndex].selectedStepPtr = selectedStepPtr;
	step->p = (Parameter *)&stepData[stepIndex];
	step->drawable = true;
	step->draw = drawStepGuiNode;
	stepNodes[stepIndex] = step;
	return step;
}

void drawStepGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	StepNodeData *d = (StepNodeData *)gn->p;
	int currentlyPlaying = -1;
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		if(d->seq->pattern_index[i] == d->patternIndex) {
			currentlyPlaying = i;
			break;
		}
	}
	Rectangle cell = (Rectangle){ gn->x, gn->y, gn->w, gn->h };
	if(*d->selectedStepPtr == d->stepIndex) {
		DrawRectangle(gn->x - 3, gn->y - 3, gn->w + 6, gn->h + 6, cs.outlineColour);
	}
	if(currentlyPlaying > -1 && d->seq->running[currentlyPlaying] && d->seq->playhead_index[currentlyPlaying] == d->stepIndex) {
		DrawRectangleRec(cell, cs.highlightedCell);
	} else {
		DrawRectangleRec(cell, cs.defaultCell);
	}
	int *note = getStep(d->pl, d->patternIndex, d->stepIndex);
	char *noteString = getNoteString(note[0], note[1]);
	DrawTextEx(textFont, noteString, (Vector2){ gn->x + 4, gn->y + 4 }, textFont.baseSize, 4, cs.fontColour);
}

void createPatternGraph(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedStep) {
	patternPl = pl;
	patternSeq = sequencer;
	patternSelectedPatternPtr = selectedPattern;
	patternSelectedStepPtr = selectedStep;

	patternGraph = createGraph(na_vertical);
	int patternIndex = *selectedPattern;
	int size = 0;
	if(patternIndex >= 0 && patternIndex < pl->pattern_count) {
		size = pl->patterns[patternIndex].pattern_size;
	}
	int rows = (size + PATTERN_STEPS_PER_ROW - 1) / PATTERN_STEPS_PER_ROW;
	GuiNode *scrollerStrip = createGuiNode(0, 0, 640, 96, 0, na_horizontal, "scroller", 0, 0);
	scrollerStrip->drawable = true;
	scrollerStrip->draw = drawBufferScrollerNode;
	GuiNode *gridWrap = createGuiNode(10, 10, 230, 460, 6, na_vertical, "grid", 0, 0);
	for(int r = 0; r < rows; r++) {
		GuiNode *row = createGuiNode(0, 0, 100, 50, 4, na_horizontal, "row", 0, 0);
		for(int c = 0; c < PATTERN_STEPS_PER_ROW; c++) {
			int i = r * PATTERN_STEPS_PER_ROW + c;
			if(i >= size) {
				break;
			}
			appendItem(row, createStepNode(pl, sequencer, patternIndex, i, selectedStep), 1);
		}
		appendItem(gridWrap, row, 1);
	}
	appendItem(patternGraph->root, scrollerStrip, 4);
	appendItem(patternGraph->root, gridWrap, 16);
	if(size > 0) {
		int current = *selectedStep;
		if(current < 0) {
			current = 0;
		}
		if(current >= size) {
			current = size - 1;
		}
		patternGraph->selected = stepNodes[current];
		stepNodes[current]->selected = 1;
	}
}

void navigatePatternGraph(int keymapping) {
	if(!patternGraph || !patternGraph->selected) {
		return;
	}
	StepNodeData *d = (StepNodeData *)patternGraph->selected->p;
	int next = *d->selectedStepPtr;
	switch(keymapping) {
		case KM_LEFT:
			next = selectStep(d->pl, d->patternIndex, *d->selectedStepPtr - 1);
			break;
		case KM_RIGHT:
			next = selectStep(d->pl, d->patternIndex, *d->selectedStepPtr + 1);
			break;
		case KM_UP:
			next = selectStep(d->pl, d->patternIndex, *d->selectedStepPtr - PATTERN_STEPS_PER_ROW);
			break;
		case KM_DOWN:
			next = selectStep(d->pl, d->patternIndex, *d->selectedStepPtr + PATTERN_STEPS_PER_ROW);
			break;
		default:
			return;
	}
	*d->selectedStepPtr = next;
	changeGraphSelection(patternGraph, stepNodes[next]);
}

void rebuildPatternGraph() {
	if(!patternPl || !patternSeq || !patternGraph) {
		return;
	}
	freeGuiNode(patternGraph->root);
	free(patternGraph);
	patternGraph = NULL;
	createPatternGraph(patternSeq, patternPl, patternSelectedPatternPtr, patternSelectedStepPtr);
}

void setSongMinimapGui(SongMinimapGui *smg) {
	smgui = smg;
}

void navigateArrangerGraph(int keymapping) {
	navigateGraph(agui, keymapping);
}

void arrangerGraphControlInput(int keymapping) {
	if(!agui || !agui->selected || !agui->selected->callback) {
		return;
	}
	switch(keymapping) {
		case KM_LEFT:
			agui->selected->callback(agui->selected->p, -0.1f);
			break;
		case KM_RIGHT:
			agui->selected->callback(agui->selected->p, 0.1f);
			break;
		case KM_UP:
			agui->selected->callback(agui->selected->p, 1.0f);
			break;
		case KM_DOWN:
			agui->selected->callback(agui->selected->p, -1.0f);
			break;
		default:
			break;
	}
}

void printArrGraph() {
	printGraph(agui->root, 0);
}

SampleWaveformGuiNode *createSampleWaveformGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Instrument *inst, Parameter *loopStart, Parameter *loopEnd) {
	SampleWaveformGuiNode *swgn = malloc(sizeof(SampleWaveformGuiNode));
	GuiNode *gn = (GuiNode *)swgn;
	if(!initGuiNode(gn, x, y, w, h, padding, na, name, 1, selected)) {
		printf("ArrangerGuiNode init problem, returning NULL.\n");
		return NULL;
	}

	swgn->instrument = inst;
	swgn->bgColour = cs.waveformBg;
	swgn->wfColour = cs.waveform;
	swgn->wfAltColour = cs.waveformAlt;
	swgn->loopStart = loopStart;
	swgn->loopEnd = loopEnd;

	gn->drawable = true;
	gn->draw = drawSampleWaveformGuiNode;
	return swgn;
}

void drawSampleWaveformGuiNode(void *self) {
	SampleWaveformGuiNode *swgn = (SampleWaveformGuiNode *)self;
	GuiNode *gn = (GuiNode *)swgn;
	int yOffset = gn->h / 2;
	int sampleIndexRatio = swgn->instrument->id.sampler.sample->length / gn->w;
	float phaseInc = 1.0 / gn->w;
	DrawRectangle(gn->x, gn->y, gn->w, gn->h, swgn->bgColour);
	for(int i = 0; i < gn->w; i++) {
		float sp = i * sampleIndexRatio;
		float s = getSampleValueFwd(swgn->instrument->id.sampler.sample, &sp, phaseInc, 0);
		s *= yOffset;
		DrawLine(gn->x + i, gn->y + yOffset, gn->x + i, gn->y + yOffset + s, swgn->wfColour);
	}

	int loopStart = getParameterValueAsInt(swgn->loopStart);
	int loopEnd = getParameterValueAsInt(swgn->loopEnd);

	int nx = gn->x + loopStart / sampleIndexRatio;
	DrawLine(nx, gn->y, nx, gn->y + gn->h, swgn->wfAltColour);
	nx = gn->x + loopEnd / sampleIndexRatio;
	DrawLine(nx, gn->y, nx, gn->y + gn->h, swgn->wfAltColour);

	DrawTextEx(pixelFont, swgn->instrument->id.sampler.sample->name, (Vector2){ gn->x + gn->padding, gn->y + gn->padding }, 12, 2, swgn->wfAltColour);
}

ArrangerGuiNode *createArrangerGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Arranger *arranger, PatternList *patternList) {
	ArrangerGuiNode *agn = malloc(sizeof(ArrangerGuiNode));
	GuiNode *gn = (GuiNode *)agn;
	if(!initGuiNode(gn, x, y, w, h, padding, na, name, 1, selected)) {
		printf("ArrangerGuiNode init problem, returning NULL.\n");
		return NULL;
	}
	gn->draw = drawArrangerGuiNode;
	gn->drawable = true;
	gn->customNav = navigateArrangerGuiNode;
	agn->grid_padding = 5;
	agn->arranger = arranger;

	agn->patternList = patternList;
	agn->border_size = 3;
	agn->iconx = gn->x;
	agn->icony = gn->y - 30;
	return agn;
}

bool navigateArrangerGuiNode(void *self, int keymapping) {
	ArrangerGuiNode *agn = (ArrangerGuiNode *)self;

	bool navSuccess = false;
	switch(keymapping) {
		case KM_LEFT:
			navSuccess = selectArrangerCell(agn->arranger, 0, -1, 0);
			break;
		case KM_RIGHT:
			navSuccess = selectArrangerCell(agn->arranger, 0, 1, 0);
			break;
		case KM_UP:
			navSuccess = selectArrangerCell(agn->arranger, 0, 0, -1);
			break;
		case KM_DOWN:
			navSuccess = selectArrangerCell(agn->arranger, 0, 0, 1);
			break;
		default:
			break;
	}
	return navSuccess;
}

void drawRotatedDial(int x, int y, int w, int h, int radius, int startAngle, int offsetAngle) {
	DrawCircleSector((Vector2){ x + radius, y + radius }, radius + 2, startAngle, startAngle + offsetAngle, 32, cs.dial);
	DrawTexturePro(dial, (Rectangle){ 0, 0, 48, 48 }, (Rectangle){ x + radius, y + radius, w, h }, (Vector2){ radius, radius }, startAngle + offsetAngle, WHITE);
}

void drawValueDisplay(int x, int y, int w, int h, char *text) {
	DrawRectangle(x, y, w, h, cs.valueDisplayBg);
	DrawTextEx(pixelFont, text, (Vector2){ x + 4, y + 4 }, 9, 1, cs.valueText);
}

void drawColourRectangle(int x, int y, int w, int h, float roundness, float line_w, bool highlighted) {
	/* Square panel with a drop shadow (offset by the border width)
	 * instead of the old rounded-rectangle border. The shadow is drawn
	 * first so it reads as an edge behind the panel. */
	(void)roundness;
	int o = (int)line_w;
	Color shadowColour = highlighted ? (Color){ 0, 0, 0, 200 } : (Color){ 0, 0, 0, 140 };
	DrawRectangle(x + o, y + o, w, h, shadowColour);
	DrawRectangle(x, y, w, h, cs.panel);
	if(highlighted) {
		DrawRectangleLinesEx((Rectangle){ x, y, w, h }, line_w, cs.highlightedCell);
	} else {
		DrawRectangleLinesEx((Rectangle){ x, y, w, h }, line_w, cs.panelBorder);
	}
}

void drawDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	/* Use baseValue (unmodulated) so the dial reflects the user's actual
	 * setting rather than the envelope-modulated currentValue. The dial
	 * is a control surface; the modulated value is for the audio path. */
	char paramValue[50];
	snprintf(paramValue, 50, "%05.2f", gn->p->baseValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		angle = (gn->p->baseValue - gn->p->minValue) / (range / 100) * 2.7;
	}
	int tmpx = gn->x;
	int tmpy = gn->y;
	drawColourRectangle(tmpx, tmpy, gn->w, gn->h, 0.125, 2.0, gn->selected);
	tmpx += gn->padding + 2;
	tmpy += gn->padding;
	/* Compact geometry so the dial + value + label stack fits the
	 * ~31px-tall cells the reflow gives the FM/envelope rows. The old
	 * 24px dial + label at +30 overflowed the cell and clipped the
	 * label into the row below (the reported unreadable control
	 * labels). 20px dial, label right under it at +22 -> 31px total. */
	drawRotatedDial(tmpx, tmpy, 20, 20, 10, -225, angle);
	tmpx += 28;
	tmpy += 2;
	drawValueDisplay(tmpx, tmpy, 38, 14, paramValue);
	DrawTextEx(pixelFont, gn->name, (Vector2){ tmpx - 28, tmpy + 18 }, 9, 1, cs.label);
}

GuiNode *createDialGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback cb, Parameter *p) {
	GuiNode *gn = createGuiNode(x, y, w, h, padding, na, name, 1, selected);
	if(gn == NULL) {
		printf("createDialGuiNode error, could not create.");
		return NULL;
	}
	gn->callback = cb;
	gn->p = p;
	gn->drawable = true;
	gn->draw = drawDialGuiNode;
	return gn;
}

GuiNode *createActionBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, ActionCallback cb, void *ctx) {
	GuiNode *gn = createGuiNode(x, y, w, h, padding, na, name, 1, selected);
	if(gn == NULL) {
		printf("createActionBtnGuiNode error, could not create.");
		return NULL;
	}
	gn->actionCb = cb;
	gn->actionCtx = ctx;
	gn->drawable = true;
	gn->draw = drawActionBtnGuiNode;
	return gn;
}

void drawActionBtnGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	drawColourRectangle(gn->x, gn->y, gn->w, gn->h, 0.125, 2.0, gn->selected);
	Color labelColour = gn->selected ? cs.labelSelected : cs.label;
	DrawTextEx(pixelFont, gn->name, (Vector2){ gn->x + gn->padding + 4, gn->y + gn->padding + 4 }, 10, 1, labelColour);
}

void drawBipolarDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	char paramValue[50];
	snprintf(paramValue, 50, "%i", (int)gn->p->currentValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		angle = (gn->p->currentValue - gn->p->minValue) / (range / 100) * 2.7;
	}
	int tmpx = gn->x;
	int tmpy = gn->y;
	drawColourRectangle(tmpx, tmpy, gn->w, gn->h, 0.125, 2.0, gn->selected);
	tmpx += gn->padding;
	tmpy += gn->padding;
	drawRotatedDial(tmpx, tmpy, 24, 24, 12, -90, angle);
}

void drawDiscreteDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	/* baseValue (unmodulated) -- see drawDialGuiNode comment. */
	char paramValue[50];
	snprintf(paramValue, 50, "%i", (int)gn->p->baseValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		angle = (gn->p->baseValue - gn->p->minValue) / (range / 100) * 2.7;
	}
	int tmpx = gn->x;
	int tmpy = gn->y;

	drawColourRectangle(tmpx, tmpy, gn->w, gn->h, 0.125, 2.0, gn->selected);
	tmpx += gn->padding;
	tmpy += gn->padding;
	/* Compact geometry -- see drawDialGuiNode. */
	drawRotatedDial(tmpx, tmpy, 20, 20, 10, -225, angle);
	tmpx += 6;
	tmpy += 5;
	drawValueDisplay(tmpx, tmpy, 10, 14, paramValue);

	DrawTextEx(pixelFont, gn->name, (Vector2){ tmpx, tmpy + 16 }, 9, 1, cs.label);
}

void drawWrapperNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h }, cs.panel);
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0, cs.wrapperBorder);
}

/* Task 3: per-instrument chip row. Drawn for each enabled channel above
 * the arranger grid. Bg colour comes from a fixed 8-colour palette keyed
 * by the arranger's per-channel labelColourIdx. Content shows V:{count}
 * top-left, type tag (FM/SMP/BLP) top-right, 8-char channel label
 * centred, current patch name bottom-left, and a voice-active light
 * bottom-right per voice in vm->voiceCount[ch].
 *
 * IMPORTANT: this MUST early-return when vm or inst is NULL — the nav
 * test in tests/dsp/test_graph_nav.c builds chips with NULL refs so it
 * can exercise the graph layout (chipRow RIGHT/DOWN navigation) without
 * needing a real VoiceManager or Instrument. */
static const Color chipPalette[8] = {
	{ 70, 130, 180, 255 }, /* steel-blue   */
	{ 200, 120, 60, 255 }, /* amber        */
	{ 90, 160, 90, 255 },  /* moss         */
	{ 170, 80, 130, 255 }, /* mulberry     */
	{ 210, 180, 70, 255 }, /* gold         */
	{ 110, 90, 170, 255 }, /* violet       */
	{ 80, 160, 160, 255 }, /* teal         */
	{ 180, 90, 90, 255 },  /* brick        */
};

static const char *voiceTypeTag(VoiceType t) {
	switch(t) {
		case VOICE_TYPE_FM:     return "FM";
		case VOICE_TYPE_BLEP:   return "BLP";
		case VOICE_TYPE_GRAIN:  return "GRN";
		case VOICE_TYPE_SPECTRAL: return "SPC";
		case VOICE_TYPE_SAMPLE:
		default:                return "SMP";
	}
}

void drawInstChipGuiNode(void *self) {
	InstChipGuiNode *chip = (InstChipGuiNode *)self;
	if(!chip->vm || !chip->arranger) return;
	Instrument *inst = chip->vm->instruments[chip->channel];
	if(!inst) return;

	GuiNode *gn = &chip->base;
	int paletteIdx = chip->arranger->labelColourIdx[chip->channel];
	if(paletteIdx < 0) paletteIdx = 0;
	if(paletteIdx >= 8) paletteIdx = paletteIdx % 8;
	Color bg = chipPalette[paletteIdx];
	DrawRectangle(gn->x, gn->y, gn->w, gn->h, bg);
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0, gn->selected ? cs.outlineColour : cs.wrapperBorder);

	/* top-left: voice count */
	char vbuf[16];
	snprintf(vbuf, sizeof(vbuf), "V:%d", chip->vm->voiceCount[chip->channel]);
	DrawTextEx(pixelFont, vbuf, (Vector2){ gn->x + 4, gn->y + 2 }, 9, 1, cs.label);

	/* top-right: type tag */
	const char *tag = voiceTypeTag(inst->voiceType);
	int tagW = MeasureText(tag, 9);
	DrawTextEx(pixelFont, tag, (Vector2){ gn->x + gn->w - tagW - 4, gn->y + 2 }, 9, 1, cs.label);

	/* centre: 8-char channel label */
	const char *lbl = chip->arranger->label[chip->channel];
	char lblBuf[9];
	strncpy(lblBuf, lbl, 8);
	lblBuf[8] = '\0';
	int lblW = MeasureText(lblBuf, 12);
	DrawTextEx(pixelFont, lblBuf, (Vector2){ gn->x + (gn->w - lblW) / 2, gn->y + (gn->h - 12) / 2 }, 12, 1, cs.label);

	/* bottom-left: patch name (inst->loaded.name) */
	const char *patch = inst->loaded.name;
	if(!patch) patch = "";
	char patchBuf[32];
	strncpy(patchBuf, patch, sizeof(patchBuf) - 1);
	patchBuf[sizeof(patchBuf) - 1] = '\0';
	DrawTextEx(pixelFont, patchBuf, (Vector2){ gn->x + 4, gn->y + gn->h - 11 }, 8, 1, cs.label);

	/* bottom-right: voice-active light per voice */
	int n = chip->vm->voiceCount[chip->channel];
	int dotSize = 4;
	int dotGap = 2;
	int rowW = n * dotSize + (n > 0 ? (n - 1) * dotGap : 0);
	int startX = gn->x + gn->w - rowW - 3;
	int dotY = gn->y + gn->h - dotSize - 3;
	for(int i = 0; i < n; i++) {
		Voice *v = chip->vm->voicePools[chip->channel][i];
		Color c = (v && v->active) ? cs.outlineColour : cs.wrapperBorder;
		DrawRectangle(startX + i * (dotSize + dotGap), dotY, dotSize, dotSize, c);
	}

	/* Task 5: expanded overlay. When `expanded` is true, render the 8
	 * colour swatches + the 8-char label input directly ABOVE the chip's
	 * rectangle so the chip itself stays visible underneath. The strip
	 * is 32px tall: top 16px = swatches, bottom 16px = label input row.
	 * Geometry is bounded by the chip's own (x, y, w): the swatches and
	 * label input are never wider than the chip and never bleed above
	 * y=0 — callers must guarantee that, just like the chip itself. */
	if(!chip->expanded) return;

	int swatchH = 16;
	int labelH = 16;
	int stripTopY = gn->y - swatchH - labelH;
	if(stripTopY < 0) stripTopY = 0;  /* defensive clamp; pin to 0 */

	/* Swatches: 8 squares side by side. Width per swatch = gn->w / 8. */
	int swW = gn->w / 8;
	for(int i = 0; i < 8; i++) {
		int sx = gn->x + i * swW;
		DrawRectangle(sx, stripTopY, swW, swatchH, chipPalette[i]);
		if(i == chip->swatchFocus) {
			/* inverted cursor block + outline so the focused swatch is
			 * obvious even when its colour is the same as the chip's bg */
			DrawRectangleLinesEx((Rectangle){ sx, stripTopY, swW, swatchH }, 2.0f, cs.outlineColour);
		} else {
			DrawRectangleLinesEx((Rectangle){ sx, stripTopY, swW, swatchH }, 1.0f, cs.wrapperBorder);
		}
	}

	/* Label input: 8 cells mirroring the preset-name node's model.
	 * Each cell is swW wide, labelH tall, drawn as the char with an
	 * inverted cursor block when i == chip->cursor. The visible label
	 * is bounded to 8 chars; positions beyond the current NUL show as
	 * blanks so the user can see there's room to extend. */
	const char *label = chip->arranger->label[chip->channel];
	int labelLen = (int)strlen(label);
	if(labelLen > 8) labelLen = 8;
	int inputY = stripTopY + swatchH;
	for(int i = 0; i < 8; i++) {
		int cx = gn->x + i * swW;
		char ch = (i < labelLen) ? label[i] : ' ';
		if(i == chip->cursor) {
			DrawRectangle(cx, inputY, swW, labelH, cs.outlineColour);
			DrawTextEx(pixelFont, (char[]){ ch, '\0' }, (Vector2){ cx + 1, inputY + 2 }, 12, 1, BLACK);
		} else {
			DrawTextEx(pixelFont, (char[]){ ch, '\0' }, (Vector2){ cx + 1, inputY + 2 }, 12, 1, cs.label);
		}
	}
}

bool isInstChipNode(const GuiNode *n) {
	if(!n) return false;
	return n->draw == drawInstChipGuiNode;
}

/* Task 4: returns the channel of the currently-selected chip in agui,
 * or -1 when the selection isn't a chip (or agui hasn't been built yet).
 * Used by main.c's SCENE_ARRANGER EDIT+arrows handler to decide whether
 * to do chip-specific input (jump to instrument page / expand chip) or
 * fall through to the dial-edit dispatch. */
int getSelectedChipChannel(void) {
	if(!agui || !agui->selected || !isInstChipNode(agui->selected)) return -1;
	InstChipGuiNode *chip = (InstChipGuiNode *)agui->selected;
	return chip->channel;
}

/* Task 7: expose the arranger graph's selected GuiNode so the scripted
 * harness's runAssertSelected() can read it from outside gui.c. */
GuiNode *getArrangerSelectedNode(void) {
	if(!agui) return NULL;
	return agui->selected;
}

/* Task 4: set the chip's `expanded` flag by channel. Channels with no
 * registered chip (disabled or out-of-range) are no-ops. Task 5+ will
 * render the expanded chip content; for now this just flips the flag
 * so the wiring is verifiable. */
void expandChip(int channel, bool expanded) {
	if(!agui) return;
	if(channel < 0 || channel >= MAX_SEQUENCER_CHANNELS) return;
	GuiNode *node = g_chipNodes[channel];
	if(!node || !isInstChipNode(node)) return;
	((InstChipGuiNode *)node)->expanded = expanded;
}

/* Task 4: read the chip's current `expanded` flag. Used by the
 * EDIT+UP handler to toggle. Returns false for unknown / unregistered
 * channels. */
bool isChipExpanded(int channel) {
	if(!agui) return false;
	if(channel < 0 || channel >= MAX_SEQUENCER_CHANNELS) return false;
	GuiNode *node = g_chipNodes[channel];
	if(!node || !isInstChipNode(node)) return false;
	return ((InstChipGuiNode *)node)->expanded;
}

/* Task 5: cap on the chip label length. label[channel] is char[9] in
 * Arranger (8 chars + NUL); the cursor is bounded to 0..8 inclusive
 * so the user can sit at the end and insert one more. */
#define CHIP_LABEL_MAX 8

/* Task 5: find the index of `c` in the arcade NAME_CHARS table, with
 * a-z folded to A-Z the same way the preset-node helper does. Returns
 * 0 for chars not in the table so cycling wraps to 'A'. Declared in
 * gui.h so tests/dsp/test_mod_voice.c can unit-test the cycle logic
 * directly; the preset node keeps its own identical helper to avoid
 * sharing fixture-critical code. */
int chipLabelCharIndex(char c) {
	if(c >= 'a' && c <= 'z') {
		c = (char)(c - 32);
	}
	for(int i = 0; i < (int)strlen(NAME_CHARS); i++) {
		if(NAME_CHARS[i] == c) {
			return i;
		}
	}
	return 0;
}

/* Task 5: cycle the char at chip->cursor through NAME_CHARS by `delta`
 * (+1 for UP, -1 for DOWN). The slot is whatever it currently holds —
 * NUL (cursor sits past the end of the label) is treated as index 0,
 * so UP lands on NAME_CHARS[1] = 'B' (not 'A', since 0 is already
 * where NUL maps). This mirrors the preset node's cycleNameChar
 * exactly so the two surfaces behave identically. */
static void chipLabelCycleChar(InstChipGuiNode *chip, int delta) {
	int count = (int)strlen(NAME_CHARS);
	int idx = chipLabelCharIndex(chip->arranger->label[chip->channel][chip->cursor]);
	idx = (idx + delta + count) % count;
	chip->arranger->label[chip->channel][chip->cursor] = NAME_CHARS[idx];
	/* ensure NUL stays at position CHIP_LABEL_MAX */
	chip->arranger->label[chip->channel][CHIP_LABEL_MAX] = '\0';
}

/* Task 5: pure helper — cycle the char at *slot through NAME_CHARS by
 * `dir` (typically +1 for UP, -1 for DOWN). Treats '\0' (NUL) as 'A'
 * so cycling past the end of a label lands on 'A', same as the preset
 * node's cycleNameChar. The slot is left untouched if the table is
 * empty (defensive — NAME_CHARS is a string literal so it's never
 * empty in practice, but the function must not UB if that changes). */
void chipLabelCycleCharAt(char *slot, int dir) {
	if(!slot) return;
	int count = (int)strlen(NAME_CHARS);
	if(count <= 0) return;
	int idx = chipLabelCharIndex(*slot);
	idx = (idx + dir + count) % count;
	*slot = NAME_CHARS[idx];
}

/* Task 5: pure helper — move *cursor by `dir` (-1 left, +1 right),
 * bounded to [0..strlen(label)] AND [0..maxLen] so a corrupt strlen
 * (label somehow larger than the buffer) can never push the cursor
 * past the array end. Anything outside {-1, +1} is silently ignored
 * so the input layer can be forgiving. */
void chipLabelCursorMove(int *cursor, const char *label, int maxLen, int dir) {
	if(!cursor) return;
	if(dir != -1 && dir != 1) return;
	int len = label ? (int)strlen(label) : 0;
	if(len > maxLen) len = maxLen;
	int next = *cursor + dir;
	if(next < 0) next = 0;
	if(next > len) next = len;
	if(next > maxLen) next = maxLen;
	*cursor = next;
}

/* Task 5: route input for the expanded chip. When KM_EDIT is NOT held:
 * LEFT/RIGHT move the swatch focus + write labelColourIdx live. When
 * KM_EDIT IS held: LEFT/RIGHT move the cursor (bounded to strlen),
 * UP/DOWN cycle the char under the cursor through NAME_CHARS. Bare
 * KM_EDIT / KM_SELECT / KM_START collapse (expandChip(channel, false)).
 * Returns nothing — the caller (main.c SCENE_ARRANGER KM_EDIT branch)
 * is responsible for routing when this function should fire. */
void handleExpandedChipInput(int channel, int km, bool editHeld) {
	if(!agui) return;
	if(channel < 0 || channel >= MAX_SEQUENCER_CHANNELS) return;
	GuiNode *node = g_chipNodes[channel];
	if(!node || !isInstChipNode(node)) return;
	InstChipGuiNode *chip = (InstChipGuiNode *)node;
	if(!chip->arranger) return;

	/* Collapse: SELECT / START (and bare EDIT, which is what flips
	 * back to the normal chip view). Bare EDIT means KM_EDIT without
	 * an arrow — callers check that with isKeyJustPressed alone. */
	if(km == KM_SELECT || km == KM_START || km == KM_EDIT) {
		expandChip(channel, false);
		return;
	}

	if(editHeld) {
		/* KM_EDIT + arrow: label edit, mirrors the preset-name node's
		 * model exactly. LEFT/RIGHT = cursor (bounded by the pure
		 * helper), UP/DOWN = char cycle. Cursor is bounded to
		 * [0..len] so it can sit one past NUL. */
		if(km == KM_LEFT) {
			chipLabelCursorMove(&chip->cursor,
				chip->arranger->label[channel], CHIP_LABEL_MAX, -1);
			return;
		}
		if(km == KM_RIGHT) {
			chipLabelCursorMove(&chip->cursor,
				chip->arranger->label[channel], CHIP_LABEL_MAX, 1);
			return;
		}
		if(km == KM_UP) {
			chipLabelCycleChar(chip, 1);
			return;
		}
		if(km == KM_DOWN) {
			chipLabelCycleChar(chip, -1);
			return;
		}
		return;
	}

	/* Bare arrow: swatch navigation. LEFT/RIGHT move the focus +
	 * commit the colour live so the chip's bg tint updates instantly. */
	if(km == KM_LEFT) {
		chip->swatchFocus = (chip->swatchFocus + 7) % 8;
		chip->arranger->labelColourIdx[channel] = chip->swatchFocus;
		return;
	}
	if(km == KM_RIGHT) {
		chip->swatchFocus = (chip->swatchFocus + 1) % 8;
		chip->arranger->labelColourIdx[channel] = chip->swatchFocus;
		return;
	}
}

GuiNode *createInstChipGuiNode(int x, int y, int w, int h, bool selected, struct VoiceManager *vm, int channel, Arranger *arranger) {
	InstChipGuiNode *chip = (InstChipGuiNode *)malloc(sizeof(InstChipGuiNode));
	if(!chip) return NULL;
	memset(chip, 0, sizeof(InstChipGuiNode));
	if(!initGuiNode(&chip->base, x, y, w, h, 2, na_horizontal, "chip", true, selected)) {
		free(chip);
		return NULL;
	}
	chip->base.draw = drawInstChipGuiNode;
	chip->base.drawable = true;
	chip->vm = vm;
	chip->channel = channel;
	chip->arranger = arranger;
	chip->expanded = false;
	chip->swatchFocus = 0;
	/* Task 5: cursor starts at 0 (first slot of the 8-char label).
	 * Bounded at runtime by handleExpandedChipInput + chipLabelMaxLen
	 * so the cursor never indexes past the NUL. */
	chip->cursor = 0;
	return &chip->base;
}

/* ----- Task 1: arranger cell node primitive -----
 *
 * One ArrangerCellGuiNode = one song[ch][row] cell rendered in the
 * arranger grid. It's a tiny GuiNode: base init + (ch, row) + backref
 * to the Arranger. We don't need an InstChipGuiNode-style embedded
 * GuiNode because there's nothing graph-children-related to manage —
 * the arranger window builds a flat grid of these directly.
 *
 * Draw: cell outline, tinted fill when selected. Text content + the
 * playhead marker are intentionally deferred — they're window-level
 * concerns that layer on once visibleStart is wired up in later tasks.
 */
typedef struct {
	GuiNode base;
	Arranger *arranger;
	int ch;
	int row;
} ArrangerCellGuiNode;

static void drawArrangerCellGuiNode(void *self) {
	GuiNode *n = (GuiNode *)self;
	Color fill = n->selected ? cs.selectedCell : cs.defaultCell;
	DrawRectangle(n->x, n->y, n->w, n->h, fill);
	DrawRectangleLines(n->x, n->y, n->w, n->h, cs.outlineColour);
}

bool isArrangerCellNode(const GuiNode *n) {
	return n != NULL && n->draw == drawArrangerCellGuiNode;
}

void getArrangerCellCoords(const GuiNode *n, int *x, int *y) {
	if(!n || !isArrangerCellNode(n)) {
		if(x) *x = -1;
		if(y) *y = -1;
		return;
	}
	const ArrangerCellGuiNode *cell = (const ArrangerCellGuiNode *)n;
	if(x) *x = cell->ch;
	if(y) *y = cell->row;
}

GuiNode *createArrangerCellGuiNode(int x, int y, int w, int h, bool selected, Arranger *arranger, int ch, int row) {
	ArrangerCellGuiNode *cell = (ArrangerCellGuiNode *)malloc(sizeof(ArrangerCellGuiNode));
	if(!cell) return NULL;
	memset(cell, 0, sizeof(ArrangerCellGuiNode));
	if(!initGuiNode(&cell->base, x, y, w, h, 0, na_horizontal, "cell", true, selected)) {
		free(cell);
		return NULL;
	}
	cell->base.draw = drawArrangerCellGuiNode;
	cell->base.drawable = true;
	cell->arranger = arranger;
	cell->ch = ch;
	cell->row = row;
	return &cell->base;
}

/* Task 9: preset-load-list state. g_loadList is populated by guiOpenLoadList
 * by enumerating data/instrument_presets/ .ipb files, stripping the
 * extension, and sorting alphabetically. While g_loadListActive is true,
 * handlePresetUiInput routes UP/DOWN/START/SELECT to the load-list node
 * regardless of which node is selected in the instrument graph. */
#define LOADLIST_MAX 256
#define LOADLIST_NAME_MAX 32
typedef struct {
	char names[LOADLIST_MAX][LOADLIST_NAME_MAX];
	int count;
	int highlight;
} PresetLoadList;
static PresetLoadList g_loadList;
static bool g_loadListActive = false;

bool guiIsLoadListActive(void) { return g_loadListActive; }

/* qsort helper: compare two fixed-width name strings lexicographically. */
static int loadListCmp(const void *a, const void *b) {
	const char *na = (const char *)a;
	const char *nb = (const char *)b;
	return strncmp(na, nb, LOADLIST_NAME_MAX);
}

/* Strip the trailing ".ipb" from a filename in-place; no-op if the
 * suffix is absent. */
static void stripIpbExtension(char *s) {
	size_t len = strlen(s);
	const char *suffix = ".ipb";
	size_t slen = strlen(suffix);
	if(len > slen && strcmp(s + len - slen, suffix) == 0) {
		s[len - slen] = '\0';
	}
}

void guiOpenLoadList(void);

void guiOpenLoadList(void) {
	g_loadList.count = 0;
	g_loadList.highlight = 0;
	DirectoryList *dl = createDirectoryList();
	if(dl) {
		populateDirectoryList(dl, "data/instrument_presets/");
		for(size_t i = 0; i < dl->count && g_loadList.count < LOADLIST_MAX; i++) {
			/* Take only the basename so we don't show the dir prefix. */
			const char *path = dl->file_paths[i];
			const char *slash = strrchr(path, '/');
			const char *base = slash ? slash + 1 : path;
			strncpy(g_loadList.names[g_loadList.count], base, LOADLIST_NAME_MAX - 1);
			g_loadList.names[g_loadList.count][LOADLIST_NAME_MAX - 1] = '\0';
			stripIpbExtension(g_loadList.names[g_loadList.count]);
			g_loadList.count++;
		}
		freeDirectoryList(dl);
	}
	if(g_loadList.count > 1) {
		qsort(g_loadList.names, (size_t)g_loadList.count, LOADLIST_NAME_MAX, loadListCmp);
	}
	g_loadListActive = true;
	/* Task 7: also push the load-list layer so the user can navigate
	 * the presets within an overlay graph. */
	if(igui) {
		guiBuildLoadListLayer(igui);
	}
}

static void guiCloseLoadList(void) {
	g_loadListActive = false;
	if(igui) {
		popLayer(&igui->overlayLayers);
	}
}

/* Task 8: overwrite-modal state. The modal is the only modal in the app
 * (per the brief), so a single-state-machine + a pending-name slot is
 * sufficient. g_overwriteChoice is the user's current YES/NO selection
 * while the modal is up (defaults to NO = false on open). */
static ModalState g_modalState = MODAL_NONE;
static char g_pendingName[33];
static bool g_overwriteChoice; /* false = NO, true = YES */

void guiSetOverwritePending(const char *name) {
	if(!name) {
		return;
	}
	strncpy(g_pendingName, name, sizeof(g_pendingName) - 1);
	g_pendingName[sizeof(g_pendingName) - 1] = '\0';
	g_overwriteChoice = false; /* safe default = NO */
	g_modalState = MODAL_CONFIRM_OVERWRITE;
	/* Task 7: push the overwrite-confirm layer so the user can
	 * navigate YES/NO with arrow keys + START. */
	if(igui) {
		guiBuildOverwriteLayer(igui, name);
	}
}

bool guiIsModalOpen(void) { return g_modalState != MODAL_NONE; }

/* Task 6: forward decl — defined further down. Used by both
 * guiSavePreset() and the overwrite-confirmation path. */
static void markInstrumentSavedAs(Instrument *inst, const char *name);

PresetFileResult guiSavePreset(Instrument *inst, const char *name) {
	if(!inst || !name) {
		return PRESET_ERROR_FORMAT;
	}
	PresetFileResult r;
	if(inst->selectedPresetIndex) {
		int curSlot = getParameterValueAsInt(inst->selectedPresetIndex);
		if(curSlot >= inst->presetBank->presetCount) {
			/* Task 8: parked on a blank slot -- save INTO it so the
			 * bank doesn't grow past the slot the user is viewing. */
			r = saveInstrumentAsPresetToSlot(inst, name, "data/instrument_presets/", curSlot);
		} else {
			r = saveInstrumentAsPreset(inst, name, "data/instrument_presets/");
		}
	} else {
		r = saveInstrumentAsPreset(inst, name, "data/instrument_presets/");
	}
	if(r == PRESET_EXISTS) {
		/* Defer the actual overwrite to the modal: the user hasn't
		 * confirmed yet. handlePresetUiInput drives the modal and
		 * calls saveInstrumentAsPresetOverwrite() on YES. We still
		 * propagate PRESET_EXISTS to the caller (commitPresetName)
		 * so it can skip firing either flash — the modal is the UI. */
		guiSetOverwritePending(name);
	} else if(r == PRESET_OK) {
		/* Task 6: save success → live state now matches disk, so
		 * the dirty flag clears and the loaded snapshot updates
		 * to this preset's name. */
		markInstrumentSavedAs(inst, name);
	}
	return r;
}

/* Task 6 stub: full modal layer lands in Task 7. Currently this is
 * a no-op so the load-list button can call it without crashing.
 * Task 7 will replace this body with a modalState push and a
 * three-way choice (discard / save / cancel) that falls through
 * to guiOpenLoadList() on discard. */
// (removed in Task 7: real implementation is below)

void guiShowDirtyConfirmModal(Instrument *inst) {
	if(!inst) {
		return;
	}
	/* Task 7: the dirty-confirm layer's SAVE branch uses g_pendingName,
	 * so seed it from the currently-loaded preset's name. The user
	 * edited this preset and we're offering to flush those edits to
	 * the same slot on disk. */
	strncpy(g_pendingName, inst->loaded.name, sizeof(g_pendingName) - 1);
	g_pendingName[sizeof(g_pendingName) - 1] = '\0';
	if(igui) {
		guiBuildDirtyConfirmLayer(igui, inst);
	}
}

/* Task 6 helper: after a successful save (or overwrite) the live
 * instrument now matches a specific preset on disk. Locate the slot
 * in the bank by name and stamp the LoadedPreset snapshot, which
 * also clears the dirty bit. Sharing this between the new-save
 * branch in guiSavePreset() and the overwrite branch in
 * handlePresetUiInput() keeps the two save paths from drifting. */
static void markInstrumentSavedAs(Instrument *inst, const char *name) {
	if(!inst || !name || !name[0]) {
		return;
	}
	PresetBank *pb = inst->presetBank;
	if(!pb) {
		return;
	}
	for(int i = 0; i < pb->presetCount; i++) {
		if(strncmp(pb->patches[i].name, name, sizeof(pb->patches[i].name)) == 0) {
			markPresetLoaded(inst, name);
			return;
		}
	}
}

/* Task 7: Layer-based overlay system. The three modals (overwrite,
 * dirty-confirm, load-list) each build a self-contained Graph of
 * action nodes and push it as a Layer on the instrument's overlay
 * stack. Input is routed to the topmost layer; drawing stacks
 * bottom-up. */

/* Modal-side callbacks. These close the topmost layer (pop it) and
 * perform the user-confirmed action. They store the pending state
 * in file-static globals below so they don't need to thread
 * per-layer state. */
static void cbOverwriteConfirm(void *ctx) {
	Instrument *inst = (Instrument *)ctx;
	InstrumentGui *ig = igui;
	if(inst && ig) {
		saveInstrumentAsPresetOverwrite(inst, g_pendingName, "data/instrument_presets/");
		markInstrumentSavedAs(inst, g_pendingName);
	}
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
	g_modalState = MODAL_NONE;
}

static void cbOverwriteCancel(void *ctx) {
	(void)ctx;
	InstrumentGui *ig = igui;
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
	g_modalState = MODAL_NONE;
}

static void cbDirtyDiscard(void *ctx) {
	Instrument *inst = (Instrument *)ctx;
	InstrumentGui *ig = igui;
	/* Discard edits and proceed straight to the load list. */
	if(inst) {
		markPresetLoaded(inst, NULL);
	}
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
	if(inst) {
		guiOpenLoadList();
	}
}

static void cbDirtySave(void *ctx) {
	Instrument *inst = (Instrument *)ctx;
	InstrumentGui *ig = igui;
	/* Save with the currently-staged name. If it exists, push the
	 * overwrite layer on top of this one; otherwise commit and
	 * pop straight to the load list. */
	if(inst && inst->presetBank) {
		PresetFileResult r = saveInstrumentAsPreset(inst, g_pendingName, "data/instrument_presets/");
		if(r == PRESET_OK) {
			markInstrumentSavedAs(inst, g_pendingName);
			if(ig) {
				popLayer(&ig->overlayLayers);
			}
			guiOpenLoadList();
			return;
		}
		if(r == PRESET_EXISTS) {
			/* Replace the dirty-confirm with overwrite-confirm.
			 * Pop the dirty layer first so the overwrite layer
			 * is the only thing on the stack. */
			if(ig) {
				popLayer(&ig->overlayLayers);
			}
			guiBuildOverwriteLayer(ig, g_pendingName);
			return;
		}
	}
	/* Any other error: just close the dirty-confirm and bail. */
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
}

static void cbDirtyCancel(void *ctx) {
	(void)ctx;
	InstrumentGui *ig = igui;
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
}

static void cbLoadListPick(void *ctx) {
	/* ctx is the index into g_loadList.names (and inst->presetBank->patches). */
	int idx = (int)(intptr_t)ctx;
	InstrumentGui *ig = igui;
	Instrument *inst = ig ? ig->vm->instruments[*ig->selectedInstrument] : NULL;
	if(!inst || !inst->presetBank || idx < 0 || idx >= g_loadList.count) {
		if(ig) {
			popLayer(&ig->overlayLayers);
		}
		g_loadListActive = false;
		return;
	}
	if(idx < inst->presetBank->presetCount) {
		/* Task 8 slot model: the chosen preset lands in the CURRENT
		 * bank slot (the one the user had selected via PREV/NEXT), not
		 * appended somewhere new. This makes "load a preset into this
		 * blank slot" work: the blank slot (index >= presetCount) gets
		 * the loaded patch, and the selection stays on that slot. */
		int slot = getParameterValueAsInt(inst->selectedPresetIndex);
		if(slot < 0 || slot >= PRESET_BANK_SLOTS) {
			slot = 0;
		}
		inst->presetBank->patches[slot] = inst->presetBank->patches[idx];
		applyInstrumentPreset(inst, inst->presetBank->patches[slot]);
		/* Task 6: refresh the loaded-preset snapshot so dirty=false and
		 * loaded.name is set. Without this the dirty-confirm gate in
		 * cbOpenLoadList never opens because isInstrumentDirty returns
		 * false on a fresh instrument with no baseline name. */
		markPresetLoaded(inst, inst->presetBank->patches[slot].name);
		if(inst->selectedPresetIndex) {
			inst->selectedPresetIndex->baseValue = (float)slot;
			inst->selectedPresetIndex->currentValue = (float)slot;
		}
		if(inst->vm) {
			pthread_mutex_lock(&g_audioLock);
			inst->rebuilding = true;
			rebuildVoicesForInstrument(inst->vm, inst);
			inst->rebuilding = false;
			pthread_mutex_unlock(&g_audioLock);
		}
		rebuildInstrumentGraph();
	}
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
	g_loadListActive = false;
}

static void cbLoadListCancel(void *ctx) {
	(void)ctx;
	InstrumentGui *ig = igui;
	if(ig) {
		popLayer(&ig->overlayLayers);
	}
	g_loadListActive = false;
}

/* Build the overwrite-confirm layer. The graph has two action
 * buttons: YES (confirms overwrite) and NO (cancels). Both pop
 * the layer; YES additionally commits the overwrite. */
void guiBuildOverwriteLayer(InstrumentGui *ig, const char *pendingName) {
	if(!ig) {
		return;
	}
	if(pendingName) {
		strncpy(g_pendingName, pendingName, sizeof(g_pendingName) - 1);
		g_pendingName[sizeof(g_pendingName) - 1] = '\0';
	}
	g_overwriteChoice = false; /* safe default: NO */
	g_modalState = MODAL_CONFIRM_OVERWRITE;

	/* The layer's graph. We use a vertical container as the root
	 * and the two action buttons as children. The createGraph()
	 * root covers the whole screen — the actual visual panel is
	 * drawn by the layer's draw step (DrawRectangle + DrawText),
	 * not by the graph nodes themselves. Action buttons need a
	 * position and size; we give them the same y to make the
	 * horizontal nav obvious (LEFT/RIGHT). */
	Graph *g = createGraph(na_horizontal);
	const int py = (SCREEN_H - 80) / 2;
	const int px = (SCREEN_W - 280) / 2;
	GuiNode *noBtn = createActionBtnGuiNode(px + 30, py + 44, 100, 22, 0, na_horizontal, "NO", 0, cbOverwriteCancel, NULL);
	noBtn->name = strdup("OVERWRITE_NO");
	GuiNode *yesBtn = createActionBtnGuiNode(px + 150, py + 44, 100, 22, 0, na_horizontal, "YES", 0, cbOverwriteConfirm, ig->vm->instruments[*ig->selectedInstrument]);
	yesBtn->name = strdup("OVERWRITE_YES");
	appendItem(g->root, noBtn, 1);
	appendItem(g->root, yesBtn, 1);
	changeGraphSelection(g, noBtn);

	Layer *layer = createLayer(g, px, py, 280, 80, "OVERWRITE", true, true);
	pushLayer(&ig->overlayLayers, layer);
}

/* Build the dirty-confirm layer. Three-way: DISCARD, CANCEL, SAVE.
 * The graph is a single row of three action buttons. */
void guiBuildDirtyConfirmLayer(InstrumentGui *ig, Instrument *inst) {
	if(!ig) {
		return;
	}
	(void)inst; /* inst is recovered from the ig->vm at callback time */
	g_modalState = MODAL_CONFIRM_OVERWRITE; /* keep legacy field in sync */

	Graph *g = createGraph(na_horizontal);
	const int pw = 360;
	const int ph = 70;
	const int px = (SCREEN_W - pw) / 2;
	const int py = (SCREEN_H - ph) / 2;
	GuiNode *discardBtn = createActionBtnGuiNode(px + 10, py + 38, 100, 22, 0, na_horizontal, "DISCARD", 1, cbDirtyDiscard, ig->vm->instruments[*ig->selectedInstrument]);
	GuiNode *saveBtn = createActionBtnGuiNode(px + 130, py + 38, 100, 22, 0, na_horizontal, "SAVE", 0, cbDirtySave, ig->vm->instruments[*ig->selectedInstrument]);
	GuiNode *cancelBtn = createActionBtnGuiNode(px + 250, py + 38, 100, 22, 0, na_horizontal, "CANCEL", 0, cbDirtyCancel, NULL);
	discardBtn->name = strdup("DIRTY_DISCARD");
	saveBtn->name = strdup("DIRTY_SAVE");
	cancelBtn->name = strdup("DIRTY_CANCEL");
	appendItem(g->root, discardBtn, 1);
	appendItem(g->root, saveBtn, 1);
	appendItem(g->root, cancelBtn, 1);
	g->selected = discardBtn;

	Layer *layer = createLayer(g, px, py, pw, ph, "DIRTY_CONFIRM", true, true);
	pushLayer(&ig->overlayLayers, layer);
}

/* Build the load-list layer. The graph is a vertical list of
 * action buttons, one per preset in the bank. Up/Down moves
 * through the list; START triggers cbLoadListPick with the
 * preset index. */
void guiBuildLoadListLayer(InstrumentGui *ig) {
	if(!ig) {
		return;
	}
	g_loadListActive = true;

	Graph *g = createGraph(na_vertical);
	const int pw = 200;
	const int rowH = 14;
	const int maxRows = 8;
	int ph = (g_loadList.count < maxRows ? g_loadList.count : maxRows) * rowH + 8;
	if(ph < rowH * 2) {
		ph = rowH * 2;
	}
	const int px = (SCREEN_W - pw) / 2;
	const int py = (SCREEN_H - ph) / 2;

	int rows = g_loadList.count;
	if(rows > maxRows) {
		rows = maxRows;
	}
	GuiNode *firstEntry = NULL;
	for(int i = 0; i < rows; i++) {
		GuiNode *row = createActionBtnGuiNode(px, py + i * rowH, pw, rowH, 0, na_horizontal, g_loadList.names[i], 0, cbLoadListPick, (void *)(intptr_t)i);
		row->name = strdup(g_loadList.names[i]);
		appendItem(g->root, row, 1);
		if(!firstEntry) {
			firstEntry = row;
		}
	}
	/* Cancel/back button lives at the bottom of the list so the
	 * default selection is the first preset entry. */
	GuiNode *cancelBtn = createActionBtnGuiNode(px, py + ph - rowH, pw, rowH, 0, na_horizontal, "< BACK", 0, cbLoadListCancel, NULL);
	cancelBtn->name = strdup("LOADLIST_BACK");
	appendItem(g->root, cancelBtn, 1);
	if(firstEntry) {
		changeGraphSelection(g, firstEntry);
	} else {
		changeGraphSelection(g, cancelBtn);
	}

	Layer *layer = createLayer(g, px, py, pw, ph, "LOADLIST", true, true);
	pushLayer(&ig->overlayLayers, layer);
}

bool guiPopOverlay(InstrumentGui *ig) {
	if(!ig || ig->overlayLayers.count == 0) {
		return false;
	}
	popLayer(&ig->overlayLayers);
	if(ig->overlayLayers.count == 0) {
		g_modalState = MODAL_NONE;
		g_loadListActive = false;
	}
	return true;
}

/* Task 7: stub fill. Pushed here as a layer so the LOAD button
 * gates the load list on the dirty bit (Task 6). */
// (the real guiShowDirtyConfirmModal definition is later in this file
// once getSelectedInstInstrument / guiBuildDirtyConfirmLayer are visible)

/* Task 7: legacy drawPresetModal is now a no-op; the layer system
 * owns modal drawing via layerStackDraw. Kept as a stub so any
 * existing call site compiles. */
void drawPresetModal(void) {
	/* no-op: see layerStackDraw in DrawGUI */
}

#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-. "
static int charIndex(char c) {
	if(c >= 'a' && c <= 'z') {
		c -= 32;
	}
	for(int i = 0; i < (int)strlen(NAME_CHARS); i++) {
		if(NAME_CHARS[i] == c) {
			return i;
		}
	}
	return 0;
}

static void cycleNameChar(PresetNameGuiNode *pn, int delta) {
	int idx = charIndex(pn->name[pn->cursor]);
	int count = (int)strlen(NAME_CHARS);
	idx = (idx + delta + count) % count;
	pn->name[pn->cursor] = NAME_CHARS[idx];
}

/* Task 5: forward decl so commitPresetName (defined below) can call it
 * before the static definition. The real implementation lives next to
 * effectiveNameLen further down. */
static int currentFrameIndex(void);

static void commitPresetName(PresetNameGuiNode *pn) {
	/* trim + default, then run the save flow (overwrite check happens here) */
	if(strlen(pn->name) == 0 || strspn(pn->name, " ") == strlen(pn->name)) {
		strncpy(pn->name, "UNNAMED", sizeof(pn->name) - 1);
		pn->name[sizeof(pn->name) - 1] = '\0';
	}
	/* Task 5: strip trailing spaces. The old "Xm1  " (cursor-left-padded
	 * to 32 chars) used to bypass the EXISTS check because strncmp(_,32)
	 * matched the trailing NULs of the shorter stored name. With the
	 * trim the save call below always sees the same "Xm1" the bank
	 * already holds, so PRESET_EXISTS fires and the modal opens. */
	for(int i = (int)strlen(pn->name) - 1; i >= 0 && pn->name[i] == ' '; i--) {
		pn->name[i] = '\0';
	}
	pn->editing = false;
	PresetFileResult r = guiSavePreset(pn->inst, pn->name);
	/* PRESET_OK -> green/success flash. PRESET_EXISTS is handled out of
	 * band by the modal (guiSetOverwritePending), so it's NOT an error
	 * from the user's perspective; the flash fires when the overwrite
	 * actually commits via saveInstrumentAsPresetOverwrite.
	 *
	 * Every other PresetFileResult (format / io / null arg) is a real
	 * failure — light the red error flash. */
	if(r == PRESET_OK) {
		pn->savedFlashUntil = currentFrameIndex() + 30;
	} else if(r != PRESET_EXISTS) {
		pn->errorFlashUntil = currentFrameIndex() + 30;
	}
}

/* Forward decls so handlePresetUiInput can activate the SAVE/LOAD buttons. */
static void cbFocusNameNode(void *ctx);
static void cbOpenLoadList(void *ctx);
static void cbCycleVoiceType(void *ctx);
static void cbPresetPrev(void *ctx);
static void cbPresetNext(void *ctx);

/* Task 7+: PREV/NEXT action buttons. Activate on KM_EDIT (same as
 * SAVE/LOAD). The Instrument is the ctx. setParameterBaseValue triggers
 * selectedPresetIndex's onChange (cb_setInstrumentPreset) which applies
 * the new preset and rebuilds voices. */
static void cbPresetPrev(void *ctx) {
	Instrument *inst = (Instrument *)ctx;
	if(!inst || !inst->selectedPresetIndex || !inst->presetBank) {
		return;
	}
	int cur = getParameterValueAsInt(inst->selectedPresetIndex);
	if(cur > 0) {
		setParameterBaseValue(inst->selectedPresetIndex, (float)(cur - 1));
	}
}

/* Task 8: PREV/NEXT walk the full PRESET_BANK_SLOTS range, including
 * blank slots (indices >= presetCount hold a default FM patch). */
static void cbPresetNext(void *ctx) {
	Instrument *inst = (Instrument *)ctx;
	if(!inst || !inst->selectedPresetIndex || !inst->presetBank) {
		return;
	}
	int cur = getParameterValueAsInt(inst->selectedPresetIndex);
	if(cur < PRESET_BANK_SLOTS - 1) {
		setParameterBaseValue(inst->selectedPresetIndex, (float)(cur + 1));
	}
}

/* Task 6: TYPE action button cycles SAMPLE -> FM -> BLEP -> SAMPLE for the
 * currently focused instrument. ctx is the VoiceManager (the brief asks for
 * VM-resolution rather than a stored channel so the same callback can serve
 * every meta row across all channels). We locate the channel by pointer
 * identity against getSelectedInstInstrument() BEFORE the swap (because
 * setInstrumentVoiceType frees the old Instrument and replaces it). If the
 * swap succeeds we tear down + rebuild the affected instrument screen so
 * the new voice-type-specific row (appendFMInstControlNode / appendSample
 * / appendBlep) appears below the meta row, and the dial / preset bindings
 * move onto the new Instrument's params. */
static void cbCycleVoiceType(void *ctx) {
	VoiceManager *vm = (VoiceManager *)ctx;
	if(!vm || vm->enabledChannels <= 0) {
		return;
	}
	Instrument *sel = getSelectedInstInstrument();
	if(!sel) {
		return;
	}
	int channel = -1;
	for(int i = 0; i < vm->enabledChannels; i++) {
		if(vm->instruments[i] == sel) {
			channel = i;
			break;
		}
	}
	if(channel < 0) {
		return;
	}
	VoiceType cur = sel->voiceType;
	VoiceType next;
	switch(cur) {
		case VOICE_TYPE_SAMPLE: next = VOICE_TYPE_FM;   break;
		case VOICE_TYPE_FM:     next = VOICE_TYPE_BLEP; break;
		case VOICE_TYPE_BLEP:   next = VOICE_TYPE_SAMPLE; break;
		default:                next = VOICE_TYPE_FM;   break;
	}
	if(setInstrumentVoiceType(vm, channel, next)) {
		rebuildInstrumentGraph();
	}
}

/* Task 4: number of valid cursor slots for the preset name node. name[33]
 * holds 32 chars + 1 NUL. effectiveNameLen returns the exclusive upper bound
 * on the cursor index (the number of editable slots). NULL or empty name
 * returns 0 so the cursor stays pinned at slot 0. Capped at PRESET_NAME_MAX. */
#define PRESET_NAME_MAX 32
static int effectiveNameLen(const char *name) {
	if(!name || !name[0]) {
		return 0;
	}
	int n = (int)strlen(name);
	if(n > PRESET_NAME_MAX) {
		n = PRESET_NAME_MAX;
	}
	return n;
}

/* Task 5: cheap wall-clock frame index used by the save/error flash
 * bookkeeping in PresetNameGuiNode. raylib's GetTime returns seconds
 * since InitWindow(); multiplying by 60 gives a 60fps counter. Not
 * exactly frame-accurate (the real dt may differ), but stable enough
 * for "flash for ~0.5s after a save". Flash getters compare against
 * this with a strict `>` so the flash dies exactly at the boundary. */
static int currentFrameIndex(void) {
	return (int)(GetTime() * 60.0f);
}

bool handlePresetUiInput(InputState *is, Instrument *inst) {
	/* Task 7: while ANY overlay layer is open we route the entire input
	 * stream to the topmost layer's graph via layerStackInput. This
	 * replaces the old "is modal open" / "is load-list active" branches
	 * — those state flags are still maintained (for legacy assertions)
	 * but the input is now driven by the layer system's navigateGraph +
	 * actionCb machinery. Returning true here is critical: the rest of
	 * the instrument input must NOT run while a layer is up, otherwise
	 * arrow keys would simultaneously navigate the layer AND change
	 * instrument parameter values. */
	InstrumentGui *ig = getInstrumentGui();
	if(ig && !layerStackIsEmpty(&ig->overlayLayers)) {
		layerStackInput(&ig->overlayLayers, is);
		return true;
	}

	/* Task 8: while the overwrite modal is up we consume ALL input and
	 * drive the modal state machine. This must run BEFORE the name-node
	 * check so a modal doesn't accidentally drop back to name editing. */
	if(g_modalState == MODAL_CONFIRM_OVERWRITE) {
		if(isKeyJustPressed(is, KM_LEFT) || isKeyJustPressed(is, KM_RIGHT)) {
			g_overwriteChoice = !g_overwriteChoice;
			return true;
		}
		if(isKeyJustPressed(is, KM_START)) {
			if(g_overwriteChoice) {
				/* User confirmed overwrite — actually overwrite now.
				 * saveInstrumentAsPresetOverwrite skips the EXISTS check
				 * and replaces the bank slot at g_pendingName. */
				saveInstrumentAsPresetOverwrite(inst, g_pendingName, "data/instrument_presets/");
				/* Task 6: after a successful overwrite, live state now
				 * matches the on-disk preset, so refresh the LoadedPreset
				 * snapshot and clear the dirty bit. */
				markInstrumentSavedAs(inst, g_pendingName);
			}
			/* Either branch: close the modal. On NO we just drop back to
			 * the name-edit screen so the user can change the name. */
			g_modalState = MODAL_NONE;
			return true;
		}
		if(isKeyJustPressed(is, KM_SELECT)) {
			/* SELECT also dismisses (same as choosing NO) — handy escape. */
			g_modalState = MODAL_NONE;
			return true;
		}
		/* Consume anything else while the modal is up so the underlying
		 * scene doesn't react to stray presses. */
		return true;
	}

	/* Task 9: while the load-list is open we hijack UP/DOWN/START/SELECT
	 * to drive list navigation. We don't need the selected node to be
	 * the load-list node — the LOAD button's callback already opened the
	 * list, and we should keep routing input to it regardless of where
	 * the user navigated in the meantime. */
	if(g_loadListActive) {
		if(g_loadList.count == 0) {
			/* Nothing to load — just close on any input and fall through. */
			guiCloseLoadList();
			return true;
		}
		if(isKeyJustPressed(is, KM_UP)) {
			g_loadList.highlight = (g_loadList.highlight + g_loadList.count - 1) % g_loadList.count;
			return true;
		}
		if(isKeyJustPressed(is, KM_DOWN)) {
			g_loadList.highlight = (g_loadList.highlight + 1) % g_loadList.count;
			return true;
		}
		if(isKeyJustPressed(is, KM_START)) {
			/* Find the highlighted preset in the bank and apply it. The
			 * name list came from the directory, so the bank's patches
			 * are 1:1 with the displayed names. */
			const char *want = g_loadList.names[g_loadList.highlight];
			if(inst && inst->presetBank) {
				for(int i = 0; i < inst->presetBank->presetCount; i++) {
					if(strcmp(inst->presetBank->patches[i].name, want) == 0) {
						applyInstrumentPreset(inst, inst->presetBank->patches[i]);
						/* Mirror the preset-selector callback pattern
						 * (see cb_setInstrumentPreset): the selector param
						 * was recreated at 0 by applyInstrumentPreset, so
						 * write the index directly to avoid re-entering
						 * the onChange callback. */
						if(inst->selectedPresetIndex) {
							inst->selectedPresetIndex->baseValue = (float)i;
							inst->selectedPresetIndex->currentValue = (float)i;
						}
						if(inst->vm) {
							inst->rebuilding = true;
							rebuildVoicesForInstrument(inst->vm, inst);
							inst->rebuilding = false;
						}
						/* NOTE: arranger->channelSlots[channel] is the
						 * authoritative "which patch this channel uses"
						 * slot for save/load sequencing. There's no
						 * backref from inst or vm to the arranger, so
						 * updating the slot from here would require a new
						 * GUI-side getter or a vm->arranger field. We
						 * skip it for now — loading updates the live
						 * instrument, and the next save-song round-trip
						 * will reconcile channelSlots when needed. */
						break;
					}
				}
			}
			guiCloseLoadList();
			return true;
		}
		if(isKeyJustPressed(is, KM_SELECT)) {
			/* Cancel without loading. */
			guiCloseLoadList();
			return true;
		}
		/* List is open but no relevant key was pressed — fall through
		 * to the rest of the instrument input path so L/R navigation
		 * (handled downstream) still works. */
		return false;
	}

	(void)inst;
	Graph *g = getSelectedInstGraph();
	GuiNode *sel = (g) ? g->selected : NULL;
	/* KM_EDIT (z) on a preset action button (SAVE/LOAD) activates it.
	 * KM_START deliberately does NOT fire buttons -- START is reserved
	 * for modal confirmation/cancel semantics. Holding KM_EDIT to fire
	 * the button mirrors the dial-edit gesture (KM_EDIT + arrow edits
	 * the selected dial); here KM_EDIT alone activates the button. */
	if(sel && isKeyJustPressed(is, KM_EDIT)) {
		/* Action buttons (SAVE/LOAD/PREV/NEXT) store their handler in
		 * `actionCb` (an ActionCallback, signature `void(void*)`), not
		 * `callback` (which is reserved for OnPressCallback on dial
		 * nodes). */
		if(sel->actionCb == cbOpenLoadList || sel->actionCb == cbFocusNameNode
		   || sel->actionCb == cbPresetPrev || sel->actionCb == cbPresetNext
		   || sel->actionCb == cbCycleVoiceType) {
			sel->actionCb(sel->actionCtx);
			/* PREV/NEXT apply a preset, which rebuilds the paramList;
			 * the graph's dials still point at the freed params. Rebuild
			 * the graph so the dials re-address the new params and the
			 * UI reflects the applied preset. cbCycleVoiceType already
			 * rebuilds internally (it swaps the Instrument on the VM),
			 * so it does not belong in this branch. */
			if(sel->actionCb == cbPresetPrev || sel->actionCb == cbPresetNext) {
				rebuildInstrumentGraph();
			}
			return true;
		}
	}
	/* Task 4: edit mode is no longer auto-armed by a fresh selection. The
	 * user must press KM_EDIT to enter, and KM_EDIT/KM_SELECT to exit.
	 * Arrows cycle chars / move the cursor only while editing; when not
	 * editing they fall through to normal navigation. */
	if(!sel || !isPresetNameNode(sel)) {
		return false;
	}
	PresetNameGuiNode *pn = (PresetNameGuiNode *)sel;

	if(!pn->editing) {
		/* Selected but not editing: KM_EDIT (z) enters edit mode, KM_START
		 * commits, and arrows + KM_SELECT fall through to normal navigation. */
		if(isKeyJustPressed(is, KM_EDIT)) {
			pn->editing = true;
			return true;
		}
		if(isKeyJustPressed(is, KM_START)) {
			commitPresetName(pn);
			return true;
		}
		return false;
	}

	/* Editing: arrows cycle the active char (UP/DOWN) and move the cursor
	 * (LEFT/RIGHT, bounded to effectiveNameLen); KM_START commits; KM_EDIT
	 * and KM_SELECT toggle edit off. */
	if(isKeyJustPressed(is, KM_UP)) {
		cycleNameChar(pn, 1);
		return true;
	}
	if(isKeyJustPressed(is, KM_DOWN)) {
		cycleNameChar(pn, -1);
		return true;
	}
	if(isKeyJustPressed(is, KM_LEFT)) {
		if(pn->cursor > 0) {
			pn->cursor--;
		}
		return true;
	}
	if(isKeyJustPressed(is, KM_RIGHT)) {
		if(pn->cursor < effectiveNameLen(pn->name)) {
			pn->cursor++;
		}
		return true;
	}
	if(isKeyJustPressed(is, KM_START)) {
		/* trim + default, then run the save flow (overwrite check happens here) */
		pn->editing = false;
		commitPresetName(pn);
		return true;
	}
	if(isKeyJustPressed(is, KM_EDIT)) {
		pn->editing = false;
		return true;
	}
	if(isKeyJustPressed(is, KM_SELECT)) {
		pn->editing = false;
		return true;
	}
	return false;
}

static void drawPresetNameGuiNode(void *self) {
	PresetNameGuiNode *pn = (PresetNameGuiNode *)self;
	GuiNode *gn = (GuiNode *)pn;
	/* Task 5: pick the background tint based on which flash (if any) is
	 * active. Error flash wins over saved flash if both somehow fire in
	 * the same frame — PRESET_OK and PRESET_ERROR_* are mutually
	 * exclusive in commitPresetName, but be defensive anyway. Default
	 * is the original solid black. */
	Color bg = BLACK;
	if(currentFrameIndex() < pn->errorFlashUntil) {
		bg = cs.sampleBg;
	} else if(currentFrameIndex() < pn->savedFlashUntil) {
		bg = cs.sampleAltBg;
	}
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h }, bg);
	/* Selected-but-not-editing outline: the "you can press z to edit me"
	 * affordance. The inverted cursor block already covers the editing
	 * state, so the outline is only drawn outside edit mode. */
	if(gn->selected && !pn->editing) {
		DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0f, cs.sampleBorder);
	}
	/* copy the current name into the node once per selection */
	int n = (int)strlen(pn->name);
	if(n < 32) {
		n = 32;
	}
	int cellW = gn->w / 32;
	if(cellW < 20) {
		cellW = 20;
	}
	for(int i = 0; i < 32; i++) {
		int cx = gn->x + i * cellW;
		int cy = gn->y;
		char ch = (i < (int)strlen(pn->name)) ? pn->name[i] : ' ';
		if(pn->editing && i == pn->cursor) {
			DrawRectangle(cx, cy, cellW, gn->h, RED);          /* inverted cursor block */
			DrawText((char[]){ ch, '\0' }, cx + 1, cy + gn->h / 2 - 15, 30, BLACK);
		} else {
			DrawText((char[]){ ch, '\0' }, cx + 1, cy + gn->h / 2 - 15, 30, RED);
		}
	}
}

bool isPresetNameNode(GuiNode *n) {
	return n && n->draw == drawPresetNameGuiNode;
}

/* Task 5: flash getters. Strict `>` so the flash dies exactly on the
 * boundary frame. isPresetNameNode guards the downcast. */
bool presetNameGuiNodeSavedFlashActive(GuiNode *n) {
	if(!isPresetNameNode(n)) {
		return false;
	}
	PresetNameGuiNode *pn = (PresetNameGuiNode *)n;
	return currentFrameIndex() < pn->savedFlashUntil;
}

bool presetNameGuiNodeErrorFlashActive(GuiNode *n) {
	if(!isPresetNameNode(n)) {
		return false;
	}
	PresetNameGuiNode *pn = (PresetNameGuiNode *)n;
	return currentFrameIndex() < pn->errorFlashUntil;
}

/* Task 3: KM_EDIT + arrow dispatch guard. The action button and preset
 * name / load-list nodes also live in the instrument graph; calling
 * their `callback` slot with a float delta would either be meaningless
 * (action buttons have no Parameter*) or, for the name node, dispatch
 * into a NULL/stale function pointer and crash. Only true for a real
 * dial node (drawDialGuiNode + non-NULL OnPressCallback). */
bool isSelectedDialNode(const Graph *g) {
	if(!g || !g->selected) return false;
	GuiNode *n = g->selected;
	/* Both continuous (drawDialGuiNode) and discrete (drawDiscreteDialGuiNode)
	 * dials expose a value-callback OnPressCallback and respond to EDIT+arrow.
	 * Some discrete dials (e.g. ROUTE) overwrite draw with the discrete variant
	 * after createDialGuiNode; both must pass the guard so the harness can
	 * drive them via the scripted EDIT<arrow> opcodes. */
	return (n->draw == drawDialGuiNode || n->draw == drawDiscreteDialGuiNode)
		&& n->callback != NULL;
}

GuiNode *createPresetNameGuiNode(int x, int y, int w, int h, Instrument *inst, bool selected) {
	PresetNameGuiNode *pn = malloc(sizeof(PresetNameGuiNode));
	if(!pn) {
		return NULL;
	}
	GuiNode *gn = (GuiNode *)pn;
	if(!initGuiNode(gn, x, y, w, h, 0, na_horizontal, "PRESET_NAME", 1, 0)) {
		free(pn);
		return NULL;
	}
	pn->inst = inst;
	memset(pn->name, 0, sizeof(pn->name));
	/* seed from the current preset's name so the node shows what's loaded */
	if(inst && inst->selectedPresetIndex && inst->presetBank) {
		int idx = getParameterValueAsInt(inst->selectedPresetIndex);
		if(idx >= 0 && idx < inst->presetBank->presetCount) {
			strncpy(pn->name, inst->presetBank->patches[idx].name, sizeof(pn->name) - 1);
		}
	}
	pn->cursor = 0;
	pn->editing = false;
	g_presetNameNode = pn;
	/* Task 5: no flash active at construction. 0 is also strictly <= any
	 * currentFrameIndex() that returns >= 0 after raylib InitWindow. */
	pn->savedFlashUntil = 0;
	pn->errorFlashUntil = 0;
	gn->drawable = true;
	gn->draw = drawPresetNameGuiNode;
	return gn;
}

/* Task 8: SAVE action. Captures the current instrument under the name
 * shown in the preset-name node (g_presetNameNode), running the same
 * commit flow as KM_START on the name node. This makes the SAVE button
 * an actual save: guiSavePreset returns PRESET_EXISTS when the name is
 * already in the bank, which opens the overwrite-confirm modal. */
static void cbFocusNameNode(void *ctx) {
	(void)ctx;
	if(g_presetNameNode) {
		commitPresetName(g_presetNameNode);
	}
}
static void cbOpenLoadList(void *ctx) {
	/* Task 6: gate the load list on the dirty bit. If the user has
	 * edits in flight, yanking the instrument away to a different
	 * preset silently throws those edits on the floor — push the
	 * dirty-confirm modal instead and let the user pick
	 * discard/save/cancel. Task 7 fills in the modal body; for now
	 * the no-op stub returns without opening the list. */
	Instrument *inst = (Instrument *)ctx;
	if(inst && isInstrumentDirty(inst)) {
		guiShowDirtyConfirmModal(inst);
		return;
	}
	guiOpenLoadList();
}

/* Task 9: scrollable preset-load-list node. The list contents live in
 * the file-static g_loadList state populated by guiOpenLoadList(); this
 * node is purely a renderer + presence-in-the-graph. Identifying the
 * node by draw fn pointer (mirroring isPresetNameNode) lets handlePresetUiInput
 * route input even when the graph's selected node has moved on. */
#define LOADLIST_VISIBLE_ROWS 6
#define LOADLIST_ROW_PX 12

static void drawPresetLoadListNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h }, BLACK);
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 1.0f, cs.stepBorder);
	if(!g_loadListActive) {
		/* Closed: render an inert placeholder so the layout doesn't
		 * collapse in tests / harness. */
		DrawText("(CLOSED)", gn->x + 6, gn->y + 6, 10, cs.stepClosed);
		return;
	}
	int rows = LOADLIST_VISIBLE_ROWS;
	if(rows > g_loadList.count) {
		rows = g_loadList.count;
	}
	for(int i = 0; i < rows; i++) {
		int ry = gn->y + 4 + i * LOADLIST_ROW_PX;
		bool isHi = (i == g_loadList.highlight);
		if(isHi) {
			DrawRectangle(gn->x + 2, ry - 1, gn->w - 4, LOADLIST_ROW_PX - 2, RED);
		}
		Color fg = isHi ? BLACK : RED;
		const char *label = g_loadList.names[i];
		DrawText(label, gn->x + 6, ry, 10, fg);
	}
}

bool isPresetLoadListNode(GuiNode *n) {
	return n && n->draw == drawPresetLoadListNode;
}

GuiNode *createPresetLoadListNode(int x, int y, int w, int h) {
	GuiNode *gn = (GuiNode *)malloc(sizeof(GuiNode));
	if(!gn) {
		return NULL;
	}
	if(!initGuiNode(gn, x, y, w, h, 0, na_horizontal, "PRESET_LOADLIST", 0, 0)) {
		free(gn);
		return NULL;
	}
	gn->drawable = true;
	gn->draw = drawPresetLoadListNode;
	return gn;
}



void appendPresetControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "PRESET_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;
	/* PREV/NEXT action buttons replace the old PRESET dial. The dial
	 * had display problems (it showed the modulated currentValue and
	 * swung wildly when the algo param got a coarse >-range increment)
	 * and its left/right increment semantics weren't obvious in the
	 * arcade UX. Two action buttons + a KM_EDIT fire is unambiguous. */
	GuiNode *prevBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "PREV", selected, cbPresetPrev, inst);
	GuiNode *pad1 = createBlankGuiNode();
	GuiNode *nameNode = createPresetNameGuiNode(0, 0, 100, 100, inst, 0);
	GuiNode *saveBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "SAVE", 0, cbFocusNameNode, inst);
	GuiNode *loadBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LOAD", 0, cbOpenLoadList, inst);
	GuiNode *nextBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "NEXT", 0, cbPresetNext, inst);
	appendItem(btnwrap, prevBtn, 1);
	appendItem(btnwrap, pad1, 7);
	appendItem(btnwrap, nameNode, 4);
	appendItem(btnwrap, saveBtn, 1);
	appendItem(btnwrap, loadBtn, 1);
	appendItem(btnwrap, nextBtn, 1);
	appendItem(container, btnwrap, weight);
	/* Task 7: the preset load list is no longer a child node in the
	 * PRESET controls row. It now lives as its own overlay Layer,
	 * pushed onto ig->overlayLayers when the user opens it (see
	 * guiOpenLoadList / guiBuildLoadListLayer). The action button
	 * (loadBtn) is still here so the user can request the list, but
	 * the list itself is rendered in the overlay. */
	if(selected) {
		g->selected = prevBtn;
	}
}

/* Task 6: meta row (TYPE toggle + VOICES dial + WIDTH placeholder).
 * Sits at the top of instwrap, above the preset row and the
 * voice-type-specific control row. Mirrors the horizontal wrapper +
 * action-buttons + dial layout of appendPresetControlNode so the
 * visual rhythm matches.
 *
 * TYPE is an action button (not a dial) because the value domain is
 * a 3-element enum (SAMPLE/FM/BLEP) and the arcade input grammar uses
 * KM_EDIT to fire an action and arrow keys to walk between controls.
 * Its label is the current voiceTypeTag at graph-build time — the
 * callback rebuilds the graph, which re-runs this builder and re-derives
 * the new tag.
 *
 * VOICES is a dial bound to inst->voiceCountParam (range 1..8, created
 * in init_instrument with cb_setVoiceCount wired via createParameterPro).
 * incParameterBaseValue drives the dial; cb_setVoiceCount rebuilds the
 * channel's voice pool and writes back the snapped baseValue so the
 * dial and the actual voiceCount stay in lockstep (setChannelVoiceCount
 * clamps + rewrites, which absorbs the dial's fp jitter).
 *
 * WIDTH is a 6-weight blank placeholder reserved for future stereo
 * width / unison-spread dial. Keeping it in the tree now means we
 * preserve the visual proportions of the row and the future dial just
 * needs a swap, not a layout change.
 *
 * SELECTION: passing `selected=false` is intentional. createInstGraph
 * passes `true` only to the voice-type-specific row (appendFMInst
 * ControlNode / Sample / Blep), which assigns g->selected to its first
 * rat1/freq dial — that's the cursor position the existing fixtures
 * (preset_save_load.txt, add_route_delete.txt) navigate from. Stealing
 * selection here would re-route the cursor on every rebuild and break
 * those fixtures. */
void appendMetaControlNode(Graph *g, GuiNode *container, Instrument *inst, VoiceManager *vm, int channel, int weight, bool selected) {
	(void)g;
	(void)channel;
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "META", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	/* TYPE label is the current voiceType tag. built once per rebuild;
	 * initGuiNode strdup's the name so the stack buffer is safe. */
	char typeLabel[8];
	snprintf(typeLabel, sizeof(typeLabel), "%s", voiceTypeTag(inst->voiceType));
	GuiNode *typeBtn = createActionBtnGuiNode(0, 0, 100, 100, 1, na_horizontal, typeLabel, selected, cbCycleVoiceType, vm);

	/* VOICES dial: inst->voiceCountParam was created in init_instrument
	 * (Task 6 voice.c) with onChange = cb_setVoiceCount. The default
	 * range is 1..8 (MAX_VOICES_PER_CHANNEL). incParameterBaseValue
	 * is the standard adjust callback used by every other dial in the
	 * instrument screen, so left/right + KM_EDIT+arrow work uniformly. */
	GuiNode *voicesDial = createDialGuiNode(0, 0, 100, 100, 1, na_horizontal, "VOICES", selected, incParameterBaseValue, inst->voiceCountParam);

	GuiNode *widthPad = createBlankGuiNode();

	appendItem(btnwrap, typeBtn, 1);
	appendItem(btnwrap, voicesDial, 1);
	appendItem(btnwrap, widthPad, 6);
	appendItem(container, btnwrap, weight);
}

void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "FM_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	GuiNode *btnrow1 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_1", 0, 0);
	GuiNode *btnrow2 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_2", 0, 0);

	GuiNode *rat1 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO1", 1, incParameterBaseValue, inst->id.fm.ops[0]->ratio);
	GuiNode *fb1 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK1", 0, incParameterBaseValue, inst->id.fm.ops[0]->feedbackAmount);
	GuiNode *lvl1 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL1", 0, incParameterBaseValue, inst->id.fm.ops[0]->level);
	GuiNode *rat2 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO2", 0, incParameterBaseValue, inst->id.fm.ops[1]->ratio);
	GuiNode *fb2 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK2", 0, incParameterBaseValue, inst->id.fm.ops[1]->feedbackAmount);
	GuiNode *lvl2 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL2", 0, incParameterBaseValue, inst->id.fm.ops[1]->level);
	GuiNode *rat3 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO3", 0, incParameterBaseValue, inst->id.fm.ops[2]->ratio);
	GuiNode *fb3 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK3", 0, incParameterBaseValue, inst->id.fm.ops[2]->feedbackAmount);
	GuiNode *lvl3 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL3", 0, incParameterBaseValue, inst->id.fm.ops[2]->level);
	GuiNode *rat4 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO4", 0, incParameterBaseValue, inst->id.fm.ops[3]->ratio);
	GuiNode *fb4 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK4", 0, incParameterBaseValue, inst->id.fm.ops[3]->feedbackAmount);
	GuiNode *lvl4 = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL4", 0, incParameterBaseValue, inst->id.fm.ops[3]->level);
	GuiNode *alg = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "ALG", 0, incParameterBaseValue, inst->id.fm.selectedAlgorithm);
	GuiNode *pan = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
	alg->draw = drawDiscreteDialGuiNode;
	pan->draw = drawDiscreteDialGuiNode;
	if(selected) {
		g->selected = rat1;
	}

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();
	GuiNode *sp3 = createBlankGuiNode();
	GuiNode *sp4 = createBlankGuiNode();
	GuiNode *sp5 = createBlankGuiNode();

	appendItem(btnrow1, rat1, 40);
	appendItem(btnrow1, fb1, 40);
	appendItem(btnrow1, lvl1, 40);
	appendItem(btnrow1, sp1, 5);
	appendItem(btnrow1, rat2, 40);
	appendItem(btnrow1, fb2, 40);
	appendItem(btnrow1, lvl2, 40);
	appendItem(btnrow1, sp2, 5);
	appendItem(btnrow1, pan, 20);

	appendItem(btnrow2, rat3, 40);
	appendItem(btnrow2, fb3, 40);
	appendItem(btnrow2, lvl3, 40);
	appendItem(btnrow2, sp4, 5);
	appendItem(btnrow2, rat4, 40);
	appendItem(btnrow2, fb4, 40);
	appendItem(btnrow2, lvl4, 40);
	appendItem(btnrow2, sp5, 5);
	appendItem(btnrow2, alg, 20);

	appendItem(btnwrap, btnrow1, 1);
	appendItem(btnwrap, btnrow2, 1);

	appendItem(container, btnwrap, weight);
}

void appendSampleInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "SAMPLE_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	GuiNode *btnrow1 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_1", 0, 0);
	GuiNode *btnrow2 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_2", 0, 0);

	GuiNode *sampleIndex = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "SAMPLE", selected, incParameterBaseValue, inst->id.sampler.sampleIndex);
	GuiNode *pan = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
	GuiNode *loop = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "LOOP", 0, incParameterBaseValue, inst->id.sampler.loopSample);
	pan->draw = drawDiscreteDialGuiNode;
	GuiNode *loopStart = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "START", 0, incParameterBaseValue, inst->id.sampler.loopStartIndex);
	GuiNode *loopEnd = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "END", 0, incParameterBaseValue, inst->id.sampler.loopEndIndex);
	GuiNode *playbackType = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "PLAYBACK", 0, incParameterBaseValue, inst->id.sampler.playbackType);
	SampleWaveformGuiNode *swgn = createSampleWaveformGuiNode(0, 0, 100, 100, 5, na_vertical, "WFRM", 0, inst, inst->id.sampler.loopStartIndex, inst->id.sampler.loopEndIndex);
	if(selected) {
		g->selected = sampleIndex;
	}

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();

	appendItem(btnrow1, sampleIndex, 1);
	appendItem(btnrow1, pan, 1);
	appendItem(btnrow1, loop, 1);
	appendItem(btnrow1, loopStart, 1);
	appendItem(btnrow1, loopEnd, 1);
	appendItem(btnrow1, playbackType, 1);
	appendItem(btnrow1, sp1, 2);

	appendItem(btnrow2, (GuiNode *)swgn, 3);
	appendItem(btnrow2, sp2, 1);

	appendItem(btnwrap, btnrow1, 1);
	appendItem(btnwrap, btnrow2, 1);

	appendItem(container, btnwrap, weight);
}

void appendBlepInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "SAMPLE_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	GuiNode *btnrow1 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_1", 0, 0);
	GuiNode *btnrow2 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_2", 0, 0);

	GuiNode *waveShape = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "SHAPE", selected, incParameterBaseValue, inst->id.blep.shape);
	GuiNode *pan = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
	pan->draw = drawDiscreteDialGuiNode;

	if(selected) {
		g->selected = waveShape;
	}

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();

	appendItem(btnrow1, waveShape, 1);
	appendItem(btnrow1, pan, 1);
	appendItem(btnrow1, sp1, 4);

	appendItem(btnrow2, sp2, 1);

	appendItem(btnwrap, btnrow1, 1);
	appendItem(btnwrap, btnrow2, 1);

	appendItem(container, btnwrap, weight);
}

static Parameter *routeTargetParam(Instrument *inst, int idx) {
	if(idx < 0 || idx >= 12) {
		return NULL;
	}
	int op = idx / 3;
	int kind = idx % 3;
	switch(kind) {
		case 0: return inst->id.fm.ops[op]->feedbackAmount;
		case 1: return inst->id.fm.ops[op]->ratio;
		default: return inst->id.fm.ops[op]->level;
	}
}

static void routeOnChange(void *data) {
	RouteState *rs = (RouteState *)data;
	Instrument *inst = rs->inst;
	if(!inst) {
		return;
	}
	/* Task 8: rewireModulation mutates the modList the audio thread
	 * iterates; hold the rebuilding flag across the swap. The audio
	 * lock closes the flag's check-then-use race. */
	pthread_mutex_lock(&g_audioLock);
	if(inst) {
		inst->rebuilding = true;
	}
	Parameter *dest = routeTargetParam(inst, getParameterValueAsInt(rs->routeIndex));
	if(rs->target) {
		removeModulation(inst->paramList, rs->target, &rs->env->base);
	}
	if(dest) {
		addModulation(inst->paramList, &rs->env->base, dest, 1.0f, MO_ADD);
	}
	rs->target = dest;
	if(inst) {
		inst->rebuilding = false;
	}
	pthread_mutex_unlock(&g_audioLock);
}

static void incRouteIndex(Parameter *p, float step) {
	wrapIncrementParameter(p, step > 0.0f ? 1.0f : -1.0f);
}

static void appendRuntimeEnvControlNode(Graph *g, GuiNode *container, char *name, int weight, Instrument *inst, int envIndex) {
	(void)g; /* g reserved for future focus / selection linkage */
	(void)name; /* name reserved for debug label symmetry */
	Envelope *env = inst->envelopes[envIndex];
	GuiNode *envwrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "ENVELOPE+", 0, 0);
	envwrap->draw = drawWrapperNode;
	envwrap->drawable = true;

	GuiNode *ar = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", 0, incParameterBaseValue, env->stages[0].duration);
	GuiNode *ac = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[0].curvature);
	GuiNode *dr = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, env->stages[1].duration);
	GuiNode *dc = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[1].curvature);
	GuiNode *route = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "ROUTE", 0, incRouteIndex, runtimeRoutes[envIndex].routeIndex);
	route->draw = drawDiscreteDialGuiNode;
	GuiNode *sp = createBlankGuiNode();

	appendItem(envwrap, ar, 4);
	appendItem(envwrap, ac, 4);
	appendItem(envwrap, dr, 4);
	appendItem(envwrap, dc, 4);
	appendItem(envwrap, route, 4);
	appendItem(envwrap, sp, 2);
	appendItem(container, envwrap, weight);
}

void addRuntimeEnvelope(Instrument *inst) {
	if(!inst || inst->envelopeCount >= MAX_ENVELOPES) {
		return;
	}
	/* Task 8: the audio thread iterates inst->paramList/modList every
	 * buffer; hold the rebuilding flag while we mutate the lists. The
	 * audio lock closes the flag's check-then-use race. */
	pthread_mutex_lock(&g_audioLock);
	if(inst) {
		inst->rebuilding = true;
	}
	int idx = inst->envelopeCount;
	inst->envelopes[idx] = createAD(inst->paramList, inst->modList, 0.25f, 4.25f, "AD+");
	runtimeRoutes[idx].inst = inst;
	runtimeRoutes[idx].env = inst->envelopes[idx];
	runtimeRoutes[idx].routeIndex = createParameter(inst->paramList, "route", 12.0f, 0.0f, 12.0f);
	runtimeRoutes[idx].target = NULL;
	runtimeRoutes[idx].routeIndex->onChange.cbData = &runtimeRoutes[idx];
	runtimeRoutes[idx].routeIndex->onChange.cbFunc = routeOnChange;
	inst->envelopeCount++;
	rebuildInstrumentGraph();
	if(inst) {
		inst->rebuilding = false;
	}
	pthread_mutex_unlock(&g_audioLock);
	/* voices alias core envelopes only; runtime envelopes route via
	 * inst->paramList which all voices share; no rebuild needed */
}

void removeRuntimeEnvelope(Instrument *inst, int envIndex) {
	if(!inst || envIndex < inst->coreEnvelopeCount || envIndex >= inst->envelopeCount) {
		return;
	}
	/* Task 8: hold the rebuilding flag while we mutate + free from the
	 * modList/paramList the audio thread iterates. The audio lock
	 * closes the flag's check-then-use race. */
	pthread_mutex_lock(&g_audioLock);
	if(inst) {
		inst->rebuilding = true;
	}
	if(runtimeRoutes[envIndex].target) {
		removeModulation(inst->paramList, runtimeRoutes[envIndex].target,
		                 &inst->envelopes[envIndex]->base);
	}
	removeMod(inst->modList, inst->paramList, &inst->envelopes[envIndex]->base);
	for(int j = envIndex; j < inst->envelopeCount - 1; j++) {
		inst->envelopes[j] = inst->envelopes[j + 1];
		runtimeRoutes[j] = runtimeRoutes[j + 1];
	}
	inst->envelopeCount--;
	memset(&runtimeRoutes[inst->envelopeCount], 0, sizeof(RouteState));
	rebuildInstrumentGraph();
	if(inst) {
		inst->rebuilding = false;
	}
	pthread_mutex_unlock(&g_audioLock);
	/* voices alias core envelopes only; runtime envelopes route via
	 * inst->paramList which all voices share; no rebuild needed */
}

void rebuildInstrumentGraph(void) {
	if(!igui || !igui->vm) {
		return;
	}
	int idx = *igui->selectedInstrument;
	for(int i = 0; i < igui->instrumentCount; i++) {
		freeGuiNode(igui->instrumentScreenGraphs[i]->root);
		free(igui->instrumentScreenGraphs[i]);
		igui->instrumentScreenGraphs[i] = NULL;
	}
	for(int i = 0; i < igui->vm->enabledChannels; i++) {
		bool isSelected = (i == idx);
		igui->instrumentScreenGraphs[i] = createInstGraph(igui->vm->instruments[i], igui->vm, i, isSelected);
	}
}

void removeSelectedEnvelope(void) {
	if(!igui || !igui->vm || !igui->selectedInstrument) {
		return;
	}
	GuiNode *sel = getSelectedInstGraph()->selected;
	if(!sel) {
		return;
	}
	GuiNode *n = sel;
	while(n && n->container) {
		if(strcmp(n->container->name, "mod_wrap") == 0) {
			int idx = 0;
			ListElement *l = n->container->items->head;
			while(l && *(GuiNode **)l->data != n) {
				idx++;
				l = l->next;
			}
			Instrument *inst = igui->vm->instruments[*igui->selectedInstrument];
			if(idx < inst->envelopeCount) {
				removeRuntimeEnvelope(inst, idx);
			}
			return;
		}
		n = n->container;
	}
}

Instrument *getSelectedInstInstrument(void) {
	if(!igui || !igui->vm) {
		return NULL;
	}
	return igui->vm->instruments[*igui->selectedInstrument];
}

void appendADEnvControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Envelope *env) {
	GuiNode *envwrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "ENVELOPE", 0, 0);
	envwrap->draw = drawWrapperNode;
	envwrap->drawable = true;

	GuiNode *ar = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", selected, incParameterBaseValue, env->stages[0].duration);
	GuiNode *ac = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[0].curvature);
	GuiNode *dr = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, env->stages[1].duration);
	GuiNode *dc = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[1].curvature);
	if(selected) { g->selected = ar; }

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();

	appendItem(envwrap, ar, 4);
	appendItem(envwrap, ac, 4);
	appendItem(envwrap, sp1, 1);
	appendItem(envwrap, dr, 4);
	appendItem(envwrap, dc, 4);
	appendItem(envwrap, sp2, 5);

	appendItem(container, envwrap, weight);
}

void appendADSREnvControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Envelope *env) {
	GuiNode *envwrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "ENVELOPE", 0, 0);
	envwrap->draw = drawWrapperNode;
	envwrap->drawable = true;

	GuiNode *ar = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", selected, incParameterBaseValue, env->stages[0].duration);
	GuiNode *ac = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[0].curvature);
	GuiNode *dr = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, env->stages[1].duration);
	GuiNode *dc = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[1].curvature);
	GuiNode *sr = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SUSTAIN", 0, incParameterBaseValue, env->stages[2].duration);
	GuiNode *sc = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[2].curvature);
	GuiNode *rr = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "RELEASE", 0, incParameterBaseValue, env->stages[3].duration);
	GuiNode *rc = createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[3].curvature);
	if(selected) { g->selected = ar; }

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();
	GuiNode *sp3 = createBlankGuiNode();
	GuiNode *sp4 = createBlankGuiNode();

	appendItem(envwrap, ar, 4);
	appendItem(envwrap, ac, 4);
	appendItem(envwrap, sp1, 1);
	appendItem(envwrap, dr, 4);
	appendItem(envwrap, dc, 4);
	appendItem(envwrap, sp2, 1);
	appendItem(envwrap, sr, 4);
	appendItem(envwrap, sc, 4);
	appendItem(envwrap, sp3, 1);
	appendItem(envwrap, rr, 4);
	appendItem(envwrap, rc, 4);

	appendItem(container, envwrap, weight);
}

void appendBlankNode(GuiNode *container, int weight) {
	GuiNode *bgn = createBlankGuiNode();
	appendItem(container, bgn, weight);
}

static void drawModStripGuiNode(void *self) {
	ModStripGuiNode *msgn = (ModStripGuiNode *)self;
	GuiNode *gn = (GuiNode *)msgn;
	drawModStrip(&msgn->strip, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
}

static ModStripGuiNode *createModStripGuiNode(int x, int y, int w, int h, VoiceManager *vm, int channel) {
	ModStripGuiNode *msgn = malloc(sizeof(ModStripGuiNode));
	GuiNode *gn = (GuiNode *)msgn;
	if(!initGuiNode(gn, x, y, w, h, 0, na_horizontal, "modstrip", 0, 0)) {
		printf("ModStripGuiNode init problem.\n");
		free(msgn);
		return NULL;
	}
	initModStrip(&msgn->strip, vm->voicePools[channel], vm->voiceCount[channel], w, h);
	gn->drawable = true;
	gn->draw = drawModStripGuiNode;
	return msgn;
}

Graph *createInstGraph(Instrument *inst, VoiceManager *vm, int channel, bool selected) {
	Graph *instGraph = createGraph(na_vertical);
	GuiNode *mainRow = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "mainrow", 0, 0);
	GuiNode *margin1 = createBlankGuiNode();
	GuiNode *margin2 = createBlankGuiNode();
	GuiNode *presetWrap = createGuiNode(0, 0, 100, 100, 2, na_vertical, "presetwrappa", 0, 0);
	appendPresetControlNode(instGraph, presetWrap, "presetz", 2, 0, inst);
	// GuiNode *pad1 = createBlankGuiNode();
	GuiNode *pad2 = createBlankGuiNode();

	GuiNode *instwrap = createGuiNode(0, 0, 100, 100, 5, na_vertical, "inst_wrap", 0, 0);
	/* Task 6: meta row sits at the TOP of instwrap, above presetWrap.
	 * Weight is intentionally small (1) so the voice-type-specific row
	 * below it (FM/Sample/Blep, weight 8) keeps most of the vertical
	 * space — the meta row is a thin ribbon of TYPE + VOICES controls.
	 * selected=false: never assign g->selected here, see the long
	 * comment on appendMetaControlNode for the fixture-compatibility
	 * reasoning. */
	appendMetaControlNode(instGraph, instwrap, inst, vm, channel, 1, false);
	appendItem(instwrap, presetWrap, 5);
	switch(inst->voiceType) {
		case VOICE_TYPE_FM:
			appendFMInstControlNode(instGraph, instwrap, "fmctrl", 8, true, inst);
			break;
		case VOICE_TYPE_SAMPLE:
			appendSampleInstControlNode(instGraph, instwrap, "sctrl", 8, true, inst);
			break;
		case VOICE_TYPE_BLEP:
			appendBlepInstControlNode(instGraph, instwrap, "blctrl", 8, true, inst);
			break;
	}
	appendItem(instwrap, pad2, 1);

	GuiNode *modwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "mod_wrap", 0, 0);
	// printf("\n\nenvelopeCount: %i\n\n", inst->envelopeCount);
	for(int i = 0; i < MAX_ENVELOPES; i++) {
		if(i < inst->envelopeCount) {
			// printf("Env I: %i\n", i);
			if(i < inst->coreEnvelopeCount) {
				appendADEnvControlNode(instGraph, modwrap, "mod", 1, false, inst->envelopes[i]);
			} else {
				appendRuntimeEnvControlNode(instGraph, modwrap, "mod", 1, inst, i);
			}
		} else {
			// printf("NOEnv I: %i\n", i);
			appendBlankNode(modwrap, 1);
		}
	}
	appendItem(instwrap, modwrap, 22);

	appendItem(mainRow, margin1, 1);
	appendItem(mainRow, instwrap, 18);
	appendItem(mainRow, margin2, 1);
	appendItem(instGraph->root, mainRow, 19);

	ModStripGuiNode *msgn = createModStripGuiNode(0, 0, 640, 100, vm, channel);
	if(msgn) {
		appendItem(instGraph->root, (GuiNode *)msgn, 4);
	} else {
		appendBlankNode(instGraph->root, 4);
	}
	return instGraph;
}

void drawArrangerGuiNode(void *self) {
	ArrangerGuiNode *aGui = (ArrangerGuiNode *)self;
	Arranger *arranger = (Arranger *)aGui->arranger;
	char *cellText = malloc(sizeof(char) * 4);
	GuiNode *gn = (GuiNode *)aGui;
	int tmpx = gn->x;
	int tmpy = gn->y;
	int cellW = (gn->w - aGui->grid_padding * arranger->enabledChannels) / arranger->enabledChannels;
	int cellH = cellW;
	int cursorx = tmpx + arranger->selected_x * (cellW + aGui->grid_padding);
	int cursory = tmpy + arranger->selected_y * (cellH + aGui->grid_padding);

	for(int i = 0; i < arranger->enabledChannels; i++) {
		switch(arranger->vm->instruments[i]->voiceType) {
			case VOICE_TYPE_BLEP:
				drawSprite(instrumentIcons, 0, tmpx + i * (cellW + aGui->grid_padding), tmpy, cellW, cellH);
				break;
			case VOICE_TYPE_SAMPLE:
				drawSprite(instrumentIcons, 1, tmpx + i * (cellW + aGui->grid_padding), tmpy, cellW, cellH);
				break;
			case VOICE_TYPE_FM:
				drawSprite(instrumentIcons, 2, tmpx + i * (cellW + aGui->grid_padding), tmpy, cellW, cellH);
				break;
			default:
				break;
		}
	}

	tmpy += cellH + aGui->grid_padding;
	cursory += cellH + aGui->grid_padding;
	/* The cell cursor is only drawn while the grid itself is the focused
	 * graph node — selecting the tempo/swing controls hides it. */
	if(gn->selected) {
		DrawRectangle(cursorx - aGui->border_size, cursory - aGui->border_size, cellW + (aGui->border_size * 2), cellH + (aGui->border_size * 2), cs.outlineColour);
	}
	int fontSize = cellW / 3;
	for(int i = 0; i < arranger->enabledChannels; i++) {
		// int px = i % arranger->enabledChannels;
		int newx = tmpx + (i * (cellW + aGui->grid_padding));
		for(int j = 0; j < MAX_SONG_LENGTH; j++) {
			int newy = tmpy + (j * (cellH + aGui->grid_padding));
			if(arranger->song[i][j] > -1) {
				sprintf(cellText, "%02i", arranger->song[i][j]);
				if(arranger->playhead_indices[i] == j && arranger->playing) {
					DrawRectangle(newx, newy, cellW, cellH, cs.arrangerPlayhead);
				} else {
					DrawRectangle(newx, newy, cellW, cellH, cs.defaultCell);
				}
				DrawTextEx(pixelFont, cellText, (Vector2){ newx + fontSize, newy + fontSize }, fontSize, 1, cs.arrangerCellText);
			} else {
				DrawRectangle(newx, newy, cellW, cellH, cs.blankCell);
				DrawTextEx(pixelFont, "--", (Vector2){ newx + fontSize, newy + fontSize }, fontSize, 1, cs.arrangerCellText);
			}
		}
	}
}

void drawSongMinimapGui(void *self) {
	SongMinimapGui *smGui = (SongMinimapGui *)self;
	Arranger *arranger = (Arranger *)smGui->arranger;
	int startIndex = 0;
	// printf("coord %d,%d\n", smGui->songIndex[0], smGui->songIndex[1]);

	for(int i = startIndex; i < smGui->maxMapLength; i++) {
		int newy = smGui->shape.y + (i * smGui->shape.w) + (smGui->padding * i);
		for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++) {
			int newx = smGui->shape.x + (j * smGui->shape.h) + (smGui->padding * j);
			if(arranger->song[j][i] > -1) {
				if(smGui->songIndex[0] == j && smGui->songIndex[1] == i) {
					DrawRectangle(newx, newy, smGui->shape.w, smGui->shape.h, smGui->selectedCellColour);
				} else if(arranger->playhead_indices[j] == i) {
					DrawRectangle(newx, newy, smGui->shape.w, smGui->shape.h, smGui->playingCellColour);
				} else {
					DrawRectangle(newx, newy, smGui->shape.w, smGui->shape.h, smGui->defaultCellColour);
				}
			} else {
				DrawRectangle(newx, newy, smGui->shape.w, smGui->shape.h, smGui->blankCellColour);
			}
		}
	}
}

void DrawGUI(int currentScene) {
	switch(currentScene) {
		case SCENE_ARRANGER:
			// printf("a!");
			// for(int i = 0; i < arrangerScreenDrawableList->size; i++) {
			// 	arrangerScreenDrawableList->drawables[i]->draw(arrangerScreenDrawableList->drawables[i]);
			// }
			// drawNode(arrangerGraph->root);
			drawNode(agui->root);

			break;
		case SCENE_PATTERN:
			drawNode(patternGraph->root);
			if(smgui) {
				drawSongMinimapGui(smgui);
			}
			break;
		case SCENE_INSTRUMENT:
			// printf("i!");
			drawNode(igui->instrumentScreenGraphs[*igui->selectedInstrument]->root);
			// drawNode(instrumentGraph->root);
			/* Task 7: draw any open overlay layers on top of the
			 * instrument screen. This replaces the old drawPresetModal()
			 * call — overwrite, dirty-confirm, and the load-list are all
			 * overlay layers now. The layer system itself handles per-layer
			 * "dim" tinting for depth. */
			if(igui) {
				layerStackDraw(&igui->overlayLayers);
			}
			break;
		default:
			printf("Invalid scene, nothing to draw\n");
			break;
	}
}

