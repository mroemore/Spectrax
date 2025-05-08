#include "modsystem.h"
// #include "envelope.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

WavetablePool *envTables;

static float _clampValue(float value, float min, float max) {
	if(value < min) return min;
	if(value > max) return max;
	return value;
}

void initModSystem() {
	envTables = createWavetablePool();
	generateCurveWavetables(envTables, 16, 1024);
}

void generateCurve(float *data, size_t length, float curve, int steepnessFactor) {
	for(int i = 0; i < length; i++) {
		const float t = (float)i / (length - 1);
		const float epsilon = 0.0001f;
		if(fabsf(curve - 0.5f) < epsilon) {
			data[i] = t;
		} else if(curve > 0.5f) {
			data[i] = powf(t, curve * 2 * steepnessFactor);
		} else if(curve < 0.5f) {
			data[i] = 1.0f - powf(1 - t, (1 - curve) * 2 * steepnessFactor);
		}
	}
}

void generateCurveWavetables(WavetablePool *wtp, size_t iterations, size_t wtLength) {
	for(int i = 0; i < iterations; i++) {
		float steepnessScaler = 0.5f + (fabsf(1 + i - ((float)iterations / 2)) / iterations);
		steepnessScaler *= 3;
		float currentTable[wtLength];
		generateCurve(currentTable, wtLength, (float)i / iterations, steepnessScaler);
		loadWavetable(wtp, "test", currentTable, wtLength);
	}
}

ModList *createModList() {
	ModList *list = (ModList *)malloc(sizeof(ModList));
	if(!list) {
		printf("could not allocate memory for modList.\n");
		return NULL;
	}
	list->count = 0;
	return list;
}

ParamList *createParamList() {
	ParamList *list = (ParamList *)malloc(sizeof(ParamList));
	if(!list) {
		printf("could not allocate memory for paramList.\n");
		return NULL;
	}
	list->count = 0;
	return list;
}

void clearParamList(ParamList *list) {
	if(list == NULL) {
		printf("ERROR: clearParamList list is NULL.\n");
		return;
	}
	if(list->count <= 0) {
		printf("WARNING: clearParamList list is empty. Size: %i\n", list->count);
		return;
	}
	for(int i = 0; i < list->count; i++) {
		freeParameter(list->params[i]);
	}
	list->count = 0;
}

void clearModList(ModList *list) {
	if(list == NULL) {
		printf("ERROR: clearModList list is NULL.\n");
		return;
	}
	if(list->count <= 0) {
		printf("WARNING: clearModList list is empty. Size: %i\n", list->count);
		return;
	}
	for(int i = 0; i < list->count; i++) {
		freeMod(list->mods[i]);
	}
	list->count = 0;
}

void addToModList(ModList *list, Mod *mod) {
	if(list->count < MAX_MODS) {
		list->mods[list->count++] = mod;
	}
}

Parameter *createParameter(ParamList *paramList, const char *name, float initialValue, float minValue, float maxValue) {
	Parameter *param = (Parameter *)malloc(sizeof(Parameter));
	if(param) {
		param->name = strndup(name, MAX_NAME_LEN);
		param->minValue = minValue;
		param->maxValue = maxValue;
		param->baseValue = _clampValue(initialValue, minValue, maxValue);
		param->currentValue = param->baseValue;
		param->fineIncrement = 0.01f;
		param->coarseIncrement = 0.10f;
		param->modulators = NULL;
		param->modulator_count = 0;
		param->onChange.cbData = NULL;
		param->onChange.cbFunc = NULL;
	}

	addToParamList(paramList, param);
	return param;
}

Parameter *createParameterEx(ParamList *paramList, const char *name, float initialValue, float minValue, float maxValue, float fineIncrement, float coarseIncrement) {
	Parameter *p = createParameter(paramList, name, initialValue, minValue, maxValue);
	p->coarseIncrement = coarseIncrement;
	p->fineIncrement = fineIncrement;
	return p;
}

