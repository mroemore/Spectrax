#include "dataviz.h"
#include "fft.h"
#include "raylib.h"
#include <math.h>
#include <stdlib.h>

void initDataVisualisation(DataVisualisation *dv, Rectangle imgProps, Rectangle imgMask, Rectangle texProps) {
	dv->enabled = false;
	dv->imageWriteIndex = 0;
	dv->outImage = GenImageColor(imgProps.width, imgProps.height, (Color){ 0, 0, 0, 100 });
	dv->outTexture = LoadTextureFromImage(dv->outImage);
	dv->imgProps = imgProps;
	dv->imgMask = imgMask;
	dv->texProps = texProps;
}

void initSpectrogram(Spectrogram *sp, int fftSize, int framesPerBuffer, int toAverage, float imageScale) {
	initFFT(&sp->fft, fftSize, framesPerBuffer, toAverage, true, false);
	initDataVisualisation(&sp->dv, (Rectangle){ 0, 0, 512, fftSize / 2.0 }, (Rectangle){ 0, 0, 512, fftSize / 2.0 }, (Rectangle){ 0, 0, 1024, 512 });
	sp->cutoffFreq = 5000;
	sp->dv.draw = drawSpectrogram;
	sp->dv.update = updateSpectrogramData;
	sp->dv.toggle = toggleSpectrogram;
}

void updateSpectrogramData(void *self) {
	Spectrogram *sp = (Spectrogram *)self;

	Color *col = (Color *)sp->dv.outImage.data;
	const double pi = 3.14159265358979;

	if(sp->fft.prevRowCount < sp->fft.rowCount) { // there's new data to write. rowcount is updated elsewhere, when new data of size sizeof(float)* fft.fftSize is added to fft.vals
		for(int r = sp->fft.prevRowCount; r < sp->fft.rowCount; r++) {
			for(int c = 0; c < sp->dv.imgProps.height; c++) {
				int dataIndex = (c / sp->dv.imgProps.height) * sp->fft.freqCount;
				// float value = sp->fft.vals[dataIndex] * pi;
				float tmpCol = sp->fft.vals[r * sp->fft.freqCount + dataIndex] * pi;
				col[sp->dv.imageWriteIndex + (int)sp->dv.imgProps.width * c] = (Color){
					(int)(200.0f * fabs(sin(3.0f / 2.0f * tmpCol))),
					(int)(200.0f * fabs(sin(3.0f / 2.0f * tmpCol))),
					50,
					150
				};
			}
			sp->dv.imageWriteIndex++;
			if(sp->dv.imageWriteIndex > sp->dv.imgProps.width) {
				sp->dv.imageWriteIndex -= sp->dv.imgProps.width;
			}
		}
		sp->fft.prevRowCount = sp->fft.rowCount;
		UnloadTexture(sp->dv.outTexture);
		sp->dv.outTexture = LoadTextureFromImage(sp->dv.outImage);
	}
}

void drawSpectrogram(void *self) {
	Spectrogram *sp = (Spectrogram *)self;
	if(!sp->dv.enabled) {
		return;
	}
	DrawTexturePro(sp->dv.outTexture, sp->dv.imgProps, sp->dv.texProps, (Vector2){ 0, 0 }, 0.0f, WHITE);
	// DrawLine(sp->dv.texProps.x + 1 + sp->dv.imageWriteIndex, sp->dv.texProps.y, sp->dv.texProps.x + 1 + sp->dv.imageWriteIndex, sp->dv.texProps.y + sp->dv.texProps.height, RED);
	char debugData[255];
	sprintf(debugData, "Window: %s, writeIndex: %i, rowcount: %i", sp->fft.windowFuncName, sp->dv.imageWriteIndex, sp->fft.rowCount);
	DrawText(debugData, 14, SCREEN_H - 14, 12, WHITE);
}

void toggleSpectrogram(void *self) {
	Spectrogram *sp = (Spectrogram *)self;
	sp->dv.enabled = !sp->dv.enabled;
	toggleFFTProcessing(&sp->fft);
}

void initTimeGraph(TimeGraph *tg, float maxDataPoints, int x, int y, int width, int height) {
	tg->timeData = calloc(maxDataPoints, sizeof(double));
	tg->maxData = maxDataPoints;
	tg->prevWriteIndex = 0;
	tg->writeIndex = 0;
	tg->clampPoint = 44100.0 / 256.0;
	initDataVisualisation(&tg->dv, (Rectangle){ 0, 0, 128, maxDataPoints }, (Rectangle){ 0, 0, 128, maxDataPoints }, (Rectangle){ x, y, width, height });
	tg->dv.draw = drawTimeGraph;
	tg->dv.update = updateTimeGraphData;
	tg->dv.toggle = toggleTimeGraph;
}

void pushTimeGraphMeasurement(TimeGraph *tg, double measurement) {
	if(!tg->dv.enabled) {
		return;
	}
	tg->timeData[tg->writeIndex] = measurement;
	tg->writeIndex++;
	if(tg->writeIndex > tg->maxData) {
		tg->writeIndex -= tg->maxData;
	}
}

void updateTimeGraphData(void *self) {
	TimeGraph *tg = (TimeGraph *)self;
	if(!tg->dv.enabled) {
		return;
	}
	Color *col = (Color *)tg->dv.outImage.data;

	if(tg->prevWriteIndex < tg->writeIndex) {
		for(int i = tg->prevWriteIndex; i < tg->writeIndex; i++) {
			for(int j = 0; j < tg->dv.imgProps.height; j++) {
				col[tg->dv.imageWriteIndex + (int)tg->dv.imgProps.width * j] = (Color){ 0, 0, 50, 150 };
			}
			if(tg->timeData[i] >= tg->clampPoint) {
				col[tg->dv.imageWriteIndex + (int)tg->dv.imgProps.height - 1] = (Color){ 125, 0, 0, 200 };
			} else {
				float scaledDatapoint = tg->timeData[i] / tg->clampPoint;
				int yoffset = (int)(scaledDatapoint * tg->dv.imgProps.height);
				col[tg->dv.imageWriteIndex + (int)tg->dv.imgProps.width * yoffset] = (Color){ 0, 125, 0, 200 };
			}

			tg->dv.imageWriteIndex++;
			if(tg->dv.imageWriteIndex > tg->dv.imgProps.width) {
				tg->dv.imageWriteIndex -= tg->dv.imgProps.width;
			}
		}
		UnloadTexture(tg->dv.outTexture);
		tg->dv.outTexture = LoadTextureFromImage(tg->dv.outImage);
		tg->prevWriteIndex = tg->writeIndex;
	}
}
void drawTimeGraph(void *self) {
	TimeGraph *tg = (TimeGraph *)self;
	if(!tg->dv.enabled) {
		return;
	}
	DrawTexturePro(tg->dv.outTexture, tg->dv.imgProps, tg->dv.texProps, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void toggleTimeGraph(void *self) {
	TimeGraph *tg = (TimeGraph *)self;
	tg->dv.enabled = !tg->dv.enabled;
}
