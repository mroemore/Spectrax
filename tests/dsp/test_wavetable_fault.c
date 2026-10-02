/* Loop-until-clean OOM injection for wavetable.c.
 *
 * For every allocation the module makes, fail exactly there and confirm the
 * module degrades cleanly: no crash, no partially-live pool, no leak. Built
 * with -Wl,--wrap=malloc,... so the module's allocator calls are redirected
 * (see tests/support/faultinject.c).
 *
 * The wrap shims deliberately do not touch libc-internal allocations, and are
 * disabled before the process exits so profiling teardown is unaffected. */

#include <stdio.h>

#include "wavetable.h"
#include "faultinject.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static float g_data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

/* createWavetablePool makes 3 allocations. Failing any one of them must
 * return NULL with every already-allocated piece freed. */
static int test_create_oom(void) {
	for(long k = 0; k < 64; k++) {
		fi_reset();
		fi_fail_after(k);

		WavetablePool *wtp = createWavetablePool();
		long fails = fi_failures();
		fi_fail_after(-1);

		if(wtp == NULL) {
			ASSERT_TRUE(fails > 0);      /* NULL only because we injected */
			ASSERT_TRUE(fi_live() == 0); /* partial allocations were freed */
		} else {
			ASSERT_TRUE(wtp->data != NULL);
			ASSERT_TRUE(wtp->tables != NULL);
			freeWavetablePool(wtp);
			ASSERT_TRUE(fi_live() == 0);
			ASSERT_TRUE(fails == 0);     /* reached on the clean run */
			printf("PASS test_create_oom (clean after %ld allocations)\n", fi_count());
			return 0;
		}
	}
	fprintf(stderr, "FAIL test_create_oom: never reached a clean create\n");
	return 1;
}

/* loadWavetable makes 1 allocation (the Wavetable struct). Failing it must
 * leave the pool untouched. */
static int test_load_oom(void) {
	for(long k = 0; k < 64; k++) {
		fi_reset();
		WavetablePool *wtp = createWavetablePool();
		fi_reset(); /* count load's allocations from zero */
		fi_fail_after(k);

		loadWavetable(wtp, "t", g_data, 4);
		long fails = fi_failures();
		fi_fail_after(-1);

		if(fails == 0) {
			ASSERT_TRUE(wtp->tableCount == 1);
			freeWavetablePool(wtp);
			ASSERT_TRUE(fi_live() == 0);
			printf("PASS test_load_oom (clean after %ld allocations)\n", fi_count());
			return 0;
		}
		ASSERT_TRUE(wtp->tableCount == 0); /* OOM => load refused */
		ASSERT_TRUE(wtp->memoryUsed == 0);
		freeWavetablePool(wtp);
		ASSERT_TRUE(fi_live() == 0);
	}
	fprintf(stderr, "FAIL test_load_oom: never reached a clean load\n");
	return 1;
}

static int test_free_null(void) {
	fi_reset();
	freeWavetablePool(NULL); /* documented no-op */
	ASSERT_TRUE(fi_live() == 0);
	printf("PASS test_free_null\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_create_oom();
	failed |= test_load_oom();
	failed |= test_free_null();

	printf("\n%s (wavetable fault injection)\n", failed ? "FAILED" : "PASSED");
	return failed;
}