Parameter *createParameterPro(ParamList *paramList, const char *name, float initialValue, float minValue, float maxValue, float fineIncrement, float coarseIncrement, void *callbackData, CallbackFunction callbackFunction) {
	Parameter *p = createParameter(paramList, name, initialValue, minValue, maxValue);
	p->coarseIncrement = coarseIncrement;
	p->fineIncrement = fineIncrement;
	p->onChange.cbData = callbackData;
	p->onChange.cbFunc = callbackFunction;
	return p;
}

void addToParamList(ParamList *list, Parameter *param) {
	if(list->count < MAX_PARAMS) {
		list->params[list->count++] = param;
	}
}

void setParameterValue(Parameter *param, float value) {
	// DEBUG_LOG("set param");
	float clamped = _clampValue(value, param->minValue, param->maxValue);
	float oldVal = param->currentValue;
	param->currentValue = clamped;
	if(fabs(fabs(oldVal) - fabs(clamped)) > 0.001f) {
		if(param->onChange.cbData != NULL && param->onChange.cbFunc != NULL) {
			param->onChange.cbFunc(param->onChange.cbData);
		}
	}
}

void setParameterBaseValue(Parameter *param, float value) {
	// DEBUG_LOG("set param");
	float clamped = _clampValue(value, param->minValue, param->maxValue);
	float oldVal = param->baseValue;
	param->baseValue = clamped;
	if(fabs(fabs(oldVal) - fabs(clamped)) > 0.001f) {
		if(param->onChange.cbData != NULL && param->onChange.cbFunc != NULL) {
			param->onChange.cbFunc(param->onChange.cbData);
		}
	}
}

void setParameterMinValue(Parameter *param, float min) {
	if(min < param->maxValue) {
		param->minValue = min;
	}
}

void setParameterMaxValue(Parameter *param, float max) {
	if(max < param->minValue) {
		param->maxValue = max;
	}
}

float getParameterValue(Parameter *param) {
	// DEBUG_LOG("get param");
	return param->currentValue;
}

int getParameterValueAsInt(Parameter *param) {
	return (int)round(param->currentValue);
}

ModConnection *createConnection(ParamList *paramList, Mod *source, float amount, ModulationOperation type) {
	// DEBUG_LOG("create con");

	ModConnection *conn = (ModConnection *)malloc(sizeof(ModConnection));
	if(conn) {
		conn->source = source;
		conn->amount = createParameter(paramList, "mod amount", 1.0f, 0.0f, 1.0f);
		conn->type = createParameterEx(paramList, "mod operation", (float)type, 0.0f, (float)MT_COUNT, 1.0f, 10.0f); // Set modulation type
		conn->next = NULL;
		conn->previous = NULL;
	}
	return conn;
}

bool addModulation(ParamList *paramList, Mod *source, Parameter *destination, float amount, ModulationOperation type) {
	ModConnection *conn = createConnection(paramList, source, amount, type);
	if(!conn) return false;
	if(destination->modulators == NULL) {
		destination->modulators = conn;
	} else {
		// Add to front of list
		conn->next = destination->modulators;
		destination->modulators->previous = conn;
		destination->modulators = conn;
	}
	destination->modulator_count++;

	return true;
}
void initRandDefaults(Random *rnd, ParamList *paramList, float rate, RandomType type) {
	rnd->lastPhase = 0.0f;
	rnd->lastRandom = 0.0f;
	rnd->rate = createParameter(paramList, "RNG rate", rate, 0.1f, 100.0f);
	rnd->phase = createParameter(paramList, "RNG phase", 0.0f, 0.0f, 1.0f);
	rnd->shape = type;
}
Random *createRandom(ParamList *paramList, ModList *modList, int index, float rate, RandomType type, char *name) {
	Random *rnd = (Random *)malloc(sizeof(Random));

	ModGenerate genFunc;
	switch(type) {
		case RT_DRK:
			genFunc = generateDrunk;
			break;
		default:
		case RT_SNH:
			genFunc = generateRandom;
			break;
	}
	initMod((Mod *)rnd, paramList, name, MT_RND, genFunc);
	initRandDefaults(rnd, paramList, rate, type);
	addToModList(modList, &rnd->base);

	return rnd;
}

