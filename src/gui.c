#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"
#include "gui.h"
#include "input.h"
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

	SetTargetFPS(60);
}

TransportGui *createTransportGui(int *playing, Arranger *arranger, int x, int y){
	TransportGui *tsGui = (TransportGui*)malloc(sizeof(TransportGui));
	tsGui->base.draw = drawTransportGui;
	tsGui->x = x;
	tsGui->y = y;
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
	seqGui->x = x;
	seqGui->y = y;
	seqGui->pad_w = 50;
	seqGui->pad_h = 50;
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
	graphGui->x = x;
	graphGui->y = y;
	graphGui->w = size;
	graphGui->h = 20;
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
	arrangerGui->x = x;
	arrangerGui->y = y;
	arrangerGui->w = 20;
	arrangerGui->h = 20;
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
	minimapGui->x = x;
	minimapGui->y = y;
	minimapGui->w = 4;
	minimapGui->h = 4;
	minimapGui->padding = 1;
	minimapGui->maxMapLength = (SCREEN_H - 150)/(minimapGui->h + minimapGui->padding);
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
	btnCont->selectedRow = 0;
	btnCont->selectedColumn = 0;
	return btnCont;
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
			ic->selectedColumn = ic->columnCount[cg->selectedRow]-1;
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

void addButtonToContainer(ButtonGui* btnGui, InputContainer* btnCont, int row, int col){
	if(row < 0 || col < 0 || row > MAX_BUTTON_CONTAINER_ROWS - 1 || col > MAX_BUTTON_CONTAINER_COLS - 1) {
		printf("attempting out-of-bounds btn container insertion.\n");
		return;
	}
	int insertRow = 0, insertCol = 0;
	printf("CONTAINER:count before add:%i,%i adding ", btnCont->rowCount, btnCont->columnCount[row]);
	
	if(row >= btnCont->rowCount){
		insertRow = btnCont->rowCount;
		btnCont->rowCount++;
		printf(" [ir, ");
	} else {
		insertRow = row;
		printf(" [nir, ");
	}

	if(col >= btnCont->columnCount[insertRow]){
		insertCol = btnCont->columnCount[insertRow];
		btnCont->columnCount[insertRow]++;
		printf("ic] ");
		
	} else {
		printf("nic] ");

		insertCol = col;
	}
	printf(btnGui->buttonText);
	printf(" at %i, %i\n", insertRow, insertCol);
	btnCont->buttonRefs[insertRow][insertCol] = btnGui;
}

ButtonGui* createButtonGui(int x, int y, int w, int h, char* buttonText, Parameter* param, void* callback){
	ButtonGui* btnGui = (ButtonGui*)malloc(sizeof(ButtonGui));
	btnGui->base.draw = drawButtonGui;
	btnGui->base.onPress = callback;
	btnGui->x = x;
	btnGui->y = y;
	btnGui->w = w;
	btnGui->h = h;
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
		DrawRectangle(btnGui->x-2, btnGui->y-2, btnGui->w+4, btnGui->h+4, btnGui->selectedColour);
	}
	DrawRectangle(btnGui->x, btnGui->y, btnGui->w, btnGui->h, btnGui->backgroundColour);
	DrawText(btnGui->buttonText, btnGui->x, btnGui->y + btnGui->h/2, 10, btnGui->textColour);
	char valueStr[32];
    snprintf(valueStr, sizeof(valueStr), "%.2f", btnGui->parameter->baseValue);
    DrawText(valueStr, btnGui->x + MeasureText(btnGui->buttonText, 10) + 5, 
             btnGui->y + btnGui->h/2, 12, btnGui->textColour);
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
	envGui->x = x;
	envGui->y = y;
	envGui->w = w;
	envGui->h = h;
	envGui->graphData = malloc(sizeof(int) * w);
}

