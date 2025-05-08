#ifndef DATAVIZ_H
#define DATAVIZ_H

#include <stdbool.h>

#include "raylib.h"
#include "settings.h"
#include "fft.h"

typedef void (*DvTextureUpdateFunc)(void *self);
typedef void (*DvDrawFunc)(void *self);
typedef void (*DvToggleFunc)(void *self);

typedef struct {
	bool enabled;
	int imageWriteIndex;
	Rectangle imgProps;
	Rectangle imgMask;
	Rectangle texProps;
	float imageScale;
	Image outImage;
	Texture outTexture;
	DvTextureUpdateFunc update;
	DvDrawFunc draw;
	DvToggleFunc toggle;
} DataVisualisation;

typedef struct {
	DataVisualisation dv;
	Fft fft;
	int cutoffFreq;
} Spectrogram;

typedef struct {
	DataVisualisation dv;
	double *timeData;
	float clampPoint;
	int maxData;
	int prevWriteIndex;
	int writeIndex;
} TimeGraph;

void initDataVisualisation(DataVisualisation *dv, Rectangle imgProps, Rectangle imgMask, Rectangle texProps);

void initSpectrogram(Spectrogram *sp, int fftSize, int framesPerBuffer, int toAverage, float imageScale);
void updateSpectrogramData(void *self);
void drawSpectrogram(void *self);
void toggleSpectrogram(void *self);

void initTimeGraph(TimeGraph *tg, float maxDataPoints, int x, int y, int width, int height);
void pushTimeGraphMeasurement(TimeGraph *tg, double measurement);
void updateTimeGraphData(void *self);
void drawTimeGraph(void *self);
void toggleTimeGraph(void *self);
#endif
