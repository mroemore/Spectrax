#include <stdio.h>
#include "raylib.h"
#include "vizulobe.h"

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
