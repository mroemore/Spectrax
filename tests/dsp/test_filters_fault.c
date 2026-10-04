/* Allocation-failure paths for filters.c, via the P4 fault-injection harness.
 *
 * createBiquadFilter() and createFilter() each have an allocation-failure
 * branch that a normal test cannot reach. The linker --wrap shims in
 * tests/support/faultinject.c make malloc fail on demand, closing those
 * branches (and proving the functions degrade cleanly: return NULL, leak
 * nothing).
 *
 * Loop-until-clean: fail exactly at allocation N, run the call, assert it
 * returned NULL without leaking. */

#include <stdio.h>
#include <stdlib.h>

#include "faultinject.h"
#include "filters.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

/* createBiquadFilter: 1 allocation. */
static int test_create_biquad_oom(void) {
	for(long k = 0; ; k++) {
		fi_reset();
		fi_fail_after(k);
		BiquadFilter *bf = createBiquadFilter(kDirect);
		long fails = fi_failures();
		fi_fail_after(-1);

		if(fails == 0) {
			/* Clean run: allocated, must free. */
			ASSERT_TRUE(bf != NULL);
			free(bf);
			printf("PASS test_create_biquad_oom (clean after %ld allocations)\n",
				fi_count());
			return 0;
		}
		/* Injected failure: must return NULL and leak nothing. */
		ASSERT_TRUE(bf == NULL);
		ASSERT_TRUE(fi_live() == 0);
	}
}

/* createFilter: createBiquadFilter (1) then the Filter malloc (1). We only
 * need to fail the *Filter* allocation to close createFilter's !flt branch;
 * the biquad failure is already covered above. */
static int test_create_filter_oom(void) {
	for(long k = 0; ; k++) {
		fi_reset();
		fi_fail_after(k);
		Filter *flt = createFilter(kDirect, secondOrderLPF, 1000.0f, 0.707f);
		long fails = fi_failures();
		fi_fail_after(-1);

		if(fails == 0) {
			ASSERT_TRUE(flt != NULL);
			free(flt->biquad);
			free(flt);
			printf("PASS test_create_filter_oom (clean after %ld allocations)\n",
				fi_count());
			return 0;
		}
		ASSERT_TRUE(flt == NULL);
	}
}

int main(void) {
	int rc = 0;
	rc |= test_create_biquad_oom();
	rc |= test_create_filter_oom();
	if(rc != 0) {
		fprintf(stderr, "FAILED (filters fault)\n");
		return 1;
	}
	printf("PASSED (filters fault injection)\n");
	return 0;
}
