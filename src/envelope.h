#ifndef ENVELOPE_H
#define ENVELOPE_H

typedef enum {
	ADD,
	SUB,
	MUL,
	DIV
} ModulationType;

typedef struct {
	int current_sample_index;
	int duration_in_samples;
	float curve;
	float target;
	int direction;
} EnvStage;

typedef struct {
	int stageIndex;
	int stageCount;
	int running;
	int loop;
	int voice_id;
	float currentLevel;
	EnvStage* stages;
} Envelope;

typedef struct {
	float *target;
	float result;
	float min_val;
	float max_val;
	float attenuation;
	int modType;
} ModMap;

typedef struct {
	float value;
	ModMap *mod;
} Param;

ModMap* initModMap(float *target, float min, float max, float attenuation, int modType);
EnvStage* initEnvStage(float rate, float curve, int direction);
Envelope* defaultAD(float attack_sec, float decay_sec, int voice_id);
void tickEnv(Envelope *env);
void resetEnvelope(Envelope *env);			
float apply_envelope(float current, float total, float curve);
void applyModulation(ModMap *param, Envelope *env);

#endif