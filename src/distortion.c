#include "distortion.h"

float fold(float sample, float ampFactor, float foldAttenuation){
	float overflow, ampsamp = sample * ampFactor;
	if(ampsamp > 1.0f){
		overflow = ampsamp - 1.0f;
		return 1.0f - overflow * foldAttenuation;
	} else if(ampsamp < -1.0f){
		overflow = ampsamp + 1.0f;
		return -1.0f + overflow * foldAttenuation;
	} else {
		return ampsamp;
	}
}