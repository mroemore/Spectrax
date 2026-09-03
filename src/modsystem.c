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
		/* Mod structs may be ENVs (heap-allocated as Envelope), generic Mods
		 * (heap-allocated as Mod), or detached params promoted to mods.
		 * mod->output / envelope stage duration+curvature Params are added
		 * to a ParamList by initMod / addEnvelopeStage and are owned by that
		 * ParamList — freeing them here would leave the ParamList with
		 * dangling pointers. clearParamList owns the Param lifecycle; we
		 * only free the Mod struct itself. */
		free(list->mods[i]);
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

bool removeFromModList(ModList *list, Mod *mod) {
	if(!list || !mod) {
		return false;
	}
	for(int i = 0; i < list->count; i++) {
		if(list->mods[i] == mod) {
			for(int j = i; j < list->count - 1; j++) {
				list->mods[j] = list->mods[j + 1];
			}
			list->count--;
			return true;
		}
	}
	return false;
}

bool removeFromParamList(ParamList *list, Parameter *param) {
	if(!list || !param) {
		return false;
	}
	for(int i = 0; i < list->count; i++) {
		if(list->params[i] == param) {
			for(int j = i; j < list->count - 1; j++) {
				list->params[j] = list->params[j + 1];
			}
			list->count--;
			return true;
		}
	}
	return false;
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
	param->currentValue = clamped; /* keep unmodulated value in sync (dials read currentValue) */
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
bool removeModulation(ParamList *list, Parameter *destination, Mod *source) {
	if(!list || !destination || !source) {
		return false;
	}
	ModConnection *conn = destination->modulators;
	while(conn != NULL) {
		ModConnection *next = conn->next;
		if(conn->source == source) {
			if(conn->previous) {
				conn->previous->next = conn->next;
			} else {
				destination->modulators = conn->next;
			}
			if(conn->next) {
				conn->next->previous = conn->previous;
			}
			destination->modulator_count--;
			if(conn->amount) {
				removeFromParamList(list, conn->amount);
				freeParameter(conn->amount);
			}
			if(conn->type) {
				removeFromParamList(list, conn->type);
				freeParameter(conn->type);
			}
			free(conn);
			return true;
		}
		conn = next;
	}
	return false;
}
int removeModulationsForSource(ParamList *list, Mod *source) {
	if(!list || !source) {
		return 0;
	}
	int removed = 0;
	/* We cannot remove params from the list while iterating it by index, so
	 * unlink+free connections now and drop their amount/type params in a
	 * second pass. */
	Parameter *orphans[MAX_PARAMS];
	int orphanCount = 0;
	for(int i = 0; i < list->count; i++) {
		Parameter *p = list->params[i];
		if(!p) {
			continue;
		}
		ModConnection *conn = p->modulators;
		while(conn != NULL) {
			ModConnection *next = conn->next;
			if(conn->source == source) {
				if(conn->previous) {
					conn->previous->next = conn->next;
				} else {
					p->modulators = conn->next;
				}
				if(conn->next) {
					conn->next->previous = conn->previous;
				}
				p->modulator_count--;
				if(orphanCount < MAX_PARAMS && conn->amount) {
					orphans[orphanCount++] = conn->amount;
				}
				if(orphanCount < MAX_PARAMS && conn->type) {
					orphans[orphanCount++] = conn->type;
				}
				free(conn);
				removed++;
			}
			conn = next;
		}
	}
	for(int k = 0; k < orphanCount; k++) {
		if(orphans[k]) {
			removeFromParamList(list, orphans[k]);
			freeParameter(orphans[k]);
		}
	}
	return removed;
}
bool removeMod(ModList *modList, ParamList *paramList, Mod *mod) {
	if(!modList || !paramList || !mod) {
		return false;
	}
	if(!removeFromModList(modList, mod)) {
		return false;
	}
	removeModulationsForSource(paramList, mod);
	/* Remove the mod's own params from the list (owned by paramList). */
	switch(mod->type) {
		case MT_LFO: {
			LFO *lfo = (LFO *)mod;
			if(lfo->rate) removeFromParamList(paramList, lfo->rate);
			if(lfo->phase) removeFromParamList(paramList, lfo->phase);
			if(lfo->shape) removeFromParamList(paramList, lfo->shape);
			break;
		}
		case MT_RND: {
			Random *rnd = (Random *)mod;
			if(rnd->rate) removeFromParamList(paramList, rnd->rate);
			if(rnd->phase) removeFromParamList(paramList, rnd->phase);
			if(rnd->shape) removeFromParamList(paramList, rnd->shape);
			break;
		}
		case MT_ENV: {
			Envelope *env = (Envelope *)mod;
			for(int i = 0; i < env->stageCount; i++) {
				if(env->stages[i].duration) {
					removeFromParamList(paramList, env->stages[i].duration);
				}
				if(env->stages[i].curvature) {
					removeFromParamList(paramList, env->stages[i].curvature);
				}
			}
			break;
		}
		default:
			break;
	}
	if(mod->output) {
		removeFromParamList(paramList, mod->output);
	}
	/* Params are no longer referenced by the list; free struct by type.
	 * (freeEnvelope/freeLFO/freeRandom free their params again — safe now
	 * because the list no longer holds those pointers.) */
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
			break;
	}
	return true;
}
bool rewireModulation(ParamList *list, Parameter *destination, Mod *oldSource, Mod *newSource) {
	(void)list;
	if(!destination || !newSource) {
		return false;
	}
	ModConnection *conn = destination->modulators;
	while(conn != NULL) {
		if(conn->source == oldSource) {
			conn->source = newSource;
			return true;
		}
		conn = conn->next;
	}
	return false;
}
void rewireModulationsForSource(ParamList *list, Mod *oldSource, Mod *newSource) {
	if(!list || !oldSource || !newSource) {
		return;
	}
	for(int i = 0; i < list->count; i++) {
		Parameter *p = list->params[i];
		if(!p) {
			continue;
		}
		ModConnection *c = p->modulators;
		while(c) {
			if(c->source == oldSource) {
				c->source = newSource;
			}
			c = c->next;
		}
	}
}

