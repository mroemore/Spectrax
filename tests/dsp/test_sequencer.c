/* test_sequencer.c — sequencer playhead / scene-navigation boundary tests.
 *
 * Covers two reported bugs:
 *   1. At startup selectedPattern == -1, so incrementScene (Shift+Right)
 *      silently does nothing until an arranger cell is selected. The
 *      startup path must seed a valid pattern.
 *   2. When a channel reaches the end of its pattern and the song has no
 *      next row (and no loop), incrementSequencer sets running=0 but still
 *      wraps playhead_index to 0. drawStepGuiNode highlights
 *      playhead_index==0, so the finished pattern's playhead sits on step 1
 *      instead of disappearing.
 *
 * The PatternList / Arranger / Sequencer structs are constructed directly
 * (plain C structs) so no raylib/portaudio GUI stack is linked. appstate.o
 * is linked for createApplicationState / incrementScene / setSelectedPattern;
 * its rebuildPatternGraph() call is stubbed (the graph doesn't exist in this
 * context).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appstate.h"
#include "sequencer.h"

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_2(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)
#define ASSERT_EQ_3(actual, expected, msg) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: %s - expected %lld, got %lld\n", \
                __FILE__, __LINE__, (msg), _e, _a); \
        return 1; \
    } \
} while (0)
#define ASSERT_EQ_GET(_1, _2, _3, NAME, ...) NAME
#define ASSERT_EQ(...) ASSERT_EQ_GET(__VA_ARGS__, ASSERT_EQ_3, ASSERT_EQ_2, MISSING)(__VA_ARGS__)

/* appstate.c's setSelectedPattern calls rebuildPatternGraph(); the graph
 * doesn't exist in this test context, so the real symbol is stubbed. */
void rebuildPatternGraph(void) {
}

/* Bug 1: incrementScene is gated on selectedPattern != -1 (appstate.c).
 * At startup selectedPattern == -1, so Shift+Right does nothing until an
 * arranger cell gets selected (which fires setSelectedPattern). The
 * startup path must seed a valid pattern. */
static int test_increment_scene_requires_selected_pattern(void) {
    ApplicationState *as = createApplicationState();
    ASSERT_TRUE(as != NULL, "createApplicationState");
    ASSERT_EQ(as->currentScene, SCENE_ARRANGER, "starts on the arranger");
    incrementScene(as);
    ASSERT_EQ(as->currentScene, SCENE_ARRANGER,
              "scene advance blocked while no pattern is selected");
    int patternID = 0;
    setSelectedPattern(as, &patternID);
    incrementScene(as);
    ASSERT_EQ(as->currentScene, SCENE_PATTERN,
              "scene advances once a pattern is selected");
    free(as->inputState);
    free(as);
    printf("PASS test_increment_scene_requires_selected_pattern\n");
    return 0;
}

/* Bug 2a: end of song (no next row, no loop). The channel stops and the
 * playhead must NOT wrap back to step 0 — drawStepGuiNode highlights
 * playhead_index==0, which made the finished pattern's playhead sit on
 * step 1 instead of disappearing. */
static int test_playhead_does_not_wrap_when_stopped(void) {
    PatternList pl;
    memset(&pl, 0, sizeof(pl));
    pl.pattern_count = 1;
    pl.patterns[0].pattern_size = 4;

    Arranger arr;
    memset(&arr, 0, sizeof(arr));
    arr.enabledChannels = 1;
    arr.playing = 1;
    arr.song[0][0] = 0;
    arr.song[0][1] = -1;
    arr.tempoSettings.loop = false;

    Sequencer seq;
    memset(&seq, 0, sizeof(seq));
    seq.pattern_index[0] = 0;
    seq.running[0] = 1;
    seq.playhead_index[0] = 0;

    for(int i = 0; i < 3; i++) {
        incrementSequencer(&seq, &pl, &arr);
    }
    ASSERT_EQ(seq.playhead_index[0], 3, "playhead on last step after 3 increments");
    ASSERT_EQ(seq.running[0], 1, "still running");

    incrementSequencer(&seq, &pl, &arr);
    ASSERT_EQ(seq.running[0], 0, "channel stops at end of song");
    ASSERT_EQ(seq.playhead_index[0], 3,
              "playhead does NOT wrap to 0 when the channel stops");
    printf("PASS test_playhead_does_not_wrap_when_stopped\n");
    return 0;
}

