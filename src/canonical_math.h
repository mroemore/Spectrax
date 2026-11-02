#ifndef SPECTRAX_CANONICAL_MATH_H
#define SPECTRAX_CANONICAL_MATH_H

/* Deterministic replacements for the inexact libm transcendentals.
 *
 * Criterion C says a voice's output must be a pure function of its settings:
 * the same patch must sound the same on every device. The inexact libm
 * functions break that. glibc's and musl's sinf differ by up to an ULP, and
 * glibc's own results differ between its x86-64 and aarch64 implementations,
 * so a phase increment that calls sinf produces a different sample on a
 * different machine -- and the cross-architecture golden gate would fail for
 * a reason that has nothing to do with the DSP.
 *
 * Everything here is a fixed sequence of IEEE-754 operations (+, -, *, / and
 * sqrt, all correctly rounded) over a table built by a fixed polynomial in
 * double precision. The results are therefore bit-identical on every
 * platform that follows IEEE-754, which is what the golden gate needs.
 *
 * The exact libm calls are NOT replaced: floor, fabs, trunc, copysign, fmin,
 * fmax and sqrt are exactly representable, so every correct implementation
 * returns the same bits for them.
 *
 * Two deliberate differences from libm, both documented at the function:
 *   - canonicalExp flushes to zero below the normal range instead of
 *     returning a subnormal (a subnormal is flushed differently by NEON's
 *     FZ mode than by x86's DAZ, which would break the gate again);
 *   - canonicalPow is defined for x >= 0 only.
 */

/* sin(2*pi*phase) for any phase, in cycles rather than radians. The phase
 * wrap is exact, so the result repeats every whole turn. */
float canonicalSin(float phase);

/* cos(2*pi*phase) for any phase. Derived from the same table, so it agrees
 * with canonicalSin(phase + 0.25) bit for bit. */
float canonicalCos(float phase);

/* e^x. The result is flushed to zero below exp(-87), the smallest normal
 * float's neighbourhood; libm would return a subnormal there. */
float canonicalExp(float x);

/* ln(x) for x > 0. For x <= 0 this returns 0.0f rather than a NaN or an
 * infinity: every caller in this tree clamps its argument first, and a NaN
 * in the audio path is a silent disaster. */
float canonicalLog(float x);

/* x^y for x >= 0. x == 0 with a negative y returns +infinity, as powf does. */
float canonicalPow(float x, float y);

/* The one-pole smoothing coefficient 1 - exp(-2*pi*fc/sr): the factor a
 * one-pole filter moves its state by per sample at cutoff fc. */
float canonicalOnePoleCoef(float fc, float sr);

#endif
