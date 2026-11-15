#include "ui_preview_mock.h"

#include <stdint.h>

/* A 64-entry sine table in thousandths, so the sample generator needs no
 * libm (see the header). One cycle per 64 steps. */
static const int16_t kSineMilli[64] = {
	0, 98, 195, 290, 382, 471, 556, 634, 707, 773, 831, 882, 924, 957, 981, 995,
	1000, 995, 981, 957, 924, 882, 831, 773, 707, 634, 556, 471, 382, 290, 195, 98,
	0, -98, -195, -290, -382, -471, -556, -634, -707, -773, -831, -882, -924, -957, -981, -995,
	-1000, -995, -981, -957, -924, -882, -831, -773, -707, -634, -556, -471, -382, -290, -195, -98
};

int previewMockSubject(int frame, int subjects, int holdFrames) {
	if(subjects <= 0 || holdFrames <= 0) {
		return 0;
	}
	int f = frame < 0 ? 0 : frame;
	return (f / holdFrames) % subjects;
}

int previewMockTriangle(int frame, int period, int amplitude, int phase) {
	if(period <= 0) {
		return 0;
	}
	int p = period;
	/* Phase 0 starts a rising zero crossing, 128 the falling one, so a
	 * half-period shift centres the wave. Integer arithmetic throughout:
	 * the quarter points are the extremes, and any remainder from an odd
	 * period stays inside [-amplitude, amplitude]. */
	int pos = (frame + (phase * p) / 256) % p;
	if(pos < 0) {
		pos += p;
	}
	int quarter = p / 4;
	if(quarter <= 0) {
		return 0;
	}
	int a = amplitude;
	int v;
	if(pos < quarter) {
		v = (pos * a) / quarter;                       /* 0 .. +a */
	} else if(pos < 2 * quarter) {
		v = a - ((pos - quarter) * a) / quarter;       /* +a .. 0 */
	} else if(pos < 3 * quarter) {
		v = -((pos - 2 * quarter) * a) / quarter;      /* 0 .. -a */
	} else {
		v = -a + ((pos - 3 * quarter) * a) / quarter;  /* -a .. 0 */
	}
	if(v > a) {
		v = a;
	}
	if(v < -a) {
		v = -a;
	}
	return v;
}

float previewMockSample(int frame) {
	int f = frame < 0 ? 0 : frame;
	/* Two components a musical signal would have: a tone at the table's
	 * cycle rate and a slower triangle that moves the envelope around. */
	int sine = kSineMilli[(f * 3) & 63];
	int tri = previewMockTriangle(f, 480, 250, 0);
	int v = sine + tri;
	if(v > 1000) {
		v = 1000;
	}
	if(v < -1000) {
		v = -1000;
	}
	return (float)v / 1000.0f;
}

int previewMockPlayhead(int frame, int rows, int framesPerRow) {
	if(rows <= 0 || framesPerRow <= 0) {
		return 0;
	}
	int f = frame < 0 ? 0 : frame;
	return (f / framesPerRow) % rows;
}

float previewMockClampToRange(float value, float minValue, float maxValue) {
	if(minValue > maxValue) {
		return minValue;
	}
	if(value < minValue) {
		return minValue;
	}
	if(value > maxValue) {
		return maxValue;
	}
	return value;
}