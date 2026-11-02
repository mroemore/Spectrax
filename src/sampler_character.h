#ifndef SPECTRAX_SAMPLER_CHARACTER_H
#define SPECTRAX_SAMPLER_CHARACTER_H

#include <stdbool.h>
#include <stdint.h>

#include "sample.h"

/* The sampler character engine.
 *
 * A sampler's character is a chain, not a bit depth: degrade ONCE at record
 * time (rate -> bits -> coding law), then pitch at playback by reading the
 * degraded buffer. Degrading after the pitch shift, or crushing on the output
 * path, is the standard way to get it wrong.
 *
 * The chain here is:
 *   bake (rate -> bits -> law)  ->  playback read (interpolator)  ->  filter
 *
 * Everything in this module is deterministic: the same input gives the same
 * output on every platform. That is a hard requirement (criterion C), so the
 * quantizer uses the canonical math module rather than libm's transcendentals
 * -- glibc's and musl's logs differ by an ULP, and an ULP in the baked data
 * is an ULP in the voice that reads it.
 */

/* The coding laws. The order is fixed: the enum values are stored in preset
 * files (the preset format carries the law's index), so a new law goes at
 * the end. */
typedef enum {
	SQL_OFF,      /* no degradation at all */
	SQL_TRUNC,    /* truncation: the vintage error profile */
	SQL_ROUND,    /* rounding: no bias, but still correlated */
	SQL_TPDF,     /* triangular-PDF dither: more noise, no distortion */
	SQL_ULAW,     /* 8-bit mu-law (G.711), as the Emu II and LM-1 used */
	SQL_COMPAND12,/* 12-bit companded, approximating the MPC60's storage */
	SQL_COUNT
} SamplerQuantLaw;

/* The baked (degraded) copy of a sample.
 *
 * The bake is the "record time" stage of the character chain: the source is
 * resampled to the machine's rate (band-limited, so nothing above the
 * target's Nyquist is folded back in), and then quantized to the machine's
 * word and coding law.
 *
 * The fields beyond the data are the cache key: a baked copy is only valid
 * for the source it came from and the settings it was baked with. */
typedef struct {
	float *data;
	int length;
	int rate;      /* the baked rate */
	int srcBit;    /* the source's bit depth (bits per frame) */
	int bits;      /* the word length the bake quantized to */
	SamplerQuantLaw law; /* the coding law the bake applied */
	const Sample *src;   /* the source it was baked from */
	int srcLength;       /* the source's length when it was baked */
} BakedSample;

/* The interpolators a machine profile can use.
 *
 * Drop-sample is not a shortcut, it is the SP-1200's actual tuning
 * algorithm: an integer-address accumulator whose fractional increment is
 * truncated when indexing, so a sample may repeat on a pitch-down and
 * never stalls. Linear interpolation is the varispeed class. */
typedef enum {
	/* Linear comes first so that the zero value is the modern default: a
	 * preset that predates the chain, or a zeroed boot preset, keeps the
	 * linear read it always had. */
	SPI_LINEAR,
	SPI_DROPSAMPLE,
	SPI_INTERP_COUNT
} SamplerInterp;

/* The machine profiles. Each one is a set of defaults for the chain's
 * stages; every stage stays dialable afterwards, so a profile sets the
 * character and the dials refine it. */
typedef enum {
	SMP_CLEAN,   /* no degradation at all */
	SMP_SP1200,  /* 12-bit at 26040, drop-sample, SSM2044 filter */
	SMP_MPC60,   /* 12-bit companded at 40000, drop-sample, no filter */
	SMP_S950,    /* 12-bit at 30000, varispeed, a switched-cap character */
	SMP_MIRAGE,  /* 8-bit at 27000, varispeed, the CEM3328 cascade */
	SMP_SK1,     /* 8-bit at 9380, varispeed, no filter */
	SMP_PROFILE_COUNT
} SamplerProfile;

