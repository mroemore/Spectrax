/*
 * test_sequencer.c — pattern list, arranger, tempo, and sequencer playback
 * tests for the Spectrax sequencer.
 *
 * Build (fil-c toolchain):  make bin/test_sequencer && ./bin/test_sequencer
 *
 * Scope notes:
 *  - sequencer.c is self-contained except for the four ApplicationState
 *    setter callbacks it invokes (setSelectedStep/setLastUsedNote/
 *    setSelectedArrangerCell/setSelectedPattern). Linking appstate.c would
 *    drag in input.c -> raylib, so this test defines faithful local copies
 *    of those four setters below. ApplicationState is allocated directly
 *    (createApplicationState() also requires the raylib input layer).
 *  - createArranger only stores its VoiceManager* (sequencer.c:71); it is
 *    never dereferenced by the sequencer code, so NULL is passed here and
 *    the heavy voice/sample/wavetable machinery is not linked at all.
 *  - Per BUG-VC-1 (freeVoice double-free) nothing here frees voice
 *    objects, and per the task constraints the arranger/pattern list are
 *    deliberately leaked (no free functions exist for them anyway).
 *
 * Pre-existing bugs pinned by these tests (see per-test comments):
 *  - sequencer.c updateBpm is declared in sequencer.h but the body is
 *    commented out (lines 97-102): calling it would be a link error.
 *    BPM changes must go through the parameter callback (cb_applyBpmParam);
 *    note that only setParameterValue() updates the currentValue that
 *    applyTempoSettings reads — setParameterBaseValue() (used by
 *    io/sequencer_io.c) alone leaves the tempo stale until a later
 *    processModulations sync.
 *  - sequencer.c applyTempoSettings recomputes samplesPerEvenStep and
 *    samplesPerOddStep but never updates currentSamplesPerStep, so the
 *    "current" field goes stale after a BPM change (lines 38-44).
 *  - sequencer.c editStep writes to notes[patternIndex] instead of
 *    notes[noteIndex] (line 330) — editing step N edits step 0 instead.
 *  - sequencer.c addBlankIfEmpty always calls addBlankPattern (allocating a
 *    new pattern every invocation) even when the destination cell is
 *    occupied; it only writes the cell when empty (lines 172-178).
 *  - sequencer.c addChannel memmoves MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH
 *    ints from song[channelIndex] to song[channelIndex + 1] (line 107) —
 *    one full row too many, so inserting at any interior index overflows
 *    the song array and panics under fil-c. Only append-style adds
 *    (channelIndex >= enabledChannels) are safe and tested here.
 *  - sequencer.c setCurrentNote passes `&note` (the address of the pointer
 *    parameter) to the onNoteSet callback (line 258) instead of `note`, so
 *    ApplicationState.lastUsedNote receives pointer bits, not the note.
 *  - sequencer.c incrementSequencer computes the wrap-around modulo after a
 *    pattern switch against the NEW pattern's size (line 322): end-of-pattern
 *    onto a different-length pattern leaves the playhead at
 *    (old_playhead+1) % new_size instead of 0 (line 322).
 *  - createPatternList sets selectedPattern = -1 (sequencer.c:20), not 0 as
 *    the header doc implies; -1 matches the appstate "nothing selected"
 *    convention.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sequencer.h"
#include "notes.h"
#include "modsystem.h"
#include "settings.h"

/* ------------------------------------------------------------------ */
/* assertion helpers (same style as tests/dsp/test_notes.c)            */
/* ------------------------------------------------------------------ */

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(actual, expected) do { \
    int _a = (int)(actual); \
    int _e = (int)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_NONNULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected non-NULL\n", __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* ApplicationState setter stubs (local copies of appstate.c)          */
/* appstate.c itself pulls in input.c -> raylib, so it is not linked.  */
/* ------------------------------------------------------------------ */

void setSelectedStep(void *self, void *step) {
    ApplicationState *as = (ApplicationState *)self;
    as->selectedStep = *(int *)step;
}

void setLastUsedNote(void *self, void *noteArray) {
    ApplicationState *as = (ApplicationState *)self;
    as->lastUsedNote[0] = ((int *)noteArray)[0];
    as->lastUsedNote[1] = ((int *)noteArray)[1];
}

void setSelectedArrangerCell(void *self, void *cellCoordinates) {
    ApplicationState *as = (ApplicationState *)self;
    as->selectedArrangerCell[0] = ((int *)cellCoordinates)[0];
    as->selectedArrangerCell[1] = ((int *)cellCoordinates)[1];
}

void setSelectedPattern(void *self, void *patternID) {
    ApplicationState *as = (ApplicationState *)self;
    as->selectedPattern = *(int *)patternID;
}

/* addBlankPattern is used internally by addBlankIfEmpty but is not in
 * sequencer.h — declare it here to test it directly. */
extern int addBlankPattern(PatternList *patternList);

/* ------------------------------------------------------------------ */
/* test environment                                                    */
/* ------------------------------------------------------------------ */

static ApplicationState *make_appstate(void) {
    ApplicationState *as = (ApplicationState *)calloc(1, sizeof(ApplicationState));
    as->selectedPattern = -1;
    return as;
}

static int make_notes(int notes[][NOTE_INFO_SIZE], int len, int note, int octave) {
    for (int i = 0; i < len; i++) {
        notes[i][0] = note;
        notes[i][1] = octave;
    }
    return 0;
}

/* Build a PatternList with `count` 16-step patterns. Pattern i is filled
 * with note (C + i) octave 3. Returns the list (leaked on purpose). */
static PatternList *make_pattern_list(ApplicationState *as, int count) {
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    for (int i = 0; i < count; i++) {
        make_notes(notes, 16, C + i, 3);
        addPattern(pl, 16, notes);
    }
    return pl;
}

/* Build an Arranger with `channels` enabled channels at defaultBPM.
 * Returns the arranger (leaked on purpose); globalParamList also leaked. */
typedef struct {
    Arranger *arr;
    ParamList *params;
    ApplicationState *as;
    Settings settings;
} ArrangerEnv;

static ArrangerEnv make_arranger(int channels, int defaultBPM) {
    ArrangerEnv e;
    memset(&e, 0, sizeof(e));
    e.as = make_appstate();
    e.settings.enabledChannels = channels;
    e.settings.defaultVoiceCount = 0;
    e.settings.defaultBPM = defaultBPM;
    e.params = createParamList();
    /* vm is NULL on purpose: createArranger only stores the pointer and the
     * sequencer code never dereferences it. */
    e.arr = createArranger(&e.settings, NULL, e.as, e.params);
    return e;
}

/* ------------------------------------------------------------------ */
/* PatternList tests                                                  */
/* ------------------------------------------------------------------ */

static int test_create_pattern_list(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    ASSERT_NONNULL(pl);
    ASSERT_INT_EQ(pl->pattern_count, 0);
    /* Impl sets -1 (sequencer.c:20), not 0 as the header doc implies. -1 is
     * the appstate "nothing selected" convention; asserting actual code. */
    ASSERT_INT_EQ(pl->selectedPattern, -1);
    printf("PASS test_create_pattern_list\n");
    return 0;
}

static int test_add_pattern(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, C, 3);

    int idx = addPattern(pl, 16, notes);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(pl->pattern_count, 1);

    int idx2 = addPattern(pl, 8, notes);
    ASSERT_INT_EQ(idx2, 1);
    ASSERT_INT_EQ(pl->pattern_count, 2);
    ASSERT_INT_EQ(pl->patterns[1].pattern_size, 8);
    printf("PASS test_add_pattern\n");
    return 0;
}

