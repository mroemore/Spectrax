/*
 * test_notes.c — verify the note frequency table and getNoteString.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "notes.h"

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

#define ASSERT_STREQ(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected NULL\n", __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static int test_known_frequencies(void) {
    /* A4 = 440 Hz, MIDI note 69 */
    ASSERT_NEAR(noteFrequencies[A][4], 440.00f, 0.01f);
    /* Middle C (C4) = 261.63 Hz, MIDI note 60 */
    ASSERT_NEAR(noteFrequencies[C][4], 261.63f, 0.01f);
    /* C0 = 16.35 Hz */
    ASSERT_NEAR(noteFrequencies[C][0], 16.35f, 0.01f);
    /* A0 = 27.50 Hz (lowest piano key) */
    ASSERT_NEAR(noteFrequencies[A][0], 27.50f, 0.01f);
    /* Concert A5 = 880 Hz */
    ASSERT_NEAR(noteFrequencies[A][5], 880.00f, 0.01f);
    printf("PASS test_known_frequencies\n");
    return 0;
}

static int test_octave_doubling(void) {
    /* Each octave up should double the frequency */
    for (int n = 0; n < NOTE_COUNT; n++) {
        for (int o = 0; o < MAX_OCTAVES - 1; o++) {
            float f0 = noteFrequencies[n][o];
            float f1 = noteFrequencies[n][o + 1];
            if (f0 <= 0.0f || f1 <= 0.0f) {
                /* Some upper octaves are 0.0 (out of range) — skip. */
                continue;
            }
            float ratio = f1 / f0;
            if (fabsf(ratio - 2.0f) > 0.005f) {
                fprintf(stderr,
                        "FAIL %s:%d: %s octave %d->%d ratio %.4f (want 2.0)\n",
                        __FILE__, __LINE__, noteNames[n], o, o + 1, ratio);
                return 1;
            }
        }
    }
    printf("PASS test_octave_doubling (%d notes × %d octaves checked)\n",
           NOTE_COUNT, MAX_OCTAVES);
    return 0;
}

static int test_note_string_valid(void) {
    /* A4 should render as "A4" */
    ASSERT_STREQ(getNoteString(A, 4), "A4");
    ASSERT_STREQ(getNoteString(C, 4), "C4");
    ASSERT_STREQ(getNoteString(Db, 3), "Db3");
    ASSERT_STREQ(getNoteString(B, 7), "B7");
    printf("PASS test_note_string_valid\n");
    return 0;
}

static int test_note_string_out_of_range(void) {
    ASSERT_NULL(getNoteString(-1, 4));
    ASSERT_NULL(getNoteString(NOTE_COUNT, 4));
    ASSERT_NULL(getNoteString(A, -1));
    ASSERT_NULL(getNoteString(A, MAX_OCTAVES));
    printf("PASS test_note_string_out_of_range\n");
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_known_frequencies();
    failed |= test_octave_doubling();
    failed |= test_note_string_valid();
    failed |= test_note_string_out_of_range();
    return failed;
}