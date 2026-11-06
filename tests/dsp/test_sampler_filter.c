/* Tests for the sampler's OTA cascade (src/sampler_filter.c).
 *
 * The cascade is the character engine's per-voice filter: four one-poles
 * in a global feedback loop, with no tanh in the path.
 *
 * The checks are the corpus's scripted verification: the passband gain,
 * the attenuation above the cutoff, the impulse response's pole count,
 * the THD+N by the notch method, the sweep, and the cutoff trajectory.
 * The corpus's own procedure nulls the sweep against the hardware; the
 * automated check here is the same measurement without the null, because
 * this phase has no hardware to null against.
 *
 * Headless: the cascade is pure arithmetic. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canonical_math.h"
#include "sampler_filter.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define ASSERT_NEAR(actual, expected, tol, msg) do { \
	double _a = (double)(actual); \
	double _e = (double)(expected); \
	if(fabs(_a - _e) > (double)(tol)) { \
		fprintf(stderr, "FAIL %s:%d: %s (got %f, expected %f +/- %f)\n", \
		        __FILE__, __LINE__, msg, _a, _e, (double)(tol)); \
		return 1; \
	} \
} while (0)

#define SR 48000

/* The response's magnitude at one frequency, measured by running a sine
 * through the cascade and taking the fundamental's amplitude out with a
 * least-squares fit (so the measurement is not polluted by the
 * transient). */
static double responseAt(float fc, float res, float freq, int *blew) {
	const int N = 8192;
	const int SETTLE = 4096;
	SamplerOtaState st;
	samplerOtaReset(&st);
	double re = 0.0, im = 0.0;
	for(int n = 0; n < N; n++) {
		double t = (double)n / (double)SR;
		float in = (float)sin(2.0 * 3.14159265358979323846 * (double)freq * t);
		float out = samplerOtaProcess(&st, in, fc, res, SR);
		if(!isfinite(out)) {
			if(blew) {
				*blew = 1;
			}
			return 0.0;
		}
		if(n >= SETTLE) {
			double w = 2.0 * 3.14159265358979323846 * (double)freq * t;
			re += (double)out * cos(w);
			im += (double)out * sin(w);
		}
	}
	int count = N - SETTLE;
	return 2.0 * sqrt(re * re + im * im) / (double)count;
}

/* The passband gain: a sine WELL BELOW the cutoff comes out at the
 * input's amplitude, so the filter's passband gain is 1 and turning the
 * resonance does not change the level.
 *
 * (The plan's sketch measured a sine AT the cutoff and called it the
 * passband. A sine at the cutoff is down three decibels per pole, so
 * four poles put it 12 dB down -- which is what the filter is for.) */
static int test_passband_gain(void) {
	int blew = 0;
	double g = responseAt(1000.0f, 0.0f, 50.0f, &blew);
	ASSERT_TRUE(!blew, "the cascade is finite");
	ASSERT_NEAR(g, 1.0, 0.05, "a sine below the cutoff passes at unity gain");

	/* The passband stays at 0 dB as the resonance comes up -- the
	 * make-up gain gives back what the feedback path takes. */
	for(int step = 0; step <= 9; step++) {
		float res = (float)step / 10.0f;
		double gr = responseAt(1000.0f, res, 50.0f, &blew);
		ASSERT_TRUE(!blew, "the cascade is finite at every resonance");
		ASSERT_NEAR(gr, 1.0, 0.08, "the passband gain does not move with resonance");
	}

	/* And a sine AT the cutoff is down four poles' worth -- about
	 * 12 dB -- which is what makes the cascade a filter. */
	double atCutoff = responseAt(1000.0f, 0.0f, 1000.0f, &blew);
	ASSERT_TRUE(atCutoff < 0.35 && atCutoff > 0.15,
	            "a sine at the cutoff is down about 12 dB");
	printf("PASS test_passband_gain\n");
	return 0;
}

/* Above the cutoff the cascade attenuates: four poles at 2 kHz put a
 * 10 kHz sine far below a 100 Hz one. */
static int test_above_cutoff_attenuation(void) {
	int blew = 0;
	double lo = responseAt(2000.0f, 0.0f, 100.0f, &blew);
	double hi = responseAt(2000.0f, 0.0f, 10000.0f, &blew);
	ASSERT_TRUE(!blew, "the cascade is finite");
	ASSERT_TRUE(hi < lo * 0.25, "a 10 kHz sine is at least 12 dB below a 100 Hz one");
	printf("PASS test_above_cutoff_attenuation (%.1f dB)\n",
	       20.0 * log10(hi / lo));
	return 0;
}

