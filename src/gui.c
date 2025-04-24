#include <stdio.h>
#include <stdlib.h>
#include "dstruct.h"
#include "raylib.h"
#include "gui.h"
#include "graph_gui.h"
#include "input.h"
#include "oscillator.h"
#include "sequencer.h"
#include "modsystem.h"
#include "notes.h"
#include "voice.h"

// Graph* globalGraph;
// Graph* patternGraph;
// Graph* arrangerGraph;
// Graph* instrumentGraph;

DrawableList *patternScreenDrawableList;
DrawableList *globalDrawableList;
DrawableList *arrangerScreenDrawableList;
DrawableList *instrumentScreenDrawableList;

// Graph* instrumentScreenGraph[MAX_SEQUENCER_CHANNELS];
InstrumentGui *igui;
Graph *agui;

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

	patternScreenDrawableList = create_drawable_list();
	arrangerScreenDrawableList = create_drawable_list();
	instrumentScreenDrawableList = create_drawable_list();
	globalDrawableList = create_drawable_list();

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

TransportGui *createTransportGui(int *playing, Arranger *arranger, int x, int y) {
	TransportGui *tsGui = (TransportGui *)malloc(sizeof(TransportGui));
	tsGui->base.draw = drawTransportGui;
	tsGui->base.enabled = true;
	tsGui->shape.x = x;
	tsGui->shape.y = y;
	tsGui->icons = createSpriteSheet("resources/fonts/iconzfin.png", 10, 12);
	tsGui->playing = playing;
	tsGui->arranger = arranger;
	tsGui->tempo = &arranger->beats_per_minute;
	add_drawable(&tsGui->base, GLOBAL); // Add TransportGui to globalDrawableList

	return tsGui;
}

SequencerGui *createSequencerGui(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedNote, int x, int y) {
	SequencerGui *seqGui = (SequencerGui *)malloc(sizeof(SequencerGui));
	seqGui->base.draw = drawSequencerGui;
	seqGui->base.enabled = true;
	seqGui->sequencer = sequencer;
	seqGui->pattern_list = pl;
	seqGui->shape.x = x;
	seqGui->shape.y = y;
	seqGui->shape.w = 50;
	seqGui->shape.h = 50;
	seqGui->pads_per_col = 4;
	seqGui->padding = 10;
	seqGui->border_size = 3;
	seqGui->selected_pattern_index = selectedPattern;
	seqGui->selected_note_index = selectedNote;
	seqGui->outline_colour = cs.outlineColour;
	seqGui->default_fill_colour = cs.defaultCell;
	seqGui->playing_fill_colour = cs.highlightedCell;

	return seqGui;
}

GraphGui *createGraphGui(float *target, char *name, float min, float max, int x, int y, int h, int size) {
	GraphGui *graphGui = (GraphGui *)malloc(sizeof(GraphGui));
	graphGui->base.draw = drawGraphGui;
	graphGui->base.enabled = true;
	graphGui->target = target;
	graphGui->name = name;
	graphGui->index = 0;
	graphGui->shape.x = x;
	graphGui->shape.y = y;
	graphGui->shape.w = size;
	graphGui->shape.h = 20;
	graphGui->padding = 1;
	graphGui->margin = 2;
	graphGui->min = min;
	graphGui->max = max;
	graphGui->history_size = size;
	for(int i = 0; i < graphGui->history_size; i++) {
		graphGui->history[i] = 0;
	}
	return graphGui; // Return the created GraphGui object
}

// ArrangerGui *createArrangerGui(Arranger *arranger, PatternList *patternList) {
// 	ArrangerGui *arrangerGui = (ArrangerGui *)malloc(sizeof(ArrangerGui));
// 	if(!arrangerGui) {
// 		printf("could not allocate arranger GUI\n");
// 		return NULL;
// 	}
// 	arrangerGui->base.draw = drawArrangerGui;
// 	arrangerGui->arranger = arranger;
// 	arrangerGui->patternList = patternList;
// 	arrangerGui->base.enabled = true;
// 	arrangerGui->grid_padding = 6;
// 	arrangerGui->shape.w = 40;
// 	arrangerGui->shape.h = 40;
// 	arrangerGui->shape.x = (SCREEN_W / 2) - (arranger->enabledChannels * (arrangerGui->shape.w + arrangerGui->grid_padding)) / 2;
// 	arrangerGui->shape.y = 40;
// 	arrangerGui->iconx = arrangerGui->shape.x;
// 	arrangerGui->icony = arrangerGui->shape.y - 30;
// 	arrangerGui->cellColour = cs.defaultCell;
// 	arrangerGui->border_size = 3;

// 	return arrangerGui;
// }

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

InputContainer *createInputContainer() {
	InputContainer *btnCont = (InputContainer *)malloc(sizeof(InputContainer));
	btnCont->rowCount = 0;
	for(int i = 0; i < MAX_BUTTON_ROWS; i++) {
		btnCont->columnCount[i] = 0;
	}
	btnCont->containerBounds = (Shape){ SCREEN_W, SCREEN_H, 0, 0 };
	btnCont->inputCount = 0;
	btnCont->otherDrawableCount = 0;
	btnCont->inputPadding = 2;
	btnCont->selectedRow = 0;
	btnCont->selectedColumn = 0;
	return btnCont;
}

void addDrawableToContainer(InputContainer *ic, Drawable *d) {
	ic->otherDrawables[ic->otherDrawableCount] = d;
	ic->otherDrawableCount++;
}

ContainerGroup *createContainerGroup() {
	ContainerGroup *cg = (ContainerGroup *)malloc(sizeof(ContainerGroup));
	cg->rowCount = 0;
	for(int i = 0; i < MAX_BUTTON_CONTAINER_ROWS; i++) {
		cg->columnCount[i] = 0;
	}
	cg->selectedRow = 0;
	cg->selectedColumn = 0;
	return cg;
}