/* Bug 2b: pattern switch A -> B. The channel keeps running on B's step 0
 * (the draw then highlights B's step 0, which is correct). */
static int test_pattern_switch_advances(void) {
    PatternList pl;
    memset(&pl, 0, sizeof(pl));
    pl.pattern_count = 2;
    pl.patterns[0].pattern_size = 4;
    pl.patterns[1].pattern_size = 4;

    Arranger arr;
    memset(&arr, 0, sizeof(arr));
    arr.enabledChannels = 1;
    arr.playing = 1;
    arr.song[0][0] = 0;
    arr.song[0][1] = 1;
    arr.song[0][2] = -1;
    arr.tempoSettings.loop = false;

    Sequencer seq;
    memset(&seq, 0, sizeof(seq));
    seq.pattern_index[0] = 0;
    seq.running[0] = 1;
    seq.playhead_index[0] = 0;

    for(int i = 0; i < 4; i++) {
        incrementSequencer(&seq, &pl, &arr);
    }
    ASSERT_EQ(seq.pattern_index[0], 1, "switched to pattern B");
    ASSERT_EQ(seq.playhead_index[0], 0, "B starts at step 0");
    ASSERT_EQ(seq.running[0], 1, "still running");
    printf("PASS test_pattern_switch_advances\n");
    return 0;
}

/* Adding a blank pattern from the arranger (SELECT held + EDIT): an empty
 * cell must get a new pattern assigned to it. */
static int test_add_blank_on_empty_cell(void) {
    PatternList pl;
    memset(&pl, 0, sizeof(pl));
    pl.pattern_count = 0;

    Arranger arr;
    memset(&arr, 0, sizeof(arr));
    arr.enabledChannels = 1;
    for(int c = 0; c < MAX_SEQUENCER_CHANNELS; c++) {
        for(int r = 0; r < MAX_SONG_LENGTH; r++) {
            arr.song[c][r] = -1;
        }
    }

    addBlankIfEmpty(&pl, &arr, 0, 0);
    ASSERT_EQ(pl.pattern_count, 1, "one blank pattern created");
    ASSERT_EQ(arr.song[0][0], 0, "empty cell assigned the new pattern");
    printf("PASS test_add_blank_on_empty_cell\n");
    return 0;
}

/* Adding a blank pattern from the arranger on an OCCUPIED cell: the cell
 * already has a pattern, so no new pattern may be created (that would leak
 * an orphan pattern never referenced by the song). */
static int test_add_blank_on_occupied_cell(void) {
    PatternList pl;
    memset(&pl, 0, sizeof(pl));
    pl.pattern_count = 1;
    pl.patterns[0].pattern_size = 4;

    Arranger arr;
    memset(&arr, 0, sizeof(arr));
    arr.enabledChannels = 1;
    for(int c = 0; c < MAX_SEQUENCER_CHANNELS; c++) {
        for(int r = 0; r < MAX_SONG_LENGTH; r++) {
            arr.song[c][r] = -1;
        }
    }
    arr.song[0][0] = 0;

    addBlankIfEmpty(&pl, &arr, 0, 0);
    ASSERT_EQ(pl.pattern_count, 1,
              "occupied cell must NOT create an orphan pattern");
    ASSERT_EQ(arr.song[0][0], 0, "occupied cell keeps its pattern");
    printf("PASS test_add_blank_on_occupied_cell\n");
    return 0;
}