static int test_pattern_notes_write_read(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, A, 4);
    addPattern(pl, 16, notes);

    /* direct array access */
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], A);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][1], 4);
    ASSERT_INT_EQ(pl->patterns[0].notes[15][0], A);
    ASSERT_INT_EQ(pl->patterns[0].notes[15][1], 4);

    /* public accessors return pointers into the same storage */
    ASSERT_TRUE(getStep(pl, 0, 3) == &pl->patterns[0].notes[3][0],
                "getStep must point at the pattern's notes storage");
    ASSERT_TRUE(getStep(pl, 0, 3) == getCurrentStep(pl, 0, 3),
                "getStep and getCurrentStep must alias");
    printf("PASS test_pattern_notes_write_read\n");
    return 0;
}

static int test_select_step_clamps(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, C, 3);
    addPattern(pl, 16, notes);

    ASSERT_INT_EQ(selectStep(pl, 0, 0), 0);
    ASSERT_INT_EQ(selectStep(pl, 0, 15), 15);
    /* beyond the end clamps to pattern_size - 1 */
    ASSERT_INT_EQ(selectStep(pl, 0, 999), 15);
    /* negative clamps to 0 */
    ASSERT_INT_EQ(selectStep(pl, 0, -4), 0);
    /* callback fired: appstate selectedStep mirrors the result */
    ASSERT_INT_EQ(as->selectedStep, 0);
    printf("PASS test_select_step_clamps\n");
    return 0;
}