void initLfoDefaults(LFO *lfo, ParamList *paramList, float rate, int shape) {
	lfo->rate = createParameter(paramList, "LFO rate", rate, 0.1f, 100.0f);
	lfo->phase = createParameter(paramList, "LFO phase", 0.0f, 0.0f, 1.0f);
	lfo->shape = shape;
}

LFO *createLFO(ParamList *paramList, ModList *modList, int index, float rate, int shape, const char *name) {
	LFO *lfo = (LFO *)malloc(sizeof(LFO));
	ModGenerate genFunc;
	switch(shape) {
		case LS_SQU:
			genFunc = generateSquare;
			break;
		case LS_RMP:
			genFunc = generateRamp;
			break;
		default:
		case LS_SIN:
			genFunc = generateSine;
			break;
	}
	initMod((Mod *)lfo, paramList, name, MT_LFO, genFunc);
	initLfoDefaults(lfo, paramList, rate, shape);
	addToModList(modList, &lfo->base);

	return lfo;
}

void generateSine(void *self) {
	LFO *lfo = (LFO *)self;
	float value = sinf(getParameterValue(lfo->phase) * TWO_PI);
	setParameterBaseValue(lfo->base.output, value);
	setParameterValue(lfo->base.output, value);
}

void generateSquare(void *self) {
	LFO *lfo = (LFO *)self;
	float value = getParameterValue(lfo->phase) < 0.5f ? 1.0f : -1.0f;
	setParameterBaseValue(lfo->base.output, value);
	setParameterValue(lfo->base.output, value);
}

void generateRamp(void *self) {
	LFO *lfo = (LFO *)self;
	float value = (getParameterValue(lfo->phase) - 1.0f) * 2.0f;
	setParameterBaseValue(lfo->base.output, value);
	setParameterValue(lfo->base.output, value);
}

void generateRandom(void *self) {
	Random *rnd = (Random *)self;
	float phase = getParameterValue(rnd->phase);

	if(phase < rnd->lastPhase) {
		rnd->lastRandom = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
	}

	rnd->lastPhase = phase;
	setParameterBaseValue(rnd->base.output, rnd->lastRandom);
	setParameterValue(rnd->base.output, rnd->lastRandom);
}

void generateDrunk(void *self) {
	Random *rnd = (Random *)self;
	float phase = getParameterValue(rnd->phase);

	rnd->lastRandom = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
	rnd->lastRandom *= 0.5f * ((float)rand() / (float)RAND_MAX);
	rnd->lastPhase = phase;
	setParameterBaseValue(rnd->base.output, rnd->base.output->currentValue + rnd->lastRandom);
	setParameterValue(rnd->base.output, rnd->base.output->currentValue + rnd->lastRandom);
}

void updateMod(Mod *mod, float deltaTime) {
	// DEBUG_LOG("update mod");
	if(mod == NULL) return;

	switch(mod->type) {
		Envelope *env = NULL;
		LFO *lfo = NULL;
		Random *rand = NULL;
		float l_phase = 0.0f;
		float r_phase = 0.0f;
		float l_rate = 0.0f;
		float r_rate = 0.0f;
		case MT_ENV:
			env = (Envelope *)mod;
			if(env->isTriggered) {
				env->currentTime += deltaTime;
			}
			break;
		case MT_LFO:
			lfo = (LFO *)mod;
			l_phase = getParameterValue(lfo->phase);
			l_rate = getParameterValue(lfo->rate);
			l_phase += l_rate * deltaTime;
			if(l_phase >= 1.0f) l_phase -= 1.0f;
			setParameterBaseValue(lfo->phase, l_phase);
			setParameterValue(lfo->phase, l_phase);
			break;
		case MT_RND:
			rand = (Random *)mod;
			r_phase = getParameterValue(rand->phase);
			r_rate = getParameterValue(rand->rate);
			r_phase += r_rate * deltaTime;
			if(r_phase >= 1.0f) r_phase -= 1.0f;
			setParameterBaseValue(rand->phase, r_phase);
			setParameterValue(rand->phase, r_phase);
		default:
			break;
	}
	// DEBUG_LOG("update mod DONE");
}