static int test_pattern_copy_paste(void) {
    PatternList pl;
    memset(&pl, 0, sizeof(pl));
    pl.pattern_count = 0;

    Arranger arr;
    memset(&arr, 0, sizeof(arr));
    arr.enabledChannels = 2;
    for(int c = 0; c < MAX_SEQUENCER_CHANNELS; c++) {
        for(int r = 0; r < MAX_SONG_LENGTH; r++) {
            arr.song[c][r] = -1;
        }
    }

    /* Give cell (0,0) a pattern (pattern index 3). */
    arr.song[0][0] = 3;

    /* Bare EDIT on a patterned cell = copy source. */
    ASSERT_EQ(copyPatternFromCell(&arr, 0, 0), 3, "copy returns the pattern");
    ASSERT_EQ(clipPattern(), 3, "clipboard holds the pattern");
    ASSERT_EQ(arr.song[0][0], 3, "copy leaves the source intact");

    /* Bare EDIT on a blank cell = paste. */
    ASSERT_EQ(pastePatternToCell(&arr, 0, 1), 3, "paste returns the pattern");
    ASSERT_EQ(arr.song[0][1], 3, "blank cell assigned the clipboard pattern");
    ASSERT_EQ(arr.song[0][0], 3, "source untouched by paste");

    /* Pasting over an occupied cell refuses. */
    arr.song[0][2] = 7;
    ASSERT_EQ(pastePatternToCell(&arr, 0, 2), -1, "paste onto occupied cell refused");
    ASSERT_EQ(arr.song[0][2], 7, "occupied cell unchanged");

    /* Cut = copy + clear the source; paste still works. */
    ASSERT_EQ(cutPatternFromCell(&arr, 0, 0), 3, "cut returns the pattern");
    ASSERT_EQ(arr.song[0][0], -1, "cut clears the source cell");
    ASSERT_EQ(clipPattern(), 3, "clipboard survives the cut");
    ASSERT_EQ(pastePatternToCell(&arr, 0, 3), 3, "paste after cut works");
    ASSERT_EQ(arr.song[0][3], 3, "cut pattern re-placed");
    printf("PASS test_pattern_copy_paste\n");
    return 0;
}

static void noop_cb(void *self, void *data) {
    (void)self;
    (void)data;
}

static int test_note_cut_paste(void) {
    PatternList pl;
    memset(&pl, 0, sizeof(pl));
    pl.pattern_count = 1;
    pl.patterns[0].pattern_size = 16;
    pl.onNoteSet.f = noop_cb;
    pl.onNoteSet.appstateRef = NULL;
    /* All steps start blank (OFF). memset 0 would leave every step as
     * a C note, which breaks currentNoteIsBlank. */
    for(int s = 0; s < MAX_SEQUENCE_LENGTH; s++) {
        pl.patterns[0].notes[s][0] = OFF;
        pl.patterns[0].notes[s][1] = 0;
    }

    /* Place C4 on step 2, then cut it. */
    int n[2] = { C, 4 };
    setCurrentNote(&pl, 0, 2, n);
    ASSERT_EQ(cutNoteFromStep(&pl, 0, 2), 1, "cut succeeds");
    ASSERT_TRUE(currentNoteIsBlank(&pl, 0, 2), "cut turns the note off");

    /* Paste onto a blank step. */
    ASSERT_EQ(pasteNoteToStep(&pl, 0, 5), 1, "paste succeeds");
    int *s = getStep(&pl, 0, 5);
    ASSERT_EQ(s[0], C, "paste restores pitch");
    ASSERT_EQ(s[1], 4, "paste restores octave");

    /* Pasting onto a step that still has a note refuses. */
    int other[2] = { A, 2 };
    setCurrentNote(&pl, 0, 7, other);
    ASSERT_EQ(pasteNoteToStep(&pl, 0, 7), 0, "paste onto occupied step refused");
    s = getStep(&pl, 0, 7);
    ASSERT_EQ(s[0], A, "occupied step unchanged");
    printf("PASS test_note_cut_paste\n");
    return 0;
}

/* First-note-skip / double-first-pattern regression (2026-09-08): the audio
 * callback used to advance the playhead (incrementSequencer) BEFORE reading
 * the note, so the first step of a pattern never sounded — pattern A fired
 * steps 1..N-1 then (loop/next row) 0..N-1, heard back-to-back ~1.5x.
 * advanceSequencerStep triggers at the CURRENT playhead then advances, so
 * the fired sequence must be A[0..3] then B[0..3] with step 0 first. */
static int g_firedPitches[32];
static int g_firedCount;
static void recordTrigger(void *ctx, int channel, const int *note) {
	(void)ctx;
	(void)channel;
	g_firedPitches[g_firedCount++] = note[0];
}

