#ifndef SPECTROGRAM_H
#define SPECTROGRAM_H

#include <stdbool.h>

#include "raylib.h"

#include "settings.h"

#include "fft.h"

typedef struct {
	bool enabled;
	int imageWriteIndex;
	int imageWidth;
	int imageHeight;
	float imageScale;
	Image specImage;
	Rectangle textureDestination;
	Texture specTexture;
	Fft fft;
} Spectro;

void initSpectro(Spectro *sp, int fftSize, int framesPerBuffer, int toAverage, float imageScale);
void updateSpectroImageData(Spectro *sp);
void drawSpectro(Spectro *sp);

#endif
