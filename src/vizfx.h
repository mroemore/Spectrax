#ifndef VIZFX_H
#define VIZFX_H

#include <stdbool.h>

#include "raylib.h"
#include "modsystem.h"

#define SCROLLER_BUFFER_SIZE 256
#define SCROLLER_COLUMN_HEIGHT 96
#define SCROLLER_WIDTH 640
#define SCROLLER_PENDING_CAPACITY 16

#define MOD_STRIP_WIDTH 640
#define MOD_STRIP_HEIGHT 100

typedef struct {
	Image image;
	Texture2D texture;
	int writeColumn;
	int pendingHead;
	int pendingTail;
	int pendingCount;
	float staging[SCROLLER_BUFFER_SIZE];
	int framePos;
	unsigned char pendingLo[SCROLLER_PENDING_CAPACITY][SCROLLER_COLUMN_HEIGHT];
	unsigned char pendingHi[SCROLLER_PENDING_CAPACITY][SCROLLER_COLUMN_HEIGHT];
} BufferScroller;

typedef struct {
	ModList *modList;
	RenderTexture2D ping;
	RenderTexture2D pong;
	int width;
	int height;
	bool pingActive;
} ModStrip;

void collapseBufferToColumn(const float *samples, int bufferSize, unsigned char *lo, unsigned char *hi, int columnHeight);

void initBufferScroller(BufferScroller *bs);
void pushBufferScrollerFrame(BufferScroller *bs, float sample);
void updateBufferScrollerData(BufferScroller *bs);
void drawBufferScroller(BufferScroller *bs, Rectangle dest);
void freeBufferScroller(BufferScroller *bs);

void initModStrip(ModStrip *ms, ModList *modList, int width, int height);
void drawModStrip(ModStrip *ms, Rectangle dest);
void freeModStrip(ModStrip *ms);

#endif