/* test_modsystem.c — modsystem unit tests: creation, processing, and the
 * dynamic add/remove/rewire primitives. Pure modsystem, no raylib. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "modsystem.h"

#define ASSERT_EQ(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
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

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

/* Teardown: params owned by paramList (freeParamList frees them all,
 * including mod output params); mod structs owned by modList, freed with
 * bare free(). Mods already removed via removeMod() are already freed and
 * no longer referenced by either list, so this never double-frees. */
static void teardown(ParamList *pl, ModList *ml) {
    if (ml) {
        for (int i = 0; i < ml->count; i++) {
            free(ml->mods[i]);
        }
        free(ml);
    }
    if (pl) {
        freeParamList(pl);
    }
}

static int test_create_lists(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    ASSERT_TRUE(pl != NULL, "createParamList");
    ASSERT_TRUE(ml != NULL, "createModList");
    ASSERT_EQ(pl->count, 0);
    ASSERT_EQ(ml->count, 0);
    teardown(pl, ml);
    printf("PASS test_create_lists\n");
    return 0;
}

int main(void) {
    initModSystem();
    int fails = 0;
    fails += test_create_lists();
    if (fails) {
        fprintf(stderr, "%d modsystem test(s) failed\n", fails);
        return 1;
    }
    printf("ALL modsystem tests passed\n");
    return 0;
}