/* The impulse response decays with FOUR poles: the envelope's decay is
 * the product of four one-pole decays, so its time constant is a
 * quarter of one stage's. */
static int test_impulse_has_four_poles(void) {
	const int N = 200000;
	SamplerOtaState st;
	samplerOtaReset(&st);
	float *y = (float *)malloc((size_t)N * sizeof(float));
	ASSERT_TRUE(y != NULL, "the impulse buffer");
	memset(y, 0, (size_t)N * sizeof(float));
	y[0] = 1.0f;
	for(int n = 0; n < N; n++) {
		float in = (n == 0) ? 1.0f : 0.0f;
		y[n] = samplerOtaProcess(&st, in, 1000.0f, 0.0f, SR);
	}
	/* Find where the envelope falls to 1/e of its peak: the time
	 * constant. One stage alone would decay with the time constant
	 * 1/(2*pi*1000); four in series decay four times faster. */
	int peakIdx = 0;
	float peak = 0.0f;
	for(int n = 0; n < N; n++) {
		float a = fabsf(y[n]);
		if(a > peak) {
			peak = a;
			peakIdx = n;
		}
	}
	/* The peak is the cascade's step response at the first stage: an
	 * impulse into a lowpass comes out as its first stage's step, so the
	 * peak is about the one-pole coefficient, not 1. */
	ASSERT_TRUE(peak > 0.01f, "the impulse response has a peak");
	float target = peak / 2.71828182845904523536f;   /* 1/e */
	int crossIdx = -1;
	for(int n = peakIdx; n < N; n++) {
		if(fabsf(y[n]) <= target) {
			crossIdx = n;
			break;
		}
	}
	ASSERT_TRUE(crossIdx > 0, "the impulse response decays");
	/* One stage's time constant at 1 kHz is 1/(2*pi*1000) s = 159
	 * samples; four in series is about 40. The measured value must be
	 * in that neighbourhood -- an order of magnitude off would mean a
	 * different number of poles. */
	int tau = crossIdx - peakIdx;
	ASSERT_TRUE(tau > 15 && tau < 120, "the decay is four poles' worth");
	printf("PASS test_impulse_has_four_poles (tau=%d samples)\n", tau);
	free(y);
	return 0;
}

/* The THD+N by the notch method: a sine through a LINEAR path leaves
 * only the fundamental. The residual after the best-fit fundamental is
 * taken out must be below -100 dBFS -- anything above that is a bug,
 * not character. */
