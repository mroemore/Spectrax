#ifndef MODSYSTEM_H
#define MODSYSTEM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#include "wavetable.h"

#define MAX_MODS 128
#define MAX_PARAMS 1024
#define MAX_CONNECTIONS 8
#define MAX_ENVELOPE_STAGES 8
#define TWO_PI 3.14159265358979323846 * 2

#define DEBUG_LOG(msg, ...) fprintf(stderr, "[DEBUG] " msg "\n", ##__VA_ARGS__)
#define DEBUG_FREE(msg, ptr) DEBUG_LOG("Freeing %s at %p", msg, (void*)ptr)

typedef enum {
	LS_SIN, // sinusoid
	LS_SQU, // Square
	LS_RMP,  // Ramp
    LS_COUNT
} LfoShape;

typedef enum {
    RT_SNH, // Sample and hold-ish
    RT_DRK,  // Meandering drunk
    RT_COUNT
} RandomType;

typedef enum {
	MT_LFO, // Low-frequency oscillator
	MT_ENV, // Envelope
    MT_RND, // Random
    MT_OFS,  // Constant Offset
    MT_COUNT
} ModType;

typedef enum {
    MO_MUL,
    MO_DIV,
    MO_ADD,
    MO_SUB,
    MO_COUNT
} ModulationOperation;

typedef struct Parameter {
    char* name;
    float baseValue;
    float currentValue;
    float minValue;
    float maxValue;
    float fineIncrement;
    float coarseIncrement;
    struct ModConnection* modulators;
    int modulator_count;
    void (*onChange)(struct Parameter* self, float newValue);
} Parameter;

typedef struct Mod {
    int index;
    ModType type;
    Parameter* output;
    char* name;
    int dependency_count;
    bool processed;
    bool visiting;
    void (*generate)(void* self);
} Mod;

typedef struct ModConnection {
    Mod* source;  // Parameter being modulated
    Parameter* amount;
    Parameter* type;
    struct ModConnection* next;
    struct ModConnection* previous;
} ModConnection;

typedef struct {
    Mod* mods[MAX_MODS];
    int count;
} ModList;

typedef struct {
    Parameter* params[MAX_PARAMS];
    int count;
} ParamList;

typedef struct {
    Mod base; 
    Parameter* rate;
    Parameter* phase;
    int shape;
} LFO;

typedef struct {
    Mod base; 
    Parameter* rate;
    Parameter* phase;
    float lastPhase;
    float lastRandom;
    int shape;
} Random;

typedef struct EnvelopeStage {
    bool isRising;         // true = rise, false = fall
    bool isSustain;        // true for sustain stages
    char* name;
    Parameter* duration;        // Time in seconds
    float targetLevel;     // Level to reach
    Parameter* curvature;      // 0.0 = log, 0.5 = linear, 1.0 = exp
    struct EnvelopeStage* next;
} EnvelopeStage;

typedef struct {
    Mod base;
    EnvelopeStage stages[MAX_ENVELOPE_STAGES];  // Fixed-size array
    int currentStageIndex;
    int stageCount;
    float sampleRate;
    float currentTime;
    float totalElapsedTime;
    float currentLevel;
    bool isTriggered;
    bool isSustaining;
    bool loop;
} Envelope;

void initModSystem();
ModList* createModList();
ParamList* createParamList();
void addToModList(ModList* list, Mod* mod);
void addToParamList(ParamList* list, Parameter* param);
bool addModulation(ParamList* paramList, Mod* source, Parameter* destination, float amount, ModulationOperation type);
void updateMod(Mod* mod, float deltaTime);
void processModulations(ParamList* paramList, ModList* modList, float deltaTime);

LFO* createLFO(ParamList* paramList, ModList* modList, int index, float rate, int shape, char* name);
Random* createRandom(ParamList* paramList, ModList* modList, int index, float rate, RandomType type, char* name);
Envelope* createEnvelope(ParamList* paramList, ModList* modList, int index, float sampleRate, char* name);
//EnvelopeStage* createEnvelopeStage(bool isRising, float duration, float targetLevel, float curvature, char* name);
void addEnvelopeStage(ParamList* paramList, Envelope* env, bool isRising, float duration, float targetLevel, float initialCurvature, char* name);
void addParamPointerEnvelopeStage(ParamList* paramList, Envelope* env, bool isRising, Parameter* duration, float targetLevel, Parameter* initialCurvature, char* name);
Envelope* createADSR(ParamList* paramList, ModList* modList, float a, float d, float s, float r, char* name);
Envelope* createParamPointerADSR(ParamList* paramList, ModList* modList, Parameter* a, Parameter* d, Parameter* s, Parameter* r, char* name);
Envelope* createAD(ParamList* paramList, ModList* modList, float a, float d, char* name);
Envelope* createParamPointerAD(ParamList* paramList, ModList* modList, Parameter* a, Parameter* d,Parameter* acurve, Parameter* dcurve, char* name);

void generateCurve(float* data, size_t length, float curve, int steepnessFactor);
void generateCurveWavetables(WavetablePool* wtp, size_t iterations, size_t wtLength);

void generateSine(void* self);
void generateSquare(void* self);
void generateRamp(void* self);
void generateRandom(void* self);
void generateDrunk(void* self);
float applyCurve(float x, float curvature);
void generateEnvelope(void* self);
void triggerEnvelope(Envelope* env);

Parameter* createParameter(ParamList* paramList, const char* name, float initialValue, float minValue, float maxValue);
Parameter* createParameterEx(ParamList* paramList, const char* name, float initialValue, float minValue, float maxValue, float fineIncrement, float coarseIncrement);
void setParameterValue(Parameter* param, float value);
void setParameterBaseValue(Parameter* param, float value);
float getParameterValue(Parameter* param);
int getParameterValueAsInt(Parameter* param);
void modifyParameterBaseValue(Parameter* parameter, float relativeValue);
void modifyParameterValue(Parameter* parameter, float relativeValue);
void incParameterBaseValue(Parameter* parameter, float relativeValue);
ModConnection* createConnection(ParamList* paramList, Mod* source, float amount, ModulationOperation type);
void incrementConnectionType(ModConnection* modConnection);
void decrementConnectionType(ModConnection* modConnection);

void freeParamList(ParamList* list);
void freeModList(ModList* list);
void freeParameter(Parameter* param);
void freeMod(Mod* mod);
void freeLFO(LFO* lfo);
void freeRandom(Random* rnd);
void freeEnvelope(Envelope* env);

void cleanupModSystem(ModList* list);

#endif