void drawEnvelopeGui(void* self){
	EnvelopeGui *eg = (EnvelopeGui*)self;
	int stageIndex = 0;
	float totalDuration = 0.0f;
	float currentTime = 0.0f;
	float currentLevel = 0.0f;
	float previousLevel = 0.0f;
	DrawRectangle(eg->x, eg->y, eg->w, eg->h, BLACK);
	for(int s = 0; s < eg->env->stageCount; s++){
		totalDuration += eg->env->stages[s].duration->currentValue;
	}
	float basicIncrement = totalDuration / eg->w; 
	for(int w = 0; w < eg->w; w++){
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
		DrawLineEx((Vector2){eg->x + w, eg->y + eg->h - (previousLevel * eg->h)},(Vector2){ eg->x + w + 1, eg->y + eg->h - (currentLevel * eg->h)}, 2.0f, RED);
		//DrawRectangle(eg->x + w, eg->y + eg->h - (currentLevel * eg->h), 2, 2, RED);

		currentTime += basicIncrement;
		if (currentTime >= 1.0f) {
        	currentTime = 0.0f;
			
			if (stageIndex < eg->env->stageCount - 1) {
				DrawLine(eg->x + w, eg->y, eg->x + w, eg->y + eg->h, GREEN);
				stageIndex++;
			}
		}
		if(eg->env->isTriggered){
			int currentInc = ( eg->w * (eg->env->totalElapsedTime / totalDuration));
			if((int)(currentInc) == w){
				DrawLine(eg->x + w, eg->y, eg->x + w, eg->y + eg->h, PURPLE);
			}

		}
		previousLevel = currentLevel;
	}
}

void drawTransportGui(void *self){
	Vector2 pos = (Vector2){600,10};
	TransportGui *tg =(TransportGui*)self;
	drawSprite(tg->icons, 0, tg->x, tg->y);
}

void drawArrangerGui(void *self){
	ArrangerGui *aGui = (ArrangerGui*)self;
	Arranger *arranger = (Arranger*)aGui->arranger;
	char *cellText = malloc(sizeof(char) *4);
	int cursorx, cursory;
	cursorx = aGui->x + arranger->selected_x * (aGui->w + aGui->grid_padding);
	cursory = aGui->y + arranger->selected_y * (aGui->h + aGui->grid_padding);

	DrawRectangle(cursorx - aGui->border_size, cursory - aGui->border_size, aGui->w + (aGui->border_size * 2),aGui->h + (aGui->border_size * 2), cs.outlineColour);
	for(int i = 0; i < MAX_SONG_LENGTH; i++){
		int px = i % MAX_SEQUENCER_CHANNELS;
		int newy = aGui->x + (i * aGui->w) + (aGui->grid_padding * i);
		for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++){
			int newx = aGui->y + (j * aGui->h) + (aGui->grid_padding * j);
			if(arranger->song[j][i] > -1){
				sprintf(cellText, "%i\0", arranger->song[j][i]);
				if(arranger->playhead_indices[j] == px){
					DrawRectangle(newx, newy, aGui->w, aGui->h, (Color){255,0,0,255});
				} else {
					DrawRectangle(newx, newy, aGui->w, aGui->h, cs.defaultCell);
				}
				DrawText(cellText, newx - 5 + aGui->w / 2, newy - 5 + aGui->h / 2, textFont.baseSize, cs.secondaryFontColour);
			} else {
				DrawRectangle(newx, newy, aGui->w, aGui->h, cs.blankCell);
				DrawText("--", newx - 5 + aGui->w / 2, newy - 5 + aGui->h / 2, textFont.baseSize, cs.secondaryFontColour);
			}
		}
	}
}

OscilloscopeGui* createOscilloscopeGui(int x, int y, int w, int h){
	OscilloscopeGui* og = (OscilloscopeGui*)malloc(sizeof(OscilloscopeGui));
	og->base.draw = drawOscilloscopeGui;
	og->x = x;
	og->y = y;
	og->w = w < OSCILLOSCOPE_HISTORY ? w : OSCILLOSCOPE_HISTORY;
	og->h = h;
	og->updateIndex = 0;
	og->backgroundColour = &cs.highlightedCell;
	og->waveformColour = &cs.backgroundColor;
	og->lineColour = &cs.fontColour;
	return og;
}