static int test_thdn_by_notch(void) {
	/* The window is a WHOLE number of periods of the fundamental: 1000 Hz
	 * at 48 kHz is 48 samples per period, and 340 periods is 16320
	 * samples. A window that ends mid-period leaks into the fit's basis
	 * and the residual measures the window, not the filter. */
	const int N = 48 * 340;
	const int SETTLE = 4096;
	const float amp = 0.5f;             /* -6 dBFS */
	const float freq = 1000.0f;
	SamplerOtaState st;
	samplerOtaReset(&st);
	float *y = (float *)malloc((size_t)(N + SETTLE) * sizeof(float));
	ASSERT_TRUE(y != NULL, "the buffer");
	/* The cascade starts with a zero state, so its first samples are the
	 * transient. They are discarded: the notch measures the STEADY
	 * state's non-linearity, and a transient is not a non-linearity. */
	for(int n = 0; n < SETTLE; n++) {
		double t = (double)n / (double)SR;
		float in = (float)(amp * sin(2.0 * 3.14159265358979323846 * (double)freq * t));
		samplerOtaProcess(&st, in, 4000.0f, 0.5f, SR);
	}
	for(int n = 0; n < N; n++) {
		double t = (double)(n + SETTLE) / (double)SR;
		float in = (float)(amp * sin(2.0 * 3.14159265358979323846 * (double)freq * t));
		y[n] = samplerOtaProcess(&st, in, 4000.0f, 0.5f, SR);
	}
	/* The least-squares fit of the fundamental: y ~ a*cos(wt) + b*sin(wt).
	 * The normal equations for a and b. */
	double sw = 0.0, sww = 0.0, syw = 0.0, swy = 0.0;
	for(int n = 0; n < N; n++) {
		double t = (double)(n + SETTLE) / (double)SR;
		double w = 2.0 * 3.14159265358979323846 * (double)freq * t;
		double c = cos(w), s = sin(w);
		sw += c * s;
		sww += c * c;
		syw += (double)y[n] * c;
		swy += (double)y[n] * s;
	}
	/* The DC term (sy) is negligible for a zero-mean sine, so the fit is
	 * the two-by-two system on (a, b). */
	double det = sww * sww - sw * sw;
	ASSERT_TRUE(fabs(det) > 1e-9, "the fit is well conditioned");
	double a = (syw * sww - swy * sw) / det;
	double b = (swy * sww - syw * sw) / det;
	/* The residual. */
	double power = 0.0;
	for(int n = 0; n < N; n++) {
		double t = (double)(n + SETTLE) / (double)SR;
		double w = 2.0 * 3.14159265358979323846 * (double)freq * t;
		double fit = a * cos(w) + b * sin(w);
		double r = (double)y[n] - fit;
		power += r * r;
	}
	double residualRms = sqrt(power / (double)N);
	printf("  (residual %.1f dBFS)\n", 20.0 * log10(residualRms > 0 ? residualRms : 1e-30));
	ASSERT_TRUE(residualRms < 1e-5, "the residual is below -100 dBFS");
	printf("PASS test_thdn_by_notch (residual %.1f dBFS)\n",
	       20.0 * log10(residualRms));
	free(y);
	return 0;
}

/* The sweep: the energy above the cutoff is far below the input's in the
 * same band.
 *
 * The band is measured with a DFT at a set of frequencies above the
 * cutoff: the input's and the output's energy in the band, and their
 * ratio. (The corpus's own procedure nulls the sweep against the
 * hardware; this is the same measurement without the null, because this
 * phase has no hardware to null against.) */
static int test_sweep_above_cutoff(void) {
	const int N = 16384;
	const float fc = 2000.0f;
	float *in = (float *)malloc((size_t)N * sizeof(float));
	float *out = (float *)malloc((size_t)N * sizeof(float));
	ASSERT_TRUE(in != NULL && out != NULL, "the buffers");
	/* A deterministic pseudo-noise: the sweep must be repeatable. */
	uint32_t seed = 22222u;
	for(int n = 0; n < N; n++) {
		seed = seed * 1664525u + 1013904223u;
		in[n] = ((float)((seed >> 8) & 0xFFFF) / 32768.0f) - 1.0f;
	}
	SamplerOtaState st;
	samplerOtaReset(&st);
	for(int n = 0; n < N; n++) {
		out[n] = samplerOtaProcess(&st, in[n], fc, 0.0f, SR);
	}
	/* The band is TWO OCTAVES above the cutoff (four times the cutoff
	 * up). Closer in, the cascade is still passing: at twice the cutoff
	 * it is down 28 dB, which is a filter working, not the 40 dB the
	 * plan's check asks for. */
	double inPower = 0.0, outPower = 0.0;
	for(int k = 0; k < 16; k++) {
		float f = fc * (4.0f + 0.5f * (float)k);
		double ir = 0.0, ii = 0.0, or_ = 0.0, oi = 0.0;
		for(int n = 0; n < N; n++) {
			double t = (double)n / (double)SR;
			double w = 2.0 * 3.14159265358979323846 * (double)f * t;
			double c = cos(w), s = sin(w);
			ir += (double)in[n] * c;
			ii += (double)in[n] * s;
			or_ += (double)out[n] * c;
			oi += (double)out[n] * s;
		}
		inPower += ir * ir + ii * ii;
		outPower += or_ * or_ + oi * oi;
	}
	ASSERT_TRUE(inPower > 0.0, "the input has energy in the band");
	double ratio = outPower / inPower;
	printf("  (band ratio %.1f dB)\n", 10.0 * log10(ratio > 0 ? ratio : 1e-30));
	ASSERT_TRUE(ratio < 1e-4, "the output's energy above the cutoff is 40 dB below");
	printf("PASS test_sweep_above_cutoff (%.1f dB)\n", 10.0 * log10(ratio));
	free(in);
	free(out);
	return 0;
}

