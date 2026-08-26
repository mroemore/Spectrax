#include <math.h>

#include "vizfx.h"

static int sampleToRow(float v) {
	if(v < -1.0f) {
		v = -1.0f;
	}
	if(v > 1.0f) {
		v = 1.0f;
	}
	return (int)((v + 1.0f) * 0.5f * (float)(SCROLLER_COLUMN_HEIGHT - 1));
}

void collapseBufferToColumn(const float *samples, int bufferSize, unsigned char *lo, unsigned char *hi, int columnHeight) {
	for(int r = 0; r < columnHeight; r++) {
		int start = (int)((float)r * bufferSize / columnHeight);
		int end = (int)((float)(r + 1) * bufferSize / columnHeight);
		if(end <= start) {
			end = start + 1;
		}
		if(end > bufferSize) {
			end = bufferSize;
		}
		int loRow = sampleToRow(samples[start]);
		int hiRow = loRow;
		for(int i = start; i < end; i++) {
			int y = sampleToRow(samples[i]);
			if(y < loRow) {
				loRow = y;
			}
			if(y > hiRow) {
				hiRow = y;
			}
		}
		lo[r] = (unsigned char)loRow;
		hi[r] = (unsigned char)hiRow;
	}
}