float applyCurve(float x, float curvature) {
	// Ensure inputs are in valid ranges
	x = fmaxf(0.0f, fminf(1.0f, x));
	curvature = fmaxf(0.0f, fminf(1.0f, curvature));

	if(fabsf(curvature - 0.5f) < 0.001f) {
		return x; // Linear interpolation
	}

	// Convert curvature from [0,1] to [-4,4] for more pronounced effect
	float curve_amount = (curvature - 0.5f) * 8.0f;

	// Apply exponential curve
	if(curve_amount > 0) {
		return powf(x, 1.0f + curve_amount);
	} else {
		return 1.0f - powf(1.0f - x, 1.0f - curve_amount);
	}
}

void triggerEnvelope(Envelope *env) {
	// DEBUG_LOG("triggering env");
	env->currentStageIndex = 0;
	env->currentTime = 0;
	env->isTriggered = true;
}

void generateEnvelope(void *self) {
	Envelope *env = (Envelope *)self;
	if(!env || !env->isTriggered || env->currentStageIndex >= env->stageCount) {
		return;
	}

	EnvelopeStage *stage = &env->stages[env->currentStageIndex];
	if(!stage->duration || !stage->curvature) {
		return;
	}

	float dt = 1.0f / PA_SR;
	env->currentTime += dt;

	int tIdx = 8;
	Wavetable *wt = envTables->tables[tIdx];
	float t = env->currentTime / stage->duration->baseValue;
	int index0 = (int)(t * wt->length);
	int index1 = index0 < wt->length ? index0 + 1 : index0;
	float diff = fmodf(t, 1.0f);
	float enval = wt->data[index0] * (1.0 - diff) + wt->data[index1] * diff;

	// float shapedT = applyCurve(t, stage->curvature->currentValue);

	float startLevel = (env->currentStageIndex > 0) ? env->stages[env->currentStageIndex - 1].targetLevel : 0.0f;

	env->currentLevel = startLevel + (stage->targetLevel - startLevel) * enval;

	if(index0 >= wt->length - 1) {
		// printf("stage %i complete\n", env->currentStageIndex);

		env->currentTime = 0.0f;
		env->currentLevel = stage->targetLevel;
		if(++env->currentStageIndex >= env->stageCount) {
			env->isTriggered = false;
			// printf("TRIGGER OFF!!!!!!\n");
		}
	}

	// Important: Update output parameter
	setParameterBaseValue(env->base.output, env->currentLevel);
	setParameterValue(env->base.output, env->currentLevel);
}

void modifyParameterValue(Parameter *parameter, float relativeValue) {
	float currentValue = getParameterValue(parameter);
	setParameterValue(parameter, currentValue + relativeValue);
}

void modifyParameterBaseValue(Parameter *parameter, float relativeValue) {
	float currentValue = parameter->baseValue;
	setParameterBaseValue(parameter, currentValue + relativeValue);
}

void incParameterBaseValue(Parameter *parameter, float relativeValue) {
	float currentValue = parameter->baseValue;
	float sign = 1.0f;
	if(relativeValue < 0.0f) {
		sign = -1.0f;
	}
	if(abs(relativeValue) > 1) {
		setParameterBaseValue(parameter, currentValue + parameter->coarseIncrement * sign);
	} else {
		setParameterBaseValue(parameter, currentValue + parameter->fineIncrement * sign);
	}
}

