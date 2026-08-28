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

InstrumentGui *igui;
Graph *agui;
static Graph *patternGraph;
static SongMinimapGui *smgui;

typedef struct {
	Instrument *inst;
	Envelope *env;
	Parameter *routeIndex;
	Parameter *target; /* current modulation target; NULL = un-routed */
} RouteState;
static RouteState runtimeRoutes[MAX_ENVELOPES];

Font textFont;
Font symbolFont;
Font pixelFont;
ColourScheme cs;
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
	index = index > spriteSheet->spriteCount ? index % spriteSheet->spriteCount : index;
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
}

void setColourScheme(ColourScheme *colourScheme) {
	cs = *colourScheme;
}

Color **getColorSchemeAsPointerArray() {
	Color **colourScheme = malloc(sizeof(Color *) * 9);
	if(!colourScheme) return NULL;

	colourScheme[0] = &cs.backgroundColor;
	colourScheme[1] = &cs.fontColour;
	colourScheme[2] = &cs.secondaryFontColour;
	colourScheme[3] = &cs.outlineColour;
	colourScheme[4] = &cs.defaultCell;
	colourScheme[5] = &cs.highlightedCell;
	colourScheme[6] = &cs.selectedCell;
	colourScheme[7] = &cs.blankCell;
	colourScheme[8] = &cs.reddish;
	return colourScheme;
}

ColourScheme *getColourScheme() {
	return &cs;
}

void clearBg() {
	ClearBackground(cs.backgroundColor);
}

void InitGUI(void) {
	const int screenWidth = SCREEN_W;
	const int screenHeight = SCREEN_H;

	initDefaultColourScheme(&cs);

	InitWindow(screenWidth, screenHeight, "Spectrax");
	textFont = LoadFont("resources/fonts/setback.png");
	// pixelFont = LoadFontEx("resources/fonts/04B_03__.TTF", 12, 0, 255);
	pixelFont = LoadFontEx("resources/fonts/console.ttf", 9, 0, 255);
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
	igui = ig;
}

Graph *getSelectedInstGraph() {
	return igui->instrumentScreenGraphs[*igui->selectedInstrument];
}

static void drawSampleWaveLinesNode(void *self);
static void drawSampleWavePolylineNode(void *self);

void createArrangerGraph(Arranger *a, PatternList *pl) {
	agui = createGraph(na_vertical);
	GuiNode *arrWrap = createGuiNode(0, 0, 100, 100, 5, na_horizontal, "awrap", 0, 0);
	GuiNode *songControls = createGuiNode(0, 0, 100, 100, 5, na_vertical, "song", 0, 0);

	GuiNode *pad0 = createNamedBlankGuiNode("pad00");
	GuiNode *pad1 = createNamedBlankGuiNode("pad01");
	GuiNode *bpm = createBtnGuiNode(0, 0, 100, 100, 5, na_vertical, "BPM", false, incParameterBaseValue, a->tempoSettings.bpm);
	GuiNode *swing = createBtnGuiNode(0, 0, 100, 100, 5, na_vertical, "Swing", false, incParameterBaseValue, a->tempoSettings.swing);
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
	appendItem(arrWrap, gn, 4);
	appendItem(arrWrap, margin2, 1);
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
	DrawTextEx(pixelFont, "VLINE", (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, (Color){ 60, 255, 150, 255 });
}

static void drawSampleWavePolylineNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!arrangerMixRing) {
		return;
	}
	drawSampleWavePolyline(arrangerMixRing, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
	DrawTextEx(pixelFont, "POLY", (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, (Color){ 255, 80, 80, 255 });
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

GuiNode *createBtnGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, OnPressCallback callback, Parameter *p) {
	GuiNode *gn = createGuiNode(x, y, w, h, padding, na, name, 1, selected);
	if(gn == NULL) {
		printf("createBtnGuiNode error, could not create.");
		return NULL;
	}
	gn->callback = callback;
	gn->p = p;
	gn->drawable = true;
	gn->draw = drawDialGuiNode;
	return gn;
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
	swgn->bgColour = (Color){ 0, 0, 0, 255 };
	swgn->wfColour = (Color){ 255, 0, 0, 255 };
	swgn->wfAltColour = (Color){ 0, 255, 0, 255 };
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
	DrawCircleSector((Vector2){ x + radius, y + radius }, radius + 2, startAngle, startAngle + offsetAngle, 32, RED);
	DrawTexturePro(dial, (Rectangle){ 0, 0, 48, 48 }, (Rectangle){ x + radius, y + radius, w, h }, (Vector2){ radius, radius }, startAngle + offsetAngle, WHITE);
}

void drawValueDisplay(int x, int y, int w, int h, char *text) {
	DrawRectangle(x, y, w, h, (Color){ 50, 40, 40, 255 });
	DrawTextEx(pixelFont, text, (Vector2){ x + 4, y + 4 }, 9, 1, RED);
}

void drawColourRectangle(int x, int y, int w, int h, float roundness, float line_w, bool highlighted) {
	DrawRectangleRounded((Rectangle){ x, y, w, h }, roundness, 12, (Color){ 80, 60, 60, 255 });
	if(highlighted) {
		DrawRectangleRoundedLinesEx((Rectangle){ x, y, w, h }, roundness, 12, line_w, cs.highlightedCell);
	} else {
		DrawRectangleRoundedLinesEx((Rectangle){ x, y, w, h }, roundness, 12, line_w, (Color){ 10, 0, 0, 255 });
	}
}

void drawBtnGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	char paramValue[50];
	snprintf(paramValue, sizeof(paramValue), "%.2f", gn->p->currentValue);
	int tmpx = gn->x;
	int tmpy = gn->y;
	drawColourRectangle(tmpx, tmpy, gn->w, gn->h, 0.125, 2.0, gn->selected);

	tmpx += gn->padding;
	tmpy += gn->padding;
	int paramInt = (int)gn->p->currentValue;
	if(paramInt == 1) {
		DrawRectangleRounded((Rectangle){ tmpx - 2, tmpy - 2, 37, 23 }, 0.3f, 12, (Color){ 205, 75, 0, 125 });
		DrawTexturePro(btnOn, (Rectangle){ 0, 0, 33, 19 }, (Rectangle){ tmpx, tmpy, 33, 19 }, (Vector2){ 0, 0 }, 0, WHITE);
	} else {
		DrawRectangleRounded((Rectangle){ tmpx, tmpy, 37, 23 }, 0.3f, 12, (Color){ 40, 30, 30, 165 });
		DrawTexturePro(btnOff, (Rectangle){ 0, 0, 33, 19 }, (Rectangle){ tmpx, tmpy, 33, 19 }, (Vector2){ 0, 0 }, 0, WHITE);
	}

	DrawTextEx(pixelFont, gn->name, (Vector2){ tmpx + 4, tmpy + 32 }, 9, 1, (Color){ 200, 180, 180, 255 });
}

void drawDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	char paramValue[50];
	snprintf(paramValue, 50, "%05.2f", gn->p->currentValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		angle = (gn->p->currentValue - gn->p->minValue) / (range / 100) * 2.7;
	}
	int tmpx = gn->x;
	int tmpy = gn->y;
	drawColourRectangle(tmpx, tmpy, gn->w, gn->h, 0.125, 2.0, gn->selected);
	tmpx += gn->padding + 2;
	tmpy += gn->padding;
	drawRotatedDial(tmpx, tmpy, 24, 24, 12, -225, angle);
	tmpx += 28;
	tmpy += 2;
	drawValueDisplay(tmpx, tmpy, 38, 16, paramValue);
	DrawTextEx(pixelFont, gn->name, (Vector2){ tmpx - 28, tmpy + 30 }, 9, 1, (Color){ 200, 180, 180, 255 });
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
	drawRotatedDial(tmpx, tmpy, 24, 24, 12, -225, angle);
	tmpx += 6;
	tmpy += 5;
	drawValueDisplay(tmpx, tmpy, 10, 14, paramValue);

	DrawTextEx(pixelFont, gn->name, (Vector2){ tmpx, tmpy + 28 }, 9, 1, (Color){ 200, 180, 180, 255 });
}

void drawWrapperNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h }, (Color){ 80, 60, 60, 255 });
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, 2.0, (Color){ 10, 5, 5, 255 });
}

/* Task 7: load-list active flag. guiOpenLoadList() flips this on; Task 9
 * will read it to render the file list UI. */
static bool g_loadListActive = false;

bool guiIsLoadListActive(void) { return g_loadListActive; }

void guiOpenLoadList(void) {
	g_loadListActive = true;
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
}

bool guiIsModalOpen(void) { return g_modalState != MODAL_NONE; }

void guiSavePreset(Instrument *inst, const char *name) {
	if(!inst || !name) {
		return;
	}
	PresetFileResult r = saveInstrumentAsPreset(inst, name, "data/instrument_presets/");
	if(r == PRESET_EXISTS) {
		/* Defer the actual overwrite to the modal: the user hasn't
		 * confirmed yet. handlePresetUiInput drives the modal and
		 * calls saveInstrumentAsPresetOverwrite() on YES. */
		guiSetOverwritePending(name);
	}
}

