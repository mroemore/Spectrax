#include <stdio.h>
#include "raylib.h"

#define VIZ_SCREEN_W 1280
#define VIZ_SCREEN_H 800

int main(void) {
	SetTraceLogLevel(LOG_WARNING);
	InitWindow(VIZ_SCREEN_W, VIZ_SCREEN_H, "vizulobe");
	SetTargetFPS(60);

	while(!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(BLACK);
		DrawText("vizulobe stub", 20, 20, 20, WHITE);
		EndDrawing();
	}

	CloseWindow();
	return 0;
}
