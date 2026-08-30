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

int main(void) {
    int fails = 0;
    fails += test_increment_scene_requires_selected_pattern();
    fails += test_playhead_does_not_wrap_when_stopped();
    fails += test_pattern_switch_advances();
    fails += test_add_blank_on_empty_cell();
    fails += test_add_blank_on_occupied_cell();
    if (fails) {
        fprintf(stderr, "%d sequencer test(s) failed\n", fails);
        return 1;
    }
    printf("ALL sequencer tests passed\n");
    return 0;
}