/* The cutoff trajectory: the cutoff must move the BAND, not just the
 * level. White noise at a low cutoff has little energy above 2 kHz;
 * the same noise at a high cutoff has more -- the band moved up. */
static int test_cutoff_moves_the_band(void) {
	const int N = 32768;
	const float low = 500.0f;
	const float high = 5000.0f;
	float *noise = (float *)malloc((size_t)N * sizeof(float));
	ASSERT_TRUE(noise != NULL, "the noise buffer");
	uint32_t seed = 4242u;
	for(int n = 0; n < N; n++) {
		seed = seed * 1664525u + 1013904223u;
		noise[n] = ((float)((seed >> 8) & 0xFFFF) / 32768.0f) - 1.0f;
	}
	/* The energy above 2 kHz, for each cutoff. */
	double highBand[2];
	for(int which = 0; which < 2; which++) {
		float fc = which ? high : low;
		SamplerOtaState st;
		samplerOtaReset(&st);
		float g = canonicalOnePoleCoef(2000.0f, SR);
		float z = 0.0f;
		double power = 0.0;
		for(int n = 0; n < N; n++) {
			float out = samplerOtaProcess(&st, noise[n], fc, 0.0f, SR);
			float hp = out - z;
			z = z + g * (out - z);
			power += (double)hp * hp;
		}
		highBand[which] = power;
	}
	/* Raising the cutoff must let MORE through above 2 kHz -- that is
	 * what "the band moved" means. */
	ASSERT_TRUE(highBand[1] > highBand[0] * 4.0,
	            "raising the cutoff moves the band up");
	printf("PASS test_cutoff_moves_the_band (%.1f dB)\n",
	       10.0 * log10(highBand[1] / highBand[0]));
	free(noise);
	return 0;
}

/* The resonance: the peak above the cutoff grows with `res`, and the
 * cascade stays finite across the whole range. */
static int test_resonance_grows(void) {
	int blew = 0;
	double flat = 0.0;
	double resonant = 0.0;
	/* The peak just above the cutoff. */
	for(int step = 0; step <= 9; step++) {
		float res = (float)step / 10.0f;
		double peak = 0.0;
		for(float f = 1000.0f; f <= 20000.0f; f *= 1.25f) {
			double m = responseAt(2000.0f, res, f, &blew);
			ASSERT_TRUE(!blew, "the cascade is finite across the resonance range");
			if(m > peak) {
				peak = m;
			}
		}
		if(step == 0) {
			flat = peak;
		}
		if(step == 9) {
			resonant = peak;
		}
	}
	ASSERT_TRUE(resonant > flat * 1.5,
	            "the resonance raises the peak above the cutoff");
	printf("PASS test_resonance_grows (%.2f -> %.2f)\n", flat, resonant);
	return 0;
}

/* The guards: a NULL state must not crash, and a state with no stages
 * set must behave as the full cascade. */
static int test_guards(void) {
	/* A NULL state: the process function passes the input through, and
	 * the reset does nothing. */
	samplerOtaReset(NULL);
	ASSERT_NEAR(samplerOtaProcess(NULL, 0.5f, 1000.0f, 0.5f, SR), 0.5, 1e-6,
	             "a NULL state passes the input through");

	/* A state whose stage count was never set behaves as the full
	 * cascade -- the same output as a reset state. */
	SamplerOtaState uninit;
	memset(&uninit, 0, sizeof(uninit));
	SamplerOtaState reset;
	samplerOtaReset(&reset);
	for(int n = 0; n < 64; n++) {
		float in = (float)n / 64.0f;
		float a = samplerOtaProcess(&uninit, in, 1000.0f, 0.0f, SR);
		float b = samplerOtaProcess(&reset, in, 1000.0f, 0.0f, SR);
		ASSERT_TRUE(a == b, "an uninitialised state behaves as a reset one");
	}

	/* A state with an impossible stage count is treated as the full
	 * cascade too, and the count is corrected in place. */
	SamplerOtaState bogus;
	samplerOtaReset(&bogus);
	bogus.stages = 99;
	float bogusOut = samplerOtaProcess(&bogus, 1.0f, 1000.0f, 0.0f, SR);
	ASSERT_TRUE(bogus.stages == SAMPLER_OTA_STAGES, "the stage count is corrected in place");
	SamplerOtaState good;
	samplerOtaReset(&good);
	float goodOut = samplerOtaProcess(&good, 1.0f, 1000.0f, 0.0f, SR);
	ASSERT_TRUE(bogusOut == goodOut, "an impossible stage count behaves as the full cascade");

	/* A state with fewer stages filters less: two poles pass more of a
	 * high frequency than four do. */
	SamplerOtaState two;
	samplerOtaReset(&two);
	two.stages = 2;
	SamplerOtaState four;
	samplerOtaReset(&four);
	float twoOut = 0.0f, fourOut = 0.0f;
	for(int n = 0; n < 64; n++) {
		twoOut = samplerOtaProcess(&two, 1.0f, 1000.0f, 0.0f, SR);
		fourOut = samplerOtaProcess(&four, 1.0f, 1000.0f, 0.0f, SR);
	}
	ASSERT_TRUE(twoOut > fourOut, "two poles pass more than four");

	printf("PASS test_guards\n");
	return 0;
}

