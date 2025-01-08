#include "envelope.h"
#include "settings.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

Envelope *defaultAD(float attack_sec, float decay_sec, int voice_id)
{
	Envelope *env = (Envelope *)malloc(sizeof(Envelope));
	env->stageCount = 2;
	env->stageIndex = 0;
	env->voice_id = voice_id;
	env->running = 0;
	env->loop = 0;
	env->stages = (EnvStage *)malloc(env->stageCount * sizeof(EnvStage));
	env->stages[0] = *initEnvStage(attack_sec, 1.0f, 1);
	env->stages[1] = *initEnvStage(decay_sec, 1.5f, -1);
	return env;
}

EnvStage *initEnvStage(float duration, float curve, int direction)
{
	EnvStage *stage = (EnvStage *)malloc(sizeof(EnvStage));
	stage->duration_in_samples = (int)(duration * 44100);
	stage->curve = curve;
	stage->current_sample_index = 0;
	stage->target = 1.0f;
	stage->direction = direction;
	return stage;	
}

ModMap* initModMap(float *target, float min, float max, float attenuation, int modType) {
	ModMap* map = (ModMap*)malloc(sizeof(ModMap));
	map->target = target;
	map->result = *target;
	map->min_val = min;
	map->max_val = max;
	map->attenuation = attenuation;
	map->modType = modType;
	return map;
}

void applyModulation(ModMap *param, Envelope *env){
	float envLevel = apply_envelope((float)env->stages[env->stageIndex].current_sample_index, (float)env->stages[env->stageIndex].duration_in_samples, env->stages[env->stageIndex].curve);
	if(envLevel < 0.1f){ //TO-DO fix this hacky shit
		envLevel = 0.0f;
	}
	if(env->stages[env->stageIndex].direction < 0){
		envLevel = 1.0f - envLevel;
	}

	//envLevel *= param->attenuation;
	switch(param->modType){
		case ADD:
			param->result = *param->target + envLevel;
			break;
		case SUB:
			param->result = *param->target - envLevel;
			break;
		case MUL:
			param->result = *param->target * envLevel;
			break;
		case DIV:
			param->result = *param->target / envLevel;
			break;
	}
}

void tickEnv(Envelope *env){
	int si = env->stageIndex;
	env->stages[si].current_sample_index += 2;
	if(env->stages[si].current_sample_index >= env->stages[si].duration_in_samples){
		env->stages[si].current_sample_index = 0;
		env->stageIndex = ++si;
		if(env->stageIndex  >= env->stageCount){
			env->stageIndex = 0;
			if(!env->loop){
				env->running = 0;
			}
		}
	}
}

void resetEnvelope(Envelope *env){
	env->currentLevel = 0.0f;
	env->stageIndex = 0;
	env->running = 0;
	for(int si = 0; si < env->stageCount; si++){
		env->stages[si].current_sample_index = 0;
	}
}

// void stopEnvelope(Envelope *env){
	
// }

float apply_envelope(float current, float total, float curve)
{
	if (curve == 1.0f)
	{
		return current / total; // Linear
	}
	else if (curve > 1.0f)
	{
		return powf(current / total, curve); // Exponential
	}
	else
	{
		return 1.0f - powf(1.0f - current / total, 1.0f / curve); // Logarithmic
	}
}