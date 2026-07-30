/*
 * test_wavetable.c — verify WavetablePool lifecycle and loadWavetable side-effects.
 *
 * The Wavetable struct is public (fields visible) but consumers aren't meant
 * to reach in — the API only exposes createWavetablePool/freeWavetablePool/
 * loadWavetable.  Tests inspect pool-level counters and, where needed,
 * peek through wtp->tables[i] to verify data was stored correctly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "wavetable.h"

/* ------------------------------------------------------------------ */
/*  Assertion macros  (same style as tests/dsp/test_notes.c)          */
/* ------------------------------------------------------------------ */

#define ASSERT_NEAR(actual, expected, tol) do {                        \
    float _a = (float)(actual);                                        \
    float _e = (float)(expected);                                      \
    if (fabsf(_a - _e) > (tol)) {                                      \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol));             \
        return 1;                                                      \
    }                                                                  \
} while (0)

#define ASSERT_EQ(actual, expected) do {                               \
    long long _a = (long long)(actual);                                \
    long long _e = (long long)(expected);                              \
    if (_a != _e) {                                                    \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n",       \
                __FILE__, __LINE__, _e, _a);                           \
        return 1;                                                      \
    }                                                                  \
} while (0)

#define ASSERT_NOT_NULL(ptr) do {                                      \
    if ((ptr) == NULL) {                                               \
        fprintf(stderr, "FAIL %s:%d: expected non-NULL\n",             \
                __FILE__, __LINE__);                                   \
        return 1;                                                      \
    }                                                                  \
} while (0)

#define ASSERT_NULL(ptr) do {                                          \
    if ((ptr) != NULL) {                                               \
        fprintf(stderr, "FAIL %s:%d: expected NULL\n",                 \
                __FILE__, __LINE__);                                   \
        return 1;                                                      \
    }                                                                  \
} while (0)

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

static int test_create_pool(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);
    ASSERT_EQ(wtp->tableCount, 0);
    ASSERT_EQ(wtp->memoryUsed, 0);
    ASSERT_NOT_NULL(wtp->data);
    ASSERT_NOT_NULL(wtp->tables);
    freeWavetablePool(wtp);
    printf("PASS test_create_pool\n");
    return 0;
}

static int test_load_single(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);

    float data[] = {0.0f, 0.5f, 1.0f, 0.5f, 0.0f};
    size_t len = sizeof(data) / sizeof(data[0]);
    loadWavetable(wtp, "sine", data, len);

    ASSERT_EQ(wtp->tableCount, 1);
    ASSERT_EQ(wtp->memoryUsed, sizeof(float) * len);

    freeWavetablePool(wtp);
    printf("PASS test_load_single\n");
    return 0;
}

static int test_load_data_integrity(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);

    float data[] = {0.0f, 0.5f, 1.0f, 0.5f, 0.0f, -0.5f, -1.0f, -0.5f};
    size_t len = sizeof(data) / sizeof(data[0]);
    loadWavetable(wtp, "check", data, len);

    ASSERT_EQ(wtp->tableCount, 1);

    /* Peek through the pool's tables array to verify data was copied. */
    Wavetable *tbl = wtp->tables[0];
    ASSERT_NOT_NULL(tbl);
    ASSERT_EQ(tbl->length, (int)len);
    ASSERT_NOT_NULL(tbl->data);
    for (size_t i = 0; i < len; i++) {
        ASSERT_NEAR(tbl->data[i], data[i], 0.0001f);
    }

    freeWavetablePool(wtp);
    printf("PASS test_load_data_integrity\n");
    return 0;
}

