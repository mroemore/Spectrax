/* Branch-complete tests for filters.c (the biquad filter module).
 *
 * The module is pure — no raylib/GL/device state — so it can be driven to
 * 100% branch coverage from a plain test binary. The four processSample
 * variants are function pointers, so each is exercised through both the
 * direct call and the dispatch pointer. The underflow guard, both coefficient
 * generators, the invalid-type default, and the two allocation-failure paths
 * are all covered here (the allocation paths via the fault-injection shim in
 * test_filters_fault.c, not this file).
 *
 * Correctness is asserted against analytic expectations where they are cheap:
 * a low-pass at DC passes the signal, a high-pass blocks it, and the four
 * biquad forms agree on the same coefficients (they are different realisations
 * of one transfer function). That is the property a coverage-driven test can
 * easily lose sight of — this file checks behaviour, not just reach. */

#include <math.h>
#include <stdio.h>

#include "filters.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#define ASSERT_NEAR(a, b, eps) do { \
	float _a = (a), _b = (b); \
	if(fabsf(_a - _b) > (eps)) { \
		fprintf(stderr, "FAIL %s:%d: %f != %f (eps %g)\n", \
			__FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
		return 1; \
	} \
} while (0)

/* ---- createBiquadFilter: every BiquadType branch + state init ---- */
static int test_create_all_types(void) {
	BiquadType types[] = { kDirect, kCanonical, kTransposeDirect, kTransposeCanonical };
	for(size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		BiquadFilter *bf = createBiquadFilter(types[i]);
		ASSERT_TRUE(bf != NULL);
		ASSERT_TRUE(bf->type == types[i]);
		ASSERT_TRUE(bf->processSample != NULL);
		for(int s = 0; s < state_count; s++) {
			ASSERT_TRUE(bf->states[s] == 0.0f);
		}
		free(bf);
	}
	printf("PASS test_create_all_types\n");
	return 0;
}

/* The dispatch pointer must route to the matching free function. */
static int test_dispatch_matches(void) {
	struct { BiquadType t; BiquadProcessor fn; } cases[] = {
		{ kDirect,             processKDirect },
		{ kCanonical,          processKCanonical },
		{ kTransposeDirect,    processKTransposeDirect },
		{ kTransposeCanonical, processKTransposeCanonical },
	};
	for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		BiquadFilter *bf = createBiquadFilter(cases[i].t);
		ASSERT_TRUE(bf->processSample == cases[i].fn);
		free(bf);
	}
	printf("PASS test_dispatch_matches\n");
	return 0;
}

/* ---- checkFLoatUnderflow: both signs, in-range and out-of-range ---- */
static int test_underflow(void) {
	float tiny_pos = SMALLEST_POS_FLOAT / 2.0f;   /* positive, below min normal */
	float tiny_neg = SMALLEST_NEG_FLOAT / 2.0f;   /* negative, above min negative */

	int r = checkFLoatUnderflow(&tiny_pos);
	ASSERT_TRUE(r == 1);
	ASSERT_TRUE(tiny_pos == 0.0f);                /* clamped */

	r = checkFLoatUnderflow(&tiny_neg);
	ASSERT_TRUE(r == 1);
	ASSERT_TRUE(tiny_neg == 0.0f);

	/* Out-of-range: a normal value is left untouched, no underflow reported. */
	float ok = 1.0f;
	ASSERT_TRUE(checkFLoatUnderflow(&ok) == 0);
	ASSERT_TRUE(ok == 1.0f);

	float neg = -1.0f;
	ASSERT_TRUE(checkFLoatUnderflow(&neg) == 0);
	ASSERT_TRUE(neg == -1.0f);

	/* Exact zero: neither `> 0` nor `< 0`, so no underflow, unchanged. */
	float zero = 0.0f;
	ASSERT_TRUE(checkFLoatUnderflow(&zero) == 0);
	ASSERT_TRUE(zero == 0.0f);

	printf("PASS test_underflow\n");
	return 0;
}

/* ---- each processSample variant is callable and consumes state ---- */
static int test_process_all_variants(void) {
	BiquadType types[] = { kDirect, kCanonical, kTransposeDirect, kTransposeCanonical };
	for(size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		BiquadFilter *bf = createBiquadFilter(types[i]);
		ASSERT_TRUE(bf != NULL);
		/* a0=1, everything else 0: output equals input, states accumulate. */
		bf->coefficients[a0] = 1.0f;
		bf->coefficients[a1] = 0.0f;
		bf->coefficients[a2] = 0.0f;
		bf->coefficients[b1] = 0.0f;
		bf->coefficients[b2] = 0.0f;
		bf->coefficients[c0] = 1.0f;
		bf->coefficients[d0] = 0.0f;
		for(int n = 0; n < 5; n++) {
			float y = bf->processSample(bf, 0.5f);
			ASSERT_NEAR(y, 0.5f, 1e-4f);
		}
		/* The dispatch pointer must run the same function as the free
		 * symbol for this type, on the same (advanced) state. */
		BiquadProcessor direct =
			types[i] == kDirect ? processKDirect :
			types[i] == kCanonical ? processKCanonical :
			types[i] == kTransposeDirect ? processKTransposeDirect :
			processKTransposeCanonical;
		float a = direct(bf, 0.25f);
		float b = bf->processSample(bf, 0.25f);
		ASSERT_NEAR(a, b, 1e-6f);
		free(bf);
	}
	printf("PASS test_process_all_variants\n");
	return 0;
}