void containerGroupNavigate(ContainerGroup *cg, int rowInc, int colInc) {
	// printf("NAV GRP: %i, %i inc: %i, %i\n", cg->selectedRow, cg->selectedColumn, rowInc, colInc);
	InputContainer *ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
	ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = false;
	// printf("NAV CONT: %i, %i\n", ic->selectedRow, ic->selectedColumn);
	int newRow = ic->selectedRow + rowInc;
	int newCol = ic->selectedColumn + colInc;
	if(newRow >= ic->rowCount) {
		// printf("newcol > inputcontainer rowcount\n");

		if(cg->selectedRow < cg->rowCount - 1) {
			printf("containergroup selectedrow %i < rowcount %i\n", cg->selectedRow, cg->rowCount);

			cg->selectedRow++;
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedRow = 0;
			if(ic->selectedColumn >= ic->columnCount[ic->selectedRow]) {
				ic->selectedColumn = ic->columnCount[ic->selectedRow] - 1;
			}
			printf("\t\tcontrowinc%i,%i, ic selected: %i, %i\n", cg->selectedRow, cg->selectedColumn, ic->selectedRow, ic->selectedColumn);
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else if(newRow < 0) {
		printf("newrow < 0\n");

		if(cg->selectedRow > 0) {
			printf("selectedrow > 0\n");

			cg->selectedRow--;
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedRow = ic->rowCount - 1;
			printf("\t\tcontrowdec%i,%i, ic selected: %i, %i\n", cg->selectedRow, cg->selectedColumn, ic->selectedRow, ic->selectedColumn);
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else {
		printf("newrow in range: %i\n", newRow);

		ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
		ic->selectedRow = newRow;
		ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
	}
	ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
	if(newCol >= ic->columnCount[ic->selectedRow]) {
		printf("newcol > inputconainer colCount\n");

		if(cg->selectedColumn < cg->columnCount[cg->selectedRow] - 1) {
			printf("sectedcolum < containergroup colCount\n");

			cg->selectedColumn++;
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedColumn = 0;
			printf("\t\tcontcolinc%i,%i, ic selected: %i, %i\n", cg->selectedRow, cg->selectedColumn, ic->selectedRow, ic->selectedColumn);
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else if(newCol < 0) {
		printf("newcol < 0");
		if(cg->selectedColumn > 0) {
			printf("containergroup selectedcolumn > 0\n");

			cg->selectedColumn--;

			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedColumn = ic->columnCount[ic->selectedRow] - 1;
			printf("\t\tcontcoldec%i,%i, ic selected: %i, %i\n", cg->selectedRow, cg->selectedColumn, ic->selectedRow, ic->selectedColumn);

			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else {
		printf("newcol in range\n");

		ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
		ic->selectedColumn = newCol;
		ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
	}
	printf("\tnew group coords: %i, %i\n", cg->selectedRow, cg->selectedColumn);
	printf("\tnew container coords: %i, %i\n", newCol, newRow);
}

void addContainerToGroup(ContainerGroup *cg, InputContainer *ic, int row, int col) {
	if(row < 0 || col < 0 || row > MAX_BUTTON_CONTAINER_ROWS - 1 || col > MAX_BUTTON_CONTAINER_COLS - 1) {
		printf("attempting out-of-bounds group insertion.\n");
		return;
	}
	printf("GROUP:count before add:%i,%i adding ", cg->rowCount, cg->columnCount[row]);

	int insertRow = 0, insertCol = 0;
	if(row >= cg->rowCount) {
		insertRow = cg->rowCount;
		cg->rowCount++;
		printf(" [ir, ");

	} else {
		insertRow = row;
		printf(" [nir, ");
	}
	if(col >= cg->columnCount[insertRow]) {
		insertCol = cg->columnCount[insertRow];
		cg->columnCount[insertRow]++;
		printf("ic] ");

	} else {
		insertCol = col;
		printf("nic] ");
	}
	printf(" at %i, %i\n", insertRow, insertCol);

	cg->containerRefs[insertRow][insertCol] = ic;
}

void removeButtonFromContainer(ButtonGui *btnGui, InputContainer *btnCont, Scene scene) {
	for(int row = 0; row < btnCont->rowCount; row++) {
		for(int col = 0; col < btnCont->columnCount[row]; col++) {
			if(btnCont->buttonRefs[row][col] == btnGui) {
				// Shift remaining buttons left
				for(int j = col; j < btnCont->columnCount[row] - 1; j++) {
					btnCont->buttonRefs[row][j] = btnCont->buttonRefs[row][j + 1];
				}
				btnCont->columnCount[row]--; // This needs to happen after finding the button
				removeDrawable(&btnGui->base, scene);
				return;
			}
		}
	}
}

void moveContainer(InputContainer *ic, int deltax, int deltay) {
	for(int row = 0; row < ic->rowCount; row++) {
		for(int col = 0; col < ic->columnCount[row]; col++) {
			ic->buttonRefs[row][col]->shape.x += deltax;
			ic->buttonRefs[row][col]->shape.y += deltay;
		}
	}
}

void addButtonToContainer(ButtonGui *btnGui, InputContainer *ic, int row, int col, int scene, int enabled) {
	if(row < 0 || col < 0 || row > MAX_BUTTON_CONTAINER_ROWS - 1 || col > MAX_BUTTON_CONTAINER_COLS - 1) {
		printf("attempting out-of-bounds btn container insertion.\n");
		return;
	}
	int insertRow = 0, insertCol = 0;
	printf("CONTAINER:count before add:%i,%i adding ", ic->rowCount, ic->columnCount[row]);

	if(row >= ic->rowCount) {
		insertRow = ic->rowCount;
		ic->rowCount++;
		printf(" [ir, ");
	} else {
		insertRow = row;
		printf(" [nir, ");
	}

	if(col >= ic->columnCount[insertRow]) {
		insertCol = ic->columnCount[insertRow];
		ic->columnCount[insertRow]++;
		printf("ic] ");

	} else {
		printf("nic] ");

		insertCol = col;
	}
	printf(btnGui->buttonText);
	printf(" at %i, %i\n", insertRow, insertCol);

	if(btnGui->shape.x < ic->containerBounds.x) {
		ic->containerBounds.x = btnGui->shape.x;
	}
	if(btnGui->shape.y < ic->containerBounds.y) {
		ic->containerBounds.y = btnGui->shape.y;
	}
	int btnEndX = btnGui->shape.x + btnGui->shape.w;
	if(btnEndX > ic->containerBounds.x + ic->containerBounds.w) {
		ic->containerBounds.h = btnEndX - ic->containerBounds.x;
	}
	int btnEndY = btnGui->shape.y + btnGui->shape.h;
	if(btnEndY > ic->containerBounds.y + ic->containerBounds.h) {
		ic->containerBounds.w = btnEndY - ic->containerBounds.y;
	}
	ic->buttonRefs[insertRow][insertCol] = btnGui;
	ic->inputCount++;

	// add the button to the drawable list and track it in the container's drawable reference array.
	btnGui->base.enabled = enabled;
	add_drawable(&btnGui->base, scene);
	addDrawableToContainer(ic, &btnGui->base);
}

ButtonGui *createButtonGui(int x, int y, int w, int h, char *buttonText, Parameter *param, void *callback) {
	ButtonGui *btnGui = (ButtonGui *)malloc(sizeof(ButtonGui));
	btnGui->base.draw = drawButtonGui;
	btnGui->base.onPress = callback;
	btnGui->base.enabled = true;
	btnGui->shape.x = x;
	btnGui->shape.y = y;
	btnGui->shape.w = w;
	btnGui->shape.h = h;
	btnGui->buttonText = buttonText;
	btnGui->backgroundColour = RED;
	btnGui->selectedColour = BROWN;
	btnGui->textColour = BLACK;
	btnGui->selected = 0;
	btnGui->parameter = param;
	btnGui->applyCallback = applyButtonCallback;
	return btnGui;
}

void drawButtonGui(void *self) {
	ButtonGui *btnGui = (ButtonGui *)self;
	if(btnGui->selected) {
		DrawRectangle(btnGui->shape.x - 2, btnGui->shape.y - 2, btnGui->shape.w + 4, btnGui->shape.h + 4, btnGui->selectedColour);
	}
	DrawRectangle(btnGui->shape.x, btnGui->shape.y, btnGui->shape.w, btnGui->shape.h, btnGui->backgroundColour);
	DrawText(btnGui->buttonText, btnGui->shape.x, btnGui->shape.y + btnGui->shape.h / 2, 10, btnGui->textColour);
	char valueStr[32];
	snprintf(valueStr, sizeof(valueStr), "%.2f", btnGui->parameter->baseValue);
	DrawText(valueStr, btnGui->shape.x + MeasureText(btnGui->buttonText, 10) + 5, btnGui->shape.y + btnGui->shape.h / 2, 12, btnGui->textColour);
}

void applyButtonCallback(void *self, float value) {
	ButtonGui *btnGui = (ButtonGui *)self;
	btnGui->base.onPress(btnGui->parameter, value);
}

ButtonGui *getSelectedInput(ContainerGroup *cg) {
	InputContainer *ic = (InputContainer *)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
	return ic->buttonRefs[ic->selectedRow][ic->selectedColumn];
}

EnvelopeGui *createEnvelopeGui(Envelope *env, int x, int y, int w, int h) {
	EnvelopeGui *envGui = (EnvelopeGui *)malloc(sizeof(EnvelopeGui));
	envGui->base.draw = drawEnvelopeGui;
	envGui->base.enabled = true;
	envGui->env = env;
	envGui->shape.x = x;
	envGui->shape.y = y;
	envGui->shape.w = w;
	envGui->shape.h = h;
	envGui->graphData = malloc(sizeof(int) * w);
}

EnvelopeContainer *createADEnvelopeContainer(Envelope *env, int x, int y, int w, int h, int scene, int enabled) {
	EnvelopeContainer *ec = (EnvelopeContainer *)malloc(sizeof(EnvelopeContainer));
	ec->envelopeGui = createEnvelopeGui(env, x, y, w, h / 2);
	ec->envInputs = createInputContainer();
	int offsetY = y + h / 2 + ec->envInputs->inputPadding;
	int offsetX = x;
	int btnH = 25;
	int btnW = 70;
	ButtonGui *attackBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "ATK", env->stages[0].duration, incParameterBaseValue);
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui *attackCurveBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "A-CRV", env->stages[1].curvature, incParameterBaseValue);
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui *decayBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "DEC", env->stages[1].duration, incParameterBaseValue);
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui *decayCurveBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "D-CRV", env->stages[1].curvature, incParameterBaseValue);
	addButtonToContainer(attackBtn, ec->envInputs, 0, 0, scene, enabled);
	addButtonToContainer(attackCurveBtn, ec->envInputs, 0, 1, scene, enabled);
	addButtonToContainer(decayBtn, ec->envInputs, 0, 2, scene, enabled);
	addButtonToContainer(decayCurveBtn, ec->envInputs, 0, 3, scene, enabled);
	return ec;
}

EnvelopeContainer *createADSREnvelopeContainer(Envelope *env, int x, int y, int w, int h, int scene, int enabled) {
	EnvelopeContainer *ec = (EnvelopeContainer *)malloc(sizeof(EnvelopeContainer));
	ec->envelopeGui = createEnvelopeGui(env, x, y, w, h / 2);
	ec->envInputs = createInputContainer();
	int offsetY = y + h / 2 + ec->envInputs->inputPadding;
	int offsetX = x;
	int btnH = 25;
	int btnW = 70;
	ButtonGui *attackBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "ATK", env->stages[0].duration, incParameterBaseValue);
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui *decayBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "DEC", env->stages[1].duration, incParameterBaseValue);
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui *sustainBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "SUS", env->stages[2].duration, incParameterBaseValue);
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui *releaseBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "REL", env->stages[3].duration, incParameterBaseValue);
	addButtonToContainer(attackBtn, ec->envInputs, 0, 0, scene, enabled);
	addButtonToContainer(decayBtn, ec->envInputs, 0, 1, scene, enabled);
	addButtonToContainer(sustainBtn, ec->envInputs, 0, 2, scene, enabled);
	addButtonToContainer(releaseBtn, ec->envInputs, 0, 3, scene, enabled);
	return ec;
}

