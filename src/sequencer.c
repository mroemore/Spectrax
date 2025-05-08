#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appstate.h"
#include "modsystem.h"
#include "settings.h"
#include "sequencer.h"
#include "notes.h"

PatternList *createPatternList(ApplicationState *appState) {
	// printf("creating patternList\n");

	PatternList *patternList = (PatternList *)malloc(sizeof(PatternList));
	if(!patternList) {
		printf("could not allocate memory for PtternList\n");
		return NULL;
	}
	patternList->pattern_count = 0;
	patternList->selectedPattern = -1;

	patternList->onStepChange.f = setSelectedStep;
	patternList->onStepChange.appstateRef = appState;
	patternList->onNoteSet.f = setLastUsedNote;
	patternList->onNoteSet.appstateRef = appState;
	printf("\t-> DONE.\n");
	return patternList;
}

static int intBpmToSamplesPerStep(int bpm) {
	return (PA_SR * 60) / (bpm * 4.0f);
}

static float floatBpmToSamplesPerStep(float bpm) {
	return (PA_SR * 60.0f) / (bpm * 4.0f);
}

static void applyTempoSettings(TempoSettings *ts) {
	int swingEven = getParameterValueAsInt(ts->swing);
	int swingOdd = 100 - swingEven;
	float samplesPerStepCent = floatBpmToSamplesPerStep(getParameterValue(ts->bpm)) / 50.0f;
	ts->samplesPerEvenStep = samplesPerStepCent * swingEven;
	ts->samplesPerOddStep = samplesPerStepCent * swingOdd;
}

void cb_applyBpmParam(void *tempoSettings) {
	TempoSettings *ts = (TempoSettings *)tempoSettings;
	applyTempoSettings(ts);
}

Arranger *createArranger(Settings *settings, ApplicationState *appState, ParamList *globalParamList) {
	printf("create arranger.\n");

	Arranger *arranger = (Arranger *)malloc(sizeof(Arranger));
	if(!arranger) {
		printf("could not allocate memory for arranger.\n");
		return NULL;
	}
	arranger->selected_x = 0;
	arranger->selected_y = 0;
	arranger->tempoSettings = (TempoSettings){
		.bpm = createParameterPro(globalParamList, "BPM", settings->defaultBPM, 1.0, 1000.0, 1.0, 10.0, &arranger->tempoSettings, cb_applyBpmParam),
		.loop = true,
		.swingStep = false,
		.samplesPerEvenStep = intBpmToSamplesPerStep(settings->defaultBPM),
		.samplesPerOddStep = intBpmToSamplesPerStep(settings->defaultBPM),
		.swing = createParameterPro(globalParamList, "Swing", 50.0, 1.0, 99.0, 1.0, 10.0, &arranger->tempoSettings, cb_applyBpmParam),
		.currentSamplesPerStep = intBpmToSamplesPerStep(settings->defaultBPM),
		.samplesElapsed = 0
	};
	arranger->enabledChannels = settings->enabledChannels;
	printf("set channels.\n");

	arranger->playing = 0;

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		arranger->playhead_indices[i] = 0;
		arranger->voiceTypes[i] = settings->voiceTypes[i];
	}
	printf("set playhead indices.\n");

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		for(int j = 0; j < MAX_SONG_LENGTH; j++) {
			arranger->song[i][j] = -1;
		}
	}
	printf("initialised song structure.\n");
	printf("\t-> DONE.\n");
	arranger->onCellSelect.appstateRef = appState;
	arranger->onCellSelect.f = setSelectedArrangerCell;

	arranger->onPatternSelection.appstateRef = appState;
	arranger->onPatternSelection.f = setSelectedPattern;
	return arranger;
}

// void updateBpm(Arranger *arranger, int bpm) {
// 	if(bpm > 1 && bpm < 800) {
// 		arranger->beats_per_minute = bpm;
// 		arranger->samplesPerStep = (SAMPLE_RATE * 60) / (arranger->beats_per_minute * 4);
// 	}
// }

void addChannel(Arranger *arranger, int channelIndex) {
	if(arranger->enabledChannels < MAX_SEQUENCER_CHANNELS && channelIndex >= 0) {
		if(channelIndex < arranger->enabledChannels) { // copy all subsequent channels to the right
			memmove(arranger->song[channelIndex + 1], arranger->song[channelIndex], (MAX_SEQUENCER_CHANNELS - channelIndex) * MAX_SONG_LENGTH * sizeof(int));
		}
		arranger->voiceTypes[channelIndex] = 0;
		for(int i = 0; i < MAX_SONG_LENGTH; i++) { // init with empty pattern refs
			arranger->song[channelIndex][i] = -1;
		}

		arranger->enabledChannels++;
	} else {
		printf("\t ERROR: max sequencer channels (%i) reached, can't insert at %i.\n", MAX_SEQUENCER_CHANNELS, channelIndex);
	}
}

