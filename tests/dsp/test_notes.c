/* Boundary tests for notes.c. getNoteString builds "<name><octave>" into a
 * shared static buffer and returns NULL for out-of-range inputs. */

#include <stdio.h>

#include "notes.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static int test_valid_notes(void) {
	ASSERT_TRUE(strcmp(getNoteString(C, 4), "C4") == 0);
	ASSERT_TRUE(strcmp(getNoteString(Db, 8), "Db8") == 0);
	ASSERT_TRUE(strcmp(getNoteString(B, 0), "B0") == 0);
	ASSERT_TRUE(strcmp(getNoteString(A, 4), "A4") == 0);
	printf("PASS test_valid_notes\n");
	return 0;
}

static int test_note_bounds(void) {
	ASSERT_TRUE(getNoteString(0, 4) != NULL);              /* C, low edge */
	ASSERT_TRUE(getNoteString(NOTE_COUNT - 1, 4) != NULL); /* B, high edge */
	ASSERT_TRUE(getNoteString(-1, 4) == NULL);             /* below range */
	ASSERT_TRUE(getNoteString(NOTE_COUNT, 4) == NULL);     /* above range */
	printf("PASS test_note_bounds\n");
	return 0;
}

static int test_octave_bounds(void) {
	ASSERT_TRUE(getNoteString(C, 0) != NULL);
	ASSERT_TRUE(getNoteString(C, MAX_OCTAVES - 1) != NULL);
	ASSERT_TRUE(getNoteString(C, -1) == NULL);
	ASSERT_TRUE(getNoteString(C, MAX_OCTAVES) == NULL);
	printf("PASS test_octave_bounds\n");
	return 0;
}

static int test_static_buffer_reuse(void) {
	char *first = getNoteString(C, 4);
	ASSERT_TRUE(strcmp(first, "C4") == 0);
	char *second = getNoteString(D, 5);
	/* Documented aliasing: same static buffer, so 'first' now reads "D5". */
	ASSERT_TRUE(first == second);
	ASSERT_TRUE(strcmp(second, "D5") == 0);
	printf("PASS test_static_buffer_reuse\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_valid_notes();
	failed |= test_note_bounds();
	failed |= test_octave_bounds();
	failed |= test_static_buffer_reuse();

	printf("\n%s (notes)\n", failed ? "FAILED" : "PASSED");
	return failed;
}
