#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"
#include "gui.h"
#include "input.h"
#include "oscillator.h"
#include "sequencer.h"
#include "modsystem.h"
#include "notes.h"


DrawableList *patternScreenDrawableList;
DrawableList *globalDrawableList;
DrawableList *arrangerScreenDrawableList;
DrawableList *instrumentScreenDrawableList;
Font textFont;
Font symbolFont;
ColourScheme cs;
SpriteSheet *instrumentIcons;

void initCustomFont(Font *f, char *path, int charCount, int width, int height){
	*f = LoadFontEx(path, width, NULL, charCount);
	if (f->texture.id == 0) {
		printf("Failed to load font: %s\n", path);
	} else {
		printf("Successfully loaded font: %s\n", path);
	}
}

SpriteSheet* createSpriteSheet(char* imagePath, int sprite_w, int sprite_h){
	SpriteSheet * sh = (SpriteSheet*)malloc(sizeof(SpriteSheet));
	sh->sheet = LoadTexture(imagePath);
	sh->spriteCount = (sh->sheet.width / sprite_w) * (sh->sheet.height / sprite_h);
	sh->spriteW = sprite_w;
	sh->spriteH = sprite_h;
	sh->spriteSize = (Rectangle){0, 0, sprite_w, sprite_h};
}

void drawSprite(SpriteSheet *spriteSheet, int index, int x, int y){
	index = index > spriteSheet->spriteCount ? index % spriteSheet->spriteCount : index;
	spriteSheet->spriteSize.x = index * spriteSheet->spriteW;
	DrawTextureRec(spriteSheet->sheet, spriteSheet->spriteSize, (Vector2){x,y}, WHITE);
	spriteSheet->spriteSize.x = 0;
}

void initDefaultColourScheme(ColourScheme* colourScheme){
	colourScheme->backgroundColor = (Color){17, 7, 8,255};
	colourScheme->fontColour = (Color){207, 212, 121,255};
	colourScheme->secondaryFontColour = (Color){112, 110, 65,255};
	colourScheme->outlineColour = (Color){82, 220, 159, 255};
	colourScheme->defaultCell = (Color){211, 246, 219, 255};
	colourScheme->highlightedCell = (Color){236, 154, 144, 255};
	colourScheme->selectedCell = (Color){170, 38, 49, 255};
	colourScheme->blankCell = (Color){94, 23, 29, 255};
	colourScheme->reddish = (Color){170, 38, 49, 255};
}

void setColourScheme(ColourScheme* colourScheme){
	cs = *colourScheme;
}

