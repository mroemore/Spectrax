#include "modsystem.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

static float _clampValue(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;    
    return value;
}

void initModSystem(){
    //paramList = createParamList();
}

ModList* createModList() {
    ModList* list = (ModList*)malloc(sizeof(ModList));
	if (!list) return NULL;
    list->count = 0;
    return list;
}

ParamList* createParamList() {
    ParamList* list = (ParamList*)malloc(sizeof(ParamList));
	if (!list) return NULL;
    list->count = 0;
    return list;
}

void addToModList(ModList* list, Mod* mod) {
	//DEBUG_LOG("add mod");
    if (list->count < MAX_MODS) {
        list->mods[list->count++] = mod;
    }
}

Parameter* createParameter(ParamList* paramList, const char* name, float initialValue, float minValue, float maxValue) {
	//DEBUG_LOG("create param");
    Parameter* param = (Parameter*)malloc(sizeof(Parameter));
    if (param) {
        param->name = strdup(name);
        param->minValue = minValue;
        param->maxValue = maxValue;
        param->baseValue = _clampValue(initialValue, minValue, maxValue);
        param->currentValue = param->baseValue;
        param->fineIncrement = 0.01f;
        param->coarseIncrement = 0.10f;
        param->modulators = NULL;
        param->modulator_count = 0;
        param->onChange = NULL;
    }
    
    addToParamList(paramList, param);
    return param;
}

Parameter* createParameterEx(ParamList* paramList, const char* name, float initialValue, float minValue, float maxValue, float fineIncrement, float coarseIncrement){
    Parameter* p = createParameter(paramList, name, initialValue, minValue, maxValue);
    p->coarseIncrement = coarseIncrement;
    p->fineIncrement = fineIncrement;
    return p;
}

void addToParamList(ParamList* list, Parameter* param){
    if (list->count < MAX_PARAMS) {
        list->params[list->count++] = param;
    }
}

void setParameterValue(Parameter* param, float value) {
	//DEBUG_LOG("set param");
    float clamped = _clampValue(value, param->minValue, param->maxValue);
    param->currentValue = clamped;
    if (param->onChange) {
        param->onChange(param, clamped);
    }
}

void setParameterBaseValue(Parameter* param, float value) {
	//DEBUG_LOG("set param");
    float clamped = _clampValue(value, param->minValue, param->maxValue);
    param->baseValue = clamped;
    if (param->onChange) {
        param->onChange(param, clamped);
    }
}

float getParameterValue(Parameter* param) {
	//DEBUG_LOG("get param");

    return param->currentValue;
}

int getParameterValueAsInt(Parameter* param){
    return (int)round(param->currentValue);
}

ModConnection* createConnection(ParamList* paramList, Mod* source, float amount, ModulationOperation type) {
	//DEBUG_LOG("create con");

    ModConnection* conn = (ModConnection*)malloc(sizeof(ModConnection));
    if (conn) {
        conn->source = source;
        conn->amount = createParameter(paramList, "mod amount", 1.0f, 0.0f, 1.0f);
        conn->type = createParameterEx(paramList, "mod operation", (float)type, 0.0f, (float)MT_COUNT, 1.0f, 10.0f);  // Set modulation type
        conn->next = NULL;
        conn->previous = NULL;
    }
    return conn;
}

