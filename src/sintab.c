#include "sintab.h"
#include <math.h>

static float g_tab[SIN_TAB_SIZE + 1];
static int g_ready = 0;

void sinTabInit(void) {
	if (g_ready) {
		return;
	}
	/* +1 entry: the interpolator reads tab[i+1], and a phase just under 1.0
	 * would otherwise read one past the end. The duplicate endpoint keeps
	 * the wrap seamless. */
	for (int i = 0; i <= SIN_TAB_SIZE; i++) {
		g_tab[i] = sinf(6.28318530717958647692f * (float)i / (float)SIN_TAB_SIZE);
	}
	g_ready = 1;
}

float sinTab(float phase) {
	if (!g_ready) {
		sinTabInit();
	}
	/* Wrap into [0,1). The phase can arrive negative (a modulator may push
	 * it below zero) or above 1.0 (an unclamped increment), so both ends are
	 * folded. floorf keeps the fractional part in [0,1), which (int) cast
	 * alone would not: a cast truncates toward zero, so a phase of -0.25
	 * would land on index 0 with a negative fraction and index -1. */
	float f = phase - floorf(phase);
	/* f == 1.0f is reachable when phase is negative-but-integral after the
	 * subtraction; keep the index in range. */
	if (f >= 1.0f) {
		f = 0.0f;
	}
	float x = f * (float)SIN_TAB_SIZE;
	int i = (int)x;
	float frac = x - (float)i;
	return g_tab[i] + frac * (g_tab[i + 1] - g_tab[i]);
}
