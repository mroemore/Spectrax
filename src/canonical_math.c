#include "canonical_math.h"
#include "sintab.h"

#include <math.h>
#include <float.h>

/* ln(2) and the constants derived from it, in double precision. */
#define CANON_LN2 0.69314718055994530942
#define CANON_SQRT_HALF 0.70710678118654752440

/* The smallest normal float is about 1.2e-38, whose natural logarithm is
 * -87.3. Below that the result of an exponential would be a subnormal, and a
 * subnormal is where NEON's flush-to-zero and x86's denormals-are-zero
 * disagree -- so the module flushes to zero instead. */
#define CANON_EXP_FLUSH_BELOW (-87.0f)

/* exp(x) for x in the normal range, evaluated in double precision.
 *
 * exp(x) = 2^k * exp(r) with k = round(x / ln 2) and r = x - k*ln2, so
 * |r| <= ln2/2 and the polynomial only has to cover a short interval. The
 * series is taken to r^12/12!: at |r| = 0.347 the first omitted term is
 * r^13/13! = 1.5e-18, well under double's 1.1e-16 resolution. */
static double canonExpDouble(double x) {
	int k = (int)floor(x / CANON_LN2 + 0.5);
	double r = x - (double)k * CANON_LN2;
	/* Horner over 1/12! down to 1/1!. */
	double p = 1.0 / 479001600.0;          /* 1/12! */
	p = p * r + 1.0 / 39916800.0;         /* 1/11! */
	p = p * r + 1.0 / 3628800.0;          /* 1/10! */
	p = p * r + 1.0 / 362880.0;           /* 1/9! */
	p = p * r + 1.0 / 40320.0;            /* 1/8! */
	p = p * r + 1.0 / 5040.0;             /* 1/7! */
	p = p * r + 1.0 / 720.0;              /* 1/6! */
	p = p * r + 1.0 / 120.0;              /* 1/5! */
	p = p * r + 1.0 / 24.0;               /* 1/4! */
	p = p * r + 1.0 / 6.0;                /* 1/3! */
	p = p * r + 1.0 / 2.0;                /* 1/2! */
	p = p * r + 1.0;                      /* 1/1! */
	p = p * r + 1.0;                      /* 1/0! */
	/* ldexp is an exact power-of-two scaling, so it introduces no rounding
	 * of its own. */
	return ldexp(p, k);
}

/* ln(x) for x > 0, evaluated in double precision.
 *
 * x = m * 2^e with m in [sqrt(1/2), sqrt(2)), then
 * ln(m) = 2*atanh(u) with u = (m-1)/(m+1) and |u| <= 0.1716, so the
 * atanh series converges fast: the first omitted term at that bound is
 * u^22/22 = 2e-18. */
static double canonLogDouble(double x) {
	int e = 0;
	double m = frexp(x, &e); /* m in [0.5, 1) */
	if(m < CANON_SQRT_HALF) {
		m *= 2.0;
		e -= 1;
	}
	double u = (m - 1.0) / (m + 1.0);
	double u2 = u * u;
	/* Horner over 1/21 down to 1/1, then multiplied by u. */
	double p = 1.0 / 21.0;
	p = p * u2 + 1.0 / 19.0;
	p = p * u2 + 1.0 / 17.0;
	p = p * u2 + 1.0 / 15.0;
	p = p * u2 + 1.0 / 13.0;
	p = p * u2 + 1.0 / 11.0;
	p = p * u2 + 1.0 / 9.0;
	p = p * u2 + 1.0 / 7.0;
	p = p * u2 + 1.0 / 5.0;
	p = p * u2 + 1.0 / 3.0;
	p = p * u2 + 1.0;
	return 2.0 * u * p + (double)e * CANON_LN2;
}

float canonicalSin(float phase) {
	return sinTab(phase);
}

float canonicalCos(float phase) {
	/* cos(2*pi*p) == sin(2*pi*(p + 1/4)). */
	return sinTab(phase + 0.25f);
}

float canonicalExp(float x) {
	if(x != x) {
		return x; /* NaN in, NaN out. */
	}
	if(x > 88.0f) {
		return FLT_MAX;
	}
	if(x < CANON_EXP_FLUSH_BELOW) {
		return 0.0f;
	}
	double r = canonExpDouble((double)x);
	if(r > (double)FLT_MAX) {
		return FLT_MAX;
	}
	return (float)r;
}

float canonicalLog(float x) {
	if(!(x > 0.0f)) {
		/* Zero, a negative number or a NaN. Every caller in this tree
		 * clamps its argument first; returning 0 here keeps a mistake
		 * audible rather than poisoning the whole graph with a NaN. */
		return 0.0f;
	}
	return (float)canonLogDouble((double)x);
}

float canonicalPow(float x, float y) {
	if(x < 0.0f) {
		/* Undefined here (and unused: the tree's pow callers clamp their
		 * base to [0,1]). Returning 0 keeps a mistake from turning into a
		 * NaN that would spread through the mix. */
		return 0.0f;
	}
	if(x == 0.0f) {
		if(y > 0.0f) {
			return 0.0f;
		}
		if(y == 0.0f) {
			return 1.0f;
		}
		return INFINITY;
	}
	if(y == 0.0f) {
		return 1.0f;
	}
	return canonicalExp(y * canonicalLog(x));
}

float canonicalOnePoleCoef(float fc, float sr) {
	if(!(sr > 0.0f) || fc <= 0.0f) {
		return 0.0f;
	}
	/* 1 - exp(-2*pi*fc/sr). The subtraction is done in double so the
	 * result keeps its precision for small coefficients, where the float
	 * subtraction would lose most of the significant bits. */
	double g = 1.0 - canonExpDouble(-2.0 * 3.14159265358979323846 * (double)fc / (double)sr);
	return (float)g;
}