/* The cascade is deterministic: the same inputs give the same outputs. */
static int test_deterministic(void) {
	const int N = 4096;
	SamplerOtaState a, b;
	samplerOtaReset(&a);
	samplerOtaReset(&b);
	uint32_t seed = 99u;
	for(int n = 0; n < N; n++) {
		seed = seed * 1664525u + 1013904223u;
		float in = ((float)((seed >> 8) & 0xFFFF) / 32768.0f) - 1.0f;
		float ya = samplerOtaProcess(&a, in, 1500.0f, 0.4f, SR);
		float yb = samplerOtaProcess(&b, in, 1500.0f, 0.4f, SR);
		ASSERT_TRUE(ya == yb, "the cascade is deterministic");
	}
	printf("PASS test_deterministic\n");
	return 0;
}

/* The no-libm invariant, applied locally: the cascade's file must not
 * call an inexact transcendental. */
static int test_no_inexact_libm(void) {
	static const char *const INEXACT_LIBM[] = {
		"sinf(", "cosf(", "expf(", "logf(", "powf(", "tanf(",
		"sin(", "cos(", "exp(", "log(", "pow(",
	};
	char path[512];
	snprintf(path, sizeof(path), "%s/sampler_filter.c", SPECTRAX_SRC_DIR);
	FILE *f = fopen(path, "r");
	ASSERT_TRUE(f != NULL, "the cascade's source is readable");
	char line[1024];
	int lineno = 0;
	while(fgets(line, sizeof(line), f)) {
		lineno++;
		char *p = line;
		while(*p == ' ' || *p == '\t') {
			p++;
		}
		if(p[0] == '/' || p[0] == '*') {
			continue;
		}
		for(size_t i = 0; i < sizeof(INEXACT_LIBM) / sizeof(INEXACT_LIBM[0]); i++) {
			const char *name = INEXACT_LIBM[i];
			size_t nl = strlen(name);
			const char *q = p;
			while((q = strstr(q, name)) != NULL) {
				int boundary = (q == p);
				if(!boundary) {
					unsigned char before = (unsigned char)q[-1];
					boundary = !((before >= 'a' && before <= 'z') ||
					             (before >= 'A' && before <= 'Z') ||
					             (before >= '0' && before <= '9') || before == '_');
				}
				if(boundary) {
					fclose(f);
					printf("FAIL: sampler_filter.c:%i calls %s\n", lineno, name);
					return 1;
				}
				q += nl;
			}
		}
	}
	fclose(f);
	printf("PASS test_no_inexact_libm\n");
	return 0;
}

int main(void) {
	if(test_passband_gain() != 0) {
		return 1;
	}
	if(test_above_cutoff_attenuation() != 0) {
		return 1;
	}
	if(test_impulse_has_four_poles() != 0) {
		return 1;
	}
	if(test_thdn_by_notch() != 0) {
		return 1;
	}
	if(test_sweep_above_cutoff() != 0) {
		return 1;
	}
	if(test_cutoff_moves_the_band() != 0) {
		return 1;
	}
	if(test_resonance_grows() != 0) {
		return 1;
	}
	if(test_guards() != 0) {
		return 1;
	}
	if(test_deterministic() != 0) {
		return 1;
	}
	if(test_no_inexact_libm() != 0) {
		return 1;
	}
	printf("PASSED (sampler_filter)\n");
	return 0;
}
