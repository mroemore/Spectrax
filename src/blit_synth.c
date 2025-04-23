#include "blit_synth.h"
#include <math.h>
#include <stdint.h>

float poly_blep(float phaseIncrement, float t) {
	float dt = phaseIncrement / TWOPI;

	// t-t^2/2 +1/2
	// 0 < t <= 1
	// discontinuities between 0 & 1
	if(t < dt) {
		t /= dt;
		return t + t - t * t - 1.0f;
	} else if(t > 1.0f - dt) {
		t = (t - 1.0f) / dt;
		return t * t + t + t + 1.0f;
	}
	// no discontinuities
	// 0 otherwise
	else
		return 0.0f;
}

float noblep_sine(float phase) { return sin(TWOPI * phase); }

float blep_tri(float phase, float increment) {
	float value = 0.0f;      // Init output to avoid nasty surprises
	float t = phase / TWOPI; // Define half phase
	float ttwo = (2.0f * t);
	if(phase < 0.0f) {
		value = 1.0f - ttwo;
		//	value -= poly_blep(increment, t);
	} else {
		value = ttwo;
		//	value += poly_blep(increment, t);
	}

	value -= 1.0f;

	return value;
}

float blep_saw(float phase, float increment) {
	float value = 0.0f;      // Init output to avoid nasty surprises
	float t = phase / TWOPI; // Define half phase

	value = (2.0f * t) - 1.0f;        // Render naive waveshape
	value -= poly_blep(increment, t); // Layer output of Poly BLEP on top

	return value; // Output
}

float blep_square(float phase, float increment) {
	float value = 0.0f;      // Init output to avoid nasty surprises
	float t = phase / TWOPI; // Define half phase

	if(phase < M_PI) {
		value = 1.0f; // Flip
		value -=
		  poly_blep(increment, t); // Layer output of Poly BLEP on top (flip)

	} else {
		value = -1.0f; // Flop
		value += poly_blep(
		  increment,
		  fmod(t + 0.5f, 1.0f)); // Layer output of Poly BLEP on top (flop)
	}

	return value; // Output
}