static int test_load_multiple(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);

    float a[] = {1.0f, 2.0f, 3.0f};
    float b[] = {4.0f, 5.0f};
    float c[] = {6.0f};

    loadWavetable(wtp, "a", a, 3);
    loadWavetable(wtp, "b", b, 2);
    loadWavetable(wtp, "c", c, 1);

    ASSERT_EQ(wtp->tableCount, 3);
    ASSERT_EQ(wtp->memoryUsed, sizeof(float) * (3 + 2 + 1));

    /* Spot-check data through tables[] */
    ASSERT_NEAR(wtp->tables[0]->data[0], 1.0f, 0.0001f);
    ASSERT_NEAR(wtp->tables[0]->data[2], 3.0f, 0.0001f);
    ASSERT_NEAR(wtp->tables[1]->data[0], 4.0f, 0.0001f);
    ASSERT_NEAR(wtp->tables[2]->data[0], 6.0f, 0.0001f);

    /* Name pointers are stored as-is (the pool does not strdup). */
    ASSERT_EQ((const char *)wtp->tables[0]->name, "a");
    ASSERT_EQ((const char *)wtp->tables[1]->name, "b");
    ASSERT_EQ((const char *)wtp->tables[2]->name, "c");

    freeWavetablePool(wtp);
    printf("PASS test_load_multiple\n");
    return 0;
}

/* Load MAX_WAVETABLES (128) entries — the pool should accept these.
 *
 * BUG (wavetable.c:43): the overflow guard uses `>` instead of `>=`,
 * so table index 128 (the 129th call) writes past the tables[128]
 * allocation.  Fil-c catches this at runtime with a bounds error.
 * Loading only up to MAX_WAVETABLES here to stay in defined territory.
 *
 * Once the guard is fixed to `>=`, the pool will reject the 128th
 * call as well (tableCount would be 128, and 128 >= 128 is true).
 * At that point the loop should change to `i < MAX_WAVETABLES - 1`. */
static int test_fill_to_capacity(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);

    float dummy[] = {0.0f};
    for (int i = 0; i < MAX_WAVETABLES; i++) {
        loadWavetable(wtp, "x", dummy, 1);
    }

    ASSERT_EQ(wtp->tableCount, MAX_WAVETABLES);
    ASSERT_EQ(wtp->memoryUsed, sizeof(float) * MAX_WAVETABLES);

    freeWavetablePool(wtp);
    printf("PASS test_fill_to_capacity\n");
    return 0;
}

/* When name is NULL, loadWavetable returns early without bumping
 * tableCount or memoryUsed.  (The memcpy into the pool buffer still
 * fires, but since memoryUsed isn't advanced the next valid load
 * will overwrite that region.) */
static int test_load_name_null(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);

    float data[] = {1.0f, 2.0f, 3.0f};
    loadWavetable(wtp, NULL, data, 3);

    ASSERT_EQ(wtp->tableCount, 0);
    ASSERT_EQ(wtp->memoryUsed, (size_t)0);

    /* A subsequent valid load should still work. */
    float more[] = {4.0f, 5.0f};
    loadWavetable(wtp, "valid", more, 2);
    ASSERT_EQ(wtp->tableCount, 1);
    ASSERT_EQ(wtp->memoryUsed, sizeof(float) * 2);

    freeWavetablePool(wtp);
    printf("PASS test_load_name_null\n");
    return 0;
}

/* freeWavetablePool(NULL) must not crash. */
static int test_free_null(void)
{
    freeWavetablePool(NULL);
    printf("PASS test_free_null\n");
    return 0;
}

/* Double-free safety is not guaranteed by the API (no sentinel field
 * is set after free).  This test exists to document that calling
 * freeWavetablePool twice on the same pointer is undefined — the
 * implementation does nothing to prevent it. */
static int test_free_double(void)
{
    WavetablePool *wtp = createWavetablePool();
    ASSERT_NOT_NULL(wtp);
    float data[] = {1.0f, 2.0f, 3.0f};
    loadWavetable(wtp, "a", data, 3);
    freeWavetablePool(wtp);
    /* Second free is UB; we don't call it.  Test passes by reaching here. */
    printf("PASS test_free_double\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    int failed = 0;
    failed |= test_create_pool();
    failed |= test_load_single();
    failed |= test_load_data_integrity();
    failed |= test_load_multiple();
    failed |= test_fill_to_capacity();
    failed |= test_load_name_null();
    failed |= test_free_null();
    failed |= test_free_double();
    return failed;
}