InputContainer *createFmParamsContainer(Instrument *inst, int x, int y, int w, int h, int scene, int enabled) {
	InputContainer *ic = createInputContainer();
	int offsetY = y + ic->inputPadding;
	int offsetX = x;
	int btnH = 20;
	int btnW = 60;
	ButtonGui *ratioBtn1 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 1", inst->ops[0]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui *fdbkBtn1 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 1", inst->ops[0]->feedbackAmount, incParameterBaseValue);
	offsetX += btnW * 2 + ic->inputPadding;
	ButtonGui *ratioBtn2 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 2", inst->ops[1]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui *fdbkBtn2 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 2", inst->ops[1]->feedbackAmount, incParameterBaseValue);
	offsetX = x;
	offsetY += btnH + ic->inputPadding;
	ButtonGui *ratioBtn3 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 3", inst->ops[2]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui *fdbkBtn3 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 3", inst->ops[2]->feedbackAmount, incParameterBaseValue);
	offsetX += btnW * 2 + ic->inputPadding;
	ButtonGui *ratioBtn4 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 4", inst->ops[3]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui *fdbkBtn4 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 4", inst->ops[3]->feedbackAmount, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui *algoBtn4 = createButtonGui(offsetX, offsetY, btnW, btnH, "ALGO", inst->selectedAlgorithm, incParameterBaseValue);
	addButtonToContainer(ratioBtn1, ic, 0, 0, scene, enabled);
	addButtonToContainer(fdbkBtn1, ic, 0, 1, scene, enabled);
	addButtonToContainer(ratioBtn2, ic, 0, 2, scene, enabled);
	addButtonToContainer(fdbkBtn2, ic, 0, 3, scene, enabled);
	addButtonToContainer(ratioBtn3, ic, 1, 0, scene, enabled);
	addButtonToContainer(fdbkBtn3, ic, 1, 1, scene, enabled);
	addButtonToContainer(ratioBtn4, ic, 1, 2, scene, enabled);
	addButtonToContainer(fdbkBtn4, ic, 1, 3, scene, enabled);
	addButtonToContainer(algoBtn4, ic, 1, 4, scene, enabled);
	AlgoGraphGui *agg = createAlgoGraphGui(inst->selectedAlgorithm, SCREEN_W - 110, 0, 100, 100);
	addDrawableToContainer(ic, &agg->base);
	return ic;
}