void initMod(Mod *mod, ParamList *paramList, const char *name, ModType type, ModGenerate generate) {
	strncpy(mod->name, name, MAX_NAME_LEN);
	mod->type = type;
	mod->output = createParameter(paramList, "output", 0.0f, 0.0f, 1.0f);
	mod->generate = generateEnvelope;
	mod->dependency_count = 0;
	mod->processed = false;
	mod->visiting = false;
}

void initEnvelopeDefaults(Envelope *env) {
	env->currentLevel = 0.0f;
	env->currentStageIndex = 0;
	env->stageCount = 0;
	env->currentTime = 0.0f;
	env->totalElapsedTime = 0.0f;
	env->isTriggered = false;
	env->isSustaining = false;
	env->loop = false;
}

Envelope *createEnvelope(ParamList *paramList, ModList *modList, const char *name) {
	Envelope *env = (Envelope *)malloc(sizeof(Envelope));
	initMod((Mod *)env, paramList, name, MT_ENV, generateEnvelope);
	initEnvelopeDefaults(env);

	addToModList(modList, &env->base);

	return env;
}

void addEnvelopeStage(ParamList *paramList, Envelope *env, bool isRising, float duration, float targetLevel, float initialCurvature, char *name) {
	if(env->stageCount >= MAX_ENVELOPE_STAGES) {
		return;
	}

	char nameBuf[32];
	int idx = env->stageCount;

	EnvelopeStage *stage = &env->stages[idx];
	stage->isRising = isRising;
	stage->isSustain = (duration <= 0.0f);
	strncpy(stage->name, name, MAX_NAME_LEN);

	stage->duration = createParameter(paramList, "duration", duration, 0.001f, 10.0f);
	stage->targetLevel = targetLevel;
	stage->curvature = createParameter(paramList, "curve", initialCurvature, -1.0f, 1.0f);
	env->stageCount++;
}

void addParamPointerEnvelopeStage(ParamList *paramList, Envelope *env, bool isRising, Parameter *duration, float targetLevel, Parameter *initialCurvature, char *name) {
	// DEBUG_LOG("add env stage");
	if(env->stageCount >= MAX_ENVELOPE_STAGES) {
		return;
	}

	char nameBuf[32];
	int idx = env->stageCount;

	EnvelopeStage *stage = &env->stages[idx];
	stage->isRising = isRising;
	stage->isSustain = (duration->baseValue <= 0.0f);
	strncpy(stage->name, name, MAX_NAME_LEN);

	stage->duration = duration;

	stage->targetLevel = targetLevel;

	stage->curvature = initialCurvature;

	env->stageCount++;
}

Envelope *createADSR(ParamList *paramList, ModList *modList, float a, float d, float s, float r, char *name) {
	// DEBUG_LOG("create adsr");
	Envelope *env = createEnvelope(paramList, modList, name);

	addEnvelopeStage(paramList, env, true, a, 1.0f, 0.75f, "A");  // Attack
	addEnvelopeStage(paramList, env, false, d, 0.7f, 0.75f, "D"); // Decay
	addEnvelopeStage(paramList, env, true, s, 0.7f, 0.5f, "S");   // Sustain
	addEnvelopeStage(paramList, env, false, r, 0.0f, 0.75f, "R"); // Release

	return env;
}

Envelope *createAD(ParamList *paramList, ModList *modList, float a, float d, char *name) {
	Envelope *env = createEnvelope(paramList, modList, name);

	addEnvelopeStage(paramList, env, true, a, 1.0f, 0.95f, "A"); // Attack
	addEnvelopeStage(paramList, env, false, d, 0.0f, 0.1f, "D"); // Decay

	return env;
}

Envelope *createParamPointerAD(ParamList *paramList, ModList *modList, Parameter *a, Parameter *d, Parameter *acurve, Parameter *dcurve, char *name) {
	Envelope *env = createEnvelope(paramList, modList, name);

	addParamPointerEnvelopeStage(paramList, env, true, a, 1.0f, acurve, "A");  // Attack
	addParamPointerEnvelopeStage(paramList, env, false, d, 0.0f, dcurve, "D"); // Decay

	return env;
}

