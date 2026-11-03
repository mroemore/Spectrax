#include "sampler_character.h"
#include "canonical_math.h"
#include "settings.h"

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The word lengths the laws use. */
#define SAMPLER_ULAW_LEVELS 127    /* levels per sign, plus the sign */
#define SAMPLER_ULAW_MU 255.0f
#define SAMPLER_COMPAND12_LEVELS 2047 /* levels per sign, plus the sign */
#define SAMPLER_COMPAND12_MU 90.0f

/* The bits a law clamps its word length into. */
#define SAMPLER_MIN_BITS 1
#define SAMPLER_MAX_BITS 24

/* The dither's seed. Fixed, so a bake is reproducible: the same sample
 * always dithers the same way, on every platform and every run. (The corpus
 * notes that a vintage quantizer's error is signal-correlated; a fixed
 * dither pattern keeps that correlation rather than inventing a random one
 * that no machine ever had.) The state itself belongs to the caller, so
 * two bakes never share a pattern. */
#define SAMPLER_DITHER_SEED 0x9E3779B9u

static float ditherUniform(uint32_t *state) {
	/* xorshift32: three shifts and three xors, all exact. */
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
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

float samplerQuantize(float x, int bits, SamplerQuantLaw law, uint32_t *ditherState) {
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
			uint32_t local = SAMPLER_DITHER_SEED;
			uint32_t *state = ditherState ? ditherState : &local;
			float d = ditherUniform(state) + ditherUniform(state);
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

/* ------------------------------------------------------------------ */
/* The bake: band-limited resampling, then the coding law.             */
/* ------------------------------------------------------------------ */

/* The resampling filter's taps. The bake is a load-time operation, so the
 * tap count costs nothing per sample of output; what it buys is the
 * stopband. A windowed sinc's transition band is roughly 3.3/N of the
 * Nyquist, so the taps have to be enough to push the source's own high
 * frequencies well down before they fold back in. */
#define BAKE_SINC_TAPS 32

/* The kernel's cutoff, as a fraction of the lower of the two rates. 0.47
 * sits just below the target's Nyquist, so a decimation rejects everything
 * that would alias and an upsampling rejects everything above the source's
 * own Nyquist. */
#define BAKE_SINC_CUTOFF 0.47f

/* The windowed-sinc kernel at offset `t` seconds, for a cutoff of `fc` Hz.
 * sinc(x) = sin(pi*x)/(pi*x), so sin(pi*x) comes from the canonical sine
 * table as canonicalSin(x/2). */
static float bakeKernel(double t, double fc) {
	double x = 2.0 * fc * t;
	double s;
	if(fabs(x) < 1e-12) {
		s = 1.0;
	} else {
		s = canonicalSin((float)(x * 0.5)) / (3.14159265358979323846 * x);
	}
	/* The Hann window over the taps: the symmetric form, which is a few
	 * thousandths away from zero at the ends and keeps the sidelobes
	 * down. */
	return (float)(s);
}

static float bakeWindow(int k) {
	/* 0.5 * (1 - cos(2*pi*(k+0.5)/taps)) */
	return (float)(0.5 * (1.0 - (double)canonicalCos(((double)k + 0.5) / (double)BAKE_SINC_TAPS)));
}

bool bakeSamplerCharacter(const Sample *src, int rate, int bits, SamplerQuantLaw law, BakedSample *out) {
	if(!out) {
		return false;
	}
	out->data = NULL;
	out->length = 0;
	out->rate = 0;
	out->srcBit = 0;
	out->bits = 0;
	out->law = SQL_OFF;
	out->src = NULL;
	if(!src || !src->data || src->length <= 0) {
		return false;
	}
	int srcRate = (src->sampleRate > 0) ? src->sampleRate : 44100;
	if(rate <= 0) {
		rate = srcRate;
	}
	if(rate < 1) {
		return false;
	}
	int outLen;
	if(rate == srcRate) {
		outLen = src->length;
	} else {
		outLen = (int)(((long)src->length * (long)rate) / (long)srcRate);
	}
	if(outLen <= 0) {
		return false;
	}
	float *data = (float *)malloc(sizeof(float) * (size_t)outLen);
	if(!data) {
		return false;
	}
	if(rate == srcRate) {
		/* The identity path: an exact copy, no resampling rounding. */
		memcpy(data, src->data, sizeof(float) * (size_t)src->length);
	} else {
		double fc = BAKE_SINC_CUTOFF * (double)((srcRate < rate) ? srcRate : rate);
		for(int m = 0; m < outLen; m++) {
			double srcPos = (double)m * (double)srcRate / (double)rate;
			int centre = (int)floor(srcPos);
			double sum = 0.0;
			double wsum = 0.0;
			for(int k = 0; k < BAKE_SINC_TAPS; k++) {
				int idx = centre - BAKE_SINC_TAPS / 2 + k;
				/* The edges mirror rather than replicate: a replicated
				 * edge is a step, and the filter rings off a step
				 * hard enough to overshoot the source's own range
				 * by 5% on the first samples -- a click at the
				 * sample's onset. A mirrored edge is continuous, so
				 * the ringing stays inside the signal. */
				if(idx < 0) {
					idx = -idx - 1;
				}
				if(idx >= src->length) {
					idx = 2 * src->length - 2 - idx;
				}
				if(idx < 0) {
					idx = 0;
				}
				if(idx >= src->length) {
					idx = src->length - 1;
				}
				double t = ((double)idx - srcPos) / (double)srcRate;
				double w = (double)bakeKernel(t, fc) * (double)bakeWindow(k);
				sum += w * (double)src->data[idx];
				wsum += w;
			}
			data[m] = (wsum != 0.0) ? (float)(sum / wsum) : 0.0f;
		}
	}
	/* The coding law comes last: degrade once, at record time. The
	 * dither's state is local to this bake and seeded with a fixed
	 * constant, so the same source always bakes the same way -- and two
	 * bakes never share a dither pattern. */
	uint32_t ditherState = SAMPLER_DITHER_SEED;
	for(int i = 0; i < outLen; i++) {
		data[i] = samplerQuantize(data[i], bits, law, &ditherState);
	}
	out->data = data;
	out->length = outLen;
	out->rate = rate;
	out->srcBit = src->bit;
	out->bits = bits;
	out->law = law;
	out->src = src;
	out->srcLength = src->length;
	return true;
}

void freeBakedSample(BakedSample *b) {
	if(!b) {
		return;
	}
	if(b->data) {
		free(b->data);
	}
	b->data = NULL;
	b->length = 0;
	b->rate = 0;
	b->srcBit = 0;
	b->bits = 0;
	b->law = SQL_OFF;
	b->src = NULL;
}

bool bakedMatches(const BakedSample *b, const Sample *src, int rate, int bits, SamplerQuantLaw law) {
	if(!b || !b->data || !src || !src->data) {
		return false;
	}
	if(rate <= 0) {
		rate = (src->sampleRate > 0) ? src->sampleRate : 44100;
	}
	return b->rate == rate && b->bits == bits && b->law == law &&
	       b->src == src && b->srcBit == src->bit && b->srcLength == src->length;
}

/* ------------------------------------------------------------------ */
/* The machine profiles.                                               */
/* ------------------------------------------------------------------ */

typedef struct {
	int rate;
	int bits;
	SamplerQuantLaw law;
	SamplerInterp interp;
	int filterOn;
	float cutoff;
	float res;
} SamplerProfileDef;

/* The table is the corpus's machine ground truth:
 *
 *   SP-1200  12-bit linear at 26040, drop-sample, SSM2044 filter
 *   MPC60    12-bit companded at 40000, drop-sample, no filter
 *   S950     12-bit linear at 30000, varispeed, a switched-cap character
 *   Mirage   8-bit at 27000, varispeed, the CEM3328 cascade
 *   SK-1     8-bit at 9380, varispeed, no filter
 *   CLEAN    no degradation: keep the source's own rate and word
 */
static const SamplerProfileDef SAMPLER_PROFILES[SMP_PROFILE_COUNT] = {
	/* CLEAN   */ { 0, 24, SQL_OFF, SPI_LINEAR, 0, 20000.0f, 0.0f },
	/* SP1200  */ { 26040, 12, SQL_TRUNC, SPI_DROPSAMPLE, 1, 12000.0f, 0.05f },
	/* MPC60   */ { 40000, 12, SQL_COMPAND12, SPI_DROPSAMPLE, 0, 20000.0f, 0.0f },
	/* S950    */ { 30000, 12, SQL_TRUNC, SPI_LINEAR, 1, 16000.0f, 0.1f },
	/* MIRAGE  */ { 27000, 8, SQL_TRUNC, SPI_LINEAR, 1, 8000.0f, 0.2f },
	/* SK1     */ { 9380, 8, SQL_TRUNC, SPI_LINEAR, 0, 20000.0f, 0.0f },
};

void samplerProfileConfig(SamplerProfile profile, int *rate, int *bits, SamplerQuantLaw *law,
                          SamplerInterp *interp, int *filterOn, float *cutoff, float *res) {
	if(profile < 0 || profile >= SMP_PROFILE_COUNT) {
		profile = SMP_CLEAN;
	}
	const SamplerProfileDef *d = &SAMPLER_PROFILES[profile];
	if(rate) {
		*rate = d->rate;
	}
	if(bits) {
		*bits = d->bits;
	}
	if(law) {
		*law = d->law;
	}
	if(interp) {
		*interp = d->interp;
	}
	if(filterOn) {
		*filterOn = d->filterOn;
	}
	if(cutoff) {
		*cutoff = d->cutoff;
	}
	if(res) {
		*res = d->res;
	}
}

/* ------------------------------------------------------------------ */
/* The playback read path, and the intended tuning.                    */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* The MPC60's velocity rules.                                        */
/* ------------------------------------------------------------------ */

/* The loudness curve.
 *
 * The MPC60's level is not a fader: reducing the level reduces the
 * sample's quality, so the quiet end of the range is not simply a
 * quieter note but a *worse* one. The curve is pinned here -- a table, not
 * a computed power -- so it is the same number on every platform, which
 * is what criterion C requires.
 *
 * The shape: a soft hit is much quieter than a linear map would make it,
 * and the curve flattens towards the top, so the loudest hits are close
 * together. The table is the velocity 1..127 mapped onto 0.25..1.0. */
static const float SAMPLER_VELOCITY_LEVEL[NOTE_VELOCITY_FULL + 1] = {
	/* 0 (unset) and 1..8: the soft end, quiet. */
	0.25f, 0.25f, 0.26f, 0.27f, 0.28f, 0.30f, 0.32f, 0.34f, 0.36f,
	/* 9..24 */
	0.39f, 0.42f, 0.45f, 0.48f, 0.51f, 0.54f, 0.57f, 0.60f, 0.63f, 0.66f,
	0.69f, 0.71f, 0.73f, 0.75f, 0.77f, 0.79f,
	/* 25..48 */
	0.80f, 0.81f, 0.82f, 0.83f, 0.84f, 0.85f, 0.86f, 0.87f, 0.87f, 0.88f,
	0.89f, 0.90f, 0.90f, 0.91f, 0.92f, 0.92f, 0.93f, 0.93f, 0.94f, 0.94f,
	0.95f, 0.95f, 0.96f, 0.96f,
	/* 49..80 */
	0.96f, 0.97f, 0.97f, 0.97f, 0.97f, 0.98f, 0.98f, 0.98f, 0.98f, 0.98f,
	0.99f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f, 0.99f,
	0.99f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
	1.00f, 1.00f,
	/* 81..127 */
	1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
	1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
	1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
	1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
	1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f
};

float samplerVelocityLevel(int velocity) {
	if(velocity < 1 || velocity > NOTE_VELOCITY_FULL) {
		/* Unset: full. A note with no velocity information must sound
		 * exactly as it did before velocity existed. */
		return 1.0f;
	}
	return SAMPLER_VELOCITY_LEVEL[velocity];
}

int samplerVelocityStartSkipFrames(int velocity, int sampleLength) {
	if(velocity < 1 || velocity > NOTE_VELOCITY_FULL || sampleLength <= 0) {
		return 0;
	}
	/* The skip is a fraction of the sample: a hard hit starts further in.
	 *
	 * The fraction is the corpus's rule made concrete, and it is pinned
	 * as an integer division so the result is the same number on every
	 * platform (criterion C). A tenth of one percent of the sample per
	 * unit of velocity: at the top of the range the read starts 0.1%
	 * into the sample, which skips a recorded attack's worth of material
	 * without running into the body of the sound.
	 *
	 * The softest hit skips nothing, so it gets the whole transient. */
	long skip = ((long)sampleLength * (long)velocity) / 127000L;
	if(skip < 0) {
		skip = 0;
	}
	if(skip > sampleLength - 1) {
		skip = sampleLength - 1;
	}
	return (int)skip;
}

/* The equal-tempered semitone ratios, 2^(k/12) for k in [0, 12). They are
 * literals rather than a computed power, so they are the same bits on every
 * platform. */
static const float SAMPLER_SEMITONE_RATIO[12] = {
	1.0f,
	1.0594630943592953f,
	1.122462048309373f,
	1.189207115002721f,
	1.2599210498948732f,
	1.3348398541700344f,
	1.4142135623730951f,
	1.4983070768766815f,
	1.5874010519681994f,
	1.681792830507429f,
	1.7817974362806785f,
	1.8877486253632068f
};

float samplerAdvance(int noteSemitones, int rootSemitones, float tune, int bakedRate) {
	/* Split the shift into octaves and a remainder, so only the
	 * twelve ratios are ever needed. */
	int semitones = noteSemitones - rootSemitones;
	int octaves = semitones / 12;
	int rem = semitones - octaves * 12;
	if(rem < 0) {
		rem += 12;
		octaves -= 1;
	}
	float ratio = SAMPLER_SEMITONE_RATIO[rem & 11];
	for(int i = 0; i < octaves; i++) {
		ratio *= 2.0f;
	}
	for(int i = 0; i > octaves; i--) {
		ratio *= 0.5f;
	}
	if(!(tune > 0.0f)) {
		tune = 1.0f;
	}
	int rate = (bakedRate > 0) ? bakedRate : SPECTRAX_ENGINE_RATE;
	return ratio * tune * ((float)rate / (float)SPECTRAX_ENGINE_RATE);
}

/* The loop region, clamped to the bake. A region that is not a proper
 * range means "the whole buffer". */
static void loopBounds(const BakedSample *b, int loopStart, int loopEnd, int *start, int *end) {
	int s = 0;
	int e = b->length;
	if(loopStart > 0) {
		s = loopStart;
	}
	if(loopEnd > 0 && loopEnd < b->length) {
		e = loopEnd;
	}
	if(e <= s) {
		s = 0;
		e = b->length;
	}
	*start = s;
	*end = e;
}

float getBakedSampleDrop(const BakedSample *b, uint32_t *addr, double *phase, float advance,
                          int loop, int loopStart, int loopEnd) {
	if(!b || !b->data || b->length <= 0 || !addr || !phase) {
		return 0.0f;
	}
	int start = 0, end = b->length;
	loopBounds(b, loopStart, loopEnd, &start, &end);
	if(!(advance > 0.0f)) {
		advance = 1.0f;
	}
	/* The fractional part is carried, and the integer part moves the
	 * address -- truncated, as the machine's accumulator did. */
	double ip = *phase + (double)advance;
	uint32_t i = (uint32_t)ip;
	*phase = ip - (double)i;
	/* The address never stalls: an integer part of zero still advances a
	 * frame, so a slow passage repeats rather than sticking. */
	*addr += (i != 0) ? i : 1u;
	if(*addr >= (uint32_t)end) {
		if(!loop) {
			*addr = (uint32_t)(end - 1);
			return 0.0f;
		}
		uint32_t span = (uint32_t)(end - start);
		if(span == 0) {
			return 0.0f;
		}
		*addr = (uint32_t)start + ((*addr - (uint32_t)start) % span);
	}
	return b->data[*addr];
}

float getBakedSampleLinear(const BakedSample *b, float *pos, float advance,
                            int loop, int loopStart, int loopEnd) {
	if(!b || !b->data || b->length <= 0 || !pos) {
		return 0.0f;
	}
	int start = 0, end = b->length;
	loopBounds(b, loopStart, loopEnd, &start, &end);
	*pos += advance;
	if(*pos >= (float)end) {
		if(!loop) {
			*pos = (float)(end - 1);
			return 0.0f;
		}
		float span = (float)(end - start);
		if(span <= 0.0f) {
			return 0.0f;
		}
		float rel = *pos - (float)start;
		rel = rel - floorf(rel / span) * span;
		*pos = (float)start + rel;
	}
	/* The head is played before the loop: the position only reaches the
	 * loop region by wrapping at the end, exactly as the drop read does.
	 * Clamping it up to the region's start would skip the head. */
	if(*pos < 0.0f) {
		*pos = 0.0f;
	}
	int floorIndex = (int)*pos;
	int ceilIndex = floorIndex + 1;
	if(ceilIndex >= end) {
		ceilIndex = loop ? start : end - 1;
	}
	if(floorIndex < 0) {
		floorIndex = 0;
	}
	float frac = *pos - (float)floorIndex;
	return b->data[floorIndex] * (1.0f - frac) + b->data[ceilIndex] * frac;
}
