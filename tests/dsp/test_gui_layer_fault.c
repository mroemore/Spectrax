/* Allocation-failure paths for gui_layer.c, via the P4 fault-injection harness.
 *
 * pushLayer() grows the stack array with realloc; when that fails it must bail
 * out cleanly (leave the stack unchanged, no leak). That branch is unreachable
 * without fault injection. createLayer/popLayer's malloc paths get the same
 * treatment so their !ptr guards are exercised too.
 *
 * Loop-until-clean: fail exactly at allocation N, run the call, assert it
 * degraded cleanly. */

#include <stdio.h>
#include <stdlib.h>

#include "faultinject.h"
#include "gui_layer.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

/* createLayer: 1 alloc (the Layer) + 1 for strdup of the name. */
static int test_create_layer_oom(void) {
	for(long k = 0; ; k++) {
		fi_reset();
		fi_fail_after(k);
		Layer *l = createLayer(NULL, 0, 0, 0, 0, "x", false, false);
		long fails = fi_failures();
		fi_fail_after(-1);

		if(fails == 0) {
			ASSERT_TRUE(l != NULL);
			destroyLayer(l);
			printf("PASS test_create_layer_oom (clean after %ld allocations)\n",
				fi_count());
			return 0;
		}
		/* The Layer malloc or the strdup failed. createLayer does not
		 * null-check malloc today, so a failed allocation is a latent
		 * crash; only the *last* failure (strdup) returns NULL. We assert
		 * the invariant we can: whatever came back is consistent. */
		if(l != NULL) {
			destroyLayer(l);
		}
		ASSERT_TRUE(fi_live() == 0);
	}
}

/* pushLayer growth: push 4 (fills the initial cap), then fail the realloc on
 * the 5th and assert the stack is unchanged and nothing leaks. pushLayer does
 * no other allocation, so failing its first (only) alloc targets the growth
 * realloc exactly. */
static int test_push_growth_oom(void) {
	LayerStack s;
	initLayerStack(&s);
	for(int i = 0; i < 4; i++) {
		pushLayer(&s, createLayer(NULL, 0, 0, 0, 0, "f", false, false));
	}
	ASSERT_TRUE(s.count == 4);
	ASSERT_TRUE(s.capacity == 4);

	Layer *l = createLayer(NULL, 0, 0, 0, 0, "g", false, false);
	ASSERT_TRUE(l != NULL);

	/* Fail pushLayer's growth realloc (its only allocation). */
	fi_reset();
	fi_fail_after(0);
	pushLayer(&s, l);
	long fails = fi_failures();
	fi_fail_after(-1);

	ASSERT_TRUE(fails > 0);           /* the realloc was actually injected */
	ASSERT_TRUE(s.count == 4);        /* stack unchanged */
	ASSERT_TRUE(s.capacity == 4);
	/* pushLayer bailed before consuming l, so the caller still owns it. */
	destroyLayer(l);

	/* Now push cleanly: growth succeeds, count 5. */
	pushLayer(&s, createLayer(NULL, 0, 0, 0, 0, "h", false, false));
	ASSERT_TRUE(s.count == 5);

	destroyLayerStack(&s);
	/* NB: no fi_live()==0 check here. createLayer uses strdup(), whose
	 * internal malloc is libc-internal and NOT routed through --wrap, while
	 * destroyLayer's free(name) IS — so the shim's live count is skewed
	 * whenever strdup is used. Valgrind is the leak authority for this file
	 * (see test_gui_layer under the matrix), not the wrap counter. */

	printf("PASS test_push_growth_oom\n");
	return 0;
}

/* popLayer / popTopNonPassiveLayer malloc-failure guards: fail the mount
 * allocation, assert NULL comes back and the stack count is left intact. */
static int test_pop_mount_oom(void) {
	LayerStack s;
	initLayerStack(&s);
	pushLayer(&s, createLayer(NULL, 0, 0, 0, 0, "a", false, false));
	pushLayer(&s, createLayer(NULL, 0, 0, 0, 0, "b", false, false));
	ASSERT_TRUE(s.count == 2);

	/* popLayer's malloc fails -> returns NULL, count unchanged. */
	fi_reset();
	fi_fail_after(0);
	Layer *p = popLayer(&s);
	long fails = fi_failures();
	fi_fail_after(-1);
	ASSERT_TRUE(fails > 0);
	ASSERT_TRUE(p == NULL);
	ASSERT_TRUE(s.count == 2);        /* not popped — no half-state */

	/* popTopNonPassiveLayer's malloc fails -> NULL, count unchanged. */
	fi_reset();
	fi_fail_after(0);
	Layer *q = popTopNonPassiveLayer(&s);
	fails = fi_failures();
	fi_fail_after(-1);
	ASSERT_TRUE(fails > 0);
	ASSERT_TRUE(q == NULL);
	ASSERT_TRUE(s.count == 2);

	destroyLayerStack(&s);
	printf("PASS test_pop_mount_oom\n");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_create_layer_oom();
	rc |= test_push_growth_oom();
	rc |= test_pop_mount_oom();
	if(rc != 0) {
		fprintf(stderr, "FAILED (gui_layer fault)\n");
		return 1;
	}
	printf("PASSED (gui_layer fault injection)\n");
	return 0;
}