Color** getColorSchemeAsPointerArray(){
	Color** colourScheme = malloc(sizeof(Color*) * 9);
    if (!colourScheme) return NULL;
	
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

ColourScheme* getColourScheme(){
	return &cs;
}

void clearBg(){
	ClearBackground(cs.backgroundColor);
}

void InitGUI(void)
{
	const int screenWidth = SCREEN_W;
	const int screenHeight = SCREEN_H;

	initDefaultColourScheme(&cs);

	patternScreenDrawableList = create_drawable_list();
	arrangerScreenDrawableList = create_drawable_list();
	instrumentScreenDrawableList = create_drawable_list();
	globalDrawableList = create_drawable_list(); // Initialize globalDrawableList

	InitWindow(screenWidth, screenHeight, "Raylib basic window");
	
	textFont = LoadFont("resources/fonts/setback.png");
	initCustomFont(&symbolFont, "resources/fonts/iconzfin.png", 8, 10, 12);
	instrumentIcons = createSpriteSheet("resources/fonts/synthicons.png", 24, 24);

	SetTargetFPS(60);
}

TransportGui *createTransportGui(int *playing, Arranger *arranger, int x, int y){
	TransportGui *tsGui = (TransportGui*)malloc(sizeof(TransportGui));
	tsGui->base.draw = drawTransportGui;
	tsGui->shape.x = x;
	tsGui->shape.y = y;
	tsGui->icons = createSpriteSheet("resources/fonts/iconzfin.png", 10, 12);
	tsGui->playing = playing;
	tsGui->arranger = arranger;
	tsGui->tempo = &arranger->beats_per_minute;
	add_drawable(&tsGui->base, GLOBAL); // Add TransportGui to globalDrawableList
	return tsGui;
}

SequencerGui *createSequencerGui(Sequencer *sequencer, PatternList *pl, int *selectedPattern, int *selectedNote, int x, int y)
{
	SequencerGui *seqGui = (SequencerGui *)malloc(sizeof(SequencerGui));
	seqGui->base.draw = drawSequencerGui;
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
}

GraphGui *createGraphGui(float* target, char* name, float min, float max, int x, int y, int h, int size){
	GraphGui *graphGui = (GraphGui*)malloc(sizeof(GraphGui));
	graphGui->base.draw = drawGraphGui;
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
	for(int i = 0; i < graphGui->history_size; i++){
		graphGui->history[i] = 0;
	}
	return graphGui; // Return the created GraphGui object
}

ArrangerGui *createArrangerGui(Arranger *arranger, PatternList *patternList, int x, int y){
	ArrangerGui *arrangerGui = (ArrangerGui*)malloc(sizeof(ArrangerGui));
	arrangerGui->base.draw = drawArrangerGui;
	arrangerGui->shape.x = x;
	arrangerGui->shape.y = y + 30;
	arrangerGui->shape.w = 24;
	arrangerGui->shape.h = 24;
	arrangerGui->iconx = x;
	arrangerGui->icony = y;
	arrangerGui->cellColour = cs.defaultCell;
	arrangerGui->arranger = arranger;
	arrangerGui->patternList = patternList;
	arrangerGui->grid_padding = 4;
	arrangerGui->border_size = 3;
	return arrangerGui;
}

SongMinimapGui* createSongMinimapGui(Arranger *arranger, int *songIndex, int x, int y){
	SongMinimapGui *minimapGui = (SongMinimapGui*)malloc(sizeof(SongMinimapGui));
	minimapGui->base.draw = drawSongMinimapGui;
	minimapGui->arranger = arranger;
	minimapGui->songIndex = songIndex;
	minimapGui->shape.x = x;
	minimapGui->shape.y = y;
	minimapGui->shape.w = 4;
	minimapGui->shape.h = 4;
	minimapGui->padding = 1;
	minimapGui->maxMapLength = (SCREEN_H - 150)/(minimapGui->shape.h + minimapGui->padding);
	minimapGui->defaultCellColour = cs.defaultCell;
	minimapGui->selectedCellColour = cs.reddish;
	minimapGui->playingCellColour = cs.highlightedCell;
	minimapGui->blankCellColour = cs.blankCell;
}

InputContainer* createInputContainer(){
	InputContainer* btnCont = (InputContainer*)malloc(sizeof(InputContainer));
	btnCont->rowCount = 0;
	for(int i = 0; i < MAX_BUTTON_ROWS; i++){
		btnCont->columnCount[i] = 0;
	}
	btnCont->containerBounds = (Shape){SCREEN_W,SCREEN_H,0,0};
	btnCont->inputCount = 0;
	btnCont->otherDrawableCount = 0;
	btnCont->inputPadding = 2;
	btnCont->selectedRow = 0;
	btnCont->selectedColumn = 0;
	return btnCont;
}

void addDrawableToContainer(InputContainer* ic, Drawable* d){
	ic->otherDrawables[ic->otherDrawableCount] = d;
	ic->otherDrawableCount++;
}

ContainerGroup* createContainerGroup(){
	ContainerGroup* cg = (ContainerGroup*)malloc(sizeof(ContainerGroup));
	cg->rowCount = 0;
	for(int i = 0; i < MAX_BUTTON_CONTAINER_ROWS; i++){
		cg->columnCount[i] = 0;
	}
	cg->selectedRow = 0;
	cg->selectedColumn = 0;
	return cg;
}

void containerGroupNavigate(ContainerGroup* cg, int rowInc, int colInc){
	printf("NAV GRP: %i, %i inc: %i, %i\n", cg->selectedRow, cg->selectedColumn, rowInc, colInc);
	InputContainer* ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
	ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = false;
	printf("NAV CONT: %i, %i\n", ic->selectedRow, ic->selectedColumn);
	int newRow = ic->selectedRow + rowInc;
	int newCol = ic->selectedColumn + colInc;
	if(newRow >= ic->rowCount){
		printf("newcol > inputcontainer rowcount\n");

		if(cg->selectedRow < cg->rowCount - 1){
			printf("containergroup selectedrow %i < rowcount %i\n", cg->selectedRow, cg->rowCount);

			cg->selectedRow++;
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedRow = 0;
			if(ic->selectedColumn >=  ic->columnCount[ic->selectedRow]){
				ic->selectedColumn = ic->columnCount[ic->selectedRow] - 1;
			} 
			printf("\t\tcontrowinc%i,%i, ic selected: %i, %i\n",cg->selectedRow,cg->selectedColumn, ic->selectedRow, ic->selectedColumn);
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else if(newRow < 0){
		printf("newrow < 0\n");

		if(cg->selectedRow > 0){
		printf("selectedrow > 0\n");

			cg->selectedRow--;
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedRow = ic->rowCount-1;
			printf("\t\tcontrowdec%i,%i, ic selected: %i, %i\n",cg->selectedRow,cg->selectedColumn, ic->selectedRow, ic->selectedColumn);
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else {
		printf("newrow in range: %i\n", newRow);

		ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
		ic->selectedRow = newRow;
		ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
	}
	ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
	if(newCol >= ic->columnCount[ic->selectedRow]){
		printf("newcol > inputconainer colCount\n");

		if(cg->selectedColumn < cg->columnCount[cg->selectedRow] - 1){
		printf("sectedcolum < containergroup colCount\n");

			cg->selectedColumn++;
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedColumn = 0;
			printf("\t\tcontcolinc%i,%i, ic selected: %i, %i\n",cg->selectedRow,cg->selectedColumn, ic->selectedRow, ic->selectedColumn);
			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 1;
		}
	} else if(newCol < 0){
		printf("newcol < 0");
		if(cg->selectedColumn > 0){
			printf("containergroup selectedcolumn > 0\n");

			cg->selectedColumn--;

			ic->buttonRefs[ic->selectedRow][ic->selectedColumn]->selected = 0;
			ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
			ic->selectedColumn = ic->columnCount[ic->selectedRow]-1;
			printf("\t\tcontcoldec%i,%i, ic selected: %i, %i\n",cg->selectedRow,cg->selectedColumn, ic->selectedRow, ic->selectedColumn);

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

void addContainerToGroup(ContainerGroup* cg, InputContainer* ic, int row, int col){
	if(row < 0 || col < 0 || row > MAX_BUTTON_CONTAINER_ROWS - 1 || col > MAX_BUTTON_CONTAINER_COLS - 1) {
		printf("attempting out-of-bounds group insertion.\n");
		return;
	}
	printf("GROUP:count before add:%i,%i adding ", cg->rowCount, cg->columnCount[row]);

	int insertRow = 0, insertCol = 0;
	if(row >= cg->rowCount){
		insertRow = cg->rowCount;
		cg->rowCount++;
		printf(" [ir, ");

	} else {
		insertRow = row;
		printf(" [nir, ");

	}
	if(col >= cg->columnCount[insertRow]){
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

void removeButtonFromContainer(ButtonGui* btnGui, InputContainer* btnCont, Scene scene) {
    for (int row = 0; row < btnCont->rowCount; row++) {
        for (int col = 0; col < btnCont->columnCount[row]; col++) {
            if (btnCont->buttonRefs[row][col] == btnGui) {
                // Shift remaining buttons left
                for (int j = col; j < btnCont->columnCount[row] - 1; j++) {
                    btnCont->buttonRefs[row][j] = btnCont->buttonRefs[row][j + 1];
                }
                btnCont->columnCount[row]--; // This needs to happen after finding the button
                removeDrawable(&btnGui->base, scene);
                return;
            }
        }
    }
}

void moveContainer(InputContainer* ic, int deltax, int deltay){
	for (int row = 0; row < ic->rowCount; row++) {
        for (int col = 0; col < ic->columnCount[row]; col++) {
			ic->buttonRefs[row][col]->shape.x += deltax;
			ic->buttonRefs[row][col]->shape.y += deltay;
		}
	}
}

void addButtonToContainer(ButtonGui* btnGui, InputContainer* ic, int row, int col){
	if(row < 0 || col < 0 || row > MAX_BUTTON_CONTAINER_ROWS - 1 || col > MAX_BUTTON_CONTAINER_COLS - 1) {
		printf("attempting out-of-bounds btn container insertion.\n");
		return;
	}
	int insertRow = 0, insertCol = 0;
	printf("CONTAINER:count before add:%i,%i adding ", ic->rowCount, ic->columnCount[row]);
	
	if(row >= ic->rowCount){
		insertRow = ic->rowCount;
		ic->rowCount++;
		printf(" [ir, ");
	} else {
		insertRow = row;
		printf(" [nir, ");
	}

	if(col >= ic->columnCount[insertRow]){
		insertCol = ic->columnCount[insertRow];
		ic->columnCount[insertRow]++;
		printf("ic] ");
		
	} else {
		printf("nic] ");

		insertCol = col;
	}
	printf(btnGui->buttonText);
	printf(" at %i, %i\n", insertRow, insertCol);

	if(btnGui->shape.x < ic->containerBounds.x){
		ic->containerBounds.x = btnGui->shape.x;
	}
	if(btnGui->shape.y < ic->containerBounds.y){
		ic->containerBounds.y = btnGui->shape.y;
	}
	int btnEndX = btnGui->shape.x + btnGui->shape.w;
	if(btnEndX > ic->containerBounds.x + ic->containerBounds.w){
		ic->containerBounds.h = btnEndX - ic->containerBounds.x;
	}
	int btnEndY = btnGui->shape.y + btnGui->shape.h;
	if(btnEndY > ic->containerBounds.y + ic->containerBounds.h){
		ic->containerBounds.w = btnEndY - ic->containerBounds.y;
	}
	ic->buttonRefs[insertRow][insertCol] = btnGui;
	ic->inputCount++;
}

ButtonGui* createButtonGui(int x, int y, int w, int h, char* buttonText, Parameter* param, void* callback){
	ButtonGui* btnGui = (ButtonGui*)malloc(sizeof(ButtonGui));
	btnGui->base.draw = drawButtonGui;
	btnGui->base.onPress = callback;
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

void drawButtonGui(void* self){
	ButtonGui* btnGui = (ButtonGui*)self;
	if(btnGui->selected){
		DrawRectangle(btnGui->shape.x-2, btnGui->shape.y-2, btnGui->shape.w+4, btnGui->shape.h+4, btnGui->selectedColour);
	}
	DrawRectangle(btnGui->shape.x, btnGui->shape.y, btnGui->shape.w, btnGui->shape.h, btnGui->backgroundColour);
	DrawText(btnGui->buttonText, btnGui->shape.x, btnGui->shape.y + btnGui->shape.h/2, 10, btnGui->textColour);
	char valueStr[32];
    snprintf(valueStr, sizeof(valueStr), "%.2f", btnGui->parameter->baseValue);
    DrawText(valueStr, btnGui->shape.x + MeasureText(btnGui->buttonText, 10) + 5, 
             btnGui->shape.y + btnGui->shape.h/2, 12, btnGui->textColour);
}

void applyButtonCallback(void* self, float value){
	ButtonGui* btnGui = (ButtonGui*)self;
	btnGui->base.onPress(btnGui->parameter, value);
}

ButtonGui* getSelectedInput(ContainerGroup* cg){
	InputContainer* ic = (InputContainer*)cg->containerRefs[cg->selectedRow][cg->selectedColumn];
	return ic->buttonRefs[ic->selectedRow][ic->selectedColumn];
}

EnvelopeGui* createEnvelopeGui(Envelope* env, int x, int y, int w, int h){
	EnvelopeGui *envGui = (EnvelopeGui*)malloc(sizeof(EnvelopeGui));
	envGui->base.draw = drawEnvelopeGui;
	envGui->env = env;
	envGui->shape.x = x;
	envGui->shape.y = y;
	envGui->shape.w = w;
	envGui->shape.h = h;
	envGui->graphData = malloc(sizeof(int) * w);
}

EnvelopeContainer* createADEnvelopeContainer(Envelope* env, int x, int y, int w, int h, int scene){
	EnvelopeContainer* ec = (EnvelopeContainer*)malloc(sizeof(EnvelopeContainer));
	ec->envelopeGui = createEnvelopeGui(env, x,y,w,h/2);
	ec->envInputs = createInputContainer();
	int offsetY = y + h/2 + ec->envInputs->inputPadding;
	int offsetX = x;
	int btnH = 25;
	int btnW = 70;
	ButtonGui* attackBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "ATK", env->stages[0].duration, incParameterBaseValue); 
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui* attackCurveBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "A-CRV", env->stages[1].curvature, incParameterBaseValue); 
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui* decayBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "DEC", env->stages[1].duration, incParameterBaseValue); 
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui* decayCurveBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "D-CRV", env->stages[1].curvature, incParameterBaseValue); 
	addButtonToContainer(attackBtn, ec->envInputs, 0,0);
	addButtonToContainer(attackCurveBtn, ec->envInputs, 0,1);
	addButtonToContainer(decayBtn, ec->envInputs, 0,2);
	addButtonToContainer(decayCurveBtn, ec->envInputs, 0,3);
	add_drawable(&ec->envelopeGui->base, scene);
	add_drawable(&attackBtn->base, scene);
	add_drawable(&decayBtn->base, scene);
	add_drawable(&attackCurveBtn->base, scene);
	add_drawable(&decayCurveBtn->base, scene);
	return ec;
}

EnvelopeContainer* createADSREnvelopeContainer(Envelope* env, int x, int y, int w, int h, int scene){
	EnvelopeContainer* ec = (EnvelopeContainer*)malloc(sizeof(EnvelopeContainer));
	ec->envelopeGui = createEnvelopeGui(env, x,y,w,h/2);
	ec->envInputs = createInputContainer();
	int offsetY = y + h/2 + ec->envInputs->inputPadding;
	int offsetX = x;
	int btnH = 25;
	int btnW = 70;
	ButtonGui* attackBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "ATK", env->stages[0].duration, incParameterBaseValue); 
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui* decayBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "DEC", env->stages[1].duration, incParameterBaseValue); 
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui* sustainBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "SUS", env->stages[2].duration, incParameterBaseValue); 
	offsetX += btnW + ec->envInputs->inputPadding;
	ButtonGui* releaseBtn = createButtonGui(offsetX, offsetY, btnW, btnH, "REL", env->stages[3].duration, incParameterBaseValue); 
	addButtonToContainer(attackBtn, ec->envInputs, 0,0);
	addButtonToContainer(decayBtn, ec->envInputs, 0,1);
	addButtonToContainer(sustainBtn, ec->envInputs, 0,2);
	addButtonToContainer(releaseBtn, ec->envInputs, 0,3);
	add_drawable(&ec->envelopeGui->base, scene);
	add_drawable(&attackBtn->base, scene);
	add_drawable(&decayBtn->base, scene);
	add_drawable(&sustainBtn->base, scene);
	add_drawable(&releaseBtn->base, scene);
	return ec;
}

InputContainer* createFmParamsContainer(Instrument* inst, int x, int y, int w, int h, int scene){
	InputContainer* ic = createInputContainer();
	int offsetY = y + ic->inputPadding;
	int offsetX = x;
	int btnH = 20;
	int btnW = 60;
	ButtonGui* ratioBtn1 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 1", inst->ops[0]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui* fdbkBtn1 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 1", inst->ops[0]->feedbackAmount, incParameterBaseValue);
	offsetX += btnW*2 + ic->inputPadding;
	ButtonGui* ratioBtn2 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 2", inst->ops[1]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui* fdbkBtn2 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 2", inst->ops[1]->feedbackAmount, incParameterBaseValue);
	offsetX = x;
	offsetY += btnH + ic->inputPadding;
	ButtonGui* ratioBtn3 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 3", inst->ops[2]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui* fdbkBtn3 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 3", inst->ops[2]->feedbackAmount, incParameterBaseValue);
	offsetX += btnW*2 + ic->inputPadding;
	ButtonGui* ratioBtn4 = createButtonGui(offsetX, offsetY, btnW, btnH, "RAT 4", inst->ops[3]->ratio, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui* fdbkBtn4 = createButtonGui(offsetX, offsetY, btnW, btnH, "FBK 4", inst->ops[3]->feedbackAmount, incParameterBaseValue);
	offsetX += btnW + ic->inputPadding;
	ButtonGui* algoBtn4 = createButtonGui(offsetX, offsetY, btnW, btnH, "ALGO", inst->selectedAlgorithm, incParameterBaseValue);
	addButtonToContainer(ratioBtn1, ic, 0,0);
	addButtonToContainer(fdbkBtn1, ic, 0,1);
	addButtonToContainer(ratioBtn2, ic, 0,2);
	addButtonToContainer(fdbkBtn2, ic, 0,3);
	addButtonToContainer(ratioBtn3, ic, 1,0);
	addButtonToContainer(fdbkBtn3, ic, 1,1);
	addButtonToContainer(ratioBtn4, ic, 1,2);
	addButtonToContainer(fdbkBtn4, ic, 1,3);
	addButtonToContainer(algoBtn4, ic, 1,4);
	AlgoGraphGui* agg = createAlgoGraphGui(inst->selectedAlgorithm, SCREEN_W-110, 0, 100,100);
	addDrawableToContainer(ic, &agg->base);
	add_drawable(&agg->base, scene);
	add_drawable(&ratioBtn1->base, scene);
	add_drawable(&fdbkBtn1->base, scene);
	add_drawable(&ratioBtn2->base, scene);
	add_drawable(&fdbkBtn2->base, scene);
	add_drawable(&ratioBtn3->base, scene);
	add_drawable(&fdbkBtn3->base, scene);
	add_drawable(&ratioBtn4->base, scene);
	add_drawable(&fdbkBtn4->base, scene);
	add_drawable(&algoBtn4->base, scene);
	return ic;
}

void removeContainerGroupFromScene(ContainerGroup* cg, int scene){

}

AlgoGraphGui* createAlgoGraphGui(Parameter* algorithm, int x, int y, int w, int h){
	AlgoGraphGui* agg = (AlgoGraphGui*)malloc(sizeof(AlgoGraphGui));
	agg->algorithm = algorithm;
	agg->shape = (Shape){x,y,w,h};
	agg->backgroundColour = cs.outlineColour;
	agg->graphColour = cs.reddish;
	agg->base.draw = drawAlgoGraphGui;
}

void drawAlgoGraphGui(void* self){
	AlgoGraphGui* agg = (AlgoGraphGui*)self;
	int opSize = agg->shape.w/5;
	int padding = 16;
	DrawRectangleLines(agg->shape.x, agg->shape.y, agg->shape.w, agg->shape.h, agg->backgroundColour);

	DrawRectangleLines(agg->shape.x + padding, agg->shape.y + padding, opSize, opSize, agg->graphColour);
	DrawText("1", agg->shape.x + padding + opSize/2, agg->shape.y + padding + opSize/2, 10, agg->graphColour);

	DrawRectangleLines(agg->shape.x + opSize + padding*2, agg->shape.y + padding, opSize, opSize, agg->graphColour);
	DrawText("2", agg->shape.x + padding + opSize/2, agg->shape.y + padding + opSize/2, 10, agg->graphColour);

	DrawRectangleLines(agg->shape.x + opSize + padding*2, agg->shape.y + opSize + padding * 2, opSize, opSize, agg->graphColour);
	DrawText("3", agg->shape.x + opSize + padding*2 + opSize/2, agg->shape.y + padding + opSize/2, 10, agg->graphColour);

	DrawRectangleLines(agg->shape.x + padding, agg->shape.y + opSize + padding * 2, opSize, opSize, agg->graphColour);
	DrawText("4", agg->shape.x + opSize + padding*2 + opSize/2, agg->shape.y + opSize + padding * 2 + opSize/2, 10, agg->graphColour);

	int alg = getParameterValueAsInt(agg->algorithm);
	int algOffset = alg * ALGO_SIZE;

	for(int i = algOffset; i < algOffset + ALGO_SIZE; i++){
		int srcIndex = 4 - fm_algorithm[i][0];
		int dstIndex = 4 - fm_algorithm[i][1];

		if(dstIndex != -1){
			DrawLine(
				agg->shape.x + (opSize/2) + (opSize * (srcIndex / 2)), 
				agg->shape.y + opSize + padding + padding + (opSize * (int)(srcIndex % 2)), 
				agg->shape.x + (opSize/2) + (opSize * (dstIndex / 2)), 
				agg->shape.y + opSize + padding + padding + (opSize * (int)(dstIndex % 2)),
				agg->graphColour);
		} else {
			DrawLine(
				agg->shape.x + (opSize/2) + padding + (opSize * (srcIndex % 2)), 
				agg->shape.y + opSize + padding + padding + (opSize * (int)(srcIndex / 2)), 
				agg->shape.x + (opSize/2) + (padding + opSize)*5, 
				agg->shape.y + opSize + padding + padding  + (padding + opSize)*5,
				agg->graphColour);
		}
	}
}

ContainerGroup* createInstrumentModulationGui(Instrument* inst, int x, int y, int contW, int contH, int scene){
	ContainerGroup* cg = createContainerGroup();
	switch(inst->voiceType){
		case VOICE_TYPE_FM:
			InputContainer* fmParams = createFmParamsContainer(inst, x, y, contW, contH, scene);
			addContainerToGroup(cg, fmParams,0,0);
	}
	for(int i = 1	; i < inst->envelopeCount+1; i++){
		EnvelopeContainer* ic = createADEnvelopeContainer(inst->envelopes[i-1], (i % 2) * contW, 200 + (i / 2) * (contH+50), contW, contH, scene);
		addContainerToGroup(cg, ic->envInputs, (int)(i)/2, (int)(i)%2);
	}

	return cg;
}

void freeEnvelopeContainer(EnvelopeContainer* ec){
	free(ec->envelopeGui);
	free(ec->envInputs);
	free(ec);
}

void drawEnvelopeGui(void* self){
	EnvelopeGui *eg = (EnvelopeGui*)self;
	int stageIndex = 0;
	float totalDuration = 0.0f;
	float currentTime = 0.0f;
	float currentLevel = 0.0f;
	float previousLevel = 0.0f;
	DrawRectangle(eg->shape.x, eg->shape.y, eg->shape.w, eg->shape.h, BLACK);
	for(int s = 0; s < eg->env->stageCount; s++){
		totalDuration += eg->env->stages[s].duration->currentValue;
	}
	float basicIncrement = totalDuration / eg->shape.w; 
	for(int w = 0; w < eg->shape.w; w++){
		// if (!eg->env->stages[stageIndex].isSustain) {
		// 	float increment = calculateCurvedIncrement(
		// 		currentLevel, 
		// 		eg->env->stages[stageIndex].targetLevel,
		// 		basicIncrement, 
		// 		eg->env->stages[stageIndex].curvature->currentValue
		// 	);
		// 	currentLevel += increment;
		// }
		float t = currentTime / eg->env->stages[stageIndex].duration->currentValue;
		float shapedT = applyCurve(t, eg->env->stages[stageIndex].curvature->currentValue);
		
		float startLevel = (stageIndex > 0) ? 
						eg->env->stages[stageIndex-1].targetLevel : 
						0.0f;
		currentLevel = startLevel + (eg->env->stages[stageIndex].targetLevel - startLevel) * shapedT;
		DrawLineEx((Vector2){eg->shape.x + w, eg->shape.y + eg->shape.h - (previousLevel * eg->shape.h)},(Vector2){ eg->shape.x + w + 1, eg->shape.y + eg->shape.h - (currentLevel * eg->shape.h)}, 2.0f, RED);
		//DrawRectangle(eg->x + w, eg->y + eg->h - (currentLevel * eg->h), 2, 2, RED);

		currentTime += basicIncrement;
		if (currentTime >= 1.0f) {
        	currentTime = 0.0f;
			
			if (stageIndex < eg->env->stageCount - 1) {
				DrawLine(eg->shape.x + w, eg->shape.y, eg->shape.x + w, eg->shape.y + eg->shape.h, GREEN);
				stageIndex++;
			}
		}
		if(eg->env->isTriggered){
			int currentInc = ( eg->shape.w * (eg->env->totalElapsedTime / totalDuration));
			if((int)(currentInc) == w){
				DrawLine(eg->shape.x + w, eg->shape.y, eg->shape.x + w, eg->shape.y + eg->shape.h, PURPLE);
			}

		}
		previousLevel = currentLevel;
	}
}

void drawTransportGui(void *self){
	Vector2 pos = (Vector2){600,10};
	TransportGui *tg =(TransportGui*)self;
	drawSprite(tg->icons, 0, tg->shape.x, tg->shape.y);
}

void drawArrangerGui(void *self){
	ArrangerGui *aGui = (ArrangerGui*)self;
	Arranger *arranger = (Arranger*)aGui->arranger;
	char *cellText = malloc(sizeof(char) *4);
	int cursorx, cursory;
	cursorx = aGui->shape.x + arranger->selected_x * (aGui->shape.w + aGui->grid_padding);
	cursory = aGui->shape.y + arranger->selected_y * (aGui->shape.h + aGui->grid_padding);

	for(int i = 0; i < arranger->enabledChannels; i++){
		switch(arranger->voiceTypes[i]){
			case VOICE_TYPE_BLEP:
				drawSprite(instrumentIcons, 1, aGui->iconx + i * (aGui->shape.w + aGui->grid_padding), aGui->icony);
				break;
			case VOICE_TYPE_SAMPLE:
				drawSprite(instrumentIcons, 0, aGui->iconx + i * (aGui->shape.w + aGui->grid_padding), aGui->icony);
				break;
			case VOICE_TYPE_FM:
				drawSprite(instrumentIcons, 2, aGui->iconx + i * (aGui->shape.w + aGui->grid_padding), aGui->icony);
				break;
		}
	}
	DrawRectangle(cursorx - aGui->border_size, cursory - aGui->border_size, aGui->shape.w + (aGui->border_size * 2),aGui->shape.h + (aGui->border_size * 2), cs.outlineColour);
	for(int i = 0; i < arranger->enabledChannels; i++){
		//int px = i % arranger->enabledChannels;
		int newx = aGui->shape.x + (i * (aGui->shape.w + aGui->grid_padding));
		for(int j = 0; j < MAX_SONG_LENGTH; j++){
			int newy = aGui->shape.y + (j * (aGui->shape.h + aGui->grid_padding));
			if(arranger->song[i][j] > -1){
				sprintf(cellText, "%i\0", arranger->song[i][j]);
				if(arranger->playhead_indices[i] == j){
					DrawRectangle(newx, newy, aGui->shape.w, aGui->shape.h, (Color){255,0,0,255});
				} else {
					DrawRectangle(newx, newy, aGui->shape.w, aGui->shape.h, cs.defaultCell);
				}
				DrawText(cellText, newx - 5 + aGui->shape.w / 2, newy - 5 + aGui->shape.h / 2, textFont.baseSize, cs.secondaryFontColour);
			} else {
				DrawRectangle(newx, newy, aGui->shape.w, aGui->shape.h, cs.blankCell);
				DrawText("--", newx - 5 + aGui->shape.w / 2, newy - 5 + aGui->shape.h / 2, textFont.baseSize, cs.secondaryFontColour);
			}
		}
	}
}

OscilloscopeGui* createOscilloscopeGui(int x, int y, int w, int h){
	OscilloscopeGui* og = (OscilloscopeGui*)malloc(sizeof(OscilloscopeGui));
	og->base.draw = drawOscilloscopeGui;
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

void drawOscilloscopeGui(void* self){
	OscilloscopeGui* og = (OscilloscopeGui*)self;

	// Draw background
	DrawRectangle(og->shape.x, og->shape.y, og->shape.w, og->shape.h, (Color){255,0,0,255});
	//draw center line
	DrawLine(og->shape.x, og->shape.y + og->shape.h/2, og->shape.x + og->shape.w, og->shape.y + og->shape.h/2, (Color){255,255,0,255});
	for(int i = 0; i < og->shape.w-1; i++){
		DrawLine(og->shape.x + i, (og->shape.y + og->shape.h/2) + og->data[i], og->shape.x+i+1, (og->shape.y + og->shape.h/2) + og->data[i+1], (Color){0,0,255,255});
	}
}

void updateOscilloscopeGui(OscilloscopeGui* og, float* data, int length){
	for(int i = 0; i < length; i++){
		og->data[og->updateIndex] = data[i];
		og->updateIndex++;
		og->updateIndex %= og->shape.w;
	}
}


void updateGraphGui(GraphGui* graphGui){
	graphGui->history[graphGui->index] = (int)(*graphGui->target * (graphGui->shape.h - (graphGui->padding*2))); // Cast float to int
	graphGui->index++;
	graphGui->index %= MAX_GRAPH_HISTORY - 1;
}

void drawGraphGui(void *self){
	GraphGui *graphGui = (GraphGui*)self;
	DrawRectangle(graphGui->shape.x + graphGui->margin, graphGui->shape.y + graphGui->margin, graphGui->shape.w, graphGui->shape.h,(Color){(int)(*graphGui->target*255),255,255,255});
	for(int gi = 0; gi < graphGui->history_size; gi++){
		int offset = (int)((float)graphGui->history[gi] / (float)graphGui->max);
		DrawRectangle(graphGui->shape.x + gi, graphGui->shape.y + graphGui->padding + offset, 1, 1, BLACK);
	}
	DrawText(graphGui->name, graphGui->shape.x, graphGui->shape.y, textFont.baseSize, RED);
}

ContainerGroup* createModMappingGroup(ParamList* paramList, Mod* mod, int scene, int x, int y){
	ContainerGroup* cg = createContainerGroup();
	for(int i = 0; i < paramList->count; i++){
		ModConnection* conn = paramList->params[i]->modulators;
		int connectionCount = 0;
		while(conn != NULL){
			if(conn->source == mod){
				InputContainer* ic = createInputContainer();
				ButtonGui* amountBtn = createButtonGui(x, y, 45, 30, conn->amount->name, conn->amount, incParameterBaseValue);
				add_drawable(&amountBtn->base, scene);
				ButtonGui* typeBtn = createButtonGui(x+47, y, 45, 30, conn->type->name, conn->amount, incParameterBaseValue);
				add_drawable(&typeBtn->base, scene);
				addButtonToContainer(amountBtn, ic, 0, 0);
				addButtonToContainer(typeBtn, ic, 0, 1);
				addContainerToGroup(cg, ic, 0, 0);	
			}
			conn = conn->next;
		}
	}

	return cg;
}

void drawSequencerGui(void *self) {
	SequencerGui *seqGui = (SequencerGui *)self;
	Sequencer *seq = (Sequencer*)seqGui->sequencer;
	PatternList *pl = (PatternList*)seqGui->pattern_list;
	int patternIndex = *seqGui->selected_pattern_index;
	int currentNoteIndex = *seqGui->selected_note_index;
	int currentlyPlaying = -1;

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		if(seq->pattern_index[i] == patternIndex){
			currentlyPlaying = i;
			break;
		}
	}
	for (int i = 0; i < pl->patterns[patternIndex].pattern_size; i++)
	{
		int ix = (i % seqGui->pads_per_col) ;
		int j = (int)(i / seqGui->pads_per_col);
		int *note = getStep(pl, patternIndex, i);
		

		
		if(currentNoteIndex == i){
			DrawRectangle((seqGui->shape.x + seqGui->padding * ix + (ix * (seqGui->shape.w))) - (seqGui->border_size), seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h) * j) - (seqGui->border_size), seqGui->shape.w + seqGui->border_size*2, seqGui->shape.h + seqGui->border_size*2, seqGui->outline_colour);
		}
		if(currentlyPlaying > -1 && seq->playhead_index[currentlyPlaying] == i)
		{
			DrawRectangle(seqGui->shape.x + seqGui->padding * ix + (ix * seqGui->shape.w), seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h) * j), seqGui->shape.w, seqGui->shape.h, seqGui->playing_fill_colour);
		}
		else
		{
			DrawRectangle(seqGui->shape.x + seqGui->padding * ix + (ix * seqGui->shape.w), seqGui->shape.y + seqGui->padding * j + ((seqGui->shape.h ) * j), seqGui->shape.w, seqGui->shape.h, seqGui->default_fill_colour);
		}
		Vector2 textPosition = (Vector2){(float)(seqGui->shape.x + (ix * seqGui->shape.w)  + (seqGui->padding * ix)),(float)(seqGui->shape.y  + seqGui->padding  * j + ((seqGui->shape.h) * j))};
		DrawTextEx(textFont, getNoteString(note[0], note[1]), textPosition, textFont.baseSize, 4, cs.fontColour);
	}
}

