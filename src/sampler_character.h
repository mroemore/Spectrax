#ifndef SPECTRAX_SAMPLER_CHARACTER_H
#define SPECTRAX_SAMPLER_CHARACTER_H

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

/* Quantize one sample.
 *
 * `bits` is the word length of the law (8 for the 8-bit laws, 12 for
 * SQL_COMPAND12); it is clamped into [1, 24] so a bad parameter cannot
 * produce a division by zero or a shift overflow. `law` is clamped into
 * [0, SQL_COUNT).
 *
 * The result is always finite and within [-1, 1]. SQL_OFF returns x exactly.
 */
float samplerQuantize(float x, int bits, SamplerQuantLaw law);

#endif