bool addModulation(ParamList* paramList, Mod* source, Parameter* destination, float amount, ModulationOperation type) {
    ModConnection* conn = createConnection(paramList, source, amount, type);
    if (!conn) return false;
    if (destination->modulators == NULL) {
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

Random* createRandom(ParamList* paramList, ModList* modList, int index, float rate, RandomType type, char* name){
    Random* rnd = (Random*)malloc(sizeof(Random));
    rnd->base.name = name;
    rnd->base.index = index;
    rnd->base.type = MT_RND;
    //rnd->base.destinations = NULL;
    rnd->lastPhase = 0.0f;
    rnd->lastRandom = 0.0f;
    rnd->base.output = createParameter(paramList, "RNG output", 0.0f, -1.0f, 1.0f);
    switch(type){
        case RT_SNH:
            rnd->base.generate = generateRandom;
            break;
        case RT_DRK:
            rnd->base.generate = generateDrunk;
            break;
    }
    rnd->rate = createParameter(paramList, "RNG rate", rate, 0.1f, 100.0f);
    rnd->phase = createParameter(paramList, "RNG phase", 0.0f, 0.0f, 1.0f);
    rnd->shape = type;
    
    addToModList(modList, &rnd->base);

    return rnd;
}

LFO* createLFO(ParamList* paramList, ModList* modList, int index, float rate, int shape, char* name) {
	//DEBUG_LOG("Create LFO");

    LFO* lfo = (LFO*)malloc(sizeof(LFO));
    memset(lfo, 0, sizeof(LFO));
    
    // Initialize base Mod
    //lfo->base.dependencies = NULL;
    lfo->base.name = name;
    lfo->base.dependency_count = 0;
    lfo->base.processed = false;
    lfo->base.visiting = false;
    lfo->base.index = index;
    lfo->base.type = MT_LFO;
    //lfo->base.destinations = NULL;
    lfo->base.output = createParameter(paramList,"LFO output", 0.0f, -1.0f, 1.0f);
    switch(shape){
		case LS_SIN:
			lfo->base.generate = generateSine;
			break;
		case LS_SQU:
			lfo->base.generate = generateSquare;
			break;
		case LS_RMP:
			lfo->base.generate = generateRamp;
			break;
	}
    
    // Initialize LFO-specific parameters
    lfo->rate = createParameter(paramList, "LFO rate", rate, 0.1f, 100.0f);
    lfo->phase = createParameter(paramList, "LFO phase", 0.0f, 0.0f, 1.0f);
    lfo->shape = shape;

    addToModList(modList, &lfo->base);

    return lfo;
}

void generateSine(void* self) {
    LFO* lfo = (LFO*)self;
    float value = sinf(getParameterValue(lfo->phase) * 2.0f * M_PI);
    setParameterBaseValue(lfo->base.output, value);
    setParameterValue(lfo->base.output, value);
}

void generateSquare(void* self) {
    LFO* lfo = (LFO*)self;
    float value = getParameterValue(lfo->phase) < 0.5f ? 1.0f : -1.0f;
    setParameterBaseValue(lfo->base.output, value);
    setParameterValue(lfo->base.output, value);
}

void generateRamp(void* self) {
    LFO* lfo = (LFO*)self;
    float value = (getParameterValue(lfo->phase) - 1.0f) * 2.0f;
    setParameterBaseValue(lfo->base.output, value);
    setParameterValue(lfo->base.output, value);
}

void generateRandom(void* self) {
    Random* rnd = (Random*)self;
    float phase = getParameterValue(rnd->phase);
    
    if (phase < rnd->lastPhase) {
        rnd->lastRandom = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    }
    
    rnd->lastPhase = phase;
    setParameterBaseValue(rnd->base.output, rnd->lastRandom);
    setParameterValue(rnd->base.output, rnd->lastRandom);
}

void generateDrunk(void* self){
    Random* rnd = (Random*)self;
    float phase = getParameterValue(rnd->phase);
    
    rnd->lastRandom = ((float)rand() / (float)RAND_MAX) * 2.0f  - 1.0f;  
    rnd->lastRandom *= 0.5f * ((float)rand() / (float)RAND_MAX);
    rnd->lastPhase = phase;
    setParameterBaseValue(rnd->base.output, rnd->base.output->currentValue + rnd->lastRandom);
    setParameterValue(rnd->base.output, rnd->base.output->currentValue + rnd->lastRandom);
}

void updateMod(Mod* mod, float deltaTime) {
	//DEBUG_LOG("update mod");
    if (mod == NULL) return;
    
    switch(mod->type){
        case MT_ENV:
            Envelope* env = (Envelope*)mod;
            if (env->isTriggered) {
                env->currentTime += deltaTime;
            }
            break;
        case MT_LFO:
            LFO* lfo = (LFO*)mod;
            float l_phase = getParameterValue(lfo->phase);
            float l_rate = getParameterValue(lfo->rate);
            l_phase += l_rate * deltaTime;
            if (l_phase >= 1.0f) l_phase -= 1.0f;
            setParameterBaseValue(lfo->phase, l_phase);
            setParameterValue(lfo->phase, l_phase);
            break;
        case MT_RND:
            Random* rand = (Random*)mod;
            float r_phase = getParameterValue(rand->phase);
            float r_rate = getParameterValue(rand->rate);
            r_phase += r_rate * deltaTime;
            if (r_phase >= 1.0f) r_phase -= 1.0f;
            setParameterBaseValue(rand->phase, r_phase);
            setParameterValue(rand->phase, r_phase);
        default:
            break;
    }
	//DEBUG_LOG("update mod DONE");
}


float applyCurve(float x, float curvature) {
    // Ensure inputs are in valid ranges
    x = fmaxf(0.0f, fminf(1.0f, x));
    curvature = fmaxf(0.0f, fminf(1.0f, curvature));
    
    if (fabsf(curvature - 0.5f) < 0.001f) {
        return x; // Linear interpolation
    }
    
    // Convert curvature from [0,1] to [-4,4] for more pronounced effect
    float curve_amount = (curvature - 0.5f) * 8.0f;
    
    // Apply exponential curve
    if (curve_amount > 0) {
        return powf(x, 1.0f + curve_amount);
    } else {
        return 1.0f - powf(1.0f - x, 1.0f - curve_amount);
    }
}

void triggerEnvelope(Envelope* env){
    //DEBUG_LOG("triggering env");
    env->currentStageIndex = 0;
    env->isTriggered = true;
}

void generateEnvelope(void* self) {
    Envelope* env = (Envelope*)self;
    if (!env || !env->isTriggered || env->currentStageIndex >= env->stageCount) {
        return;
    }
    
    EnvelopeStage* stage = &env->stages[env->currentStageIndex];
    if (!stage->duration || !stage->curvature) {
        return;
    }

    float dt = 1.0f / env->sampleRate;
    env->currentTime += dt;
    
    float t = env->currentTime / stage->duration->currentValue;
    float shapedT = applyCurve(t, stage->curvature->currentValue);
    
    float startLevel = (env->currentStageIndex > 0) ? 
                      env->stages[env->currentStageIndex-1].targetLevel : 
                      0.0f;
    
    env->currentLevel = startLevel + (stage->targetLevel - startLevel) * shapedT;
    if (t >= 1.0f) {
            //printf("stage %i complete\n", env->currentStageIndex);

        env->currentTime = 0.0f;
        env->currentLevel = stage->targetLevel;
        if (++env->currentStageIndex >= env->stageCount) {
            env->isTriggered = false;
            //printf("TRIGGER OFF!!!!!!\n");
        }
    }

    // Important: Update output parameter
    setParameterBaseValue(env->base.output, env->currentLevel);
    setParameterValue(env->base.output, env->currentLevel);

}

void modifyParameterValue(Parameter* parameter, float relativeValue){
    float currentValue = getParameterValue(parameter);
    setParameterValue(parameter, currentValue + relativeValue);
}

void modifyParameterBaseValue(Parameter* parameter, float relativeValue){
    float currentValue = parameter->baseValue;
    setParameterBaseValue(parameter, currentValue + relativeValue);
}

void incParameterBaseValue(Parameter* parameter, float relativeValue){
    float currentValue = parameter->baseValue;
    float sign = 1.0f;
    if( relativeValue < 0.0f){
        sign = -1.0f;
    }
    if(abs(relativeValue) > 1){
        setParameterBaseValue(parameter, currentValue + parameter->coarseIncrement * sign);
    } else {
        setParameterBaseValue(parameter, currentValue + parameter->fineIncrement * sign);
    }
}

Envelope* createEnvelope(ParamList* paramList, ModList* modList, int index, float sampleRate, char* name) {
	//DEBUG_LOG("create env");
    Envelope* env = (Envelope*)malloc(sizeof(Envelope));
    
    // Initialize base mod
    env->base.name = name;
    env->base.index = index;
    env->base.type = MT_ENV;
    env->base.output = createParameter(paramList, "env output", 0.0f, 0.0f, 1.0f);
    env->base.generate = generateEnvelope;
    //env->base.destinations = NULL;
    //env->base.dependencies = NULL;
    env->base.dependency_count = 0;
    env->base.processed = false;
    env->base.visiting = false;
    
    // Initialize envelope properties
    env->sampleRate = sampleRate;
    env->currentLevel = 0.0f;
    env->currentStageIndex = 0;
    env->stageCount = 0;
    env->currentTime = 0.0f;
    env->totalElapsedTime = 0.0f;
    env->isTriggered = false;
    env->isSustaining = false;
    env->loop = false;
    
    addToModList(modList, &env->base);

    return env;
}

void addEnvelopeStage(ParamList* paramList, Envelope* env, bool isRising, float duration, float targetLevel, float initialCurvature, char* name) {
	//DEBUG_LOG("add env stage");
    if (env->stageCount >= MAX_ENVELOPE_STAGES) {
        return;
    }
    
    char nameBuf[32];
    int idx = env->stageCount;
    
    EnvelopeStage* stage = &env->stages[idx];
    stage->isRising = isRising;
    stage->isSustain = (duration <= 0.0f);
    stage->name = name;
    
    sprintf(nameBuf, "env%d_dur%d", env->base.index, idx);
    stage->duration = createParameter(paramList, nameBuf, duration, 0.001f, 10.0f);
    
    stage->targetLevel = targetLevel;
    
    sprintf(nameBuf, "env%d_curve%d", env->base.index, idx);
    stage->curvature = createParameter(paramList, nameBuf, initialCurvature, -1.0f, 1.0f);
    
    env->stageCount++;
}

void addParamPointerEnvelopeStage(ParamList* paramList, Envelope* env, bool isRising, Parameter* duration, float targetLevel, Parameter* initialCurvature, char* name) {
	//DEBUG_LOG("add env stage");
    if (env->stageCount >= MAX_ENVELOPE_STAGES) {
        return;
    }
    
    char nameBuf[32];
    int idx = env->stageCount;
    
    EnvelopeStage* stage = &env->stages[idx];
    stage->isRising = isRising;
    stage->isSustain = (duration->baseValue <= 0.0f);
    stage->name = name;
    
    sprintf(nameBuf, "env%d_dur%d", env->base.index, idx);
    stage->duration = duration;
    
    stage->targetLevel = targetLevel;
    
    sprintf(nameBuf, "env%d_curve%d", env->base.index, idx);
    stage->curvature = initialCurvature;
    
    env->stageCount++;
}

Envelope* createADSR(ParamList* paramList, ModList* modList, float a, float d, float s, float r, char* name){
	//DEBUG_LOG("create adsr");
	Envelope* env = createEnvelope(paramList, modList, 4, 44100, name);

	addEnvelopeStage(paramList, env, true,  a, 1.0f, 0.75f, "A");  // Attack
	addEnvelopeStage(paramList, env, false, d, 0.7f, 0.75f, "D");  // Decay
	addEnvelopeStage(paramList, env, true,  s, 0.7f, 0.5f, "S");  // Sustain
	addEnvelopeStage(paramList, env, false, r, 0.0f, 0.75f, "R");  // Release

	return env;
}

Envelope* createAD(ParamList* paramList, ModList* modList, float a, float d, char* name){
    Envelope* env = createEnvelope(paramList, modList, 4, 44100, name);

	addEnvelopeStage(paramList, env, true,  a, 1.0f, 0.95f, "A");  // Attack
	addEnvelopeStage(paramList, env, false, d, 0.0f, 0.1f, "D");  // Decay

	return env;
}

Envelope* createParamPointerAD(ParamList* paramList, ModList* modList, Parameter* a, Parameter* d, Parameter* acurve, Parameter* dcurve, char* name){
    Envelope* env = createEnvelope(paramList, modList, 4, 44100, name);

	addParamPointerEnvelopeStage(paramList, env, true,  a, 1.0f, acurve, "A");  // Attack
	addParamPointerEnvelopeStage(paramList, env, false, d, 0.0f, dcurve, "D");  // Decay

	return env;
}

void processModulations(ParamList* paramList, ModList* modList, float deltaTime) {
    if (!modList) return;
    if (!paramList) return;
    
    for (int i = 0; i < modList->count; i++) {
        Mod* mod = modList->mods[i];
        updateMod(mod, deltaTime);
        if (!mod->generate) continue;
        
        mod->generate(mod);
    }
    
    for(int i = 0; i < paramList->count; i++){
        ModConnection* conn = paramList->params[i]->modulators;
        float finalValue = paramList->params[i]->baseValue;

        while (conn != NULL) {
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
                    if (modValue != 0.0f) {
                        finalValue /= modValue;
                    }
                    break;
                default:
                    break;
            }
            

            ModConnection* next = conn->next;
            conn = next;
        }
        setParameterValue(paramList->params[i], finalValue);
    }
}

void freeParameter(Parameter* param) {
    if (!param) {
        return;
    }

    ModConnection* current = param->modulators;
    while (current != NULL) {
        ModConnection* next = current->next;
        if (current) {
            free(current);
        }
        current = next;
    }
    param->modulators = NULL; 

    if (param->name) {
        free(param->name);
        param->name = NULL;
    }
    free(param);
}

void freeMod(Mod* mod) {
    if (!mod) {
        return;
    }
    
    if (mod->output) {
        freeParameter(mod->output);
        mod->output = NULL;
    }
    
    free(mod);
}

void freeModList(ModList* list) {
    if (!list) {
        return;
    }

    for (int i = 0; i < list->count; i++) {
        freeMod(list->mods[i]);
    }
    free(list);
}

void freeParamList(ParamList* list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        freeParameter(list->params[i]);
    }
    free(list);
}


