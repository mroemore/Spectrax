#ifndef SPECTRAX_SAMPLER_FILTER_H
#define SPECTRAX_SAMPLER_FILTER_H

/* The sampler's OTA cascade -- the character engine's per-voice filter.
 *
 * The corpus's class (the CEM3320/SSM2040 OTA ladder): four one-poles in
 * a GLOBAL feedback loop, with no tanh in the path. That is why the class
 * needs far less oversampling than a ladder with saturating stages, and
 * why there is no half-delay compensation here either.
 *
 * The coefficient comes from the canonical math module
 * (canonicalOnePoleCoef), never from libm -- the no-libm invariant. */

#include <stdbool.h>

/* The number of one-pole stages. */
#define SAMPLER_OTA_STAGES 4

/* The feedback amount at full resonance. The RES parameter runs 0..1 and
 * maps onto 0..SAMPLER_OTA_MAX_FEEDBACK. */
#define SAMPLER_OTA_MAX_FEEDBACK 4.0f

typedef struct {
	/* The four one-poles' states. */
	float z[SAMPLER_OTA_STAGES];
	/* The global feedback path's state. */
	float feedback;
	/* The stage count. Zero means "not initialised", and the process
	 * function treats it as the full cascade. */
	int stages;
} SamplerOtaState;

/* Clear the cascade's state. */
void samplerOtaReset(SamplerOtaState *f);

/* The largest feedback amount the cascade is stable at, for a cutoff.
 *
 * The bound FALLS as the cutoff rises, so the RES dial's usable range
 * depends on where the CUTOFF dial is. The caller uses this to make the
 * dial's ceiling the stable one, so the dial says what the filter does.
 * The result is the feedback AMOUNT (0..SAMPLER_OTA_MAX_FEEDBACK); the
 * RES parameter's range is that divided by the maximum. */
float samplerOtaFeedbackLimit(float cutoff, int sr);

/* One sample through the cascade.
 *
 * `cutoff` is in Hz, `res` is 0..1 (the feedback amount), and `sr` is the
 * engine rate. The passband gain is 0 dB at every `res`: the feedback
 * path takes the bass with it as `res` grows, and the make-up gain
 * gives it back, so turning the resonance changes the SHAPE and not the
 * level.
 *
 * The cascade is stable across the whole `res` range. */
float samplerOtaProcess(SamplerOtaState *f, float in, float cutoff, float res, int sr);

#endif