InputContainer *createSampleParamsContainer(Instrument *inst, int x, int y, int w, int h, int scene, int enabled) {
	InputContainer *ic = createInputContainer();
	int offsetY = y + ic->inputPadding;
	int offsetX = x;
	int btnH = 20;
	int btnW = 60;
	ButtonGui *sampleBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "SAMPLE", inst->sampleIndex, incParameterBaseValue);
	addButtonToContainer(sampleBtn, ic, 0, 0, scene, enabled);
	return ic;
}

InputContainer *createBlepParamsContainer(Instrument *inst, int x, int y, int w, int h, int scene, int enabled) {
	InputContainer *ic = createInputContainer();
	int offsetY = y + ic->inputPadding;
	int offsetX = x;
	int btnH = 20;
	int btnW = 60;
	ButtonGui *shapeBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "SHAPE", inst->shape, incParameterBaseValue);
	addButtonToContainer(shapeBtn, ic, 0, 0, scene, enabled);
	return ic;
}

void createInstrumentGui(VoiceManager *vm, int *selectedInstrument, int scene) {
	InstrumentGui *ig = (InstrumentGui *)malloc(sizeof(InstrumentGui));
	if(!ig) return;
	ig->selectedInstrument = selectedInstrument;

	for(int i = 0; i < vm->enabledChannels; i++) {
		bool isSelected = *selectedInstrument == i;
		ig->instrumentScreenGraphs[i] = createInstGraph(vm->instruments[i], isSelected);
		ig->instrumentCount++;
	}
	igui = ig;
}

Graph *getSelectedInstGraph() {
	return igui->instrumentScreenGraphs[*igui->selectedInstrument];
}