void freeLFO(LFO* lfo) {
    if (!lfo) return;
    
    // Free parameters in specific order
    if (lfo->phase) {
        freeParameter(lfo->phase);
        lfo->phase = NULL;
    }
    if (lfo->rate) {
        freeParameter(lfo->rate);
        lfo->rate = NULL;
    }
    if (lfo->base.output) {
        freeParameter(lfo->base.output);
        lfo->base.output = NULL;
    }
    
    free(lfo);
}

void freeRandom(Random* rnd) {
    if (!rnd) return;
    
    freeParameter(rnd->base.output);
    freeParameter(rnd->rate);
    freeParameter(rnd->phase);
    
    free(rnd);
}

void freeEnvelope(Envelope* env) {
    if (!env) return;
    
    freeParameter(env->base.output);
    
    for (int i = 0; i < env->stageCount; i++) {
        freeParameter(env->stages[i].duration);
        freeParameter(env->stages[i].curvature);
    }
    
    free(env);
}

void cleanupModSystem(ModList* list) {
    if (!list) return;
    
    for (int i = 0; i < list->count; i++) {
        Mod* mod = list->mods[i];
        if (!mod) continue;

        // Free mod-specific resources
        switch (mod->type) {
            case MT_LFO:
                freeLFO((LFO*)mod);
                break;
            case MT_RND:
                freeRandom((Random*)mod);
                break;
            case MT_ENV:
                freeEnvelope((Envelope*)mod);
                break;
            default:
                freeMod(mod);
        }
    }
    free(list);
}
