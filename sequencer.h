#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "settings.h"
#include "voice.h"


/**
 * @brief Represents a musical pattern containing a sequence of notes. 
 * Each row is a note n your pattern, each column contains the octave and a note index from 0-12.
 */
typedef struct {
	int pattern_size;
	int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
} Pattern;

typedef struct {
	int pattern_count;
	Pattern patterns[MAX_PATTERNS];
} PatternList;

typedef struct
{
	int playhead_indices[MAX_SEQUENCER_CHANNELS];
	int enabledChannels;
	int selected_x;
	int selected_y;
	int loop;
	int beats_per_minute;
	int playing;
	int voiceTypes[MAX_SEQUENCER_CHANNELS];
	int song[MAX_SEQUENCER_CHANNELS][MAX_SONG_LENGTH];
} Arranger;

typedef struct
{
	int running[MAX_SEQUENCER_CHANNELS];
	int playhead_index[MAX_SEQUENCER_CHANNELS];
	int selected_index[MAX_SEQUENCER_CHANNELS];
	int pattern_index[MAX_SEQUENCER_CHANNELS];
} Sequencer;


/**
 * @brief Creates a new PatternList.
 * @return A pointer to the newly created PatternList.
 */
PatternList* createPatternList();
/**
 * @brief Creates a new Arranger with the given settings.
 * @param settings Pointer to the settings to initialize the Arranger.
 * @return A pointer to the newly created Arranger.
 */
Arranger *createArranger(Settings* settings);

/**
 * @brief Adds a new channel to the Arranger at the specified index.
 * @param arranger Pointer to the Arranger.
 * @param channelIndex Index at which to add the new channel.
 */
void addChannel(Arranger* arranger, int channelIndex);
/**
 * @brief Removes a channel from the Arranger at the specified index.
 * @param arranger Pointer to the Arranger.
 * @param channelIndex Index of the channel to remove.
 */
void removeChannel(Arranger* arranger, int channelIndex);
/**
 * @brief Adds a new pattern to the PatternList.
 * @param patternList Pointer to the PatternList.
 * @param patternSize Size of the pattern.
 * @param notes Array of notes to add to the pattern.
 * @return The index of the newly added pattern.
 */
int addPattern(PatternList *patternList, int patternSize, int notes[][NOTE_INFO_SIZE]);
/**
 * @brief Adds a blank pattern to the PatternList.
 * @param patternList Pointer to the PatternList.
 * @return The index of the newly added pattern.
 */
void addPatternToArranger(Arranger *arranger, int patternId, int sequencer_id, int column);
/**
 * @brief Adds a blank pattern to the Arranger if the specified location is empty.
 * @param patternList Pointer to the PatternList.
 * @param arranger Pointer to the Arranger.
 * @param sequencerId Channel index.
 * @param row Row index.
 */
void addBlankIfEmpty(PatternList* patternList, Arranger* arranger, int sequencerId, int row);
/**
 * @brief Creates a new Sequencer based on the current state of the Arranger.
 * @param arranger Pointer to the Arranger.
 * @return A pointer to the newly created Sequencer.
 */
Sequencer* createSequencer(Arranger *arranger);

/**
 * @brief Retrieves a step (note) from a pattern.
 * @param patternList Pointer to the PatternList.
 * @param patternIndex Index of the pattern.
 * @param noteIndex Index of the note.
 * @return Pointer to the note data.
 */
int *getStep(PatternList *patternList, int patternIndex, int noteIndex);
/**
 * @brief Retrieves the current step (note) from a pattern.
 * @param patternList Pointer to the PatternList.
 * @param patternIndex Index of the pattern.
 * @param noteIndex Index of the note.
 * @return Pointer to the note data.
 */
int* getCurrentStep(PatternList *patternList, int patternIndex, int noteIndex);
/**
 * @brief Selects a cell in the Arranger and updates the selected coordinates.
 * @param arranger Pointer to the Arranger.
 * @param checkBlankPattern Whether to check for blank patterns.
 * @param relativex Relative x-coordinate offset.
 * @param relativey Relative y-coordinate offset.
 * @param selectedArrangerCell Array to store the selected coordinates.
 * @return Pointer to the selected coordinates.
 */
int* selectArrangerCell(Arranger* arranger, int checkBlankPattern, int relativex, int relativey, int *selectedArrangerCell);
/**
 * @brief Selects a step in a pattern and ensures it is within bounds.
 * @param patternList Pointer to the PatternList.
 * @param patternIndex Index of the pattern.
 * @param selectedStep Index of the step to select.
 * @return The selected step index.
 */
int selectStep(PatternList *patternList, int patternIndex, int selectedStep);
/**
 * @brief Edits the current note in a pattern.
 * @param patternList Pointer to the PatternList.
 * @param patternIndex Index of the pattern.
 * @param noteIndex Index of the note to edit.
 * @param note New note data.
 * @return Pointer to the edited note data.
 */
int* editCurrentNote(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]);
/**
 * @brief Edits the current note in a pattern with relative changes.
 * @param patternList Pointer to the PatternList.
 * @param patternIndex Index of the pattern.
 * @param noteIndex Index of the note to edit.
 * @param note Relative changes to the note data. [1,0] would increase note chromatically, [0,1] would increase octave.
 * @return Pointer to the edited note data.
 */
int* editCurrentNoteRelative(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]);
/**
 * @brief Advances the sequencer to the next step for all active channels.
 * @param sequencer Pointer to the Sequencer.
 * @param patternList Pointer to the PatternList.
 * @param arranger Pointer to the Arranger.
 */
void incrementSequencer(Sequencer* sequencer, PatternList *patternList, Arranger *arranger);

/**
 * @brief Edits a step in a pattern.
 * @param patternList Pointer to the PatternList.
 * @param patternIndex Index of the pattern.
 * @param noteIndex Index of the note to edit.
 * @param note New note data.
 */
void editStep(PatternList *patternList, int patternIndex, int noteIndex, int note[NOTE_INFO_SIZE]);
/**
 * @brief Stops playback in the Arranger.
 * @param arranger Pointer to the Arranger.
 */
void stopPlaying(Arranger *arranger);
/**
 * @brief Starts playback in the Sequencer and Arranger from the current cursor position, resetting all pattern playback posiitons.
 * @param sequencer Pointer to the Sequencer.
 * @param patternList Pointer to the PatternList.
 * @param arranger Pointer to the Arranger.
 * @param playMode Playback mode.
 */
void startPlaying(Sequencer* sequencer, PatternList *patternList, Arranger *arranger, int playMode);

#endif