#ifndef VIZFX_H
#define VIZFX_H

#include <stdbool.h>

#include "raylib.h"
#include "modsystem.h"

#define SCROLLER_BUFFER_SIZE 256
#define SCROLLER_COLUMN_HEIGHT 96
#define SCROLLER_WIDTH 640
#define SCROLLER_PENDING_CAPACITY 16
#define SCROLLER_GAIN_WINDOW SCROLLER_WIDTH
#define SCROLLER_MAX_GAIN 20.0f

#define MOD_STRIP_WIDTH 640
#define MOD_STRIP_HEIGHT 100
#define MOD_STRIP_RESPONSE_CURVE 2.0f
#define SCROLLER_LOG_FACTOR 10.0f

#define MIX_RING_LEN 1024

typedef struct Voice Voice;

typedef struct {
	float samples[MIX_RING_LEN];
	int writeIndex;
	int count;
} MixRing;

typedef struct {
	Image image;
	Texture2D texture;
	int writeColumn;
	int pendingHead;
	int pendingTail;
	int pendingCount;
	float staging[SCROLLER_BUFFER_SIZE];
	int framePos;
	float peakRing[SCROLLER_GAIN_WINDOW];
	int peakIndex;
	unsigned char pendingLo[SCROLLER_PENDING_CAPACITY][SCROLLER_COLUMN_HEIGHT];
	unsigned char pendingHi[SCROLLER_PENDING_CAPACITY][SCROLLER_COLUMN_HEIGHT];
} BufferScroller;

typedef struct {
	Voice **voicePool;
	int voiceCount;
	RenderTexture2D ping;
	RenderTexture2D pong;
	int width;
	int height;
	bool pingActive;
} ModStrip;

void collapseBufferToColumn(const float *samples, int bufferSize, unsigned char *lo, unsigned char *hi, int columnHeight, float gain);
void packScrollerColumn(const unsigned char *lo, const unsigned char *hi, int columnHeight, Color *out, Color waveColour);

void initBufferScroller(BufferScroller *bs);
void pushBufferScrollerFrame(BufferScroller *bs, float sample);
void updateBufferScrollerData(BufferScroller *bs);
void drawBufferScroller(BufferScroller *bs, Rectangle dest);
void freeBufferScroller(BufferScroller *bs);

void initModStrip(ModStrip *ms, Voice **voicePool, int voiceCount, int width, int height);
void drawModStrip(ModStrip *ms, Rectangle dest);
void freeModStrip(ModStrip *ms);

void initMixRing(MixRing *r);
void pushMixRingSample(MixRing *r, float sample);
void drawSampleWaveLines(const MixRing *r, Rectangle dest);
void drawSampleWavePolyline(const MixRing *r, Rectangle dest);

#endif