/* Swap a mod's type in place: same modList slot, same output parameter,
 * existing routes preserved. The old concrete struct is freed (its type
 * params are removed from paramList first); a fresh struct of `newType`
 * is registered in the same slot and all connections are rewired to it.
 * Runtime sources only — the caller (UI) guards against core indices. */
bool changeModType(ModList *modList, Mod *mod, ModType newType, ParamList *paramList) {
	if(!modList || !mod || !paramList) {
		return false;
	}
	if(newType != MT_ENV && newType != MT_LFO && newType != MT_RND) {
		return false;
	}
	if(mod->type == newType) {
		return true;
	}
	int slot = -1;
	for(int i = 0; i < modList->count; i++) {
		if(modList->mods[i] == mod) {
			slot = i;
			break;
		}
	}
	if(slot < 0) {
		return false;
	}
	Parameter *output = mod->output;
	char name[MAX_NAME_LEN];
	strncpy(name, mod->name, MAX_NAME_LEN);

/* Remove the OLD type params from the list (they are freed with the
	 * old struct below). */
	switch(mod->type) {
		case MT_LFO: {
			LFO *l = (LFO *)mod;
			if(l->rate) removeFromParamList(paramList, l->rate);
			if(l->phase) removeFromParamList(paramList, l->phase);
			if(l->shape) removeFromParamList(paramList, l->shape);
			break;
		}
		case MT_RND: {
			Random *r = (Random *)mod;
			if(r->rate) removeFromParamList(paramList, r->rate);
			if(r->phase) removeFromParamList(paramList, r->phase);
			if(r->shape) removeFromParamList(paramList, r->shape);
			break;
		}
		case MT_ENV: {
			Envelope *e = (Envelope *)mod;
			for(int i = 0; i < e->stageCount; i++) {
				if(e->stages[i].duration) removeFromParamList(paramList, e->stages[i].duration);
				if(e->stages[i].curvature) removeFromParamList(paramList, e->stages[i].curvature);
			}
			break;
		}
		default:
			break;
	}

	Mod *fresh = NULL;
	switch(newType) {
		case MT_LFO: {
			LFO *l = (LFO *)calloc(1, sizeof(LFO));
			memcpy(&l->base, mod, sizeof(Mod));
			l->base.output = output;
			l->base.type = MT_LFO;
			/* initLfoDefaults sets l->shapeValue, l->shape (Parameter), and
			 * l->base.generate via cbLfoShapeOnChange. */
			initLfoDefaults(l, paramList, 1.0f, LS_SIN);
			fresh = &l->base;
			break;
		}
		case MT_RND: {
			Random *r = (Random *)calloc(1, sizeof(Random));
			memcpy(&r->base, mod, sizeof(Mod));
			r->base.output = output;
			r->base.type = MT_RND;
			/* initRandDefaults sets r->shapeValue, r->shape (Parameter), and
			 * r->base.generate via cbRandShapeOnChange. */
			initRandDefaults(r, paramList, 1.0f, RT_SNH);
			fresh = &r->base;
			break;
		}
		case MT_ENV: {
			Envelope *e = (Envelope *)calloc(1, sizeof(Envelope));
			memcpy(&e->base, mod, sizeof(Mod));
			e->base.output = output;
			e->base.type = MT_ENV;
			initEnvelopeDefaults(e);
			addEnvelopeStage(paramList, e, true, 0.25f, 1.0f, 0.95f, "A");
			addEnvelopeStage(paramList, e, false, 4.25f, 0.0f, 0.1f, "D");
			e->base.generate = generateEnvelope;
			fresh = &e->base;
			break;
		}
		default:
			break;
	}
	if(!fresh) {
		return false;
	}

	rewireModulationsForSource(paramList, mod, fresh);
	modList->mods[slot] = fresh;

	/* Free the old struct WITHOUT freeing the shared output param. */
	mod->output = NULL;
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
			break;
	}
	return true;
}

