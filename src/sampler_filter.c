/* sampler_filter.c -- the sampler's OTA cascade.
 *
 * The corpus's class (the CEM3320/SSM2040 OTA ladder): four one-poles in
 * a GLOBAL feedback loop, with no tanh in the path.
 *
 *   y = in - k * feedback
 *   for i in 0..3: z[i] += g * (y - z[i]); y = z[i];
 *   feedback = y
 *   out = y * (1 + k)
 *
 * Each stage is a one-pole with unity DC gain (z += g*(x - z) settles at
 * x), so the cascade's passband gain is 1 with the feedback closed... no:
 * with the feedback closed the ladder's DC gain is 1/(1+k), so the bass
 * falls away as the resonance comes up. The make-up gain (1+k) gives it
 * back, and the passband stays at 0 dB for every `res`. What the
 * feedback adds is the resonant PEAK just above the cutoff -- which is
 * the character the class is known for.
 *
 * There is no tanh here, and therefore no oversampling and no
 * half-delay compensation: the OTA class needs far less of both than a
 * ladder with saturating stages.
 *
 * The coefficient comes from the canonical math module, never from libm
 * -- the no-libm invariant. */

#include <math.h>
#include <string.h>

#include "canonical_math.h"
#include "sampler_filter.h"

/* The feedback amount's stability bound.
 *
 * The four one-poles in a global feedback loop have their closed-loop
 * poles at z = (1-g) + g*k^(1/4)*e^(j*(pi/4 + n*pi/2)), and the loop is
 * stable while every one of them is inside the unit circle. The bound
 * FALLS as the cutoff rises -- at 20 kHz the feedback must stay below
 * about 1.1, so the raw k of 4 would go unstable and the NaN would
 * poison the whole mix, not just that voice.
 *
 * Solving the bound for t = k^(1/4):
 *   g^2*t^2 + sqrt(2)*u*g*t + (u^2 - 1) < 0,   u = 1 - g
 *   => t < (sqrt(4 - 2u^2) - sqrt(2)*u) / (2g)
 *
 * sqrt is exact, so it does not break the no-libm invariant. */
static float otaFeedbackLimit(float g) {
	if(!(g > 0.0f)) {
		/* A cutoff of zero gives a zero coefficient, and a zero
		 * coefficient means a zero loop gain: no feedback at all. */
		return 0.0f;
	}
	float u = 1.0f - g;
	/* With 0 < g <= 1, u is in [0, 1), so the root is in (2, 4] and the
	 * limit is positive: no further guards are needed. */
	float root = 4.0f - 2.0f * u * u;
	float t = (sqrtf(root) - 1.41421356f * u) / (2.0f * g);
	return t * t * t * t;
}

float samplerOtaFeedbackLimit(float cutoff, int sr) {
	if(!(sr > 0) || !(cutoff > 0.0f)) {
		return 0.0f;
	}
	return otaFeedbackLimit(canonicalOnePoleCoef(cutoff, sr));
}

void samplerOtaReset(SamplerOtaState *f) {
	if(!f) {
		return;
	}
	memset(f, 0, sizeof(*f));
	f->stages = SAMPLER_OTA_STAGES;
}

float samplerOtaProcess(SamplerOtaState *f, float in, float cutoff, float res, int sr) {
	if(!f) {
		return in;
	}
	int stages = f->stages;
	if(stages <= 0 || stages > SAMPLER_OTA_STAGES) {
		stages = SAMPLER_OTA_STAGES;
		f->stages = stages;
	}
	/* The one-pole coefficient. canonicalOnePoleCoef is 1-exp(-2*pi*fc/sr)
	 * computed in double, so a small cutoff keeps its precision. */
	float g = canonicalOnePoleCoef(cutoff, sr);
	/* The feedback amount: the RES parameter's 0..1 maps onto 0..4 -- and
	 * then the stability bound for THIS cutoff clamps it. The bound falls
	 * as the cutoff rises, so a fixed limit would go unstable over part
	 * of the parameter range. */
	float k = res * SAMPLER_OTA_MAX_FEEDBACK;
	float limit = otaFeedbackLimit(g);
	if(k > limit) {
		k = limit;
	}
	float y = in - k * f->feedback;
	for(int i = 0; i < stages; i++) {
		f->z[i] += g * (y - f->z[i]);
		y = f->z[i];
	}
	f->feedback = y;
	/* The make-up gain: the feedback path's 1/(1+k) loss, given back, so
	 * the passband stays at 0 dB and only the shape changes. */
	return y * (1.0f + k);
}