static int test_set_current_note(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, OFF, 0);
    addPattern(pl, 16, notes);

    ASSERT_TRUE(currentNoteIsBlank(pl, 0, 4), "step should start blank");

    int note[NOTE_INFO_SIZE] = { E, 5 };
    setCurrentNote(pl, 0, 4, note);
    ASSERT_INT_EQ(pl->patterns[0].notes[4][0], E);
    ASSERT_INT_EQ(pl->patterns[0].notes[4][1], 5);
    ASSERT_TRUE(!currentNoteIsBlank(pl, 0, 4), "step should no longer be blank");
    /* BUG-SEQ-5: setCurrentNote feeds `&note` (address of the pointer
     * parameter) to onNoteSet (sequencer.c:258), so the callback stores
     * pointer bits into lastUsedNote instead of the note data. The note
     * write above still succeeds; only the appstate mirror is corrupt. */
    ASSERT_TRUE(!(as->lastUsedNote[0] == E && as->lastUsedNote[1] == 5),
                "BUG-SEQ-5: lastUsedNote should mirror [E,5] but the "
                "callback receives &note");
    printf("PASS test_set_current_note (documents &note callback bug)\n");
    return 0;
}

static int test_edit_current_note_blank_default(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, OFF, 0);
    addPattern(pl, 16, notes);

    /* Pins actual behaviour of sequencer.c:261-268: editing a blank step
     * discards the supplied note and writes the [C,3] default. */
    int note[NOTE_INFO_SIZE] = { G, 6 };
    editCurrentNote(pl, 0, 2, note);
    ASSERT_INT_EQ(pl->patterns[0].notes[2][0], C);
    ASSERT_INT_EQ(pl->patterns[0].notes[2][1], 3);
    printf("PASS test_edit_current_note_blank_default\n");
    return 0;
}

static int test_edit_current_note_relative(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, C, 3);
    addPattern(pl, 16, notes);

    int rel[NOTE_INFO_SIZE];

    /* chromatic step down wrapping: C3 -> B2 (note underflows to B, octave--)*/
    rel[0] = -1; rel[1] = 0;
    editCurrentNoteRelative(pl, 0, 0, rel);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], B);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][1], 2);

    /* wrap at top: B2 -> C3 */
    rel[0] = 1; rel[1] = 0;
    editCurrentNoteRelative(pl, 0, 0, rel);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], C);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][1], 3);

    /* One semitone up from C is Db (the Note enum is chromatic and includes
     * the sharp/flat names, so +1 index = +1 semitone). */
    rel[0] = 1; rel[1] = 0;
    editCurrentNoteRelative(pl, 0, 0, rel);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], Db);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][1], 3);

    /* octave clamp: MAX_OCTAVES-1 is the ceiling */
    rel[0] = 0; rel[1] = 100;
    editCurrentNoteRelative(pl, 0, 0, rel);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][1], MAX_OCTAVES - 1);

    printf("PASS test_edit_current_note_relative\n");
    return 0;
}

