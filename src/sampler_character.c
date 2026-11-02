#include "sampler_character.h"
#include "canonical_math.h"

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* The word lengths the laws use. */
#define SAMPLER_ULAW_LEVELS 127    /* levels per sign, plus the sign */
#define SAMPLER_ULAW_MU 255.0f
#define SAMPLER_COMPAND12_LEVELS 2047 /* levels per sign, plus the sign */
#define SAMPLER_COMPAND12_MU 90.0f

/* The bits a law clamps its word length into. */
#define SAMPLER_MIN_BITS 1
#define SAMPLER_MAX_BITS 24

/* A xorshift32, seeded from a fixed constant so a bake is reproducible: the
 * same sample always dithers the same way, on every platform and every run.
 * (The corpus notes that a vintage quantizer's error is signal-correlated;
 * a fixed dither pattern keeps that correlation rather than inventing a
 * random one that no machine ever had.) */
static uint32_t g_ditherState = 0x9E3779B9u;

static float ditherUniform(void) {
	/* xorshift32: three shifts and three xors, all exact. */
	uint32_t x = g_ditherState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g_ditherState = x;
	/* [-0.5, 0.5): x >> 8 is 24 bits, so the divisor is 2^24. */
	return ((float)(x >> 8) / 16777216.0f) - 0.5f;
}

/* The word's range. A dithered rounding can step outside it at the edges
 * (the dither pushes the input past the last grid point), and the result of
 * a quantizer is a word, so it is clamped back. */
static float clampUnit(float v) {
	if(v > 1.0f) {
		return 1.0f;
	}
	if(v < -1.0f) {
		return -1.0f;
	}
	return v;
}

/* The linear grid: 2^bits levels over [-1, 1], so the step is 2 / 2^bits. */
static float linearStep(int bits) {
	return 2.0f / (float)(1u << bits);
}

static float quantizeLinear(float x, int bits, bool roundToNearest) {
	float step = linearStep(bits);
	float scaled = x / step;
	float q;
	if(roundToNearest) {
		/* Round half away from zero, so the grid is symmetric about 0
		 * and the error is centred. */
		q = (scaled >= 0.0f) ? floorf(scaled + 0.5f) : ceilf(scaled - 0.5f);
	} else {
		/* Truncate toward negative infinity: the vintage error profile
		 * has a -Delta/2 DC bias because of it. */
		q = floorf(scaled);
	}
	return q * step;
}

/* mu-law compaction: sign(x) * ln(1 + mu*|x|) / ln(1 + mu), which lands in
 * [-1, 1] for x in [-1, 1]. The inverse expands the code back to a
 * representative value. */
static float compand(float x, float mu, int levels) {
	float sign = (x < 0.0f) ? -1.0f : 1.0f;
	float ax = fabsf(x);
	if(ax > 1.0f) {
		ax = 1.0f;
	}
	float v = canonicalLog(1.0f + mu * ax) / canonicalLog(1.0f + mu);
	float q = (float)(int)(v * (float)levels + 0.5f);
	if(q > (float)levels) {
		q = (float)levels;
	}
	float expanded = canonicalExp(q / (float)levels * canonicalLog(1.0f + mu));
	return sign * (expanded - 1.0f) / mu;
}

float samplerQuantize(float x, int bits, SamplerQuantLaw law) {
	if(bits < SAMPLER_MIN_BITS) {
		bits = SAMPLER_MIN_BITS;
	}
	if(bits > SAMPLER_MAX_BITS) {
		bits = SAMPLER_MAX_BITS;
	}
	if((int)law < 0) {
		law = SQL_OFF;
	}
	if((int)law >= SQL_COUNT) {
		law = SQL_OFF;
	}
	if(!(x == x)) {
		return 0.0f; /* NaN in, silence out. */
	}
	switch(law) {
		case SQL_OFF:
			return x; /* exact, and not clamped: the law does nothing */
		case SQL_TRUNC:
			return clampUnit(quantizeLinear(x, bits, false));
		case SQL_ROUND:
			return clampUnit(quantizeLinear(x, bits, true));
		case SQL_TPDF: {
			/* Triangular-PDF dither: the sum of two uniform variates
			 * shifted to [-1, 1). It spans a whole step, so it can
			 * push the input across a grid boundary -- that is what
			 * decorrelates the error from the signal. */
			float step = linearStep(bits);
			float d = ditherUniform() + ditherUniform();
			float scaled = x / step + d;
			float q = (scaled >= 0.0f) ? floorf(scaled + 0.5f) : ceilf(scaled - 0.5f);
			return clampUnit(q * step);
		}
		case SQL_ULAW:
			return clampUnit(compand(x, SAMPLER_ULAW_MU, SAMPLER_ULAW_LEVELS));
		case SQL_COMPAND12:
			return clampUnit(compand(x, SAMPLER_COMPAND12_MU, SAMPLER_COMPAND12_LEVELS));
		default:
			return x;
	}
}