void initADPresetData(ModPreset *mp, float aDuration, float dDuration, float aCurve, float dCurve) {
	mp->type = MT_ENV;

	mp->md.env = (EnvPresetData){
		.loop = false,
		.stageCount = 2
	};
	mp->md.env.stages[0] = (EnvStagePresetData){
		.duration = aDuration,
		.curvature = aCurve,
		.isRising = true,
		.isSustain = false,
		.name = "AD_Atk",
		.targetLevel = 1.0
	};
	mp->md.env.stages[1] = (EnvStagePresetData){
		.duration = dDuration,
		.curvature = dCurve,
		.isRising = false,
		.isSustain = false,
		.name = "AD_Dec",
		.targetLevel = 0.0
	};
}
void initLfoPresetData(ModPreset *mp, LfoShape shape, float rate, float phase) {
	mp->type = MT_LFO;
	mp->md.lfo = (LfoPresetData){
		.phase = phase,
		.rate = rate,
		.shape = shape
	};
}
void initRandPresetData(ModPreset *mp, LfoShape shape, float rate, float phase) {
	mp->type = MT_RND;
	mp->md.rand = (RandPresetData){
		.phase = phase,
		.rate = rate,
		.shape = shape
	};
}
void initEnvelopeFromPreset(EnvPresetData *epd, Envelope *e, ParamList *paramList, ModList *modlist) {
	if(!epd || !e || !paramList) {
		printf("ERROR: NULL passed to envelope preset init.\n");
		return;
	}
	initEnvelopeDefaults(e);
	e->loop = epd->loop;
	e->stageCount = epd->stageCount;
	for(int i = 0; i < e->stageCount; i++) {
		e->stages[i] = (EnvelopeStage){
			.curvature = createParameter(paramList, "es_Curve", epd->stages[i].curvature, 0.0f, 1.0f),
			.duration = createParameter(paramList, "es_Duration", epd->stages[i].duration, 0.001f, 10.0f),
			.isRising = epd->stages[i].isRising,
			.isSustain = epd->stages[i].isSustain,
			.targetLevel = epd->stages[i].targetLevel
		};
		strncpy(e->stages[i].name, epd->stages[i].name, MAX_NAME_LEN);
	}

	if(modlist) {
		addToModList(modlist, &e->base);
	}
}
void saveEnvPreset(EnvPresetData *epd, Envelope *e) {
	if(!epd || !e) {
		printf("ERROR: NULL passed to envelope preset save.\n");
		return;
	}
	epd->loop = e->loop;
	epd->stageCount = e->stageCount;
	for(int i = 0; i < e->stageCount; i++) {
		epd->stages[i] = (EnvStagePresetData){
			.curvature = getParameterValue(e->stages[i].curvature),
			.duration = getParameterValue(e->stages[i].duration),
			.isRising = e->stages[i].isRising,
			.isSustain = e->stages[i].isSustain,
			.targetLevel = e->stages[i].targetLevel
		};
		strncpy(epd->stages[i].name, e->stages[i].name, MAX_NAME_LEN);
	}
}
void initLfoFromPreset(LfoPresetData *lpd, LFO *lfo, ParamList *paramList, ModList *modlist) {
	ModGenerate genFunc;
	switch(lpd->shape) {
		case LS_SQU:
			genFunc = generateSquare;
			break;
		case LS_RMP:
			genFunc = generateRamp;
			break;
		default:
		case LS_SIN:
			genFunc = generateSine;
			break;
	}
	initMod((Mod *)lfo, paramList, "LFO", MT_LFO, genFunc);
	initLfoDefaults(lfo, paramList, lpd->rate, lpd->shape);

	if(modlist) {
		addToModList(modlist, &lfo->base);
	}
}
void saveLfoPreset(LfoPresetData *lpd, LFO *lfo) {
	lpd->phase = getParameterValue(lfo->phase);
	lpd->rate = getParameterValue(lfo->rate);
	lpd->shape = lfo->shape;
}
void initRandFromPreset(RandPresetData *rpd, Random *rnd, ParamList *paramList, ModList *modlist) {
}
void saveRandPreset(RandPresetData *rpd, Random *rng) {
}