/* Task 8: centered overlay panel for the overwrite confirmation. Drawn
 * AFTER drawNode(...) in the SCENE_INSTRUMENT case so the modal sits on
 * top of the underlying scene. We dim the scene with a translucent
 * black rectangle so the panel reads as the active surface. */
void drawPresetModal(void) {
	if(g_modalState != MODAL_CONFIRM_OVERWRITE) {
		return;
	}
	/* Scene dimmer. */
	DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 0, 0, 0, 160 });

	/* Centered panel: 280x80 in the middle of the 640x480 screen. */
	const int pw = 280;
	const int ph = 80;
	const int px = (SCREEN_W - pw) / 2;
	const int py = (SCREEN_H - ph) / 2;
	DrawRectangleRec((Rectangle){ px, py, pw, ph }, (Color){ 30, 20, 20, 255 });
	DrawRectangleLinesEx((Rectangle){ px, py, pw, ph }, 2.0f, (Color){ 200, 60, 60, 255 });

	/* Header: OVERWRITE <name>? */
	char header[64];
	snprintf(header, sizeof(header), "OVERWRITE \"%s\"?", g_pendingName);
	int headerFontSize = 16;
	int headerW = MeasureText(header, headerFontSize);
	DrawText(header, px + (pw - headerW) / 2, py + 10, headerFontSize, (Color){ 230, 200, 200, 255 });

	/* Buttons: [YES] [NO]. Highlighted one in red, the other in grey.
	 * Layout: split the panel vertically at pw/2; YES flush to the right
	 * of the left half (with a 12px gap), NO flush to the left of the
	 * right half. We only need each label's measured width to position
	 * the text inside its half. */
	const int btnY = py + 44;
	const char *yesLabel = "[YES]";
	const char *noLabel = "[NO]";
	const int yesFont = 18;
	const int noFont = 18;
	const int yesW = MeasureText(yesLabel, yesFont);
	const int yesX = px + (pw / 2) - yesW - 12;
	const int noX = px + (pw / 2) + 12;
	Color yesCol = g_overwriteChoice ? (Color){ 230, 60, 60, 255 } : (Color){ 120, 110, 110, 255 };
	Color noCol = !g_overwriteChoice ? (Color){ 230, 60, 60, 255 } : (Color){ 120, 110, 110, 255 };
	DrawText(yesLabel, yesX, btnY, yesFont, yesCol);
	DrawText(noLabel, noX, btnY, noFont, noCol);

	/* Tiny hint: L/R toggle, START confirm, SELECT cancel. */
	const char *hint = "L/R toggle  START confirm  SELECT cancel";
	int hintFont = 9;
	int hintW = MeasureText(hint, hintFont);
	DrawText(hint, px + (pw - hintW) / 2, py + ph - 14, hintFont, (Color){ 160, 140, 140, 255 });
}

#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-. "
static int charIndex(char c) {
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

bool handlePresetUiInput(InputState *is, Instrument *inst) {
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

	(void)inst;
	Graph *g = getSelectedInstGraph();
	if(!g || !g->selected || !isPresetNameNode(g->selected)) {
		return false;
	}
	PresetNameGuiNode *pn = (PresetNameGuiNode *)g->selected;
	pn->editing = true;
	if(isKeyJustPressed(is, KM_UP)) {
		cycleNameChar(pn, 1);
		return true;
	}
	if(isKeyJustPressed(is, KM_DOWN)) {
		cycleNameChar(pn, -1);
		return true;
	}
	if(isKeyJustPressed(is, KM_LEFT)) {
		pn->cursor = (pn->cursor + 31) % 32;
		return true;
	}
	if(isKeyJustPressed(is, KM_RIGHT)) {
		pn->cursor = (pn->cursor + 1) % 32;
		return true;
	}
	if(isKeyJustPressed(is, KM_START)) {
		/* trim + default, then run the save flow (overwrite check happens here) */
		pn->editing = false;
		if(strlen(pn->name) == 0 || strspn(pn->name, " ") == strlen(pn->name)) {
			strncpy(pn->name, "UNNAMED", sizeof(pn->name) - 1);
		}
		guiSavePreset(pn->inst, pn->name); /* opens the overwrite modal if EXISTS */
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
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h }, BLACK);
	/* copy the current name into the node once per selection */
	int n = (int)strlen(pn->name);
	if(n < 32) {
		n = 32;
	}
	int cellW = gn->w / 32;
	if(cellW < 4) {
		cellW = 4;
	}
	for(int i = 0; i < 32; i++) {
		int cx = gn->x + i * cellW;
		int cy = gn->y;
		char ch = (i < (int)strlen(pn->name)) ? pn->name[i] : ' ';
		if(pn->editing && i == pn->cursor) {
			DrawRectangle(cx, cy, cellW, gn->h, RED);          /* inverted cursor block */
			DrawText((char[]){ ch, '\0' }, cx + 1, cy + gn->h / 2 - 5, 10, BLACK);
		} else {
			DrawText((char[]){ ch, '\0' }, cx + 1, cy + gn->h / 2 - 5, 10, RED);
		}
	}
}

