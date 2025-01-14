#include "oscillator.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>



static float blep[BLEP_SIZE] = {
    // Precomputed BLEP table values
    0.0f, 0.0001f, 0.0004f, 0.0009f, 0.0016f, 0.0025f, 0.0036f, 0.0049f,
    0.0064f, 0.0081f, 0.01f, 0.0121f, 0.0144f, 0.0169f, 0.0196f, 0.0225f,
    0.0256f, 0.0289f, 0.0324f, 0.0361f, 0.04f, 0.0441f, 0.0484f, 0.0529f,
    0.0576f, 0.0625f, 0.0676f, 0.0729f, 0.0784f, 0.0841f, 0.09f, 0.0961f
};

WavetablePool* createWavetablePool(){
    WavetablePool* wtp = (WavetablePool*)malloc(sizeof(WavetablePool));
    if(!wtp) return NULL;
    wtp->data = (char*)malloc(MAX_WTPOOL_BYTES);
    if(!wtp->data){
        free(wtp);
        return NULL;
    }

    wtp->memoryUsed = 0;
    wtp->tableCount = 0;
    wtp->tables = malloc(sizeof(Wavetable*) * MAX_WAVETABLES);
    if(!wtp->tables){
        free(wtp->data);
        free(wtp);
        return NULL;
    }

    return wtp;
}

void freeWavetablePool(WavetablePool* wtp){
    if(!wtp) return;
    for(int i = 0; i < wtp->tableCount; i++){
        free(wtp->tables[i]);
    }
    free(wtp->tables);
    free(wtp->data);
    free(wtp);
}

void loadWavetable(WavetablePool* wtp, char* name, float* data, size_t length){
    if(wtp->tableCount > MAX_WAVETABLES){
        printf("Max WT count reached\n");
        return;
    }

    size_t dataSize = sizeof(float)* length;
    float* wtData = (float*)((char*)wtp->data + wtp->memoryUsed);
    memcpy(wtData, data, dataSize);

    Wavetable* wt = (Wavetable*)malloc(sizeof(Wavetable));
    if(!wt) return;

    wt->data = wtData;
    wt->length = length;
    wt->name = name;

    wtp->tables[wtp->tableCount] = wt;
    wtp->tableCount++;
    wtp->memoryUsed += dataSize;
}

float sawtooth_wave(float phase) {
    return 2.0f * phase - 1.0f;
}

float sine_wave(float phase, float mod) {
    return sinf(TWO_PI * (phase + mod));
}

float sine_fm(Operator* ops[4], float frequency){
    float a = sine_op(ops[2], frequency, 0.0f);
    float b = sine_op(ops[1], frequency, a);
    float c = sine_op(ops[0], frequency, b);

    return c;
}

float sineFmAlgo(Operator* ops[OP_COUNT], float frequency, int algorithm){
    int algoOffset = algorithm * ALGO_SIZE;
    float out = 0.0f;
    for(int i = 0; i < OP_COUNT; i++){
        ops[i]->lastVal = ops[i]->currentVal;
        ops[i]->currentVal = 0.0f;
        ops[i]->modVal = 0.0f;
        ops[i]->generated = 0;
    }

    for(int i = algoOffset; i < algoOffset + ALGO_SIZE; i++){
        int modDstIndex = fm_algorithm[i][1];
        int modSrcIndex = fm_algorithm[i][0];
        if(modSrcIndex == -1){
            continue;
        }
        if(!ops[modSrcIndex]->generated){
            ops[modSrcIndex]->currentVal = sine_op(ops[modSrcIndex], frequency, ops[modSrcIndex]->modVal);
            ops[modSrcIndex]->generated = 1;
        }
        if(modDstIndex == -1){
            out += ops[modSrcIndex]->currentVal;
        } else {
            ops[modDstIndex]->modVal += ops[modSrcIndex]->currentVal;
        }
    }

    return out;
}

float sine_op(Operator* op, float frequency, float mod){
    float phase_inc = (frequency * getParameterValue(op->ratio)) / SAMPLE_RATE;
    float feedbackLevel = getParameterValue(op->feedbackAmount) * op->lastVal;
    op->phase = fmodf(op->phase + phase_inc, 1.0f);
    float a = sinf(TWO_PI * (op->phase + mod));
    return a * getParameterValue(op->level);
}

float square_wave(float phase) {
    return phase < 0.5f ? 1.0f : -1.0f;
}

float band_limited_sawtooth(float phase, float increment) {
    float value = 2.0f * phase - 1.0f;
    if (phase < increment) {
        value -= blep[(int)(phase / increment * BLEP_SIZE)];
    } else if (phase > 1.0f - increment) {
        value += blep[(int)((phase - 1.0f) / increment * BLEP_SIZE)];
    }
    return value;
}

float band_limited_square(float phase, float increment) {
    float value = phase < 0.5f ? 1.0f : -1.0f;
    if (phase < increment) {
        value -= blep[(int)(phase / increment * BLEP_SIZE)];
    } else if (phase > 1.0f - increment) {
        value += blep[(int)((phase - 1.0f) / increment * BLEP_SIZE)];
    }
    return value;
}

Operator* createOperator(ParamList* paramList, float ratio){
    Operator* op = (Operator*)malloc(sizeof(Operator));
    op->generated = 0;
    op->phase = 0.0f;
    op->currentVal = 0.0f;
    op->lastVal = 0.0f;
    op->modVal = 0.0f;
    op->feedbackAmount = createParameterEx(paramList, "feedback", 0.0f, 0.0f, 1.0f, 0.01f, 0.10f);
    op->ratio = createParameterEx(paramList, "ratio", ratio, 0.25f, 30.0f, 0.01f, 1.0f);
    op->level = createParameter(paramList, "level", .5f, 0.0f, 1.0f);
    return op;
}

Operator* createParamPointerOperator(ParamList* paramList, Parameter* fbamt, Parameter* ratio, Parameter* level){
    Operator* op = (Operator*)malloc(sizeof(Operator));
    op->generated = 0;
    op->phase = 0.0f;
    op->currentVal = 0.0f;
    op->lastVal = 0.0f;
    op->modVal = 0.0f;
    op->feedbackAmount = fbamt;
    op->ratio = ratio;
    op->level = level;
    return op;
}

void freeOperator(Operator* op){
    freeParameter(op->ratio);
    freeParameter(op->level);
    free(op);
}