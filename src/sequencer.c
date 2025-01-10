#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "sequencer.h"
#include "notes.h"

PatternList* createPatternList(){
	PatternList * patternList = (PatternList*)malloc(sizeof(PatternList));
	patternList->pattern_count = 0;
	return patternList;
}

Arranger* createArranger(Settings* settings){
	Arranger *arranger = (Arranger*)malloc(sizeof(Arranger));
	arranger->selected_x = 0;
	arranger->selected_y = 0;
	arranger->loop = 1;
	arranger->enabledChannels = settings->enabledChannels;
	arranger->beats_per_minute = 120;
	arranger->playing = 0;

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		arranger->playhead_indices[i] = 0;
		arranger->voiceTypes[i] = settings->voiceTypes[i];
	}

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		for(int j = 0; j < MAX_SONG_LENGTH; j++){
			arranger->song[i][j] = -1;
		}
	}

	return arranger;
}

void addChannel(Arranger* arranger, int channelIndex){
	if(arranger->enabledChannels < MAX_SEQUENCER_CHANNELS && channelIndex >= 0){
		if(channelIndex  < arranger->enabledChannels){ //copy all subsequent channels to the right
			memmove(arranger->song[channelIndex + 1], arranger->song[channelIndex], (MAX_SEQUENCER_CHANNELS - channelIndex) * MAX_SONG_LENGTH * sizeof(int));
		}
		arranger->voiceTypes[channelIndex] = 0;
		for(int i = 0; i < MAX_SONG_LENGTH; i++){ //init with empty pattern refs
			arranger->song[channelIndex][i] = -1;
		}

		arranger->enabledChannels++;
	} else {
		printf("\t ERROR: max sequencer channels (%i) reached, can't insert at %i.\n", MAX_SEQUENCER_CHANNELS, channelIndex);
	}
}

void removeChannel(Arranger* arranger, int channelIndex) {
    // Check if the channel index is valid
    if (channelIndex < 0 || channelIndex >= arranger->enabledChannels) {
        printf("\t ERROR: invalid channel index %d (enabled channels: %d).\n", channelIndex, arranger->enabledChannels);
        return;
    }

    // Shift channels to the left if necessary
    if (channelIndex < arranger->enabledChannels - 1) {
        memmove(arranger->song[channelIndex], arranger->song[channelIndex + 1], (arranger->enabledChannels - channelIndex - 1) * MAX_SONG_LENGTH * sizeof(int));
    }
    arranger->enabledChannels--;
}

Sequencer* createSequencer(Arranger *arranger){
    Sequencer* sequencer = (Sequencer*)malloc(sizeof(Sequencer));
	
	for(int i = 0; i < arranger->enabledChannels; i++){
		sequencer->playhead_index[i] = 0;
		sequencer->selected_index[i] = 0;
		sequencer->pattern_index[i] = arranger->song[i][0];
		if(sequencer->pattern_index[i] == -1){
			sequencer->running[i] = 0;
		} else {
			sequencer->running[i] = 1;
		}
	}
	return sequencer;
}

int addBlankPattern(PatternList * patternList){
	return addPattern(patternList, 16, (int[][NOTE_INFO_SIZE])
	{
		{OFF, 0}, {OFF, 0}, {OFF, 0}, {OFF, 0},
		{OFF, 0}, {OFF, 0}, {OFF, 0}, {OFF, 0},
		{OFF, 0}, {OFF, 0}, {OFF, 0}, {OFF, 0},
		{OFF, 0}, {OFF, 0}, {OFF, 0}, {OFF, 0}
	});
}

int addPattern(PatternList * patternList, int patternSize, int notes[][NOTE_INFO_SIZE]){
	patternList->patterns[patternList->pattern_count].pattern_size = patternSize;
	for(int i = 0; i < patternSize; i++){
		for(int j = 0; j < NOTE_INFO_SIZE; j++){
            patternList->patterns[patternList->pattern_count].notes[i][j] = notes[i][j];
		}
	}
	patternList->pattern_count++;
	return patternList->pattern_count - 1;
}

void addPatternToArranger(Arranger* arranger, int patternId, int sequencer_id, int row){
	arranger->song[sequencer_id][row] = patternId;
}

void addBlankIfEmpty(PatternList* patternList, Arranger* arranger, int sequencerId, int row){
	int patternID = addBlankPattern(patternList);
	if(arranger->song[sequencerId][row] == -1){
		addPatternToArranger(arranger, patternID, sequencerId, row);
	}
}

int *getStep(PatternList *patternList, int patternIndex, int noteIndex){
	return patternList->patterns[patternIndex].notes[noteIndex];
}

int* getCurrentStep(PatternList *patternList, int patternIndex, int noteIndex){
	return patternList->patterns[patternIndex].notes[noteIndex];
}

