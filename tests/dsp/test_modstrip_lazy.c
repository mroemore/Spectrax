/* ModStrip construction must be GL-free.
 *
 * initModStrip used to call LoadRenderTexture eagerly, which dispatches
 * through rlgl's function-pointer table -- NULL before InitWindow, so it
 * segfaulted through address zero. That made every caller that builds a
 * ModStripGuiNode need a live GL context, which is what forced
 * test_gui_instrument to run under Xvfb for no reason (TESTING.md I41).
 *
 * This test is the regression guard, and it is deliberately headless: if
 * anyone makes initModStrip eager again, this binary segfaults with no
 * display and no wrapper, which is exactly the signal we want.
 *
 * The other half -- that the lazy load actually happens and the strip draws
 * correctly once a context exists -- is test_modstrip_lazy_draw.c. */

#include <stdio.h>
#include <string.h>

#include "vizfx.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

int main(void) {
	ModStrip ms;
	memset(&ms, 0xFF, sizeof(ms));    /* poison, so init must set every field */
	initModStrip(&ms, NULL, 0, 640, 100);

	ASSERT_TRUE(!ms.loaded, "initModStrip must not allocate GPU state");
	ASSERT_TRUE(ms.width == 640, "initModStrip records width");
	ASSERT_TRUE(ms.height == 100, "initModStrip records height");
	ASSERT_TRUE(!ms.pingActive, "initModStrip leaves pingActive false");
	ASSERT_TRUE(ms.ping.texture.id == 0, "ping texture is zeroed, not garbage");
	ASSERT_TRUE(ms.pong.texture.id == 0, "pong texture is zeroed, not garbage");

	/* freeModStrip on a never-loaded strip must be a no-op, not a crash:
	 * UnloadRenderTexture dereferences rlgl state, so this is the guard that
	 * makes lazy construction symmetric with teardown. */
	freeModStrip(&ms);
	ASSERT_TRUE(!ms.loaded, "freeModStrip on an unloaded strip is a no-op");

	/* Idempotent teardown. */
	freeModStrip(&ms);
	freeModStrip(NULL);

	printf("PASSED (modstrip construction is GL-free)\n");
	return 0;
}