void wrapIncrementParameter(Parameter *p, float step) {
	if(!p) {
		return;
	}
	float count = p->maxValue - p->minValue + 1.0f;
	if(count <= 0.0f) {
		setParameterBaseValue(p, p->minValue);
		return;
	}
	float v = p->baseValue + step;
	while(v > p->maxValue) {
		v -= count;
	}
	while(v < p->minValue) {
		v += count;
	}
	setParameterBaseValue(p, v);
}
void initRandDefaults(Random *rnd, ParamList *paramList, float rate, RandomType type) {
	rnd->lastPhase = 0.0f;
	rnd->lastRandom = 0.0f;
	rnd->rate = createParameter(paramList, "RNG rate", rate, 0.1f, 100.0f);
	rnd->phase = createParameter(paramList, "RNG phase", 0.0f, 0.0f, 1.0f);
	rnd->shape = createParameterPro(paramList, "RNG shape", (float)type, 0.0f, (float)(RT_COUNT - 1), 1.0f, 1.0f, rnd, cbRandShapeOnChange);
	rnd->shapeValue = type;
	cbRandShapeOnChange(rnd); /* sync base.generate + shapeValue from the param */
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

void cbLfoShapeOnChange(void *data) {
	LFO *lfo = (LFO *)data;
	if(!lfo) {
		return;
	}
	int sh = (lfo->shape) ? getParameterValueAsInt(lfo->shape) : lfo->shapeValue;
	if(sh < 0) sh = 0;
	if(sh >= LS_COUNT) sh = LS_COUNT - 1;
	lfo->shapeValue = sh;
	switch(sh) {
		case LS_SQU: lfo->base.generate = generateSquare; break;
		case LS_RMP: lfo->base.generate = generateRamp; break;
		default:     lfo->base.generate = generateSine; break;
	}
}

void cbRandShapeOnChange(void *data) {
	Random *rnd = (Random *)data;
	if(!rnd) {
		return;
	}
	int sh = (rnd->shape) ? getParameterValueAsInt(rnd->shape) : rnd->shapeValue;
	if(sh < 0) sh = 0;
	if(sh >= RT_COUNT) sh = RT_COUNT - 1;
	rnd->shapeValue = sh;
	switch(sh) {
		case RT_DRK: rnd->base.generate = generateDrunk; break;
		default:     rnd->base.generate = generateRandom; break;
	}
}

void initLfoDefaults(LFO *lfo, ParamList *paramList, float rate, int shape) {
	lfo->rate = createParameter(paramList, "LFO rate", rate, 0.1f, 100.0f);
	lfo->phase = createParameter(paramList, "LFO phase", 0.0f, 0.0f, 1.0f);
	lfo->shape = createParameterPro(paramList, "LFO shape", (float)shape, 0.0f, (float)(LS_COUNT - 1), 1.0f, 1.0f, lfo, cbLfoShapeOnChange);
	lfo->shapeValue = shape;
	cbLfoShapeOnChange(lfo); /* sync base.generate + shapeValue from the param */
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
void initEnvelopeFromPreset(ModPreset *mp, Envelope *e, ParamList *paramList, ModList *modlist) {
	if(!mp || !e || !paramList) {
		printf("ERROR: NULL passed to envelope preset init.\n");
		return;
	}
	mp->type = MT_ENV;
	EnvPresetData *epd = &mp->md.env;

	initMod((Mod *)e, paramList, "env", MT_ENV, generateEnvelope);
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
	initMod((Mod *)lfo, paramList, "LFO", MT_LFO, NULL);
	/* initLfoDefaults creates the shape Parameter, sets shapeValue from
	 * lpd->shape, and cbLfoShapeOnChange syncs lfo->base.generate. Any
	 * subsequent route that changes lfo->shape will also re-sync via that
	 * callback. */
	initLfoDefaults(lfo, paramList, lpd->rate, lpd->shape);

	if(modlist) {
		addToModList(modlist, &lfo->base);
	}
}
void saveLfoPreset(LfoPresetData *lpd, LFO *lfo) {
	lpd->phase = getParameterValue(lfo->phase);
	lpd->rate = getParameterValue(lfo->rate);
	lpd->shape = lfo->shapeValue;
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

	/* Mod structs may be ENVs (heap-allocated as Envelope), generic Mods
	 * (heap-allocated as Mod), or detached params promoted to mods.
	 * mod->output / envelope stage duration+curvature Params are owned by
	 * paramList (registered via initMod / addEnvelopeStage) and freed by
	 * the caller's freeParamList — freeing them here would double-free.
	 * Bare free matches clearModList's convention (see comment above). */
	for(int i = 0; i < list->count; i++) {
		free(list->mods[i]);
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
	if(lfo->shape) {
		freeParameter(lfo->shape);
		lfo->shape = NULL;
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
	freeParameter(rnd->shape);
	rnd->shape = NULL;

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