void drawOscilloscopeGui(void* self){
	OscilloscopeGui* og = (OscilloscopeGui*)self;

	// Draw background
	DrawRectangle(og->x, og->y, og->w, og->h, (Color){255,0,0,255});
	//draw center line
	DrawLine(og->x, og->y + og->h/2, og->x + og->w, og->y + og->h/2, (Color){255,255,0,255});
	for(int i = 0; i < og->w-1; i++){
		DrawLine(og->x + i, (og->y + og->h/2) + og->data[i], og->x+i+1, (og->y + og->h/2) + og->data[i+1], (Color){0,0,255,255});
	}
}

void updateOscilloscopeGui(OscilloscopeGui* og, float* data, int length){
	for(int i = 0; i < length; i++){
		og->data[og->updateIndex] = data[i];
		og->updateIndex++;
		og->updateIndex %= og->w;
	}
}


void updateGraphGui(GraphGui* graphGui){
	graphGui->history[graphGui->index] = (int)(*graphGui->target * (graphGui->h - (graphGui->padding*2))); // Cast float to int
	graphGui->index++;
	graphGui->index %= MAX_GRAPH_HISTORY - 1;
}

void drawGraphGui(void *self){
	GraphGui *graphGui = (GraphGui*)self;
	DrawRectangle(graphGui->x + graphGui->margin, graphGui->y + graphGui->margin, graphGui->w, graphGui->h,(Color){(int)(*graphGui->target*255),255,255,255});
	for(int gi = 0; gi < graphGui->history_size; gi++){
		int offset = (int)((float)graphGui->history[gi] / (float)graphGui->max);
		DrawRectangle(graphGui->x + gi, graphGui->y + graphGui->padding + offset, 1, 1, BLACK);
	}
	DrawText(graphGui->name, graphGui->x, graphGui->y, textFont.baseSize, RED);
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
			DrawRectangle((seqGui->x + seqGui->padding * ix + (ix * (seqGui->pad_w))) - (seqGui->border_size), seqGui->y + seqGui->padding * j + ((seqGui->pad_h) * j) - (seqGui->border_size), seqGui->pad_w + seqGui->border_size*2, seqGui->pad_h + seqGui->border_size*2, seqGui->outline_colour);
		}
		if(currentlyPlaying > -1 && seq->playhead_index[currentlyPlaying] == i)
		{
			DrawRectangle(seqGui->x + seqGui->padding * ix + (ix * seqGui->pad_w), seqGui->y + seqGui->padding * j + ((seqGui->pad_h) * j), seqGui->pad_w, seqGui->pad_h, seqGui->playing_fill_colour);
		}
		else
		{
			DrawRectangle(seqGui->x + seqGui->padding * ix + (ix * seqGui->pad_w), seqGui->y + seqGui->padding * j + ((seqGui->pad_h ) * j), seqGui->pad_w, seqGui->pad_h, seqGui->default_fill_colour);
		}
		Vector2 textPosition = (Vector2){(float)(seqGui->x + (ix * seqGui->pad_w)  + (seqGui->padding * ix)),(float)(seqGui->y  + seqGui->padding  * j + ((seqGui->pad_h) * j))};
		DrawTextEx(textFont, getNoteString(note[0], note[1]), textPosition, textFont.baseSize, 4, cs.fontColour);
	}
}

void drawSongMinimapGui(void *self){
	SongMinimapGui *smGui = (SongMinimapGui*)self;
	Arranger *arranger = (Arranger*)smGui->arranger;
	int startIndex = 0;
	//printf("coord %d,%d\n", smGui->songIndex[0], smGui->songIndex[1]);
	
	for(int i = startIndex; i < smGui->maxMapLength; i++){
		int newy = smGui->y + (i * smGui->w) + (smGui->padding * i);
		for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++){
			int newx = smGui->x + (j * smGui->h) + (smGui->padding * j);
			if(arranger->song[j][i] > -1){
				if(smGui->songIndex[0] == j && smGui->songIndex[1] == i){
					DrawRectangle(newx, newy, smGui->w, smGui->h, smGui->selectedCellColour);
				}else if(arranger->playhead_indices[j] == i){
					DrawRectangle(newx, newy, smGui->w, smGui->h, smGui->playingCellColour);
				} else {
					DrawRectangle(newx, newy, smGui->w, smGui->h, smGui->defaultCellColour);
				}
			} else {
				DrawRectangle(newx, newy, smGui->w, smGui->h, smGui->blankCellColour);
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