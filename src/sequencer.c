#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appstate.h"
#include "modsystem.h"
#include "settings.h"
#include "sequencer.h"
#include "notes.h"
#include "voice.h"

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

Arranger *createArranger(Settings *settings, VoiceManager *vm, ApplicationState *appState, ParamList *globalParamList) {
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
	arranger->vm = vm;
	arranger->enabledChannels = settings->enabledChannels;
	printf("set channels.\n");

	arranger->playing = 0;

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		/* Playhead rows are 0-based song indices; -1 means "this channel has
		 * no playhead". Anything outside that range would be used to index
		 * song[][], so keep the loop honest (TESTING.md I32). */
		arranger->playhead_indices[i] = -1;
		arranger->channelSlots[i] = 0;
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

	if(!arranger) {
		return NULL;
	}
	/* Zeroed, not malloc'd: every array here is MAX_SEQUENCER_CHANNELS long
	 * but the loop below only initialises enabledChannels entries, so the
	 * remainder would otherwise be garbage that a later loop bounded by a
	 * different count reads. */
	Sequencer *sequencer = (Sequencer *)calloc(1, sizeof(Sequencer));
	if(!sequencer) {
		return NULL;
	}

	/* Bound by the array, not just by enabledChannels: that field arrives
	 * from a project file (I32), and this loop writes playhead_index[i] for
	 * each i — an oversized count is an out-of-bounds write. The loader
	 * clamps it, and this clamp is the second lock on the same door. */
	int channels = arranger->enabledChannels;
	if(channels < 0) {
		channels = 0;
	}
	if(channels > MAX_SEQUENCER_CHANNELS) {
		channels = MAX_SEQUENCER_CHANNELS;
	}

	for(int i = 0; i < channels; i++) {
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
	if(!patternList || !notes) {
		return -1;
	}
	/* Pattern.notes is a fixed MAX_SEQUENCE_LENGTH x NOTE_INFO_SIZE array and
	 * every consumer treats pattern_size as a step count into fixed-size
	 * arrays, so the size has to be bounded here — this is the chokepoint
	 * every pattern passes through. An unbounded size is an out-of-bounds
	 * write in the copy loop below and an out-of-bounds read in every
	 * consumer (gui_pattern.c's stepData[]/stepNodes[], selectStep's clamp,
	 * the sequencer's playhead modulo). Clamp rather than reject so callers
	 * still get a usable index back. */
	if(patternSize < 0) {
		patternSize = 0;
	}
	if(patternSize > MAX_SEQUENCE_LENGTH) {
		patternSize = MAX_SEQUENCE_LENGTH;
	}
	if(patternList->pattern_count >= MAX_PATTERNS) {
		return -1;
	}
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
	if(arranger->song[sequencerId][row] == -1) {
		printf("addblank\n");
		int patternID = addBlankPattern(patternList);
		addPatternToArranger(arranger, patternID, sequencerId, row);
	}
}

int *getStep(PatternList *patternList, int patternIndex, int noteIndex) {
	// printf("getstep\n");
	if(!patternList || patternIndex < 0 || patternIndex >= MAX_PATTERNS
	   || noteIndex < 0 || noteIndex >= MAX_SEQUENCE_LENGTH) {
		return NULL;
	}
	return patternList->patterns[patternIndex].notes[noteIndex];
}

int *getCurrentStep(PatternList *patternList, int patternIndex, int noteIndex) {
	/* patternIndex comes from the arranger, where -1 means "no pattern in
	 * this cell"; enforce the array bounds rather than indexing patterns[-1]
	 * (caught by UBSan). Callers must handle NULL. */
	if(!patternList || patternIndex < 0 || patternIndex >= MAX_PATTERNS
	   || noteIndex < 0 || noteIndex >= MAX_SEQUENCE_LENGTH) {
		return NULL;
	}
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
	patternList->onNoteSet.f(patternList->onNoteSet.appstateRef, note);
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

void advanceSequencerStep(Sequencer *sequencer, PatternList *patternList, Arranger *arranger, StepTriggerFn trigger, void *ctx) {
	/* Trigger-before-advance: fire each running channel's note at its
	 * CURRENT playhead first, then advance. The previous model advanced
	 * first (incrementSequencer then read), which skipped step 0 of the
	 * first pattern on play — the first note heard was step 1 — and the
	 * pattern's material played back-to-back ~1.5x before the loop/next
	 * row picked up step 0 (the "double first pattern"). Triggering first
	 * also lets a pattern's last note sound before an end-of-song stop. */
	for(int c = 0; c < arranger->enabledChannels; c++) {
		if(sequencer->running[c] && trigger) {
			int *note = getCurrentStep(patternList, sequencer->pattern_index[c], sequencer->playhead_index[c]);
			if(note != NULL && note[0] != OFF) {
				trigger(ctx, c, note);
			}
		}
	}
	incrementSequencer(sequencer, patternList, arranger);
}

void incrementSequencer(Sequencer *sequencer, PatternList *patternList, Arranger *arranger) { // TO-DO: add pattern mode func
	arranger->tempoSettings.swingStep = !arranger->tempoSettings.swingStep;
	for(int i = 0; i < arranger->enabledChannels; i++) {
		int patternIndex = sequencer->pattern_index[i];
		int patternSize = 0;
		if(patternIndex >= 0 && patternIndex < patternList->pattern_count) {
			patternSize = patternList->patterns[patternIndex].pattern_size;
		}
		if(patternSize <= 0) {
			continue;
		}
		if(sequencer->playhead_index[i] + 1 > patternSize - 1) {
			// printf("\n\tEOP. ");
			/* playhead_indices[i] arrives from a project file, so bound it
			 * here before it is used to index song[][]. The loader clamps it
			 * (I32); this is the second lock on the same door — a row count
			 * one past the end must not wrap into row 0 silently either, or
			 * playback jumps to the top of the song at the loop point. */
			int currentRow = arranger->playhead_indices[i];
			if(currentRow < 0) {
				currentRow = 0;
			}
			if(currentRow >= MAX_SONG_LENGTH - 1) {
				currentRow = MAX_SONG_LENGTH - 2;
			}
			int nextRowIndex = currentRow + 1;
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
		if(sequencer->pattern_index[i] > -1 && sequencer->running[i]) {
			/* pattern_index[i] was just taken from arranger->song[i][row], i.e.
			 * straight from a project file, so it can name a pattern that does
			 * not exist. And pattern_size can be 0, which makes the modulo a
			 * division by zero — integer division by zero is undefined and traps
			 * as SIGFPE on x86. Both were found by fuzz/fuzz_seqfile.c (I32). */
			int pi = sequencer->pattern_index[i];
			if(pi >= patternList->pattern_count) {
				sequencer->pattern_index[i] = -1;
				sequencer->running[i] = 0;
				continue;
			}
			int size = patternList->patterns[pi].pattern_size;
			if(size <= 0) {
				sequencer->pattern_index[i] = -1;
				sequencer->running[i] = 0;
				continue;
			}
			sequencer->playhead_index[i] = (sequencer->playhead_index[i] + 1) % size;
		}
	}
}

void editStep(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]) { // TO-DO: error checking
	if(!patternList || !note || patternIndex < 0 || patternIndex >= patternList->pattern_count) {
		return;
	}
	/* Both indices need their own bound: the guard compares noteIndex against
	 * pattern_size, which bounds noteIndex, but the write was indexing
	 * notes[patternIndex] — a different index, unbounded by anything here and
	 * up to MAX_PATTERNS-1 against a notes array of MAX_SEQUENCE_LENGTH rows.
	 * Editing step 2 of pattern 200 wrote outside the pattern's note array.
	 * (Found while fuzzing the project loader; the index confusion was not.)
	 */
	if(noteIndex >= 0 && noteIndex < patternList->patterns[patternIndex].pattern_size &&
	   noteIndex < MAX_SEQUENCE_LENGTH) {
		for(int j = 0; j < NOTE_INFO_SIZE; j++) {
			patternList->patterns[patternIndex].notes[noteIndex][j] = note[j];
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

/* Copy/cut/paste clipboard for the arranger's pattern cells and the
 * pattern screen's note steps.
 *
 * g_clipPattern: the arranger clipboard. Copy = remember the cell's
 * pattern index (the pattern itself stays in the patternList); Cut =
 * remember + clear the cell's reference; Paste = assign the index to a
 * (blank) cell. g_clipNote: the pattern-screen clipboard. Cut = remember
 * the step's note + turn it off; Paste = place it on a (blank) step. */
static int g_clipPattern = -1;
static int g_clipNote[NOTE_INFO_SIZE] = { OFF, 0 };

int clipPattern(void) {
	return g_clipPattern;
}

int copyPatternFromCell(Arranger *arranger, int x, int y) {
	if(!arranger || x < 0 || x >= arranger->enabledChannels || y < 0 || y >= MAX_SONG_LENGTH) {
		return -1;
	}
	int pid = arranger->song[x][y];
	if(pid >= 0) {
		g_clipPattern = pid;
	}
	return pid;
}

int cutPatternFromCell(Arranger *arranger, int x, int y) {
	int pid = copyPatternFromCell(arranger, x, y);
	if(pid >= 0) {
		arranger->song[x][y] = -1;
	}
	return pid;
}

int pastePatternToCell(Arranger *arranger, int x, int y) {
	if(!arranger || x < 0 || x >= arranger->enabledChannels || y < 0 || y >= MAX_SONG_LENGTH) {
		return -1;
	}
	if(g_clipPattern < 0 || arranger->song[x][y] >= 0) {
		return -1; /* nothing to paste, or the target already has a pattern */
	}
	arranger->song[x][y] = g_clipPattern;
	return arranger->song[x][y];
}

int cutNoteFromStep(PatternList *patternList, int patternIndex, int noteIndex) {
	int *step = getStep(patternList, patternIndex, noteIndex);
	if(!step || step[0] == OFF) {
		return 0;
	}
	g_clipNote[0] = step[0];
	g_clipNote[1] = step[1];
	setCurrentNote(patternList, patternIndex, noteIndex, (int[]){ OFF, 0 });
	return 1;
}

int pasteNoteToStep(PatternList *patternList, int patternIndex, int noteIndex) {
	if(g_clipNote[0] == OFF) {
		return 0;
	}
	if(!currentNoteIsBlank(patternList, patternIndex, noteIndex)) {
		return 0; /* target step already has a note */
	}
	setCurrentNote(patternList, patternIndex, noteIndex, g_clipNote);
	return 1;
}