static int test_edit_step_bug(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);
    int notes[MAX_SEQUENCE_LENGTH][NOTE_INFO_SIZE];
    make_notes(notes, 16, OFF, 0);
    addPattern(pl, 16, notes);

    /* Pins sequencer.c:330: editStep writes to notes[patternIndex] instead
     * of notes[noteIndex]. Editing step 5 lands in step 0. */
    int note[NOTE_INFO_SIZE] = { F, 4 };
    editStep(pl, 0, 5, note);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], F);   /* buggy target: step 0 */
    ASSERT_INT_EQ(pl->patterns[0].notes[5][0], OFF); /* intended target    */
    printf("PASS test_edit_step_bug (documents notes[patternIndex] bug)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Arranger tests                                                     */
/* ------------------------------------------------------------------ */

static int test_create_arranger(void) {
    ArrangerEnv e = make_arranger(2, 120);
    ASSERT_NONNULL(e.arr);
    ASSERT_NONNULL(e.params);
    ASSERT_NONNULL(e.arr->tempoSettings.bpm);
    ASSERT_NONNULL(e.arr->tempoSettings.swing);
    ASSERT_INT_EQ(e.arr->enabledChannels, 2);
    ASSERT_INT_EQ(e.arr->playing, 0);
    ASSERT_INT_EQ(e.arr->selected_x, 0);
    ASSERT_INT_EQ(e.arr->selected_y, 0);
    ASSERT_TRUE(e.arr->tempoSettings.loop, "loop should default to true");
    ASSERT_TRUE(!e.arr->tempoSettings.swingStep, "swingStep should default to false");
    /* 120 BPM at 44.1k = (44100*60)/(120*4) = 5512.5 samples per 16th step */
    ASSERT_INT_EQ(e.arr->tempoSettings.currentSamplesPerStep, 5512);
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerEvenStep, 5512);
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerOddStep, 5512);
    /* all song cells initialised to -1 */
    ASSERT_INT_EQ(e.arr->song[0][0], -1);
    ASSERT_INT_EQ(e.arr->song[1][MAX_SONG_LENGTH - 1], -1);
    printf("PASS test_create_arranger\n");
    return 0;
}

static int test_bpm_param_update(void) {
    ArrangerEnv e = make_arranger(1, 120);

    /* updateBpm() is declared in sequencer.h but its body is commented out
     * (sequencer.c:97-102), so the supported path is setting the BPM
     * parameter, which fires cb_applyBpmParam -> applyTempoSettings.
     * setParameterValue() must be used: applyTempoSettings reads the
     * parameter currentValue, which setParameterBaseValue() alone leaves
     * stale (io/sequencer_io.c:122 relies on a later processModulations
     * sync). */
    setParameterValue(e.arr->tempoSettings.bpm, 240);
    /* 240 BPM -> (44100*60)/(240*4) = 2756.25; even/odd at 50% swing both
     * round to 2756. */
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerEvenStep, 2756);
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerOddStep, 2756);
    /* Pins sequencer.c:38-44: applyTempoSettings never updates
     * currentSamplesPerStep, so the "current" field goes stale. */
    ASSERT_INT_EQ(e.arr->tempoSettings.currentSamplesPerStep, 5512);

    setParameterValue(e.arr->tempoSettings.bpm, 120);
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerEvenStep, 5512);
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerOddStep, 5512);
    printf("PASS test_bpm_param_update (notes stale currentSamplesPerStep)\n");
    return 0;
}

static int test_swing_param_update(void) {
    ArrangerEnv e = make_arranger(1, 120);

    setParameterValue(e.arr->tempoSettings.swing, 60);
    /* 60/40 split at 120 BPM: cent = 5512.5/50 = 110.25
     * even = 110.25*60 = 6615.0, odd = 110.25*40 = 4410.0 */
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerEvenStep, 6615);
    ASSERT_INT_EQ(e.arr->tempoSettings.samplesPerOddStep, 4410);
    printf("PASS test_swing_param_update\n");
    return 0;
}

static int test_add_remove_channel(void) {
    ArrangerEnv e = make_arranger(2, 120);

    /* BUG-SEQ-4: addChannel at an interior index (channelIndex <
     * enabledChannels) memmoves one row too many (sequencer.c:107) and
     * panics under fil-c, so only append-style adds are safe to exercise.
     * Inserting at index == enabledChannels skips the memmove branch. */
    addChannel(e.arr, 2);
    ASSERT_INT_EQ(e.arr->enabledChannels, 3);
    /* the new channel's song row must start empty */
    ASSERT_INT_EQ(e.arr->song[2][0], -1);

    /* channelIndex -1 is rejected */
    addChannel(e.arr, -1);
    ASSERT_INT_EQ(e.arr->enabledChannels, 3);

    removeChannel(e.arr, 0);
    ASSERT_INT_EQ(e.arr->enabledChannels, 2);

    /* invalid indices are no-ops */
    removeChannel(e.arr, -1);
    removeChannel(e.arr, 99);
    ASSERT_INT_EQ(e.arr->enabledChannels, 2);

    printf("PASS test_add_remove_channel (append-style only; see BUG-SEQ-4)\n");
    return 0;
}