static int test_play_fires_step_zero_first(void) {
	PatternList pl;
	memset(&pl, 0, sizeof(pl));
	pl.pattern_count = 2;
	pl.patterns[0].pattern_size = 4;
	pl.patterns[1].pattern_size = 4;
	/* distinguishable pitches per step: A = 100..103, B = 200..203 */
	for(int s = 0; s < 4; s++) {
		pl.patterns[0].notes[s][0] = 100 + s;
		pl.patterns[0].notes[s][1] = 0;
		pl.patterns[1].notes[s][0] = 200 + s;
		pl.patterns[1].notes[s][1] = 0;
	}

	Arranger arr;
	memset(&arr, 0, sizeof(arr));
	arr.enabledChannels = 1;
	arr.selected_y = 0;
	arr.song[0][0] = 0;
	arr.song[0][1] = 1;
	arr.song[0][2] = -1;
	arr.tempoSettings.loop = false;

	Sequencer seq;
	memset(&seq, 0, sizeof(seq));
	startPlaying(&seq, &pl, &arr, 0);

	g_firedCount = 0;
	const int expected[8] = { 100, 101, 102, 103, 200, 201, 202, 203 };
	for(int i = 0; i < 8; i++) {
		advanceSequencerStep(&seq, &pl, &arr, recordTrigger, NULL);
	}
	ASSERT_EQ(g_firedCount, 8, "all 8 steps fire a note");
	for(int i = 0; i < 8; i++) {
		if(g_firedPitches[i] != expected[i]) {
			ASSERT_EQ(g_firedPitches[i], expected[i],
			          "fired sequence matches A[0..3],B[0..3]");
		}
	}
	printf("PASS test_play_fires_step_zero_first\n");
	return 0;
}

/* The last note of the last pattern must still sound before the channel
 * stops at end-of-song (no next row, no loop). The old advance-first order
 * set running=0 before the trigger gate, cutting the final note. */
static int test_last_note_plays_at_song_end(void) {
	PatternList pl;
	memset(&pl, 0, sizeof(pl));
	pl.pattern_count = 1;
	pl.patterns[0].pattern_size = 4;
	for(int s = 0; s < 4; s++) {
		pl.patterns[0].notes[s][0] = 300 + s;
		pl.patterns[0].notes[s][1] = 0;
	}

	Arranger arr;
	memset(&arr, 0, sizeof(arr));
	arr.enabledChannels = 1;
	arr.selected_y = 0;
	arr.song[0][0] = 0;
	arr.song[0][1] = -1;
	arr.tempoSettings.loop = false;

	Sequencer seq;
	memset(&seq, 0, sizeof(seq));
	startPlaying(&seq, &pl, &arr, 0);

	g_firedCount = 0;
	const int expected[4] = { 300, 301, 302, 303 };
	for(int i = 0; i < 5; i++) {
		advanceSequencerStep(&seq, &pl, &arr, recordTrigger, NULL);
	}
	ASSERT_EQ(g_firedCount, 4, "all four notes fire including the last");
	for(int i = 0; i < 4; i++) {
		if(g_firedPitches[i] != expected[i]) {
			ASSERT_EQ(g_firedPitches[i], expected[i], "A[0..3] all fire");
		}
	}
	ASSERT_EQ(seq.running[0], 0, "channel stopped after song end");
	printf("PASS test_last_note_plays_at_song_end\n");
	return 0;
}

int main(void) {
    int fails = 0;
    fails += test_increment_scene_requires_selected_pattern();
    fails += test_playhead_does_not_wrap_when_stopped();
    fails += test_pattern_switch_advances();
    fails += test_add_blank_on_empty_cell();
    fails += test_add_blank_on_occupied_cell();
    fails += test_pattern_copy_paste();
    fails += test_note_cut_paste();
    fails += test_play_fires_step_zero_first();
    fails += test_last_note_plays_at_song_end();
    if (fails) {
        fprintf(stderr, "%d sequencer test(s) failed\n", fails);
        return 1;
    }
    printf("ALL sequencer tests passed\n");
    return 0;
}