void removeChannel(Arranger *arranger, int channelIndex) {
	// Check if the channel index is valid
	if(channelIndex < 0 || channelIndex >= arranger->enabledChannels) {
		printf("\t ERROR: invalid channel index %d (enabled channels: %d).\n", channelIndex, arranger->enabledChannels);
		return;
	}

	// Shift channels to the left if necessary
	if(channelIndex < arranger->enabledChannels - 1) {
		memmove(arranger->song[channelIndex], arranger->song[channelIndex + 1], (arranger->enabledChannels - channelIndex - 1) * MAX_SONG_LENGTH * sizeof(int));
	}
	arranger->enabledChannels--;
}

Sequencer *createSequencer(Arranger *arranger) {
	// printf("creating sequencer\n");

	Sequencer *sequencer = (Sequencer *)malloc(sizeof(Sequencer));

	for(int i = 0; i < arranger->enabledChannels; i++) {
		sequencer->playhead_index[i] = 0;
		sequencer->selected_index[i] = 0;
		sequencer->pattern_index[i] = arranger->song[i][0];
		if(sequencer->pattern_index[i] == -1) {
			sequencer->running[i] = 0;
		} else {
			sequencer->running[i] = 1;
		}
	}
	return sequencer;
}

int addBlankPattern(PatternList *patternList) {
	return addPattern(patternList, 16, (int[][NOTE_INFO_SIZE]){ { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 }, { OFF, 0 } });
}

int addPattern(PatternList *patternList, int patternSize, int notes[][NOTE_INFO_SIZE]) {
	patternList->patterns[patternList->pattern_count].pattern_size = patternSize;
	for(int i = 0; i < patternSize; i++) {
		for(int j = 0; j < NOTE_INFO_SIZE; j++) {
			patternList->patterns[patternList->pattern_count].notes[i][j] = notes[i][j];
		}
	}
	patternList->pattern_count++;
	return patternList->pattern_count - 1;
}

void addPatternToArranger(Arranger *arranger, int patternId, int sequencer_id, int row) {
	printf("addpattern\n");

	arranger->song[sequencer_id][row] = patternId;
}

void addBlankIfEmpty(PatternList *patternList, Arranger *arranger, int sequencerId, int row) {
	printf("addblank\n");
	int patternID = addBlankPattern(patternList);
	if(arranger->song[sequencerId][row] == -1) {
		addPatternToArranger(arranger, patternID, sequencerId, row);
	}
}

int *getStep(PatternList *patternList, int patternIndex, int noteIndex) {
	// printf("getstep\n");
	return patternList->patterns[patternIndex].notes[noteIndex];
}

int *getCurrentStep(PatternList *patternList, int patternIndex, int noteIndex) {
	return patternList->patterns[patternIndex].notes[noteIndex];
}

int findArrangerLoopIndex(Arranger *arranger, int sequencerId, int currentY) {
	int loopIndex = currentY;
	for(int i = currentY; i > 0; i--) {
		if(arranger->song[sequencerId][i - 1] == -1) {
			break;
		}
		loopIndex = i - 1;
	}
	return loopIndex;
}

bool selectArrangerCell(Arranger *arranger, int checkBlankPattern, int relativex, int relativey) {
	printf("selectcell\n");
	int newx, newy;
	bool navSuccess = false;
	newx = arranger->selected_x + relativex;
	newy = arranger->selected_y + relativey;
	if(newx > arranger->enabledChannels - 1) {
		newx = arranger->enabledChannels - 1;
	}
	if(newx < 0) {
		newx = 0;
	}
	if(newy > MAX_SONG_LENGTH - 1) {
		newy = MAX_SONG_LENGTH - 1;
	}
	if(newy < 0) {
		newy = 0;
	}
	if(arranger->song[newx][newy] != -1 || !checkBlankPattern) {
		if(arranger->selected_x != newx || arranger->selected_y != newy) {
			navSuccess = true;
		}
		arranger->selected_x = newx;
		arranger->selected_y = newy;
	}
	int selectedArrangerCell[2];
	selectedArrangerCell[0] = arranger->selected_x;
	selectedArrangerCell[1] = arranger->selected_y;
	int patternIndex = arranger->song[arranger->selected_x][arranger->selected_y];
	arranger->onCellSelect.f(arranger->onCellSelect.appstateRef, selectedArrangerCell);
	arranger->onPatternSelection.f(arranger->onPatternSelection.appstateRef, &patternIndex);
	// printf("SELECTED: %i,%i\n", selectedArrangerCell[0], selectedArrangerCell[1]);
	return navSuccess;
}