void drawSongMinimapGui(void *self){
	SongMinimapGui *smGui = (SongMinimapGui*)self;
	Arranger *arranger = (Arranger*)smGui->arranger;
	int startIndex = 0;
	//printf("coord %d,%d\n", smGui->songIndex[0], smGui->songIndex[1]);
	
	for(int i = startIndex; i < smGui->maxMapLength; i++){
		int newy = smGui->shape.y + (i * smGui->shape.w) + (smGui->padding * i);
		for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++){
			int newx = smGui->shape.x + (j * smGui->shape.h) + (smGui->padding * j);
			if(arranger->song[j][i] > -1){
				if(smGui->songIndex[0] == j && smGui->songIndex[1] == i){
					DrawRectangle(newx, newy, smGui->shape.w, smGui->shape.h, smGui->selectedCellColour);
				}else if(arranger->playhead_indices[j] == i){
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

InputsGui* createInputsGui(InputState* inputState, int x, int y){
	InputsGui* iGui = (InputsGui*)malloc(sizeof(InputsGui));
	iGui->base.draw = drawInputsGui;
	iGui->x = x;
	iGui->y = y;
	iGui->inputState = inputState;
	return iGui;
}

void drawInputsGui(void* self){
	InputsGui* iGui = (InputsGui*)self;
	InputState* is = (InputState*)iGui->inputState;
	for(int i = 0; i < KEY_MAPPING_COUNT; i++){
		if(is->keys[i].isPressed){
			DrawRectangle(iGui->x + i * 22, iGui->y, 20, 20, GREEN);
		} else {
			DrawRectangle(iGui->x + i * 22, iGui->y, 20, 20, RED);
		}
		DrawText(KEY_NAMES[i], iGui->x + i * 22, iGui->y, 10, BLACK);
	}
	for(int i = is->historyIndex; i > 0; i--){
		DrawText(KEY_NAMES[is->inputHistory[i]], SCREEN_W - 50, iGui->y - i * 11, 10, GRAY);
		
	}
}

DrawableList *create_drawable_list()
{
	DrawableList *drawableList = (DrawableList *)malloc(sizeof(DrawableList));
	drawableList->size = 0;
	drawableList->capacity = 32;
	drawableList->drawables = (Drawable **)malloc(sizeof(Drawable) * drawableList->capacity);

	return drawableList;
}

void free_drawable_list(DrawableList *list)
{
	if(!list) {
	    printf("no list\n");
		return;
	}
	free(list->drawables);
	free(list);
}

void removeDrawable(Drawable* drawable, int scene){
	DrawableList *list;
	switch(scene){
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
			return;
			break;
	}

	int addressMatch = 0;
	int matchIndex = 0;
	for (int i = 0; i < list->size; i++) {
        if (list->drawables[i] == drawable) {
            // Shift remaining elements left
            for (; i < list->size - 1; i++) {
                list->drawables[i] = list->drawables[i + 1];
            }
            list->size--;
            return;
        }
    }
	printf("drawable DONE\n");

}

void add_drawable(Drawable *drawable, int scene)
{
	DrawableList *list;
	switch(scene){
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
			return;
			break;
	}

	if (list->size == list->capacity)
	{
		list->capacity += 16;
		list->drawables = (Drawable **)realloc(list->drawables, sizeof(Drawable *) * list->capacity);
	}

	list->drawables[list->size++] = drawable;
}

void DrawGUI(int currentScene)
{
	switch(currentScene){
		case SCENE_ARRANGER:
			for (int i = 0; i < arrangerScreenDrawableList->size; i++)
			{
				arrangerScreenDrawableList->drawables[i]->draw(arrangerScreenDrawableList->drawables[i]);
			}
			break;
		case SCENE_PATTERN:
			for (int i = 0; i < patternScreenDrawableList->size; i++)
			{
				patternScreenDrawableList->drawables[i]->draw(patternScreenDrawableList->drawables[i]);
			}
			break;
		case SCENE_INSTRUMENT:
			for (int i = 0; i < instrumentScreenDrawableList->size; i++)
			{
				instrumentScreenDrawableList->drawables[i]->draw(instrumentScreenDrawableList->drawables[i]);
			}
			break;
		default:
			break;
	}
	for (int i = 0; i < globalDrawableList->size; i++)
	{
		globalDrawableList->drawables[i]->draw(globalDrawableList->drawables[i]);
	}
}

void CleanupGUI(void)
{
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