static int test_add_channel_caps_at_max(void) {
    ArrangerEnv e = make_arranger(1, 120);

    while (e.arr->enabledChannels < MAX_SEQUENCER_CHANNELS) {
        addChannel(e.arr, e.arr->enabledChannels); /* append is memmove-free */
    }
    ASSERT_INT_EQ(e.arr->enabledChannels, MAX_SEQUENCER_CHANNELS);
    /* one more must be rejected */
    addChannel(e.arr, MAX_SEQUENCER_CHANNELS);
    ASSERT_INT_EQ(e.arr->enabledChannels, MAX_SEQUENCER_CHANNELS);
    printf("PASS test_add_channel_caps_at_max\n");
    return 0;
}

static int test_add_pattern_to_arranger(void) {
    ArrangerEnv e = make_arranger(2, 120);
    ApplicationState *as = e.as;
    PatternList *pl = make_pattern_list(as, 2);

    addPatternToArranger(e.arr, 1, 0, 3);
    ASSERT_INT_EQ(e.arr->song[0][3], 1);

    /* select the cell, then read the pattern id back from the cursor */
    selectArrangerCell(e.arr, 0, 0, 3);
    ASSERT_INT_EQ(getPatternIDfromArranger(e.arr), 1);
    printf("PASS test_add_pattern_to_arranger\n");
    return 0;
}

static int test_add_blank_if_empty(void) {
    ArrangerEnv e = make_arranger(1, 120);
    PatternList *pl = make_pattern_list(e.as, 0);

    addBlankIfEmpty(pl, e.arr, 0, 0);
    ASSERT_INT_EQ(pl->pattern_count, 1);
    ASSERT_INT_EQ(e.arr->song[0][0], 0);

    /* Pins sequencer.c:172-178: every call allocates a blank pattern even
     * when the cell is occupied — only the cell write is conditional. */
    addBlankIfEmpty(pl, e.arr, 0, 0);
    ASSERT_INT_EQ(pl->pattern_count, 2);
    ASSERT_INT_EQ(e.arr->song[0][0], 0); /* cell unchanged */
    printf("PASS test_add_blank_if_empty (documents always-allocates quirk)\n");
    return 0;
}

static int test_add_blank_pattern(void) {
    ApplicationState *as = make_appstate();
    PatternList *pl = createPatternList(as);

    int idx = addBlankPattern(pl);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(pl->patterns[0].pattern_size, 16);
    ASSERT_INT_EQ(pl->patterns[0].notes[0][0], OFF);
    ASSERT_INT_EQ(pl->patterns[0].notes[15][0], OFF);
    printf("PASS test_add_blank_pattern\n");
    return 0;
}