int getPatternIDfromArranger(Arranger *a) {
	return a->song[a->selected_x][a->selected_y];
}

int selectStep(PatternList *patternList, int patternIndex, int selectedStep) {
	int sequenceLength = patternList->patterns[patternIndex].pattern_size;
	if(selectedStep >= sequenceLength) {
		selectedStep = sequenceLength - 1;
	}
	if(selectedStep < 0) {
		selectedStep = 0;
	}
	patternList->onStepChange.f(patternList->onStepChange.appstateRef, &selectedStep);
	return selectedStep;
}

bool currentNoteIsBlank(PatternList *patternList, int patternIndex, int noteIndex) {
	return OFF == patternList->patterns[patternIndex].notes[noteIndex][0];
}

void setCurrentNote(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]) {
	patternList->patterns[patternIndex].notes[noteIndex][0] = note[0];
	patternList->patterns[patternIndex].notes[noteIndex][1] = note[1];
	patternList->onNoteSet.f(patternList->onNoteSet.appstateRef, &note);
}

void editCurrentNote(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]) {
	printf("editing...");
	if(patternList->patterns[patternIndex].notes[noteIndex][0] == OFF) {
		note[0] = C;
		note[1] = 3;
	}
	setCurrentNote(patternList, patternIndex, noteIndex, note);
}

void editCurrentNoteRelative(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]) {
	int newNote[NOTE_INFO_SIZE] = {
		patternList->patterns[patternIndex].notes[noteIndex][0] + note[0],
		patternList->patterns[patternIndex].notes[noteIndex][1] + note[1]
	};

	if(newNote[0] < 0) {
		newNote[0] = B;
		newNote[1]--;
	}
	if(newNote[0] >= NOTE_COUNT) {
		newNote[0] = C;
		newNote[1]++;
	}

	if(newNote[1] > MAX_OCTAVES - 1) {
		newNote[1] = MAX_OCTAVES - 1;
	}
	if(newNote[1] < 0) {
		newNote[1] = 0;
	}

	setCurrentNote(patternList, patternIndex, noteIndex, newNote);

	// patternList->patterns[patternIndex].notes[noteIndex][0] = newNote[0];
	// patternList->patterns[patternIndex].notes[noteIndex][1] = newNote[1];
}

void incrementSequencer(Sequencer *sequencer, PatternList *patternList, Arranger *arranger) { // TO-DO: add pattern mode func
	arranger->tempoSettings.swingStep = !arranger->tempoSettings.swingStep;
	for(int i = 0; i < arranger->enabledChannels; i++) {
		int patternSize = patternList->patterns[sequencer->pattern_index[i]].pattern_size;
		if(sequencer->playhead_index[i] + 1 > patternSize - 1) {
			// printf("\n\tEOP. ");
			int nextRowIndex = arranger->playhead_indices[i] + 1;
			if(arranger->song[i][nextRowIndex] > -1) {
				// printf("\n\t\tPattern Switch. \n");
				arranger->playhead_indices[i]++;
				sequencer->pattern_index[i] = arranger->song[i][nextRowIndex];
			} else {
				if(arranger->tempoSettings.loop) {
					// printf("\n\t\tLoop. \n");
					int loopIndex = findArrangerLoopIndex(arranger, i, nextRowIndex);
					arranger->playhead_indices[i] = loopIndex;
					sequencer->pattern_index[i] = arranger->song[i][loopIndex];
				} else {
					// printf("\n\t\tEnd. \n");
					sequencer->running[i] = 0;
				}
			}
		}
		if(sequencer->pattern_index[i] > -1) {
			sequencer->playhead_index[i] = (sequencer->playhead_index[i] + 1) % patternList->patterns[sequencer->pattern_index[i]].pattern_size;
		}
	}
}

void editStep(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]) { // TO-DO: error checking
	if(noteIndex >= 0 && noteIndex < patternList->patterns[patternIndex].pattern_size) {
		for(int j = 0; j < NOTE_INFO_SIZE; j++) {
			patternList->patterns[patternIndex].notes[patternIndex][j] = note[j];
		}
	}
}

void stopPlaying(Arranger *arranger) {
	arranger->playing = 0;
}

void startPlaying(Sequencer *sequencer, PatternList *patternList, Arranger *arranger, int playMode) {
	arranger->playing = 1;
	int playRow = arranger->selected_y;
	for(int i = 0; i < arranger->enabledChannels; i++) {
		arranger->playhead_indices[i] = playRow;
		sequencer->playhead_index[i] = 0;
		sequencer->pattern_index[i] = arranger->song[i][playRow];
		if(sequencer->pattern_index[i] == -1) {
			sequencer->running[i] = 0;
		} else {
			sequencer->running[i] = 1;
		}
	}
}