void createArrangerGraph(Arranger *a, PatternList *pl) {
	agui = createGraph(na_horizontal);
	GuiNode *margin1 = createBlankGuiNode();
	GuiNode *margin2 = createBlankGuiNode();
	ArrangerGuiNode *agn = createArrangerGuiNode(0, 0, SCREEN_W * 0.75, SCREEN_H, 5, na_vertical, "arr", 1, a, pl);
	GuiNode *gn = (GuiNode *)agn;

	appendItem(agui->root, margin1, 1);
	appendItem(agui->root, &agn->base, 4);
	appendItem(agui->root, margin2, 1);
	reflowCoordinates(agui->root);
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

ArrangerGuiNode *createArrangerGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Arranger *arranger, PatternList *patternList) {
	ArrangerGuiNode *agn = malloc(sizeof(ArrangerGuiNode));
	GuiNode *gn = (GuiNode *)agn;
	if(!initGuiNode(gn, x, y, w, h, padding, na, name, 1, 0)) {
		printf("ArrangerGuiNode init problem, returning NULL.\n");
		return NULL;
	}
	gn->draw = drawArrangerGuiNode;
	gn->drawable = true;
	agn->grid_padding = 5;
	agn->arranger = arranger;
	agn->patternList = patternList;
	agn->border_size = 3;
	agn->iconx = gn->x;
	agn->icony = gn->y - 30;
	return agn;
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
	char paramValue[50];
	snprintf(paramValue, 50, "%05.2f", gn->p->currentValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = (gn->p->currentValue - gn->p->minValue) / (range / 100) * 2.7;
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
	char paramValue[50];
	snprintf(paramValue, 50, "%i", (int)gn->p->currentValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = (gn->p->currentValue - gn->p->minValue) / (range / 100) * 2.7;
	int tmpx = gn->x;
	int tmpy = gn->y;
	drawColourRectangle(tmpx, tmpy, gn->w, gn->h, 0.125, 2.0, gn->selected);
	tmpx += gn->padding;
	tmpy += gn->padding;
	drawRotatedDial(tmpx, tmpy, 24, 24, 12, -90, angle);
}

void drawDiscreteDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	char paramValue[50];
	snprintf(paramValue, 50, "%i", (int)gn->p->currentValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = (gn->p->currentValue - gn->p->minValue) / (range / 100) * 2.7;
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

void appendFMInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "FM_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	GuiNode *btnrow1 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_1", 0, 0);
	GuiNode *btnrow2 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_2", 0, 0);

	GuiNode *rat1 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO1", 1, incParameterBaseValue, inst->ops[0]->ratio);
	GuiNode *fb1 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK1", 0, incParameterBaseValue, inst->ops[0]->feedbackAmount);
	GuiNode *lvl1 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL1", 0, incParameterBaseValue, inst->ops[0]->level);
	GuiNode *rat2 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO2", 0, incParameterBaseValue, inst->ops[1]->ratio);
	GuiNode *fb2 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK2", 0, incParameterBaseValue, inst->ops[1]->feedbackAmount);
	GuiNode *lvl2 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL2", 0, incParameterBaseValue, inst->ops[1]->level);
	GuiNode *rat3 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO3", 0, incParameterBaseValue, inst->ops[2]->ratio);
	GuiNode *fb3 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK3", 0, incParameterBaseValue, inst->ops[2]->feedbackAmount);
	GuiNode *lvl3 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL3", 0, incParameterBaseValue, inst->ops[2]->level);
	GuiNode *rat4 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "RATIO4", 0, incParameterBaseValue, inst->ops[3]->ratio);
	GuiNode *fb4 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "FEEDBACK4", 0, incParameterBaseValue, inst->ops[3]->feedbackAmount);
	GuiNode *lvl4 = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEVEL4", 0, incParameterBaseValue, inst->ops[3]->level);
	GuiNode *alg = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "ALG", 0, incParameterBaseValue, inst->selectedAlgorithm);
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

	GuiNode *sampleIndex = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "SAMPLE", selected, incParameterBaseValue, inst->sampleIndex);
	GuiNode *pan = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
	GuiNode *loop = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "LOOP", 0, incParameterBaseValue, inst->loopSample);
	loop->draw = drawBtnGuiNode;
	pan->draw = drawDiscreteDialGuiNode;

	if(selected) {
		g->selected = sampleIndex;
	}

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();

	appendItem(btnrow1, sampleIndex, 1);
	appendItem(btnrow1, pan, 1);
	appendItem(btnrow1, loop, 1);
	appendItem(btnrow1, sp1, 4);
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

	GuiNode *waveShape = createBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "SHAPE", selected, incParameterBaseValue, inst->shape);
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

Graph *createInstGraph(Instrument *inst, bool selected) {
	Graph *instGraph = createGraph(na_horizontal);
	GuiNode *margin1 = createBlankGuiNode();
	GuiNode *margin2 = createBlankGuiNode();
	GuiNode *pad1 = createBlankGuiNode();
	GuiNode *pad2 = createBlankGuiNode();

	GuiNode *instwrap = createGuiNode(0, 0, 100, 100, 5, na_vertical, "inst_wrap", 0, 0);
	appendItem(instwrap, pad1, 1);
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

			appendADEnvControlNode(instGraph, modwrap, "mod", 1, false, inst->envelopes[i]);
		} else {
			// printf("NOEnv I: %i\n", i);
			appendBlankNode(modwrap, 1);
		}
	}
	appendItem(instwrap, modwrap, 22);
	appendItem(instGraph->root, margin1, 1);
	appendItem(instGraph->root, instwrap, 18);
	appendItem(instGraph->root, margin2, 1);
	return instGraph;
}

ContainerGroup *createInstrumentModulationGui(Instrument *inst, int x, int y, int contW, int contH, int scene, int enabled) {
	ContainerGroup *cg = createContainerGroup();
	InputContainer *fmParams = NULL;
	InputContainer *sampleParams = NULL;
	InputContainer *blepParams = NULL;
	switch(inst->voiceType) {
		case VOICE_TYPE_FM:
			fmParams = createFmParamsContainer(inst, x, y, contW, contH, scene, enabled);
			addContainerToGroup(cg, fmParams, 0, 0);
			break;
		case VOICE_TYPE_BLEP:
			blepParams = createBlepParamsContainer(inst, x, y, contW, contH, scene, enabled);
			addContainerToGroup(cg, blepParams, 0, 0);
			break;
		case VOICE_TYPE_SAMPLE:
			sampleParams = createSampleParamsContainer(inst, x, y, contW, contH, scene, enabled);
			addContainerToGroup(cg, sampleParams, 0, 0);
			break;
		default:
			break;
	}
	for(int i = 1; i < inst->envelopeCount + 1; i++) {
		EnvelopeContainer *ic = createADEnvelopeContainer(inst->envelopes[i - 1], (i % 2) * contW, 200 + (i / 2) * (contH + 50), contW, contH, scene, enabled);
		addContainerToGroup(cg, ic->envInputs, (int)(i) / 2, (int)(i) % 2);
	}
	return cg;
}