bool isPresetNameNode(GuiNode *n) {
	return n && n->draw == drawPresetNameGuiNode;
}

GuiNode *createPresetNameGuiNode(int x, int y, int w, int h, Instrument *inst, bool selected) {
	PresetNameGuiNode *pn = malloc(sizeof(PresetNameGuiNode));
	if(!pn) {
		return NULL;
	}
	GuiNode *gn = (GuiNode *)pn;
	if(!initGuiNode(gn, x, y, w, h, 0, na_horizontal, "PRESET_NAME", 0, 0)) {
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
	gn->drawable = true;
	gn->draw = drawPresetNameGuiNode;
	return gn;
}

static void cbFocusNameNode(Parameter *p, float v) { (void)v; (void)p; /* focus handled via selection; the node is directly editable */ }
static void cbOpenLoadList(Parameter *p, float v) { (void)v; (void)p; guiOpenLoadList(); }

void appendPresetControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_horizontal, "PRESET_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;
	GuiNode *presetIndex = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "PRESET", selected, incParameterBaseValue, inst->selectedPresetIndex);
	GuiNode *pad1 = createBlankGuiNode();
	GuiNode *nameNode = createPresetNameGuiNode(0, 0, 100, 100, inst, 0);
	GuiNode *saveBtn = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "SAVE", 0, cbFocusNameNode, (Parameter *)inst);
	GuiNode *loadBtn = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LOAD", 0, cbOpenLoadList, (Parameter *)inst);
	appendItem(btnwrap, presetIndex, 1);
	appendItem(btnwrap, pad1, 7);
	appendItem(btnwrap, nameNode, 4);
	appendItem(btnwrap, saveBtn, 1);
	appendItem(btnwrap, loadBtn, 1);
	appendItem(container, btnwrap, weight);
	if(selected) {
		g->selected = presetIndex;
	}
}

void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "FM_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	GuiNode *btnrow1 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_1", 0, 0);
	GuiNode *btnrow2 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_2", 0, 0);

	GuiNode *rat1 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO1", 1, incParameterBaseValue, inst->id.fm.ops[0]->ratio);
	GuiNode *fb1 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK1", 0, incParameterBaseValue, inst->id.fm.ops[0]->feedbackAmount);
	GuiNode *lvl1 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL1", 0, incParameterBaseValue, inst->id.fm.ops[0]->level);
	GuiNode *rat2 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO2", 0, incParameterBaseValue, inst->id.fm.ops[1]->ratio);
	GuiNode *fb2 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK2", 0, incParameterBaseValue, inst->id.fm.ops[1]->feedbackAmount);
	GuiNode *lvl2 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL2", 0, incParameterBaseValue, inst->id.fm.ops[1]->level);
	GuiNode *rat3 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO3", 0, incParameterBaseValue, inst->id.fm.ops[2]->ratio);
	GuiNode *fb3 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK3", 0, incParameterBaseValue, inst->id.fm.ops[2]->feedbackAmount);
	GuiNode *lvl3 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL3", 0, incParameterBaseValue, inst->id.fm.ops[2]->level);
	GuiNode *rat4 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO4", 0, incParameterBaseValue, inst->id.fm.ops[3]->ratio);
	GuiNode *fb4 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK4", 0, incParameterBaseValue, inst->id.fm.ops[3]->feedbackAmount);
	GuiNode *lvl4 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL4", 0, incParameterBaseValue, inst->id.fm.ops[3]->level);
	GuiNode *alg = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ALG", 0, incParameterBaseValue, inst->id.fm.selectedAlgorithm);
	GuiNode *pan = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
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

	GuiNode *sampleIndex = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "SAMPLE", selected, incParameterBaseValue, inst->id.sampler.sampleIndex);
	GuiNode *pan = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
	GuiNode *loop = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "LOOP", 0, incParameterBaseValue, inst->id.sampler.loopSample);
	loop->draw = drawBtnGuiNode;
	pan->draw = drawDiscreteDialGuiNode;
	GuiNode *loopStart = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "START", 0, incParameterBaseValue, inst->id.sampler.loopStartIndex);
	GuiNode *loopEnd = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "END", 0, incParameterBaseValue, inst->id.sampler.loopEndIndex);
	GuiNode *playbackType = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "PLAYBACK", 0, incParameterBaseValue, inst->id.sampler.playbackType);
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

	GuiNode *waveShape = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "SHAPE", selected, incParameterBaseValue, inst->id.blep.shape);
	GuiNode *pan = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
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
	Parameter *dest = routeTargetParam(inst, getParameterValueAsInt(rs->routeIndex));
	if(rs->target) {
		removeModulation(inst->paramList, rs->target, &rs->env->base);
	}
	if(dest) {
		addModulation(inst->paramList, &rs->env->base, dest, 1.0f, MO_ADD);
	}
	rs->target = dest;
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

	GuiNode *ar = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", 0, incParameterBaseValue, env->stages[0].duration);
	GuiNode *ac = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[0].curvature);
	GuiNode *dr = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, env->stages[1].duration);
	GuiNode *dc = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[1].curvature);
	GuiNode *route = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ROUTE", 0, incRouteIndex, runtimeRoutes[envIndex].routeIndex);
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
	/* voices alias core envelopes only; runtime envelopes route via
	 * inst->paramList which all voices share; no rebuild needed */
}

