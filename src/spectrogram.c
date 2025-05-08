#include "spectrogram.h"
#include "raylib.h"
#include <math.h>

void initSpectro(Spectro *sp, int fftSize, int framesPerBuffer, int toAverage, float imageScale) {
	initFFT(&sp->fft, fftSize, framesPerBuffer, toAverage);
	sp->enabled = true;
	sp->imageWriteIndex = 0;
	sp->imageScale = imageScale;
	sp->imageWidth = 256;
	sp->imageHeight = sp->fft.freqCount;
	sp->specImage = GenImageColor(sp->imageWidth, sp->imageHeight, (Color){ 0, 0, 0, 100 });
	sp->specTexture = LoadTextureFromImage(sp->specImage);
	sp->textureDestination = (Rectangle){ 0, 0, 1024, 512 };
}

void updateSpectroImageData(Spectro *sp) {
	if(sp->fft.prevRowCount < sp->fft.rowCount) { // there's new data to write
		Color *col = (Color *)sp->specImage.data;
		for(int r = sp->fft.prevRowCount; r < sp->fft.rowCount; r++) {
			for(int c = 0; c < sp->fft.freqCount; c++) {
				const double pi = 3.14159265358979;
				float tmpCol = sp->fft.vals[r * sp->fft.freqCount + c] * pi;
				col[sp->imageWriteIndex + sp->imageWidth * c] = (Color){
					(int)(200.0f * fabs(sin(5.0f / 2.0f * tmpCol))),
					//(int)(200.0f * sin(tmpCol)),
					(int)(200.0f * fabs(sin(3.0f / 2.0f * tmpCol))),
					50,
					150
				};
			}
			sp->imageWriteIndex++;
			if(sp->imageWriteIndex > sp->imageWidth) {
				sp->imageWriteIndex -= sp->imageWidth;
			}
		}
		sp->fft.prevRowCount = sp->fft.rowCount;
		UnloadTexture(sp->specTexture);
		sp->specTexture = LoadTextureFromImage(sp->specImage);
	}
}

void drawSpectro(Spectro *sp) {
	DrawTexturePro(sp->specTexture, (Rectangle){ 0, 0, sp->imageWidth, sp->imageHeight }, sp->textureDestination, (Vector2){ 0, 0 }, 0.0f, WHITE);
	DrawLine(sp->textureDestination.x + 1 + sp->imageWriteIndex * 4, sp->textureDestination.y, sp->textureDestination.x + 1 + sp->imageWriteIndex * 4, sp->textureDestination.y + sp->imageHeight, RED);
	char debugData[255];
	sprintf(debugData, "Window: %s, writeIndex: %i, rowcount: %i", sp->fft.windowFuncName, sp->imageWriteIndex, sp->fft.rowCount);
	DrawText(debugData, 14, SCREEN_H - 14, 12, WHITE);
}