void processModulations(ParamList *paramList, ModList *modList, float deltaTime) {
	if(!modList) return;
	if(!paramList) return;

	for(int i = 0; i < modList->count; i++) {
		Mod *mod = modList->mods[i];
		updateMod(mod, deltaTime);
		if(!mod->generate) continue;

		mod->generate(mod);
	}

	for(int i = 0; i < paramList->count; i++) {
		ModConnection *conn = paramList->params[i]->modulators;
		float finalValue = paramList->params[i]->baseValue;

		while(conn != NULL) {
			float modValue = getParameterValue(conn->source->output);

			switch(getParameterValueAsInt(conn->type)) {
				case MO_ADD:
					finalValue += modValue;
					break;
				case MO_MUL:
					finalValue *= modValue;
					break;
				case MO_SUB:
					finalValue -= modValue;
					break;
				case MO_DIV:
					if(modValue != 0.0f) {
						finalValue /= modValue;
					}
					break;
				default:
					break;
			}

			ModConnection *next = conn->next;
			conn = next;
		}
		setParameterValue(paramList->params[i], finalValue);
	}
}

void freeParameter(Parameter *param) {
	if(!param) {
		return;
	}

	ModConnection *current = param->modulators;
	while(current != NULL) {
		ModConnection *next = current->next;
		if(current) {
			free(current);
		}
		current = next;
	}
	param->modulators = NULL;

	if(param->name) {
		free(param->name);
		param->name = NULL;
	}
	free(param);
}

void freeMod(Mod *mod) {
	if(!mod) {
		return;
	}

	if(mod->output) {
		freeParameter(mod->output);
		mod->output = NULL;
	}

	free(mod);
}

void freeModList(ModList *list) {
	if(!list) {
		return;
	}

	for(int i = 0; i < list->count; i++) {
		freeMod(list->mods[i]);
	}
	free(list);
}

void freeParamList(ParamList *list) {
	if(!list) {
		return;
	}
	for(int i = 0; i < list->count; i++) {
		freeParameter(list->params[i]);
	}
	free(list);
}

void freeLFO(LFO *lfo) {
	if(!lfo) return;

	// Free parameters in specific order
	if(lfo->phase) {
		freeParameter(lfo->phase);
		lfo->phase = NULL;
	}
	if(lfo->rate) {
		freeParameter(lfo->rate);
		lfo->rate = NULL;
	}
	if(lfo->base.output) {
		freeParameter(lfo->base.output);
		lfo->base.output = NULL;
	}

	free(lfo);
}

void freeRandom(Random *rnd) {
	if(!rnd) return;

	freeParameter(rnd->base.output);
	freeParameter(rnd->rate);
	freeParameter(rnd->phase);

	free(rnd);
}

void freeEnvelope(Envelope *env) {
	if(!env) return;

	freeParameter(env->base.output);

	for(int i = 0; i < env->stageCount; i++) {
		freeParameter(env->stages[i].duration);
		freeParameter(env->stages[i].curvature);
	}

	free(env);
}

void cleanupModSystem(ModList *list) {
	if(!list) return;

	for(int i = 0; i < list->count; i++) {
		Mod *mod = list->mods[i];
		if(!mod) continue;

		// Free mod-specific resources
		switch(mod->type) {
			case MT_LFO:
				freeLFO((LFO *)mod);
				break;
			case MT_RND:
				freeRandom((Random *)mod);
				break;
			case MT_ENV:
				freeEnvelope((Envelope *)mod);
				break;
			default:
				freeMod(mod);
		}
	}
	free(list);
}
