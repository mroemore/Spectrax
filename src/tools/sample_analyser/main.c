#include <stdio.h>
#include <math.h>
#include <time.h>

#include "raylib.h"
#include "io.h"
#include "dataviz.h"
#include "sample.h"

int main(void) {
	const int screenWidth = SCREEN_W;
	const int screenHeight = SCREEN_H;
	SamplePool *sp = createSamplePool();
	loadSamplesfromDirectory(".", sp);
	if(sp->sampleCount <= 0) {
		printf("NO WAVS!\n");
		return -1;
	}
	Spectrogram *spectrogram;
	int sampleIndex = 0;

	InitWindow(screenWidth, screenHeight, "sample_analyser");
	SetTargetFPS(60);
	while(!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(BLACK);
		DrawFPS(5, 5);

		EndDrawing();
	}
}
