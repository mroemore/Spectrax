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

/* Task 2.2: the modList holds BOTH sources (ENV/LFO/RND) and the
 * connection-internal attenuators addModulation inserts. Source-count
 * consumers (source container rows, envelopeCount sync, ADD/REMOVE
 * guards) must count only the non-atten mods. modIndexAt maps a source
 * position (0-based, skipping attens) back to the modList index; -1 if
 * out of range. */
int modSourceCount(ModList *list) {
	if(!list) {
		return 0;
	}
	int n = 0;
	for(int i = 0; i < list->count; i++) {
		if(list->mods[i] && list->mods[i]->type != MT_ATTEN) {
			n++;
		}
	}
	return n;
}

int modIndexAt(ModList *list, int sourcePos) {
	if(!list) {
		return -1;
	}
	int n = 0;
	for(int i = 0; i < list->count; i++) {
		if(list->mods[i] && list->mods[i]->type != MT_ATTEN) {
			if(n == sourcePos) {
				return i;
			}
			n++;
		}
	}
	return -1;
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

/* MT_ATTEN attenuator node: passes the upstream mod's output through an
 * amount scale (0..2), an optional unipolar clamp, and an optional
 * sign-preserving sqrt curve. Stateless across calls - updateMod has
 * nothing to advance for this type. */
static void modGenerateAtten(void *self) {
	Mod *m = (Mod *)self;
	if(!m->data.atten.input || !m->data.atten.input->output) {
		setParameterValue(m->output, 0.0f);
		return;
	}
	float v = getParameterValue(m->data.atten.input->output);
	float amt = m->data.atten.attenAmount ? getParameterValue(m->data.atten.attenAmount) : 1.0f;
	v *= amt;
	if(m->data.atten.attenPolarity && getParameterValueAsInt(m->data.atten.attenPolarity) == 1) {
		v = fmaxf(v, 0.0f);
	}
	if(m->data.atten.attenCurve && getParameterValueAsInt(m->data.atten.attenCurve) == 1) {
		v = copysignf(powf(fabsf(v), 0.5f), v);
	}
	setParameterValue(m->output, v);
}

/* MT_ATTEN attenuator factory: creates the connection-internal node that
 * addModulation inserts between a real source (ENV/LFO/RND) and its
 * destinations. input points at the upstream mod; the three params are
 * list-owned (named `<source>_amt/_pol/_crv`) and torn down by removeMod's
 * MT_ATTEN case. The amount range is 0..2 (boost allowed) - it REPLACES
 * the connection's original 0..1 amount param, so a route has exactly one
 * amount. The mod name is cosmetic (attenuators are hidden from the UI);
 * the params carry the source's name so they are identifiable in the
 * paramList. */
Mod *createAttenuatorMod(ParamList *paramList, ModList *modList, Mod *source, const char *name) {
	if(!paramList || !modList || !source) {
		return NULL;
	}
	if(modList->count >= MAX_MODS) {
		return NULL;
	}
	Mod *m = (Mod *)malloc(sizeof(Mod));
	if(!m) {
		printf("could not allocate memory for attenuator mod.\n");
		return NULL;
	}
	/* Truncate to a guaranteed-NUL-terminated MAX_NAME_LEN name -
	 * initMod's strncpy does not force a terminator on long input. */
	char modName[MAX_NAME_LEN];
	strncpy(modName, name, MAX_NAME_LEN - 1);
	modName[MAX_NAME_LEN - 1] = '\0';
	initMod(m, paramList, modName, MT_ATTEN, modGenerateAtten);
	/* initMod ignores its generate argument (it hardcodes
	 * generateEnvelope) - set the real generate fn explicitly. */
	m->generate = modGenerateAtten;
	m->data.atten.input = source;
	/* The attenuator must not clip bipolar values before the destination
	 * sees them: initMod gives the output param a [0,1] range, which
	 * would clamp negative swings before the polarity logic can act.
	 * Widen to [-100,100] (amount up to 100 x source magnitude up to 1);
	 * the destination's own range clamps the final applied value. The
	 * upper bound was raised from 2 so Hz-style dests (per-op pitch,
	 * ±1000Hz) can carry a real vibrato depth through the seed. */
	m->output->minValue = -100.0f;
	m->output->maxValue = 100.0f;
	/* Buffers are MAX_NAME_LEN + 8 so snprintf can never truncate;
	 * createParameter's strndup does the final safe truncate. */
	char pName[MAX_NAME_LEN + 8];
	snprintf(pName, sizeof(pName), "%s_amt", source->name);
	m->data.atten.attenAmount = createParameter(paramList, pName, 1.0f, 0.0f, 100.0f);
	snprintf(pName, sizeof(pName), "%s_pol", source->name);
	m->data.atten.attenPolarity = createParameterEx(paramList, pName, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
	snprintf(pName, sizeof(pName), "%s_crv", source->name);
	m->data.atten.attenCurve = createParameterEx(paramList, pName, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
	addToModList(modList, m);
	return m;
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

bool addModulation(ParamList *paramList, ModList *modList, Mod *source, Parameter *destination, float amount, ModulationOperation type) {
	ModConnection *conn = createConnection(paramList, source, amount, type);
	if(!conn) return false;
	/* Every route from a real source (ENV/LFO/RND) runs through its own
	 * attenuator node: addModulation inserts one between the source and
	 * this destination, and the connection's amount param is swapped for
	 * the attenuator's 0..2 amount (a single param, not two - the
	 * connection's original 0..1 amount is dropped). Sources that are
	 * already attenuators are reused as-is (no nested attenuators). If
	 * the attenuator cannot be created (modList full / malloc failure)
	 * the connection simply stays plain. */
	if(modList && source && source->type != MT_ATTEN) {
		char attenName[MAX_NAME_LEN + 8];
		snprintf(attenName, sizeof(attenName), "%s_atten", source->name);
		Mod *atten = createAttenuatorMod(paramList, modList, source, attenName);
		if(atten) {
			if(conn->amount) {
				removeFromParamList(paramList, conn->amount);
				freeParameter(conn->amount);
			}
			conn->amount = atten->data.atten.attenAmount;
			conn->source = atten;
		}
	}
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
/* True if any parameter in the list carries a live connection whose
 * source is `atten`. Attenuators are connection-internal nodes: when
 * their last connection goes away they have no remaining purpose and
 * are garbage-collected by the remove* functions. */
static bool attenStillInUse(ParamList *list, Mod *atten) {
	if(!list || !atten) {
		return false;
	}
	for(int i = 0; i < list->count; i++) {
		Parameter *p = list->params[i];
		if(!p) {
			continue;
		}
		for(ModConnection *conn = p->modulators; conn != NULL; conn = conn->next) {
			if(conn->source == atten) {
				return true;
			}
		}
	}
	return false;
}

/* A connection is "from src" when its source IS src, or when it routes
 * through src's attenuator (MT_ATTEN with input == src). Route matching
 * and removal against a real source must follow the attenuator. */
static bool modConnFromSource(ModConnection *conn, Mod *src) {
	if(!conn || !src) {
		return false;
	}
	if(conn->source == src) {
		return true;
	}
	return conn->source && conn->source->type == MT_ATTEN && conn->source->data.atten.input == src;
}
bool removeModulation(ParamList *list, ModList *modList, Parameter *destination, Mod *source) {
	if(!list || !destination || !source) {
		return false;
	}
	ModConnection *conn = destination->modulators;
	while(conn != NULL) {
		ModConnection *next = conn->next;
		if(modConnFromSource(conn, source)) {
			Mod *connSource = conn->source;
			if(conn->previous) {
				conn->previous->next = conn->next;
			} else {
				destination->modulators = conn->next;
			}
			if(conn->next) {
				conn->next->previous = conn->previous;
			}
			destination->modulator_count--;
			/* An MT_ATTEN connection source OWNS conn->amount (it is
			 * the attenuator's amount param, list-owned by the
			 * attenuator and freed with it) - do NOT free it here.
			 * The per-connection type param is always freed here. */
			if(conn->amount && connSource->type != MT_ATTEN) {
				removeFromParamList(list, conn->amount);
				freeParameter(conn->amount);
			}
			if(conn->type) {
				removeFromParamList(list, conn->type);
				freeParameter(conn->type);
			}
			free(conn);
			/* Connection-internal node GC: an attenuator whose last
			 * connection just went away is removed (which frees its
			 * amount/polarity/curve/output params). */
			if(connSource->type == MT_ATTEN && !attenStillInUse(list, connSource)) {
				removeMod(modList, list, connSource);
			}
			return true;
		}
		conn = next;
	}
	return false;
}
int removeModulationsForSource(ParamList *list, ModList *modList, Mod *source) {
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
			if(modConnFromSource(conn, source)) {
				if(conn->previous) {
					conn->previous->next = conn->next;
				} else {
					p->modulators = conn->next;
				}
				if(conn->next) {
					conn->next->previous = conn->previous;
				}
				p->modulator_count--;
				/* MT_ATTEN connection sources own conn->amount (freed
				 * with the attenuator) - only the type param is
				 * orphaned here. */
				if(conn->amount && conn->source->type != MT_ATTEN) {
					if(orphanCount < MAX_PARAMS) {
						orphans[orphanCount++] = conn->amount;
					}
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
	/* Connection-internal node GC: attenuators whose last connection just
	 * went away are removed. Covers both call patterns: removeMods on an
	 * already-unlinked MT_ATTEN source (no-op, recursive removeMod bails
	 * at removeFromModList), and a REAL source whose attenuators were all
	 * unlinked above (collect them by input == source first — the list
	 * shifts as removeMod unlinks, so removing during the walk is unsafe). */
	if(source->type == MT_ATTEN) {
		if(!attenStillInUse(list, source)) {
			removeMod(modList, list, source);
		}
	} else {
		Mod *toGc[MAX_MODS];
		int gcCount = 0;
		for(int i = 0; i < modList->count && gcCount < MAX_MODS; i++) {
			Mod *m = modList->mods[i];
			if(m && m->type == MT_ATTEN && m->data.atten.input == source && !attenStillInUse(list, m)) {
				toGc[gcCount++] = m;
			}
		}
		for(int k = 0; k < gcCount; k++) {
			removeMod(modList, list, toGc[k]);
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
	removeModulationsForSource(paramList, modList, mod);
	/* Remove the mod's own params from the list (owned by paramList). */
	switch(mod->type) {
		case MT_LFO: {
			LfoState *lfo = &mod->data.lfo;
			if(lfo->rate) removeFromParamList(paramList, lfo->rate);
			if(lfo->phase) removeFromParamList(paramList, lfo->phase);
			if(lfo->shape) removeFromParamList(paramList, lfo->shape);
			if(lfo->playMode) removeFromParamList(paramList, lfo->playMode);
			break;
		}
		case MT_RND: {
			RndState *rnd = &mod->data.rnd;
			if(rnd->rate) removeFromParamList(paramList, rnd->rate);
			if(rnd->phase) removeFromParamList(paramList, rnd->phase);
			if(rnd->shape) removeFromParamList(paramList, rnd->shape);
			if(rnd->playMode) removeFromParamList(paramList, rnd->playMode);
			break;
		}
		case MT_ENV: {
			EnvState *env = &mod->data.env;
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
		case MT_ATTEN: {
			/* The atten's own params are list-owned too. */
			if(mod->data.atten.attenAmount) removeFromParamList(paramList, mod->data.atten.attenAmount);
			if(mod->data.atten.attenPolarity) removeFromParamList(paramList, mod->data.atten.attenPolarity);
			if(mod->data.atten.attenCurve) removeFromParamList(paramList, mod->data.atten.attenCurve);
			break;
		}
		default:
			break;
	}
	if(mod->output) {
		removeFromParamList(paramList, mod->output);
	}
	/* The source is about to be freed: attenuators that passed through
	 * it (input == mod) would dangle. Remove them - each removal also
	 * drops the attenuator's connection to its destination. Runs before
	 * the free-by-type switch so `mod` is still valid for the compare.
	 * An attenuator's input is always a real source (addModulation never
	 * nests attenuators), so one pass finds them all; rescan because
	 * removeMod mutates the list. */
	if(mod->type != MT_ATTEN) {
		for(;;) {
			Mod *orphan = NULL;
			for(int i = 0; i < modList->count; i++) {
				Mod *m = modList->mods[i];
				if(m && m->type == MT_ATTEN && m->data.atten.input == mod) {
					orphan = m;
					break;
				}
			}
			if(!orphan) {
				break;
			}
			removeMod(modList, paramList, orphan);
		}
	}
	/* Params are no longer referenced by the list; free struct by type.
	 * (freeEnvelope/freeLFO/freeRandom free their params again — safe now
	 * because the list no longer holds those pointers.) */
	switch(mod->type) {
		case MT_LFO:
			freeLFO((Mod *)mod);
			break;
		case MT_RND:
			freeRandom((Mod *)mod);
			break;
		case MT_ENV:
			freeEnvelope((Mod *)mod);
			break;
		case MT_ATTEN:
			/* Free the atten's own params before freeMod takes the output. */
			if(mod->data.atten.attenAmount) {
				freeParameter(mod->data.atten.attenAmount);
				mod->data.atten.attenAmount = NULL;
			}
			if(mod->data.atten.attenPolarity) {
				freeParameter(mod->data.atten.attenPolarity);
				mod->data.atten.attenPolarity = NULL;
			}
			if(mod->data.atten.attenCurve) {
				freeParameter(mod->data.atten.attenCurve);
				mod->data.atten.attenCurve = NULL;
			}
			freeMod(mod);
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
	/* The mod must be a member of the list it is being retyped in (the
	 * list is passed for registration invariants; membership is the
	 * established contract). */
	bool registered = false;
	for(int i = 0; i < modList->count; i++) {
		if(modList->mods[i] == mod) {
			registered = true;
			break;
		}
	}
	if(!registered) {
		return false;
	}

	/* Free the OLD type's payload params from the list (they are list-owned).
	 * The Mod struct itself is REUSED for the new type: output + name are
	 * preserved and the pointer is stable, so every ModConnection.source
	 * that references this mod stays valid (routes survive the retype). */
	switch(mod->type) {
		case MT_LFO: {
			LfoState *l = &mod->data.lfo;
			if(l->rate) removeFromParamList(paramList, l->rate);
			if(l->phase) removeFromParamList(paramList, l->phase);
			if(l->shape) removeFromParamList(paramList, l->shape);
			if(l->playMode) removeFromParamList(paramList, l->playMode);
			break;
		}
		case MT_RND: {
			RndState *r = &mod->data.rnd;
			if(r->rate) removeFromParamList(paramList, r->rate);
			if(r->phase) removeFromParamList(paramList, r->phase);
			if(r->shape) removeFromParamList(paramList, r->shape);
			if(r->playMode) removeFromParamList(paramList, r->playMode);
			break;
		}
		case MT_ENV: {
			EnvState *e = &mod->data.env;
			for(int i = 0; i < e->stageCount; i++) {
				if(e->stages[i].duration) removeFromParamList(paramList, e->stages[i].duration);
				if(e->stages[i].curvature) removeFromParamList(paramList, e->stages[i].curvature);
			}
			break;
		}
		default:
			break;
	}

	/* Swap the payload in place on the SAME Mod. `type` selects the new
	 * union member; output + name are untouched. */
	memset(&mod->data, 0, sizeof(mod->data));
	switch(newType) {
		case MT_LFO:
			mod->type = MT_LFO;
			/* initLfoDefaults sets shapeValue, shape (Parameter) and
			 * generate via cbLfoShapeOnChange. */
			initLfoDefaults(mod, paramList, 1.0f, LS_SIN);
			break;
		case MT_RND:
			mod->type = MT_RND;
			/* initRandDefaults sets shapeValue, shape (Parameter) and
			 * generate via cbRandShapeOnChange. */
			initRandDefaults(mod, paramList, 1.0f, RT_SNH);
			break;
		case MT_ENV:
			mod->type = MT_ENV;
			initEnvelopeDefaults(mod);
			addEnvelopeStage(paramList, mod, true, 0.25f, 1.0f, 0.95f, "A");
			addEnvelopeStage(paramList, mod, false, 4.25f, 0.0f, 0.1f, "D");
			mod->generate = generateEnvelope;
			break;
		default:
			return false;
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
void initRandDefaults(Mod *rnd, ParamList *paramList, float rate, RandomType type) {
	RndState *r = &rnd->data.rnd;
	r->lastPhase = 0.0f;
	r->lastRandom = 0.0f;
	r->rate = createParameter(paramList, "RNG rate", rate, 0.1f, 100.0f);
	r->phase = createParameter(paramList, "RNG phase", 0.0f, 0.0f, 1.0f);
	r->shape = createParameterPro(paramList, "RNG shape", (float)type, 0.0f, (float)(RT_COUNT - 1), 1.0f, 1.0f, rnd, cbRandShapeOnChange);
	r->shapeValue = type;
	cbRandShapeOnChange(rnd); /* sync base.generate + shapeValue from the param */
}
Mod *createRandom(ParamList *paramList, ModList *modList, int index, float rate, RandomType type, char *name) {
	Mod *rnd = (Mod *)calloc(1, sizeof(Mod));

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
	initMod(rnd, paramList, name, MT_RND, genFunc);
	initRandDefaults(rnd, paramList, rate, type);
	addToModList(modList, rnd);

	return rnd;
}

void cbLfoShapeOnChange(void *data) {
	Mod *lfo = (Mod *)data;
	if(!lfo) {
		return;
	}
	LfoState *l = &lfo->data.lfo;
	int sh = (l->shape) ? getParameterValueAsInt(l->shape) : l->shapeValue;
	if(sh < 0) sh = 0;
	if(sh >= LS_COUNT) sh = LS_COUNT - 1;
	l->shapeValue = sh;
	switch(sh) {
		case LS_SQU: lfo->generate = generateSquare; break;
		case LS_RMP: lfo->generate = generateRamp; break;
		default:     lfo->generate = generateSine; break;
	}
}

void cbRandShapeOnChange(void *data) {
	Mod *rnd = (Mod *)data;
	if(!rnd) {
		return;
	}
	RndState *r = &rnd->data.rnd;
	int sh = (r->shape) ? getParameterValueAsInt(r->shape) : r->shapeValue;
	if(sh < 0) sh = 0;
	if(sh >= RT_COUNT) sh = RT_COUNT - 1;
	r->shapeValue = sh;
	switch(sh) {
		case RT_DRK: rnd->generate = generateDrunk; break;
		default:     rnd->generate = generateRandom; break;
	}
}

void initLfoDefaults(Mod *lfo, ParamList *paramList, float rate, int shape) {
	LfoState *l = &lfo->data.lfo;
	l->rate = createParameter(paramList, "LFO rate", rate, 0.1f, 100.0f);
	l->phase = createParameter(paramList, "LFO phase", 0.0f, 0.0f, 1.0f);
	l->shape = createParameterPro(paramList, "LFO shape", (float)shape, 0.0f, (float)(LS_COUNT - 1), 1.0f, 1.0f, lfo, cbLfoShapeOnChange);
	l->shapeValue = shape;
	cbLfoShapeOnChange(lfo); /* sync base.generate + shapeValue from the param */
}

Mod *createLFO(ParamList *paramList, ModList *modList, int index, float rate, int shape, const char *name) {
	Mod *lfo = (Mod *)calloc(1, sizeof(Mod));
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
	initMod(lfo, paramList, name, MT_LFO, genFunc);
	initLfoDefaults(lfo, paramList, rate, shape);
	addToModList(modList, lfo);

	return lfo;
}

void generateSine(void *self) {
	Mod *lfo = (Mod *)self;
	float value = sinf(getParameterValue(lfo->data.lfo.phase) * TWO_PI);
	setParameterBaseValue(lfo->output, value);
	setParameterValue(lfo->output, value);
}

void generateSquare(void *self) {
	Mod *lfo = (Mod *)self;
	float value = getParameterValue(lfo->data.lfo.phase) < 0.5f ? 1.0f : -1.0f;
	setParameterBaseValue(lfo->output, value);
	setParameterValue(lfo->output, value);
}

void generateRamp(void *self) {
	Mod *lfo = (Mod *)self;
	float value = (getParameterValue(lfo->data.lfo.phase) - 1.0f) * 2.0f;
	setParameterBaseValue(lfo->output, value);
	setParameterValue(lfo->output, value);
}

void generateRandom(void *self) {
	Mod *rnd = (Mod *)self;
	RndState *r = &rnd->data.rnd;
	float phase = getParameterValue(r->phase);

	if(phase < r->lastPhase) {
		r->lastRandom = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
	}

	r->lastPhase = phase;
	setParameterBaseValue(rnd->output, r->lastRandom);
	setParameterValue(rnd->output, r->lastRandom);
}

void generateDrunk(void *self) {
	Mod *rnd = (Mod *)self;
	RndState *r = &rnd->data.rnd;
	float phase = getParameterValue(r->phase);

	r->lastRandom = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
	r->lastRandom *= 0.5f * ((float)rand() / (float)RAND_MAX);
	r->lastPhase = phase;
	setParameterBaseValue(rnd->output, rnd->output->currentValue + r->lastRandom);
	setParameterValue(rnd->output, rnd->output->currentValue + r->lastRandom);
}

void updateMod(Mod *mod, float deltaTime) {
	// DEBUG_LOG("update mod");
	if(mod == NULL) return;

	switch(mod->type) {
		float l_phase = 0.0f;
		float r_phase = 0.0f;
		float l_rate = 0.0f;
		float r_rate = 0.0f;
		case MT_ENV: {
			EnvState *env = &mod->data.env;
			if(env->isTriggered) {
				env->currentTime += deltaTime;
			}
			break;
		}
		case MT_LFO: {
			LfoState *l = &mod->data.lfo;
			l_phase = getParameterValue(l->phase);
			l_rate = getParameterValue(l->rate);
			l_phase += l_rate * deltaTime;
			if(l_phase >= 1.0f) l_phase -= 1.0f;
			setParameterBaseValue(l->phase, l_phase);
			setParameterValue(l->phase, l_phase);
			break;
		}
		case MT_RND: {
			RndState *r = &mod->data.rnd;
			r_phase = getParameterValue(r->phase);
			r_rate = getParameterValue(r->rate);
			r_phase += r_rate * deltaTime;
			if(r_phase >= 1.0f) r_phase -= 1.0f;
			setParameterBaseValue(r->phase, r_phase);
			setParameterValue(r->phase, r_phase);
			break;
		}
		case MT_ATTEN:
			// Stateless: generate handles the passthrough, nothing to advance
			break;
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

void triggerEnvelope(Mod *env) {
	// DEBUG_LOG("triggering env");
	EnvState *e = &env->data.env;
	e->currentStageIndex = 0;
	e->currentTime = 0;
	e->isTriggered = true;
}

void generateEnvelope(void *self) {
	Mod *env = (Mod *)self;
	if(!env) {
		return;
	}
	EnvState *e = &env->data.env;
	if(!e->isTriggered || e->currentStageIndex >= e->stageCount) {
		return;
	}

	EnvelopeStage *stage = &e->stages[e->currentStageIndex];
	if(!stage->duration || !stage->curvature) {
		return;
	}

	float dt = 1.0f / PA_SR;
	e->currentTime += dt;

	int tIdx = 8;
	Wavetable *wt = envTables->tables[tIdx];
	float t = e->currentTime / stage->duration->baseValue;
	int index0 = (int)(t * wt->length);
	int index1 = index0 < wt->length ? index0 + 1 : index0;
	float diff = fmodf(t, 1.0f);
	float enval = wt->data[index0] * (1.0 - diff) + wt->data[index1] * diff;

	// float shapedT = applyCurve(t, stage->curvature->currentValue);

	float startLevel = (e->currentStageIndex > 0) ? e->stages[e->currentStageIndex - 1].targetLevel : 0.0f;

	e->currentLevel = startLevel + (stage->targetLevel - startLevel) * enval;

	if(index0 >= wt->length - 1) {
		// printf("stage %i complete\n", e->currentStageIndex);

		e->currentTime = 0.0f;
		e->currentLevel = stage->targetLevel;
		if(++e->currentStageIndex >= e->stageCount) {
			e->isTriggered = false;
			// printf("TRIGGER OFF!!!!!!\n");
		}
	}

	// Important: Update output parameter
	setParameterBaseValue(env->output, e->currentLevel);
	setParameterValue(env->output, e->currentLevel);
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

/* Task 2.3: deep-copy a source mod into the voice's own lists. The clone
 * carries its OWN payload params (values + ranges copied from `src`), its
 * own `output`, and the same `name`/`generate`. For MT_ATTEN the `input`
 * pointer is left NULL — the caller resolves it to the voice's clone of
 * the real upstream source. Envelope clones start untriggered. */
Mod *cloneMod(ParamList *voicePl, ModList *voiceMl, const Mod *src) {
	if(!src || !voicePl) {
		return NULL;
	}
	Mod *c = (Mod *)calloc(1, sizeof(Mod));
	initMod(c, voicePl, src->name, src->type, src->generate);
	c->generate = src->generate;
	switch(src->type) {
		case MT_ENV: {
			EnvState *e = &c->data.env;
			const EnvState *se = &src->data.env;
			e->stageCount = se->stageCount;
			e->loop = se->loop;
			e->isTriggered = false;
			e->currentStageIndex = 0;
			e->currentTime = 0.0f;
			e->totalElapsedTime = 0.0f;
			e->currentLevel = 0.0f;
			e->isSustaining = false;
			for(int i = 0; i < e->stageCount && i < MAX_ENVELOPE_STAGES; i++) {
				e->stages[i].isRising = se->stages[i].isRising;
				e->stages[i].isSustain = se->stages[i].isSustain;
				e->stages[i].targetLevel = se->stages[i].targetLevel;
				strncpy(e->stages[i].name, se->stages[i].name, MAX_NAME_LEN);
				if(se->stages[i].duration) {
					e->stages[i].duration = createParameter(voicePl, se->stages[i].duration->name,
						se->stages[i].duration->baseValue, se->stages[i].duration->minValue, se->stages[i].duration->maxValue);
				}
				if(se->stages[i].curvature) {
					e->stages[i].curvature = createParameter(voicePl, se->stages[i].curvature->name,
						se->stages[i].curvature->baseValue, se->stages[i].curvature->minValue, se->stages[i].curvature->maxValue);
				}
			}
			break;
		}
		case MT_LFO: {
			LfoState *l = &c->data.lfo;
			const LfoState *sl = &src->data.lfo;
			if(sl->rate) {
				l->rate = createParameter(voicePl, sl->rate->name, sl->rate->baseValue, sl->rate->minValue, sl->rate->maxValue);
			}
			if(sl->phase) {
				l->phase = createParameter(voicePl, sl->phase->name, sl->phase->baseValue, sl->phase->minValue, sl->phase->maxValue);
			}
			if(sl->shape) {
				l->shape = createParameter(voicePl, sl->shape->name, sl->shape->baseValue, sl->shape->minValue, sl->shape->maxValue);
			}
			if(sl->playMode) {
				l->playMode = createParameter(voicePl, sl->playMode->name, sl->playMode->baseValue, sl->playMode->minValue, sl->playMode->maxValue);
			}
			l->shapeValue = sl->shapeValue;
			break;
		}
		case MT_RND: {
			RndState *r = &c->data.rnd;
			const RndState *sr = &src->data.rnd;
			r->lastPhase = 0.0f;
			r->lastRandom = 0.0f;
			if(sr->rate) {
				r->rate = createParameter(voicePl, sr->rate->name, sr->rate->baseValue, sr->rate->minValue, sr->rate->maxValue);
			}
			if(sr->phase) {
				r->phase = createParameter(voicePl, sr->phase->name, sr->phase->baseValue, sr->phase->minValue, sr->phase->maxValue);
			}
			if(sr->shape) {
				r->shape = createParameter(voicePl, sr->shape->name, sr->shape->baseValue, sr->shape->minValue, sr->shape->maxValue);
			}
			if(sr->playMode) {
				r->playMode = createParameter(voicePl, sr->playMode->name, sr->playMode->baseValue, sr->playMode->minValue, sr->playMode->maxValue);
			}
			r->shapeValue = sr->shapeValue;
			break;
		}
		case MT_ATTEN: {
			AttenState *a = &c->data.atten;
			const AttenState *sa = &src->data.atten;
			/* The attenuator must not clip bipolar values: mirror the
			 * widened [-2,2] output range. */
			c->output->minValue = -2.0f;
			c->output->maxValue = 2.0f;
			if(sa->attenAmount) {
				a->attenAmount = createParameter(voicePl, sa->attenAmount->name,
					sa->attenAmount->baseValue, sa->attenAmount->minValue, sa->attenAmount->maxValue);
			}
			if(sa->attenPolarity) {
				a->attenPolarity = createParameter(voicePl, sa->attenPolarity->name,
					sa->attenPolarity->baseValue, sa->attenPolarity->minValue, sa->attenPolarity->maxValue);
			}
			if(sa->attenCurve) {
				a->attenCurve = createParameter(voicePl, sa->attenCurve->name,
					sa->attenCurve->baseValue, sa->attenCurve->minValue, sa->attenCurve->maxValue);
			}
			a->input = NULL; /* caller resolves to the voice's source clone */
			break;
		}
		default:
			break;
	}
	if(voiceMl) {
		addToModList(voiceMl, c);
	}
	return c;
}

/* Task 2.3: copy each payload param baseValue from `src` into the clone's
 * OWN params. Dispatch on the source's type so a retyped source syncs into
 * a retyped clone. Attenuators copy amount/polarity/curve (input is stable
 * once resolved). */
void syncModValues(Mod *c, const Mod *src) {
	if(!c || !src) {
		return;
	}
	switch(src->type) {
		case MT_ENV: {
			EnvState *e = &c->data.env;
			const EnvState *se = &src->data.env;
			int n = (e->stageCount < se->stageCount) ? e->stageCount : se->stageCount;
			for(int i = 0; i < n; i++) {
				if(e->stages[i].duration && se->stages[i].duration) {
					setParameterBaseValue(e->stages[i].duration, se->stages[i].duration->baseValue);
				}
				if(e->stages[i].curvature && se->stages[i].curvature) {
					setParameterBaseValue(e->stages[i].curvature, se->stages[i].curvature->baseValue);
				}
			}
			break;
		}
		case MT_LFO: {
			LfoState *l = &c->data.lfo;
			const LfoState *sl = &src->data.lfo;
			if(l->rate && sl->rate) {
				setParameterBaseValue(l->rate, sl->rate->baseValue);
			}
			if(l->phase && sl->phase) {
				setParameterBaseValue(l->phase, sl->phase->baseValue);
			}
			if(l->shape && sl->shape) {
				setParameterBaseValue(l->shape, sl->shape->baseValue);
			}
			if(l->playMode && sl->playMode) {
				setParameterBaseValue(l->playMode, sl->playMode->baseValue);
			}
			break;
		}
		case MT_RND: {
			RndState *r = &c->data.rnd;
			const RndState *sr = &src->data.rnd;
			if(r->rate && sr->rate) {
				setParameterBaseValue(r->rate, sr->rate->baseValue);
			}
			if(r->phase && sr->phase) {
				setParameterBaseValue(r->phase, sr->phase->baseValue);
			}
			if(r->shape && sr->shape) {
				setParameterBaseValue(r->shape, sr->shape->baseValue);
			}
			if(r->playMode && sr->playMode) {
				setParameterBaseValue(r->playMode, sr->playMode->baseValue);
			}
			break;
		}
		case MT_ATTEN: {
			AttenState *a = &c->data.atten;
			const AttenState *sa = &src->data.atten;
			if(a->attenAmount && sa->attenAmount) {
				setParameterBaseValue(a->attenAmount, sa->attenAmount->baseValue);
			}
			if(a->attenPolarity && sa->attenPolarity) {
				setParameterBaseValue(a->attenPolarity, sa->attenPolarity->baseValue);
			}
			if(a->attenCurve && sa->attenCurve) {
				setParameterBaseValue(a->attenCurve, sa->attenCurve->baseValue);
			}
			break;
		}
		default:
			break;
	}
}

void initEnvelopeDefaults(Mod *env) {
	EnvState *e = &env->data.env;
	e->currentLevel = 0.0f;
	e->currentStageIndex = 0;
	e->stageCount = 0;
	e->currentTime = 0.0f;
	e->totalElapsedTime = 0.0f;
	e->isTriggered = false;
	e->isSustaining = false;
	e->loop = false;
}

Mod *createEnvelope(ParamList *paramList, ModList *modList, const char *name) {
	Mod *env = (Mod *)calloc(1, sizeof(Mod));
	initMod(env, paramList, name, MT_ENV, generateEnvelope);
	initEnvelopeDefaults(env);

	addToModList(modList, env);

	return env;
}

void addEnvelopeStage(ParamList *paramList, Mod *env, bool isRising, float duration, float targetLevel, float initialCurvature, char *name) {
	EnvState *e = &env->data.env;
	if(e->stageCount >= MAX_ENVELOPE_STAGES) {
		return;
	}

	char nameBuf[32];
	int idx = e->stageCount;

	EnvelopeStage *stage = &e->stages[idx];
	stage->isRising = isRising;
	stage->isSustain = (duration <= 0.0f);
	strncpy(stage->name, name, MAX_NAME_LEN);

	stage->duration = createParameter(paramList, "duration", duration, 0.001f, 10.0f);
	stage->targetLevel = targetLevel;
	stage->curvature = createParameter(paramList, "curve", initialCurvature, -1.0f, 1.0f);
	e->stageCount++;
}

Mod *createADSR(ParamList *paramList, ModList *modList, float a, float d, float s, float r, char *name) {
	// DEBUG_LOG("create adsr");
	Mod *env = createEnvelope(paramList, modList, name);

	addEnvelopeStage(paramList, env, true, a, 1.0f, 0.75f, "A");  // Attack
	addEnvelopeStage(paramList, env, false, d, 0.7f, 0.75f, "D"); // Decay
	addEnvelopeStage(paramList, env, true, s, 0.7f, 0.5f, "S");   // Sustain
	addEnvelopeStage(paramList, env, false, r, 0.0f, 0.75f, "R"); // Release

	return env;
}

Mod *createAD(ParamList *paramList, ModList *modList, float a, float d, char *name) {
	Mod *env = createEnvelope(paramList, modList, name);

	addEnvelopeStage(paramList, env, true, a, 1.0f, 0.95f, "A"); // Attack
	addEnvelopeStage(paramList, env, false, d, 0.0f, 0.1f, "D"); // Decay

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
void initEnvelopeFromPreset(ModPreset *mp, Mod *e, ParamList *paramList, ModList *modlist) {
	if(!mp || !e || !paramList) {
		printf("ERROR: NULL passed to envelope preset init.\n");
		return;
	}
	mp->type = MT_ENV;
	EnvPresetData *epd = &mp->md.env;

	initMod(e, paramList, "env", MT_ENV, generateEnvelope);
	initEnvelopeDefaults(e);

	EnvState *e2 = &e->data.env;
	e2->loop = epd->loop;
	e2->stageCount = epd->stageCount;
	for(int i = 0; i < e2->stageCount; i++) {
		e2->stages[i] = (EnvelopeStage){
			.curvature = createParameter(paramList, "es_Curve", epd->stages[i].curvature, 0.0f, 1.0f),
			.duration = createParameter(paramList, "es_Duration", epd->stages[i].duration, 0.001f, 10.0f),
			.isRising = epd->stages[i].isRising,
			.isSustain = epd->stages[i].isSustain,
			.targetLevel = epd->stages[i].targetLevel
		};
		strncpy(e2->stages[i].name, epd->stages[i].name, MAX_NAME_LEN);
	}

	if(modlist) {
		addToModList(modlist, e);
	}
}
void saveEnvPreset(EnvPresetData *epd, Mod *e) {
	if(!epd || !e) {
		printf("ERROR: NULL passed to envelope preset save.\n");
		return;
	}
	EnvState *e2 = &e->data.env;
	epd->loop = e2->loop;
	epd->stageCount = e2->stageCount;
	for(int i = 0; i < e2->stageCount; i++) {
		epd->stages[i] = (EnvStagePresetData){
			.curvature = getParameterValue(e2->stages[i].curvature),
			.duration = getParameterValue(e2->stages[i].duration),
			.isRising = e2->stages[i].isRising,
			.isSustain = e2->stages[i].isSustain,
			.targetLevel = e2->stages[i].targetLevel
		};
		strncpy(epd->stages[i].name, e2->stages[i].name, MAX_NAME_LEN);
	}
}
void initLfoFromPreset(LfoPresetData *lpd, Mod *lfo, ParamList *paramList, ModList *modlist) {
	initMod(lfo, paramList, "LFO", MT_LFO, NULL);
	/* initLfoDefaults creates the shape Parameter, sets shapeValue from
	 * lpd->shape, and cbLfoShapeOnChange syncs lfo->generate. Any
	 * subsequent route that changes lfo->shape will also re-sync via that
	 * callback. */
	initLfoDefaults(lfo, paramList, lpd->rate, lpd->shape);

	if(modlist) {
		addToModList(modlist, lfo);
	}
}
void saveLfoPreset(LfoPresetData *lpd, Mod *lfo) {
	LfoState *l = &lfo->data.lfo;
	lpd->phase = getParameterValue(l->phase);
	lpd->rate = getParameterValue(l->rate);
	lpd->shape = l->shapeValue;
}
void initRandFromPreset(RandPresetData *rpd, Mod *rnd, ParamList *paramList, ModList *modlist) {
}
void saveRandPreset(RandPresetData *rpd, Mod *rng) {
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

void freeLFO(Mod *lfo) {
	if(!lfo) return;

	LfoState *l = &lfo->data.lfo;
	// Free parameters in specific order
	if(l->phase) {
		freeParameter(l->phase);
		l->phase = NULL;
	}
	if(l->rate) {
		freeParameter(l->rate);
		l->rate = NULL;
	}
	if(l->shape) {
		freeParameter(l->shape);
		l->shape = NULL;
	}
	if(l->playMode) {
		freeParameter(l->playMode);
		l->playMode = NULL;
	}
	if(lfo->output) {
		freeParameter(lfo->output);
		lfo->output = NULL;
	}

	free(lfo);
}

void freeRandom(Mod *rnd) {
	if(!rnd) return;

	RndState *r = &rnd->data.rnd;
	freeParameter(rnd->output);
	freeParameter(r->rate);
	freeParameter(r->phase);
	freeParameter(r->shape);
	freeParameter(r->playMode);
	r->shape = NULL;

	free(rnd);
}

void freeEnvelope(Mod *env) {
	if(!env) return;

	EnvState *e = &env->data.env;
	freeParameter(env->output);

	for(int i = 0; i < e->stageCount; i++) {
		freeParameter(e->stages[i].duration);
		freeParameter(e->stages[i].curvature);
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
				freeLFO((Mod *)mod);
				break;
			case MT_RND:
				freeRandom((Mod *)mod);
				break;
			case MT_ENV:
				freeEnvelope((Mod *)mod);
				break;
			default:
				freeMod(mod);
		}
	}
	free(list);
}