AlgoGraphGui *createAlgoGraphGui(Parameter *algorithm, int x, int y, int w, int h) {
	AlgoGraphGui *agg = (AlgoGraphGui *)malloc(sizeof(AlgoGraphGui));
	agg->algorithm = algorithm;
	agg->shape = (Shape){ x, y, w, h };
	agg->backgroundColour = cs.outlineColour;
	agg->graphColour = cs.reddish;
	agg->base.draw = drawAlgoGraphGui;
	agg->base.enabled = true;
	return agg;
}

void drawAlgoGraphGui(void *self) {
	AlgoGraphGui *agg = (AlgoGraphGui *)self;
	int opSize = agg->shape.w / 5;
	int padding = 16;
	DrawRectangleLines(agg->shape.x, agg->shape.y, agg->shape.w, agg->shape.h, agg->backgroundColour);

	DrawRectangleLines(agg->shape.x + padding, agg->shape.y + padding, opSize, opSize, agg->graphColour);
	DrawText("1", agg->shape.x + padding + opSize / 2, agg->shape.y + padding + opSize / 2, 10, agg->graphColour);

	DrawRectangleLines(agg->shape.x + opSize + padding * 2, agg->shape.y + padding, opSize, opSize, agg->graphColour);
	DrawText("2", agg->shape.x + padding + opSize / 2, agg->shape.y + padding + opSize / 2, 10, agg->graphColour);

	DrawRectangleLines(agg->shape.x + opSize + padding * 2, agg->shape.y + opSize + padding * 2, opSize, opSize, agg->graphColour);
	DrawText("3", agg->shape.x + opSize + padding * 2 + opSize / 2, agg->shape.y + padding + opSize / 2, 10, agg->graphColour);

	DrawRectangleLines(agg->shape.x + padding, agg->shape.y + opSize + padding * 2, opSize, opSize, agg->graphColour);
	DrawText("4", agg->shape.x + opSize + padding * 2 + opSize / 2, agg->shape.y + opSize + padding * 2 + opSize / 2, 10, agg->graphColour);

	int alg = getParameterValueAsInt(agg->algorithm);
	int algOffset = alg * ALGO_SIZE;

	for(int i = algOffset; i < algOffset + ALGO_SIZE; i++) {
		int srcIndex = 4 - fm_algorithm[i][0];
		int dstIndex = 4 - fm_algorithm[i][1];

		if(dstIndex != -1) {
			DrawLine(
			  agg->shape.x + (opSize / 2) + (opSize * (srcIndex / 2)),
			  agg->shape.y + opSize + padding + padding + (opSize * (int)(srcIndex % 2)),
			  agg->shape.x + (opSize / 2) + (opSize * (dstIndex / 2)),
			  agg->shape.y + opSize + padding + padding + (opSize * (int)(dstIndex % 2)),
			  agg->graphColour);
		} else {
			DrawLine(
			  agg->shape.x + (opSize / 2) + padding + (opSize * (srcIndex % 2)),
			  agg->shape.y + opSize + padding + padding + (opSize * (int)(srcIndex / 2)),
			  agg->shape.x + (opSize / 2) + (padding + opSize) * 5,
			  agg->shape.y + opSize + padding + padding + (padding + opSize) * 5,
			  agg->graphColour);
		}
	}
}

void freeEnvelopeContainer(EnvelopeContainer *ec) {
	free(ec->envelopeGui);
	free(ec->envInputs);
	free(ec);
}

void drawEnvelopeGui(void *self) {
	EnvelopeGui *eg = (EnvelopeGui *)self;
	int stageIndex = 0;
	float totalDuration = 0.0f;
	float currentTime = 0.0f;
	float currentLevel = 0.0f;
	float previousLevel = 0.0f;
	DrawRectangle(eg->shape.x, eg->shape.y, eg->shape.w, eg->shape.h, BLACK);
	for(int s = 0; s < eg->env->stageCount; s++) {
		totalDuration += eg->env->stages[s].duration->currentValue;
	}
	float basicIncrement = totalDuration / eg->shape.w;
	for(int w = 0; w < eg->shape.w; w++) {
		float t = currentTime / eg->env->stages[stageIndex].duration->currentValue;
		float shapedT = applyCurve(t, eg->env->stages[stageIndex].curvature->currentValue);

		float startLevel = (stageIndex > 0) ? eg->env->stages[stageIndex - 1].targetLevel : 0.0f;
		currentLevel = startLevel + (eg->env->stages[stageIndex].targetLevel - startLevel) * shapedT;
		DrawLineEx((Vector2){ eg->shape.x + w, eg->shape.y + eg->shape.h - (previousLevel * eg->shape.h) }, (Vector2){ eg->shape.x + w + 1, eg->shape.y + eg->shape.h - (currentLevel * eg->shape.h) }, 2.0f, RED);
		// DrawRectangle(eg->x + w, eg->y + eg->h - (currentLevel * eg->h), 2, 2, RED);

		currentTime += basicIncrement;
		if(currentTime >= 1.0f) {
			currentTime = 0.0f;

			if(stageIndex < eg->env->stageCount - 1) {
				DrawLine(eg->shape.x + w, eg->shape.y, eg->shape.x + w, eg->shape.y + eg->shape.h, GREEN);
				stageIndex++;
			}
		}
		if(eg->env->isTriggered) {
			int currentInc = (eg->shape.w * (eg->env->totalElapsedTime / totalDuration));
			if((int)(currentInc) == w) {
				DrawLine(eg->shape.x + w, eg->shape.y, eg->shape.x + w, eg->shape.y + eg->shape.h, PURPLE);
			}
		}
		previousLevel = currentLevel;
	}
}

