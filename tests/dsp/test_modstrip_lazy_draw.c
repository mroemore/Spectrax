/* The lazy half of I41: with a real GL context, the ping/pong pair is
 * allocated on first use, survives a draw, and is released exactly once.
 *
 * The headless guarantee (construction touches no GL) is test_modstrip_lazy.c.
 * This one is under Xvfb because it is doing the GPU work it is testing. */

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
	InitWindow(640, 480, "spx-modstrip-lazy");
	if(!IsWindowReady()) {
		fprintf(stderr, "FAIL: no window; this test must run under Xvfb\n");
		return 1;
	}

	ModStrip ms;
	memset(&ms, 0xFF, sizeof(ms));
	initModStrip(&ms, NULL, 0, 640, 100);
	ASSERT_TRUE(!ms.loaded, "construction stays unloaded");

	/* updateModStripTexture is the first toucher in the real app: main.c
	 * calls updateModStripTextures() at the top of every frame, before any
	 * draw. It must allocate on this call. */
	updateModStripTexture(&ms);
	ASSERT_TRUE(ms.loaded, "updateModStripTexture allocates on first call");
	ASSERT_TRUE(ms.ping.texture.id != 0, "ping texture has a real GL id");
	ASSERT_TRUE(ms.pong.texture.id != 0, "pong texture has a real GL id");
	ASSERT_TRUE(ms.pingActive, "the update flipped pingActive");

	/* Second call must reuse, not reallocate -- otherwise every frame would
	 * leak a framebuffer. */
	int ping_id = ms.ping.texture.id;
	int pong_id = ms.pong.texture.id;
	updateModStripTexture(&ms);
	ASSERT_TRUE(ms.ping.texture.id == ping_id, "second update reuses ping");
	ASSERT_TRUE(ms.pong.texture.id == pong_id, "second update reuses pong");
	ASSERT_TRUE(!ms.pingActive, "the update toggled back");

	/* Presenting must work, including for a strip the update loop never
	 * touched -- that is the backstop in drawModStrip. */
	ModStrip cold;
	memset(&cold, 0xFF, sizeof(cold));
	initModStrip(&cold, NULL, 0, 320, 100);
	BeginDrawing();
	ClearBackground(BLACK);
	drawModStrip(&cold, (Rectangle){ 0, 0, 320, 100 });
	EndDrawing();
	ASSERT_TRUE(cold.loaded, "drawModStrip allocates a strip that was never updated");

	BeginDrawing();
	ClearBackground(BLACK);
	drawModStrip(&ms, (Rectangle){ 0, 0, 640, 100 });
	EndDrawing();

	/* Teardown releases the pair and resets the flag, so freeModStrip stays
	 * idempotent and a freed strip can be re-initialised. */
	freeModStrip(&ms);
	ASSERT_TRUE(!ms.loaded, "freeModStrip clears loaded");
	ASSERT_TRUE(ms.ping.texture.id == 0, "freeModStrip zeroes the texture ids");
	freeModStrip(&ms);
	freeModStrip(&cold);
	freeModStrip(NULL);

	/* And a freed strip can go round again. */
	initModStrip(&ms, NULL, 0, 640, 100);
	updateModStripTexture(&ms);
	ASSERT_TRUE(ms.loaded, "re-initialised strip loads again");
	freeModStrip(&ms);

	CloseWindow();
	printf("PASSED (modstrip lazy load under Xvfb)\n");
	return 0;
}