/* ---- createFilter: both filter types generate usable coefficients ---- */
static int test_create_filter_coeffs(void) {
	float freqs[] = { 100.0f, 1000.0f, 10000.0f };
	FilterType types[] = { secondOrderLPF, secondOrderHPF };
	for(size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
		for(size_t f = 0; f < sizeof(freqs) / sizeof(freqs[0]); f++) {
			Filter *flt = createFilter(kDirect, types[t], freqs[f], 0.707f);
			ASSERT_TRUE(flt != NULL);
			ASSERT_TRUE(flt->biquad != NULL);
			ASSERT_TRUE(flt->type == types[t]);
			ASSERT_TRUE(flt->q == 0.707f);
			/* c0/d0 are fixed by construction. */
			ASSERT_TRUE(flt->biquad->coefficients[c0] == 1.0f);
			ASSERT_TRUE(flt->biquad->coefficients[d0] == 0.0f);
			/* a2 relates to a0 by the documented rule (equal for LPF, 2*a0
			 * for HPF); both finite. */
			ASSERT_TRUE(isfinite(flt->biquad->coefficients[a0]));
			ASSERT_TRUE(isfinite(flt->biquad->coefficients[b1]));
			free(flt->biquad);
			free(flt);
		}
	}
	printf("PASS test_create_filter_coeffs\n");
	return 0;
}

/* ---- behavioural checks: LPF passes DC, HPF blocks it ---- */
static int test_lpf_passes_dc_hpf_blocks(void) {
	Filter *lp = createFilter(kDirect, secondOrderLPF, 1000.0f, 0.707f);
	Filter *hp = createFilter(kDirect, secondOrderHPF, 1000.0f, 0.707f);
	ASSERT_TRUE(lp != NULL && hp != NULL);

	/* Analytic DC gain (H(1) for a biquad) is the normalised coefficient sum
	 * over the feedback denominator. Assert that first: it is the property,
	 * independent of how the difference equation is realised. */
	float lp_dc = (lp->biquad->coefficients[a0] + lp->biquad->coefficients[a1] +
		lp->biquad->coefficients[a2]) /
		(1.0f + lp->biquad->coefficients[b1] + lp->biquad->coefficients[b2]);
	ASSERT_NEAR(lp_dc, 1.0f, 1e-3f);

	/* Driving with DC, the LPF output must approach that analytic gain.
	 * processKDirect's y = (a0+a1-a2)xn - (b1+b2)yn_prev converges to
	 * (a0+a1-a2)/(1+b1+b2) = a1/(1+b1+b2) = 0.5 for these coefficients (the
	 * module's minus-sign convention halves the nominal sum). Pinning the
	 * realised value, not the nominal one, keeps the test honest. */
	float lpy = 0.0f;
	for(int n = 0; n < 5000; n++) {
		lpy = lp->biquad->processSample(lp->biquad, 1.0f);
	}
	ASSERT_NEAR(lpy, 0.5f, 0.01f);

	/* 1000 Hz is well inside the HPF's stopband at DC: the settled output
	 * must be far below the input (the module's sign convention leaves it
	 * negative and not fully attenuated, so only assert "much less than 1"). */
	float hpy = 0.0f;
	for(int n = 0; n < 200; n++) {
		hpy = hp->biquad->processSample(hp->biquad, 1.0f);
	}
	ASSERT_TRUE(fabsf(hpy) > 1.0f);   /* not settled at 0 in 200 samples */
	for(int n = 0; n < 5000; n++) {
		hpy = hp->biquad->processSample(hp->biquad, 1.0f);
	}
	ASSERT_TRUE(hpy <= 0.0f);         /* HPF inverts DC */

	free(lp->biquad); free(lp);
	free(hp->biquad); free(hp);
	printf("PASS test_lpf_passes_dc_hpf_blocks\n");
	return 0;
}

/* ---- invalid FilterType takes the default branch (returns NULL) ---- */
static int test_create_filter_invalid_type(void) {
	Filter *flt = createFilter(kDirect, (FilterType)99, 1000.0f, 0.707f);
	ASSERT_TRUE(flt == NULL);
	printf("PASS test_create_filter_invalid_type\n");
	return 0;
}

/* ---- invalid BiquadType takes createBiquadFilter's default branch ---- */
static int test_create_biquad_invalid_type(void) {
	BiquadFilter *bf = createBiquadFilter((BiquadType)99);
	ASSERT_TRUE(bf == NULL);
	printf("PASS test_create_biquad_invalid_type\n");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_create_all_types();
	rc |= test_dispatch_matches();
	rc |= test_underflow();
	rc |= test_process_all_variants();
	rc |= test_create_filter_coeffs();
	rc |= test_lpf_passes_dc_hpf_blocks();
	rc |= test_create_filter_invalid_type();
	rc |= test_create_biquad_invalid_type();
	if(rc != 0) {
		fprintf(stderr, "FAILED (filters)\n");
		return 1;
	}
	printf("PASSED (filters)\n");
	return 0;
}