void removeRuntimeEnvelope(Instrument *inst, int envIndex) {
	if(!inst || envIndex < inst->coreEnvelopeCount || envIndex >= inst->envelopeCount) {
		return;
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

	GuiNode *ar = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", selected, incParameterBaseValue, env->stages[0].duration);
	GuiNode *ac = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[0].curvature);
	GuiNode *dr = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, env->stages[1].duration);
	GuiNode *dc = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[1].curvature);
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

	GuiNode *ar = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ATTACK", selected, incParameterBaseValue, env->stages[0].duration);
	GuiNode *ac = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[0].curvature);
	GuiNode *dr = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "DECAY", 0, incParameterBaseValue, env->stages[1].duration);
	GuiNode *dc = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[1].curvature);
	GuiNode *sr = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "SUSTAIN", 0, incParameterBaseValue, env->stages[2].duration);
	GuiNode *sc = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[2].curvature);
	GuiNode *rr = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RELEASE", 0, incParameterBaseValue, env->stages[3].duration);
	GuiNode *rc = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "CURVE", 0, incParameterBaseValue, env->stages[3].curvature);
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
	appendPresetControlNode(instGraph, presetWrap, "presetz", 1, 0, inst);
	// GuiNode *pad1 = createBlankGuiNode();
	GuiNode *pad2 = createBlankGuiNode();

	GuiNode *instwrap = createGuiNode(0, 0, 100, 100, 5, na_vertical, "inst_wrap", 0, 0);
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
		appendItem(instGraph->root, (GuiNode *)msgn, 5);
	} else {
		appendBlankNode(instGraph->root, 5);
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
	DrawRectangle(cursorx - aGui->border_size, cursory - aGui->border_size, cellW + (aGui->border_size * 2), cellH + (aGui->border_size * 2), cs.outlineColour);
	int fontSize = cellW / 3;
	for(int i = 0; i < arranger->enabledChannels; i++) {
		// int px = i % arranger->enabledChannels;
		int newx = tmpx + (i * (cellW + aGui->grid_padding));
		for(int j = 0; j < MAX_SONG_LENGTH; j++) {
			int newy = tmpy + (j * (cellH + aGui->grid_padding));
			if(arranger->song[i][j] > -1) {
				sprintf(cellText, "%02i", arranger->song[i][j]);
				if(arranger->playhead_indices[i] == j && arranger->playing) {
					DrawRectangle(newx, newy, cellW, cellH, (Color){ 255, 0, 0, 255 });
				} else {
					DrawRectangle(newx, newy, cellW, cellH, cs.defaultCell);
				}
				DrawTextEx(pixelFont, cellText, (Vector2){ newx + fontSize, newy + fontSize }, fontSize, 1, (Color){ 200, 180, 180, 255 });
			} else {
				DrawRectangle(newx, newy, cellW, cellH, cs.blankCell);
				DrawTextEx(pixelFont, "--", (Vector2){ newx + fontSize, newy + fontSize }, fontSize, 1, (Color){ 200, 180, 180, 255 });
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
			/* Task 8: overlay the overwrite modal last so it sits on top
			 * of the instrument screen while the user is deciding. */
			if(g_modalState != MODAL_NONE) {
				drawPresetModal();
			}
			break;
		default:
			printf("Invalid scene, nothing to draw\n");
			break;
	}
}