void drawTransportGui(void *self) {
	Vector2 pos = (Vector2){ 600, 10 };
	TransportGui *tg = (TransportGui *)self;
	drawSprite(tg->icons, 0, tg->shape.x, tg->shape.y, 20, 20);
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
		switch(arranger->voiceTypes[i]) {
			case VOICE_TYPE_BLEP:
				drawSprite(instrumentIcons, 1, tmpx + i * (cellW + aGui->grid_padding), tmpy, cellW, cellH);
				break;
			case VOICE_TYPE_SAMPLE:
				drawSprite(instrumentIcons, 0, tmpx + i * (cellW + aGui->grid_padding), tmpy, cellW, cellH);
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
				if(arranger->playhead_indices[i] == j) {
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

OscilloscopeGui *createOscilloscopeGui(int x, int y, int w, int h) {
	OscilloscopeGui *og = (OscilloscopeGui *)malloc(sizeof(OscilloscopeGui));
	og->base.draw = drawOscilloscopeGui;
	og->base.enabled = true;
	og->shape.x = x;
	og->shape.y = y;
	og->shape.w = w < OSCILLOSCOPE_HISTORY ? w : OSCILLOSCOPE_HISTORY;
	og->shape.h = h;
	og->updateIndex = 0;
	og->backgroundColour = &cs.highlightedCell;
	og->waveformColour = &cs.backgroundColor;
	og->lineColour = &cs.fontColour;
	return og;
}

void drawOscilloscopeGui(void *self) {
	OscilloscopeGui *og = (OscilloscopeGui *)self;

	// Draw background
	DrawRectangle(og->shape.x, og->shape.y, og->shape.w, og->shape.h, (Color){ 255, 0, 0, 255 });
	// draw center line
	DrawLine(og->shape.x, og->shape.y + og->shape.h / 2, og->shape.x + og->shape.w, og->shape.y + og->shape.h / 2, (Color){ 255, 255, 0, 255 });
	for(int i = 0; i < og->shape.w - 1; i++) {
		DrawLine(og->shape.x + i, (og->shape.y + og->shape.h / 2) + og->data[i], og->shape.x + i + 1, (og->shape.y + og->shape.h / 2) + og->data[i + 1], (Color){ 0, 0, 255, 255 });
	}
}

void updateOscilloscopeGui(OscilloscopeGui *og, float *data, int length) {
	for(int i = 0; i < length; i++) {
		og->data[og->updateIndex] = data[i];
		og->updateIndex++;
		og->updateIndex %= og->shape.w;
	}
}

void updateGraphGui(GraphGui *graphGui) {
	graphGui->history[graphGui->index] = (int)(*graphGui->target * (graphGui->shape.h - (graphGui->padding * 2))); // Cast float to int
	graphGui->index++;
	graphGui->index %= MAX_GRAPH_HISTORY - 1;
}

void drawGraphGui(void *self) {
	GraphGui *graphGui = (GraphGui *)self;
	DrawRectangle(graphGui->shape.x + graphGui->margin, graphGui->shape.y + graphGui->margin, graphGui->shape.w, graphGui->shape.h, (Color){ (int)(*graphGui->target * 255), 255, 255, 255 });
	for(int gi = 0; gi < graphGui->history_size; gi++) {
		int offset = (int)((float)graphGui->history[gi] / (float)graphGui->max);
		DrawRectangle(graphGui->shape.x + gi, graphGui->shape.y + graphGui->padding + offset, 1, 1, BLACK);
	}
	DrawText(graphGui->name, graphGui->shape.x, graphGui->shape.y, textFont.baseSize, RED);
}

ContainerGroup *createModMappingGroup(ParamList *paramList, Mod *mod, int x, int y, int scene, int enabled) {
	ContainerGroup *cg = createContainerGroup();
	for(int i = 0; i < paramList->count; i++) {
		ModConnection *conn = paramList->params[i]->modulators;
		int connectionCount = 0;
		while(conn != NULL) {
			if(conn->source == mod) {
				InputContainer *ic = createInputContainer();
				ButtonGui *amountBtn = createButtonGui(x, y, 45, 30, conn->amount->name, conn->amount, incParameterBaseValue);
				add_drawable(&amountBtn->base, scene);
				ButtonGui *typeBtn = createButtonGui(x + 47, y, 45, 30, conn->type->name, conn->amount, incParameterBaseValue);
				add_drawable(&typeBtn->base, scene);
				addButtonToContainer(amountBtn, ic, 0, 0, scene, enabled);
				addButtonToContainer(typeBtn, ic, 0, 1, scene, enabled);
				addContainerToGroup(cg, ic, 0, 0);
			}
			conn = conn->next;
		}
	}

	return cg;
}

void drawSequencerGui(void *self) {
	SequencerGui *seqGui = (SequencerGui *)self;
	Sequencer *seq = (Sequencer *)seqGui->sequencer;
	PatternList *pl = (PatternList *)seqGui->pattern_list;
	int patternIndex = *seqGui->selected_pattern_index;
	int currentNoteIndex = *seqGui->selected_note_index;
	int currentlyPlaying = -1;

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		if(seq->pattern_index[i] == patternIndex) {
			currentlyPlaying = i;
			break;
		}
	}
	for(int i = 0; i < pl->patterns[patternIndex].pattern_size; i++) {
		int ix = (i % seqGui->pads_per_col);
		int j = (int)(i / seqGui->pads_per_col);
		int *note = getStep(pl, patternIndex, i);

		if(currentNoteIndex == i) {
			DrawRectangle((seqGui->shape.x + seqGui->padding * ix + (ix * (seqGui->shape.w))) - (seqGui->border_size), seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h) * j) - (seqGui->border_size), seqGui->shape.w + seqGui->border_size * 2, seqGui->shape.h + seqGui->border_size * 2, seqGui->outline_colour);
		}
		if(currentlyPlaying > -1 && seq->playhead_index[currentlyPlaying] == i) {
			DrawRectangle(seqGui->shape.x + seqGui->padding * ix + (ix * seqGui->shape.w), seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h) * j), seqGui->shape.w, seqGui->shape.h, seqGui->playing_fill_colour);
		} else {
			DrawRectangle(seqGui->shape.x + seqGui->padding * ix + (ix * seqGui->shape.w), seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h) * j), seqGui->shape.w, seqGui->shape.h, seqGui->default_fill_colour);
		}
		Vector2 textPosition = (Vector2){ (float)(seqGui->shape.x + (ix * seqGui->shape.w) + (seqGui->padding * ix)), (float)(seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h) * j)) };
		DrawTextEx(textFont, getNoteString(note[0], note[1]), textPosition, textFont.baseSize, 4, cs.fontColour);
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

