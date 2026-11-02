/* OOM injection for gui_arranger.c's two node factories.
 *
 * createInstChipGuiNode and createArrangerCellGuiNode each have two failure
 * points: the malloc, and initGuiNode returning false. Both must yield NULL,
 * not a half-built node and not a leak. Unreachable without --wrap.
 *
 * Built with -Wl,--wrap=malloc (see tests/meson.build). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "graph_gui.h"
#include "sequencer.h"
#include "gui_internal.h"
#include "faultinject.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

static int test_chip_node_oom(void) {
	Arranger a;
	memset(&a, 0, sizeof(a));
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			a.song[ch][r] = -1;
		}
		a.playhead_indices[ch] = -1;
	}

	int saw_oom = 0;
	for(long k = 0; k < 8; k++) {
		fi_reset();
		fi_fail_after(k);
		GuiNode *n = createInstChipGuiNode(0, 0, 40, 20, false, NULL, 0, &a);
		long fails = fi_failures();
		fi_fail_after(-1);
		if(fails == 0) {
			/* Past the last allocation: the clean build must succeed. */
			ASSERT_TRUE(n != NULL, "createInstChipGuiNode succeeds when memory is available");
			freeGuiNode(n);
			break;
		}
		saw_oom = 1;
		/* Allocation failed, so the factory must report failure. */
		if(n != NULL) {
			fprintf(stderr, "FAIL alloc #%ld failed but a chip node was returned\n", k);
			freeGuiNode(n);
			return 1;
		}
	}
	ASSERT_TRUE(saw_oom, "chip factory has at least one reachable allocation failure");
	fi_reset();
	printf("PASS test_chip_node_oom\n");
	return 0;
}

static int test_cell_node_oom(void) {
	Arranger a;
	memset(&a, 0, sizeof(a));

	int saw_oom = 0;
	for(long k = 0; k < 8; k++) {
		fi_reset();
		fi_fail_after(k);
		GuiNode *n = createArrangerCellGuiNode(0, 0, 12, 12, false, &a, 0, 0);
		long fails = fi_failures();
		fi_fail_after(-1);
		if(fails == 0) {
			ASSERT_TRUE(n != NULL, "createArrangerCellGuiNode succeeds when memory is available");
			freeGuiNode(n);
			break;
		}
		saw_oom = 1;
		if(n != NULL) {
			fprintf(stderr, "FAIL alloc #%ld failed but a cell node was returned\n", k);
			freeGuiNode(n);
			return 1;
		}
	}
	ASSERT_TRUE(saw_oom, "cell factory has at least one reachable allocation failure");
	fi_reset();
	printf("PASS test_cell_node_oom\n");
	return 0;
}

int main(void) {
	if(test_chip_node_oom() != 0) {
		return 1;
	}
	if(test_cell_node_oom() != 0) {
		return 1;
	}
	printf("PASSED (gui_arranger fault injection)\n");
	return 0;
}