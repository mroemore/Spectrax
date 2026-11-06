#ifndef SPECTRAX_SAMPLER_CROSSFADE_H
#define SPECTRAX_SAMPLER_CROSSFADE_H

#include <stdbool.h>

#include "sampler_character.h"

/* The fade law for a loop's crossfade.
 *
 * `correlated` is true when the loop's head and tail are the same
 * waveform (a sustained tone): the fades are equal-gain, so the two
 * copies sum to unity where they overlap. Otherwise (a drum hit, noise)
 * the fades are equal-power, so the sum's power is constant.
 *
 * The corpus's rule, verbatim: "The crossfade must be at least one loop
 * period. At the seam the two crossfading copies must be the same
 * waveform or they cancel: a phase error of 0.5 cycles gives complete
 * cancellation. A crossfade shorter than one loop period therefore
 * imposes a fixed phase error and produces a periodic amplitude dip,
 * audible as a thumping loop. Equal-gain linear fades for correlated
 * material, equal-power sine/cosine fades for uncorrelated drum hits and
 * noise." */
typedef enum {
	XF_EQUAL_GAIN,
	XF_EQUAL_POWER
} SamplerFadeLaw;

/* Measure the correlation of the loop's head and tail.
 *
 * The head is `loopStart .. loopStart + frames` and the tail is
 * `loopEnd - frames .. loopEnd`, correlated and normalised by the two
 * segments' energies. The result is in -1..1; it is 0 when either
 * segment has no energy (silence, or an empty region). */
float samplerCrossfadeCorrelation(const BakedSample *b, int loopStart, int loopEnd, int frames);

/* The law for a loop, from its measured correlation. A correlation above
 * 0.5 is correlated material. */
SamplerFadeLaw samplerCrossfadeLaw(float correlation);

/* The mix of the two copies at the seam. `phase` is 0..1 across the
 * crossfade region; `outgoing` is the copy leaving, `incoming` the one
 * arriving.
 *
 * The equal-gain law is a straight cross: outgoing*(1-phase) +
 * incoming*phase. The equal-power law is the quarter-circle:
 * outgoing*cos(phase*pi/2) + incoming*sin(phase*pi/2). */
float samplerCrossfadeMix(float outgoing, float incoming, float phase, SamplerFadeLaw law);

/* The crossfade's length in frames, from the parameter's milliseconds.
 *
 * It is clamped to the loop's length: a crossfade longer than the loop
 * would read past the region it is fading. */
int samplerCrossfadeFrames(float ms, int loopFrames, int rate);

/* Measure the loop's correlation and cache the law on the bake, so the
 * measurement is made ONCE per loop rather than per sample.
 *
 * The cache is keyed on the loop points: a bake measured for one loop
 * says nothing about another. Returns the law. */
SamplerFadeLaw samplerCrossfadeMeasure(BakedSample *b, int loopStart, int loopEnd, int frames);

#endif
