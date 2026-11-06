/* sampler_crossfade.c -- the loop's crossfade.
 *
 * A loop's seam is where the material that is leaving meets the material
 * that is arriving. Played straight, the two copies are the same
 * waveform offset by a whole loop, and at the seam they CANCEL: a phase
 * error of half a cycle gives complete cancellation, which is the
 * thumping loop every sampler without a crossfade has.
 *
 * The crossfade fades the loop's last frames out while its first frames
 * fade in, over the same span. Which law is right depends on the
 * material: for a correlated loop (a sustained tone, where the head and
 * the tail are the same waveform) the fades are equal-gain, so the two
 * copies sum to unity where they overlap. For uncorrelated material (a
 * drum hit, noise) they are equal-power, so the sum's power is
 * constant.
 *
 * Whether the material is correlated is MEASURED, once per loop, off the
 * audio path -- never per sample. The trigonometry comes from the
 * canonical math module, never from libm: the no-libm invariant. */

#include <math.h>
#include <string.h>

#include "canonical_math.h"
#include "sampler_crossfade.h"

/* The correlation above which the material counts as correlated. */
#define XF_CORRELATED_ABOVE 0.5f

float samplerCrossfadeCorrelation(const BakedSample *b, int loopStart, int loopEnd, int frames) {
	if(!b || !b->data || frames <= 0 || loopEnd <= loopStart) {
		return 0.0f;
	}
	/* The head and the tail are the same length, and both must fit
	 * inside the region. */
	if(loopStart + frames > loopEnd) {
		frames = loopEnd - loopStart;
	}
	if(loopStart < 0) {
		loopStart = 0;
	}
	if(loopEnd > b->length) {
		loopEnd = b->length;
	}
	if(loopStart + frames > loopEnd || loopEnd - frames < loopStart) {
		return 0.0f;
	}
	/* Correlate the head with the tail, normalised by the two segments'
	 * energies. */
	double dot = 0.0;
	double headEnergy = 0.0;
	double tailEnergy = 0.0;
	for(int i = 0; i < frames; i++) {
		double h = (double)b->data[loopStart + i];
		double t = (double)b->data[loopEnd - frames + i];
		dot += h * t;
		headEnergy += h * h;
		tailEnergy += t * t;
	}
	if(headEnergy <= 0.0 || tailEnergy <= 0.0) {
		/* Silence on one side: there is nothing to correlate. */
		return 0.0f;
	}
	return (float)(dot / sqrt(headEnergy * tailEnergy));
}

SamplerFadeLaw samplerCrossfadeLaw(float correlation) {
	return (correlation > XF_CORRELATED_ABOVE) ? XF_EQUAL_GAIN : XF_EQUAL_POWER;
}

float samplerCrossfadeMix(float outgoing, float incoming, float phase, SamplerFadeLaw law) {
	if(phase <= 0.0f) {
		return outgoing;
	}
	if(phase >= 1.0f) {
		return incoming;
	}
	if(law == XF_EQUAL_POWER) {
		/* The quarter-circle: the two copies' squares sum to one, so
		 * the sum's POWER is constant across the fade.
		 *
		 * canonicalCos and canonicalSin take their phase in CYCLES, not
		 * radians, so a quarter of a circle is a quarter of a cycle. */
		float a = canonicalCos(phase * 0.25f);
		float b = canonicalSin(phase * 0.25f);
		return outgoing * a + incoming * b;
	}
	/* The equal-gain cross: the two copies sum to one, so the sum's
	 * AMPLITUDE is constant across the fade. */
	return outgoing * (1.0f - phase) + incoming * phase;
}

int samplerCrossfadeFrames(float ms, int loopFrames, int rate) {
	if(!(ms > 0.0f) || rate <= 0 || loopFrames <= 0) {
		return 0;
	}
	double frames = (double)ms * (double)rate / 1000.0;
	if(frames < 1.0) {
		/* A crossfade shorter than a frame still fades something, so it
		 * rounds up to one frame rather than to none. */
		return 1;
	}
	int n = (int)frames;
	if((double)n < frames) {
		n++;
	}
	/* A crossfade longer than the loop would read past the region it is
	 * fading, so it is clamped to the loop's length. */
	if(n > loopFrames) {
		n = loopFrames;
	}
	return n;
}

SamplerFadeLaw samplerCrossfadeMeasure(BakedSample *b, int loopStart, int loopEnd, int frames) {
	if(!b) {
		return XF_EQUAL_POWER;
	}
	/* The measurement is cached: it is made once per loop, off the audio
	 * path, and the bake carries it. The cache is keyed on the loop
	 * points, so a bake measured for one loop says nothing about
	 * another. */
	if(b->lawLoopStart == loopStart && b->lawLoopEnd == loopEnd && b->lawFrames == frames) {
		return (SamplerFadeLaw)b->fadeLaw;
	}
	float correlation = samplerCrossfadeCorrelation(b, loopStart, loopEnd, frames);
	b->lawCorrelation = correlation;
	b->fadeLaw = (int)samplerCrossfadeLaw(correlation);
	b->lawLoopStart = loopStart;
	b->lawLoopEnd = loopEnd;
	b->lawFrames = frames;
	return b->law;
}
