/* Boundary tests for wavetable.c. The pool owns a fixed arena
 * (MAX_WTPOOL_BYTES) carved by successive loadWavetable() calls, plus a fixed
 * table-pointer array (MAX_WAVETABLES). These tests pin the edges: exact
 * capacity, exact arena fill, and rejection just past each limit. */

#include <stdio.h>

#include "wavetable.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static int test_create_free(void) {
	WavetablePool *wtp = createWavetablePool();
	ASSERT_TRUE(wtp != NULL);
	ASSERT_TRUE(wtp->tableCount == 0);
	ASSERT_TRUE(wtp->memoryUsed == 0);
	freeWavetablePool(wtp);
	printf("PASS test_create_free\n");
	return 0;
}

static int test_load_one(void) {
	WavetablePool *wtp = createWavetablePool();
	char name[] = "sine";
	float in[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

	loadWavetable(wtp, name, in, 4);

	ASSERT_TRUE(wtp->tableCount == 1);
	ASSERT_TRUE(wtp->memoryUsed == sizeof(float) * 4);
	ASSERT_TRUE(wtp->tables[0] != NULL);
	ASSERT_TRUE(wtp->tables[0]->name == name);
	ASSERT_TRUE(wtp->tables[0]->length == 4);
	/* First load lands at the start of the arena. */
	ASSERT_TRUE((char *)wtp->tables[0]->data == wtp->data);
	ASSERT_TRUE(wtp->tables[0]->data[0] == 1.0f);
	ASSERT_TRUE(wtp->tables[0]->data[3] == 4.0f);

	freeWavetablePool(wtp);
	printf("PASS test_load_one\n");
	return 0;
}

static int test_arena_packing(void) {
	WavetablePool *wtp = createWavetablePool();
	float a[3] = { 1.0f, 2.0f, 3.0f };
	float b[2] = { 4.0f, 5.0f };

	loadWavetable(wtp, "a", a, 3);
	loadWavetable(wtp, "b", b, 2);

	ASSERT_TRUE(wtp->tableCount == 2);
	ASSERT_TRUE(wtp->memoryUsed == sizeof(float) * 5);
	/* Second table is packed immediately after the first. */
	ASSERT_TRUE((char *)wtp->tables[1]->data == wtp->data + sizeof(float) * 3);
	ASSERT_TRUE(wtp->tables[1]->data[0] == 4.0f);
	ASSERT_TRUE(wtp->tables[1]->data[1] == 5.0f);

	freeWavetablePool(wtp);
	printf("PASS test_arena_packing\n");
	return 0;
}

/* Regression for TESTING.md I5: the old check was '>' so the (MAX+1)th load
 * wrote tables[MAX] out of bounds. Filling exactly to MAX must succeed and the
 * next load must be refused. */
static int test_capacity_boundary(void) {
	WavetablePool *wtp = createWavetablePool();
	float in[1] = { 0.5f };

	for(int i = 0; i < MAX_WAVETABLES; i++) {
		loadWavetable(wtp, "t", in, 1);
	}
	ASSERT_TRUE(wtp->tableCount == MAX_WAVETABLES);
	ASSERT_TRUE(wtp->memoryUsed == (size_t)MAX_WAVETABLES * sizeof(float));

	loadWavetable(wtp, "t", in, 1); /* must be rejected, not overflow */
	ASSERT_TRUE(wtp->tableCount == MAX_WAVETABLES);
	ASSERT_TRUE(wtp->memoryUsed == (size_t)MAX_WAVETABLES * sizeof(float));

	freeWavetablePool(wtp);
	printf("PASS test_capacity_boundary\n");
	return 0;
}

/* Fill the arena to exactly MAX_WTPOOL_BYTES, then confirm the next load is
 * refused (the old code had no arena guard at all). */
static int test_arena_boundary(void) {
	enum { PER_BYTES = MAX_WTPOOL_BYTES / 4 };        /* exact quarter */
	enum { PER_FLOATS = PER_BYTES / sizeof(float) };  /* 100000 */
	static float big[PER_FLOATS];

	WavetablePool *wtp = createWavetablePool();
	for(int i = 0; i < 4; i++) {
		loadWavetable(wtp, "t", big, PER_FLOATS);
	}
	ASSERT_TRUE(wtp->tableCount == 4);
	ASSERT_TRUE(wtp->memoryUsed == (size_t)MAX_WTPOOL_BYTES);

	loadWavetable(wtp, "t", big, PER_FLOATS); /* would exceed the arena */
	ASSERT_TRUE(wtp->tableCount == 4);
	ASSERT_TRUE(wtp->memoryUsed == (size_t)MAX_WTPOOL_BYTES);

	freeWavetablePool(wtp);
	printf("PASS test_arena_boundary\n");
	return 0;
}

static int test_null_name_rejected(void) {
	WavetablePool *wtp = createWavetablePool();
	float in[2] = { 1.0f, 2.0f };

	loadWavetable(wtp, NULL, in, 2);

	ASSERT_TRUE(wtp->tableCount == 0);
	ASSERT_TRUE(wtp->memoryUsed == 0); /* name-null path returns before accounting */

	freeWavetablePool(wtp);
	printf("PASS test_null_name_rejected\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_create_free();
	failed |= test_load_one();
	failed |= test_arena_packing();
	failed |= test_capacity_boundary();
	failed |= test_arena_boundary();
	failed |= test_null_name_rejected();

	printf("\n%s (wavetable)\n", failed ? "FAILED" : "PASSED");
	return failed;
}
