/* Allocation-failure path for createSongMinimapGui (gui_pattern.c).
 *
 * createSongMinimapGui mallocs the minimap and used to dereference the result
 * without a null check, so an allocation failure was a crash rather than a
 * NULL return. That branch is only reachable through fault injection.
 *
 * NB: no fi_live() assertion here — initDefaultColourScheme allocates outside
 * the wrapped set in some configs, so the shim's live counter is not
 * authoritative. Valgrind is the leak authority for this file. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "faultinject.h"
#include "gui.h"
#include "gui_internal.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static int test_create_minimap_oom(void) {
	initDefaultColourScheme(&cs);
	static Arranger arranger;
	memset(&arranger, 0, sizeof(arranger));
	static int songIndex[2] = { 0, 0 };

	for(long k = 0; ; k++) {
		fi_reset();
		fi_fail_after(k);
		SongMinimapGui *mg = createSongMinimapGui(&arranger, songIndex, 4, 4);
		long fails = fi_failures();
		fi_fail_after(-1);

		if(fails == 0) {
			ASSERT_TRUE(mg != NULL);
			ASSERT_TRUE(mg->arranger == &arranger);
			free(mg);
			printf("PASS test_create_minimap_oom (clean after %ld allocations)\n",
				fi_count());
			return 0;
		}
		/* Allocation failed: must return NULL, not dereference it. */
		ASSERT_TRUE(mg == NULL);
	}
}

int main(void) {
	int rc = 0;
	rc |= test_create_minimap_oom();
	if(rc != 0) {
		fprintf(stderr, "FAILED (gui_pattern fault)\n");
		return 1;
	}
	printf("PASSED (gui_pattern fault injection)\n");
	return 0;
}