/* Fill in a profile's defaults. Any output pointer may be NULL.
 *
 * `rate` of 0 means "keep the source's own rate", which is what the CLEAN
 * profile asks for: a fixed rate would resample every source for no
 * reason. */
void samplerProfileConfig(SamplerProfile profile, int *rate, int *bits, SamplerQuantLaw *law,
                          SamplerInterp *interp, int *filterOn, float *cutoff, float *res);

/* Build the degraded copy of a sample.
 *
 * `rate` is the baked rate; 0 means "keep the source's own". `bits` is the
 * word length and `law` the coding law. The bake is band-limited: the
 * resampling filter's cutoff sits below the target's Nyquist, so nothing
 * above it is folded back in. The crudeness comes from the truncation and
 * the drop-sample reads, not from a sloppy bake.
 *
 * Returns false and leaves `out` empty if the source is unusable. */
bool bakeSamplerCharacter(const Sample *src, int rate, int bits, SamplerQuantLaw law, BakedSample *out);

/* Release a baked copy and reset it. Safe on an already-freed or a
 * zeroed struct. */
void freeBakedSample(BakedSample *b);

/* True when `b` is the bake of `src` under the given settings. */
bool bakedMatches(const BakedSample *b, const Sample *src, int rate, int bits, SamplerQuantLaw law);

/* The per-output-sample advance, in baked frames.
 *
 *   advance = 2^((note - root)/12) * tune * (bakedRate / engineRate)
 *
 * `noteSemitones` is the note being played and `rootSemitones` the note the
 * recording was made at, both counted from C4 (so C4 is 0, A4 is 9, and C3
 * is -12). The engine's own octave numbering starts at C0, so the note is
 * converted with (octave - 4) * 12 + note. `tune` is the machine's own tuning trim. `bakedRate` is the baked
 * buffer's rate.
 *
 * At unity -- the note played is the root note, tune 1, and the baked rate
 * equal to the engine's -- the advance is exactly 1.0: one baked frame per
 * output frame, and the sample sounds at its recorded pitch. The device's
 * own audio rate is not an input: the engine's rate is a constant, so a
 * voice's output does not depend on the machine it plays on. */
float samplerAdvance(int noteSemitones, int rootSemitones, float tune, int bakedRate);

/* The SP-1200's drop-sample read.
 *
 * An integer-address accumulator whose fractional increment is truncated
 * when it indexes: the fractional part is carried in `phase` and the
 * integer part moves the address. The address never stalls -- it advances
 * at least one frame per tick -- so a slow passage repeats frames instead
 * of sticking, which is exactly what the machine did.
 *
 * `addr` and `phase` are the voice's read state. `loopStart` and `loopEnd`
 * are the loop region in baked frames; when `loop` is zero the read falls
 * silent at the end instead of wrapping. */
float getBakedSampleDrop(const BakedSample *b, uint32_t *addr, double *phase, float advance,
                          int loop, int loopStart, int loopEnd);

/* The linear-interpolated read: the varispeed class. `pos` is the voice's
 * read position in baked frames. */
float getBakedSampleLinear(const BakedSample *b, float *pos, float advance,
                            int loop, int loopStart, int loopEnd);

/* Quantize one sample.
 *
 * `bits` is the word length of the law (8 for the 8-bit laws, 12 for
 * SQL_COMPAND12); it is clamped into [1, 24] so a bad parameter cannot
 * produce a division by zero or a shift overflow. `law` is clamped into
 * [0, SQL_COUNT).
 *
 * `ditherState` is the TPDF dither's running state, and it belongs to the
 * caller: a module-global state would make the dither depend on how many
 * quantizations happened before, so the same sample baked twice would come
 * out different -- a voice's output would depend on the load order, which
 * criterion C forbids. Pass a word seeded with a fixed constant and let it
 * run; pass NULL for a law that does not dither.
 *
 * The result is always finite and within [-1, 1]. SQL_OFF returns x exactly.
 */
float samplerQuantize(float x, int bits, SamplerQuantLaw law, uint32_t *ditherState);

#endif