InputsGui *createInputsGui(InputState *inputState, int x, int y) {
	InputsGui *iGui = (InputsGui *)malloc(sizeof(InputsGui));
	iGui->base.draw = drawInputsGui;
	iGui->base.enabled = true;
	iGui->x = x;
	iGui->y = y;
	iGui->inputState = inputState;
	return iGui;
}

void drawInputsGui(void *self) {
	InputsGui *iGui = (InputsGui *)self;
	InputState *is = (InputState *)iGui->inputState;
	for(int i = 0; i < KEY_MAPPING_COUNT; i++) {
		if(is->keys[i].isPressed) {
			DrawRectangle(iGui->x + i * 22, iGui->y, 20, 20, GREEN);
		} else {
			DrawRectangle(iGui->x + i * 22, iGui->y, 20, 20, RED);
		}
		DrawText(KEY_NAMES[i], iGui->x + i * 22, iGui->y, 10, BLACK);
	}
	// TO-DO: FIX this code
	//  for(int i = is->historyIndex; i > 0; i--) {
	//  	DrawText(KEY_NAMES[is->inputHistory[i]], SCREEN_W - 50, iGui->y - i * 11, 10, GRAY);
	//  }
}

DrawableList *create_drawable_list() {
	DrawableList *drawableList = (DrawableList *)malloc(sizeof(DrawableList));
	drawableList->size = 0;
	drawableList->capacity = 32;
	drawableList->drawables = (Drawable **)malloc(sizeof(Drawable) * drawableList->capacity);

	return drawableList;
}

void free_drawable_list(DrawableList *list) {
	if(!list) {
		printf("no list\n");
		return;
	}
	free(list->drawables);
	free(list);
}

void removeDrawable(Drawable *drawable, int scene) {
	DrawableList *list;
	switch(scene) {
		case GLOBAL:
			list = globalDrawableList;
			break;
		case SCENE_ARRANGER:
			list = arrangerScreenDrawableList;
			break;
		case SCENE_PATTERN:
			list = patternScreenDrawableList;
			break;
		case SCENE_INSTRUMENT:
			list = instrumentScreenDrawableList;
			break;
		default:
			printf("invalid scene, nothing removed.\n");
			return;
			break;
	}

	int addressMatch = 0;
	int matchIndex = 0;
	for(int i = 0; i < list->size; i++) {
		if(list->drawables[i] == drawable) {
			// Shift remaining elements left
			for(; i < list->size - 1; i++) {
				list->drawables[i] = list->drawables[i + 1];
			}
			list->size--;
			return;
		}
	}
	// printf("drawable DONE\n");
}

void add_drawable(Drawable *drawable, int scene) {
	DrawableList *list;
	switch(scene) {
		case GLOBAL:
			list = globalDrawableList;
			break;
		case SCENE_ARRANGER:
			list = arrangerScreenDrawableList;
			break;
		case SCENE_PATTERN:
			list = patternScreenDrawableList;
			break;
		case SCENE_INSTRUMENT:
			list = instrumentScreenDrawableList;
			break;
		default:
			printf("invalid scene, drawable not added.\n");
			return;
			break;
	}

	if(list->size == list->capacity) {
		list->capacity += 16;
		list->drawables = (Drawable **)realloc(list->drawables, sizeof(Drawable *) * list->capacity);
	}

	list->drawables[list->size++] = drawable;
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
			// printf("p!");
			// drawNode(patternGraph->root);
			for(int i = 0; i < patternScreenDrawableList->size; i++) {
				patternScreenDrawableList->drawables[i]->draw(patternScreenDrawableList->drawables[i]);
			}
			break;
		case SCENE_INSTRUMENT:
			// printf("i!");
			drawNode(igui->instrumentScreenGraphs[*igui->selectedInstrument]->root);
			// drawNode(instrumentGraph->root);
			break;
		default:
			printf("Invalid scene, nothing to draw\n");
			break;
	}
	for(int i = 0; i < globalDrawableList->size; i++) {
		// globalDrawableList->drawables[i]->draw(globalDrawableList->drawables[i]);
	}
	// drawNode(globalGraph->root);
	// printf("\n");
}

void CleanupGUI(void) {
	printf("freeing drawables 1...");
	free_drawable_list(globalDrawableList);
	printf("freeing drawables 2...");
	free_drawable_list(instrumentScreenDrawableList);
	printf("freeing drawables 3...");
	free_drawable_list(patternScreenDrawableList);
	printf("freeing drawables 4...");
	free_drawable_list(arrangerScreenDrawableList);
	printf("freeing drawables 5...");
}