int* selectArrangerCell(Arranger* arranger, int checkBlankPattern, int relativex, int relativey, int *selectedArrangerCell){
	int newx, newy;
	newx = arranger->selected_x + relativex;
	newy = arranger->selected_y + relativey;
	if(newx > arranger->enabledChannels - 1){
		newx = arranger->enabledChannels - 1;
	}
	if(newx < 0){
		newx = 0;
	}
	if(newy > MAX_SONG_LENGTH - 1){
		newy = MAX_SONG_LENGTH - 1;
	}
	if(newy < 0){
		newy = 0;
	}
	if(arranger->song[newx][newy] != -1 || !checkBlankPattern){
		arranger->selected_x = newx;
		arranger->selected_y = newy;
	}
	selectedArrangerCell[0] = arranger->selected_x;
	selectedArrangerCell[1] = arranger->selected_y;
	//printf("SELECTED: %i,%i\n", selectedArrangerCell[0], selectedArrangerCell[1]);
	return selectedArrangerCell;
}

int selectStep(PatternList *patternList, int patternIndex, int selectedStep){
	int sequenceLength = patternList->patterns[patternIndex].pattern_size;
	if(selectedStep >= sequenceLength){
		selectedStep = sequenceLength - 1;
	}
	if(selectedStep < 0){
		selectedStep = 0;
	}

	return selectedStep;
}

int* editCurrentNote(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]){
	if(patternList->patterns[patternIndex].notes[noteIndex][0] == OFF){
		patternList->patterns[patternIndex].notes[noteIndex][0] = C;
		patternList->patterns[patternIndex].notes[noteIndex][1] = 3;	
	} else {
		patternList->patterns[patternIndex].notes[noteIndex][0] = note[0];
		patternList->patterns[patternIndex].notes[noteIndex][1] = note[1];
	}
}

int* editCurrentNoteRelative(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]){
	
	int newNote[NOTE_INFO_SIZE] = 
	{
		patternList->patterns[patternIndex].notes[noteIndex][0] + note[0], 
		patternList->patterns[patternIndex].notes[noteIndex][1] + note[1]
	};

	if(newNote[0] < 0){
		newNote[0] = B;
		newNote[1]--;
	}
	if(newNote[0] >= NOTE_COUNT){
		newNote[0] = C;
		newNote[1]++;
	}

	if(newNote[1] > MAX_OCTAVES - 1){
		newNote[1] = MAX_OCTAVES - 1;
	}
	if(newNote[1] < 0){
		newNote[1] = 0;
	}

	patternList->patterns[patternIndex].notes[noteIndex][0] = newNote[0];
	patternList->patterns[patternIndex].notes[noteIndex][1] = newNote[1];
}

void incrementSequencer(Sequencer* sequencer, PatternList *patternList, Arranger *arranger){ //TO-DO: add pattern mode func
	for(int i = 0; i < arranger->enabledChannels; i++){
		int patternSize = patternList->patterns[sequencer->pattern_index[i]].pattern_size;
		if(sequencer->playhead_index[i] + 1 > patternSize - 1){
			//printf("\n\tEOP. ");
			int nextRowIndex = arranger->playhead_indices[i] + 1;
			if(arranger->song[i][nextRowIndex] > -1){
				//printf("\n\t\tPattern Switch. \n");
				arranger->playhead_indices[i]++;
				sequencer->pattern_index[i] = arranger->song[i][nextRowIndex];
			} else {
				if(arranger->loop){
					//printf("\n\t\tLoop. \n");
					arranger->playhead_indices[i] = 0;
				} else {
					//printf("\n\t\tEnd. \n");
					sequencer->running[i] = 0;
				}
			}
		}
		if(sequencer->pattern_index[i] > -1){
			sequencer->playhead_index[i] = (sequencer->playhead_index[i] + 1) % patternList->patterns[sequencer->pattern_index[i]].pattern_size;
		}
	}
	
}

void editStep(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]){ //TO-DO: error checking
	if (noteIndex >= 0 && noteIndex < patternList->patterns[patternIndex].pattern_size) {
        for (int j = 0; j < NOTE_INFO_SIZE; j++) {
            patternList->patterns[patternIndex].notes[patternIndex][j] = note[j];
        }
    }
}

void stopPlaying(Arranger *arranger){
	arranger->playing = 0;
}

void startPlaying(Sequencer* sequencer, PatternList *patternList, Arranger *arranger, int playMode){
	arranger->playing = 1;
	int playRow = arranger->selected_x;
	for(int i = 0; i < arranger->enabledChannels; i++){
		arranger->playhead_indices[i] = playRow;
		sequencer->playhead_index[i] = 0;
		sequencer->pattern_index[i] = arranger->song[i][playRow];
		if(sequencer->pattern_index[i] == -1){
			sequencer->running[i] = 0;
		} else {
			sequencer->running[i] = 1;
		}
	}
}