#include "sintab.h"
#include <math.h>

static float g_tab[SIN_TAB_SIZE + 1];
static int g_ready = 0;

/* sin of an angle in radians, for |a| <= pi/2, evaluated with a fixed
 * double-precision Taylor series.
 *
 * Every operation below is an IEEE-754 add or multiply, and both are
 * correctly rounded, so the result is bit-identical on every platform. That
 * is the whole point: the table used to be built with the platform's own
 * sine, whose result differs by an ULP between glibc's x86-64 and aarch64
 * implementations -- and a table that differs by an ULP makes every sample
 * that reads it differ by an ULP.
 *
 * The series is taken to a^23/23!: at a = pi/2 the first omitted term is
 * a^25/25! = 5.2e-21, far below double's 1.1e-16 resolution. */
static double canonSinQuarter(double a) {
	static const double C23 = -1.0 / 25852016738884976640000.0; /* 1/23! */
	static const double C21 = 1.0 / 51090942171709440000.0;    /* 1/21! */
	static const double C19 = -1.0 / 121645100408832000.0;     /* 1/19! */
	static const double C17 = 1.0 / 355687428096000.0;        /* 1/17! */
	static const double C15 = -1.0 / 1307674368000.0;          /* 1/15! */
	static const double C13 = 1.0 / 6227020800.0;              /* 1/13! */
	static const double C11 = -1.0 / 39916800.0;               /* 1/11! */
	static const double C9 = 1.0 / 362880.0;                   /* 1/9! */
	static const double C7 = -1.0 / 5040.0;                    /* 1/7! */
	static const double C5 = 1.0 / 120.0;                      /* 1/5! */
	static const double C3 = -1.0 / 6.0;                       /* 1/3! */
	double x2 = a * a;
	double p = C23;
	p = p * x2 + C21;
	p = p * x2 + C19;
	p = p * x2 + C17;
	p = p * x2 + C15;
	p = p * x2 + C13;
	p = p * x2 + C11;
	p = p * x2 + C9;
	p = p * x2 + C7;
	p = p * x2 + C5;
	p = p * x2 + C3;
	return a * (1.0 + x2 * p);
}

/* sin(2*pi*i/SIN_TAB_SIZE) for i in [0, SIN_TAB_SIZE], with the quadrant
 * symmetry applied first so the polynomial only ever sees [0, pi/2]. */
static double canonSinNode(int i) {
	if(i == 0 || i == SIN_TAB_SIZE) {
		/* Exactly zero, so the table's wrap is seamless: the last entry
		 * is the first one, not "nearly" it. */
		return 0.0;
	}
	int q = i;
	double sign = 1.0;
	if(q > SIN_TAB_SIZE / 2) {
		q = SIN_TAB_SIZE - q;
		sign = -1.0;
	}
	double a;
	if(q > SIN_TAB_SIZE / 4) {
		/* pi - a folds the second quadrant onto the first. */
		a = 3.14159265358979323846 - 6.28318530717958647692 * (double)q / (double)SIN_TAB_SIZE;
	} else {
		a = 6.28318530717958647692 * (double)q / (double)SIN_TAB_SIZE;
	}
	return sign * canonSinQuarter(a);
}

void sinTabInit(void) {
	if(g_ready) {
		return;
	}
	/* +1 entry: the interpolator reads tab[i+1], and a phase just under 1.0
	 * would otherwise read one past the end. The duplicate endpoint keeps
	 * the wrap seamless. */
	for(int i = 0; i <= SIN_TAB_SIZE; i++) {
		g_tab[i] = (float)canonSinNode(i);
	}
	g_ready = 1;
}

float sinTab(float phase) {
	if(!g_ready) {
		sinTabInit();
	}
	/* Wrap into [0,1). The phase can arrive negative (a modulator may push
	 * it below zero) or above 1.0 (an unclamped increment), so both ends are
	 * folded. floorf keeps the fractional part in [0,1), which (int) cast
	 * alone would not: a cast truncates toward zero, so a phase of -0.25
	 * would land on index 0 with a negative fraction and index -1.
	 * floorf is exact -- its result is representable -- so it is the same
	 * on every platform. */
	float f = phase - floorf(phase);
	/* f == 1.0f is reachable when phase is negative-but-integral after the
	 * subtraction; keep the index in range. */
	if(f >= 1.0f) {
		f = 0.0f;
	}
	float x = f * (float)SIN_TAB_SIZE;
	int i = (int)x;
	float frac = x - (float)i;
	return g_tab[i] + frac * (g_tab[i + 1] - g_tab[i]);
}