static int test_select_arranger_cell(void) {
    ArrangerEnv e = make_arranger(2, 120);
    ApplicationState *as = e.as;

    /* move right one column (blank allowed with checkBlankPattern=0) */
    ASSERT_TRUE(selectArrangerCell(e.arr, 0, 1, 0), "nav right should succeed");
    ASSERT_INT_EQ(e.arr->selected_x, 1);
    ASSERT_INT_EQ(e.arr->selected_y, 0);

    /* clamp past the last channel */
    ASSERT_TRUE(!selectArrangerCell(e.arr, 0, 50, 0),
                "navigation beyond last channel clamps and reports no move");
    ASSERT_INT_EQ(e.arr->selected_x, e.arr->enabledChannels - 1);

    /* clamp below zero */
    ASSERT_TRUE(selectArrangerCell(e.arr, 0, -50, 0), "clamp to 0");
    ASSERT_INT_EQ(e.arr->selected_x, 0);

    /* clamp the song length axis */
    ASSERT_TRUE(selectArrangerCell(e.arr, 0, 0, 1000), "clamp y to MAX_SONG_LENGTH-1");
    ASSERT_INT_EQ(e.arr->selected_y, MAX_SONG_LENGTH - 1);

    /* with checkBlankPattern=1, an empty destination cell must not move */
    ASSERT_TRUE(selectArrangerCell(e.arr, 0, 0, -1000), "back to y=0");
    ASSERT_INT_EQ(e.arr->selected_y, 0);
    ASSERT_TRUE(!selectArrangerCell(e.arr, 1, 0, 1),
                "blank cell blocks navigation when checkBlankPattern=1");
    ASSERT_INT_EQ(e.arr->selected_y, 0);

    /* callbacks mirror the selection into appstate */
    ASSERT_INT_EQ(as->selectedArrangerCell[0], 0);
    ASSERT_INT_EQ(as->selectedArrangerCell[1], 0);
    printf("PASS test_select_arranger_cell\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sequencer tests                                                    */
/* ------------------------------------------------------------------ */

static int test_create_sequencer(void) {
    ArrangerEnv e = make_arranger(2, 120);
    PatternList *pl = make_pattern_list(e.as, 1);

    addPatternToArranger(e.arr, 0, 0, 0);   /* channel 0 has a pattern */
    /* channel 1 stays empty */

    Sequencer *seq = createSequencer(e.arr);
    ASSERT_NONNULL(seq);
    ASSERT_INT_EQ(seq->playhead_index[0], 0);
    ASSERT_INT_EQ(seq->pattern_index[0], 0);
    ASSERT_INT_EQ(seq->running[0], 1);
    /* channel 1: empty first row -> not running */
    ASSERT_INT_EQ(seq->pattern_index[1], -1);
    ASSERT_INT_EQ(seq->running[1], 0);
    printf("PASS test_create_sequencer\n");
    return 0;
}

static int test_start_stop_playing(void) {
    ArrangerEnv e = make_arranger(1, 120);
    PatternList *pl = make_pattern_list(e.as, 2);
    addPatternToArranger(e.arr, 0, 0, 0);
    addPatternToArranger(e.arr, 1, 0, 1);

    Sequencer *seq = createSequencer(e.arr);

    /* start from the second song row */
    selectArrangerCell(e.arr, 0, 0, 1);
    startPlaying(seq, pl, e.arr, 0);
    ASSERT_INT_EQ(e.arr->playing, 1);
    ASSERT_INT_EQ(e.arr->playhead_indices[0], 1);
    ASSERT_INT_EQ(seq->playhead_index[0], 0);
    ASSERT_INT_EQ(seq->pattern_index[0], 1);
    ASSERT_INT_EQ(seq->running[0], 1);

    stopPlaying(e.arr);
    ASSERT_INT_EQ(e.arr->playing, 0);
    printf("PASS test_start_stop_playing\n");
    return 0;
}

static int test_increment_sequencer_loops(void) {
    ArrangerEnv e = make_arranger(1, 120);
    PatternList *pl = make_pattern_list(e.as, 1);
    addPatternToArranger(e.arr, 0, 0, 0);

    Sequencer *seq = createSequencer(e.arr);

    /* one step toggles swing and advances the playhead */
    incrementSequencer(seq, pl, e.arr);
    ASSERT_TRUE(e.arr->tempoSettings.swingStep, "swingStep toggles on each step");
    ASSERT_INT_EQ(seq->playhead_index[0], 1);

    /* 15 more steps -> 16 total: EOP on a looping song returns to row 0 and
     * wraps the playhead (16 toggles = back to false). */
    for (int i = 0; i < 15; i++) {
        incrementSequencer(seq, pl, e.arr);
    }
    ASSERT_INT_EQ(seq->playhead_index[0], 0);
    ASSERT_INT_EQ(seq->pattern_index[0], 0);
    ASSERT_INT_EQ(e.arr->playhead_indices[0], 0);
    ASSERT_INT_EQ(seq->running[0], 1);
    ASSERT_TRUE(!e.arr->tempoSettings.swingStep, "even toggles land back at false");
    printf("PASS test_increment_sequencer_loops\n");
    return 0;
}

static int test_increment_sequencer_ends(void) {
    ArrangerEnv e = make_arranger(1, 120);
    PatternList *pl = make_pattern_list(e.as, 1);
    addPatternToArranger(e.arr, 0, 0, 0);
    e.arr->tempoSettings.loop = false;

    Sequencer *seq = createSequencer(e.arr);
    for (int i = 0; i < 16; i++) {
        incrementSequencer(seq, pl, e.arr);
    }
    ASSERT_INT_EQ(seq->running[0], 0);
    printf("PASS test_increment_sequencer_ends\n");
    return 0;
}

static int test_increment_sequencer_pattern_advance(void) {
    ArrangerEnv e = make_arranger(1, 120);
    PatternList *pl = make_pattern_list(e.as, 2); /* pattern 0 = 16 steps */
    pl->patterns[1].pattern_size = 8;             /* pattern 1 = 8 steps  */
    addPatternToArranger(e.arr, 0, 0, 0);
    addPatternToArranger(e.arr, 1, 0, 1);

    Sequencer *seq = createSequencer(e.arr);

    /* 16 steps of pattern 0: end-of-pattern switches to row 1 (pattern 1) */
    for (int i = 0; i < 16; i++) {
        incrementSequencer(seq, pl, e.arr);
    }
    ASSERT_INT_EQ(e.arr->playhead_indices[0], 1);
    ASSERT_INT_EQ(seq->pattern_index[0], 1);
    ASSERT_INT_EQ(seq->playhead_index[0], 0);

    /* 8 more steps of pattern 1: song row 2 is empty, so loop returns to the
     * first non-empty row (row 0, pattern 0). The pattern index and song
     * playhead do loop back correctly...
     *
     * ...but BUG-SEQ-6: the wrap-around modulo re-reads the pattern size
     * AFTER the pattern switch (sequencer.c:322), so the playhead is set to
     * (7+1) % patterns[0].pattern_size = 8 instead of resetting to 0. */
    for (int i = 0; i < 8; i++) {
        incrementSequencer(seq, pl, e.arr);
    }
    ASSERT_INT_EQ(e.arr->playhead_indices[0], 0);
    ASSERT_INT_EQ(seq->pattern_index[0], 0);
    ASSERT_INT_EQ(seq->playhead_index[0], 8); /* BUG-SEQ-6: should be 0 */
    ASSERT_INT_EQ(seq->running[0], 1);
    printf("PASS test_increment_sequencer_pattern_advance "
           "(documents post-switch modulo bug)\n");
    return 0;
}

static int test_find_arranger_loop_index(void) {
    ArrangerEnv e = make_arranger(1, 120);

    /* contiguous occupied rows: loops all the way back to row 0 */
    e.arr->song[0][0] = 5;
    e.arr->song[0][1] = 7;
    ASSERT_INT_EQ(findArrangerLoopIndex(e.arr, 0, 2), 0);

    /* row 0 empty: loop point is row 1 (scan stops at the gap) */
    e.arr->song[0][0] = -1;
    e.arr->song[0][1] = 7;
    ASSERT_INT_EQ(findArrangerLoopIndex(e.arr, 0, 1), 1);

    /* currentY == 0 has nothing before it */
    ASSERT_INT_EQ(findArrangerLoopIndex(e.arr, 0, 0), 0);
    printf("PASS test_find_arranger_loop_index\n");
    return 0;
}

/* ------------------------------------------------------------------ */

int main(void) {
    int failed = 0;
    failed |= test_create_pattern_list();
    failed |= test_add_pattern();
    failed |= test_pattern_notes_write_read();
    failed |= test_select_step_clamps();
    failed |= test_set_current_note();
    failed |= test_edit_current_note_blank_default();
    failed |= test_edit_current_note_relative();
    failed |= test_edit_step_bug();
    failed |= test_create_arranger();
    failed |= test_bpm_param_update();
    failed |= test_swing_param_update();
    failed |= test_add_remove_channel();
    failed |= test_add_channel_caps_at_max();
    failed |= test_add_pattern_to_arranger();
    failed |= test_add_blank_if_empty();
    failed |= test_add_blank_pattern();
    failed |= test_select_arranger_cell();
    failed |= test_create_sequencer();
    failed |= test_start_stop_playing();
    failed |= test_increment_sequencer_loops();
    failed |= test_increment_sequencer_ends();
    failed |= test_increment_sequencer_pattern_advance();
    failed |= test_find_arranger_loop_index();

    printf("\n%s (%d tests, %s)\n",
           failed ? "FAILED" : "PASSED", 23, failed ? ">=1 failed" : "all passed");
    return failed;
}
