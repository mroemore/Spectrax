#include "oscillator.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define BLEP_SIZE 32
#define ALGO_SIZE 6
#define OP_COUNT 4

static float blep[BLEP_SIZE] = {
    // Precomputed BLEP table values
    0.0f, 0.0001f, 0.0004f, 0.0009f, 0.0016f, 0.0025f, 0.0036f, 0.0049f,
    0.0064f, 0.0081f, 0.01f, 0.0121f, 0.0144f, 0.0169f, 0.0196f, 0.0225f,
    0.0256f, 0.0289f, 0.0324f, 0.0361f, 0.04f, 0.0441f, 0.0484f, 0.0529f,
    0.0576f, 0.0625f, 0.0676f, 0.0729f, 0.0784f, 0.0841f, 0.09f, 0.0961f
};

static int fm_algorithm[ALGO_SIZE][2] = {
    {3,2}, {2,0}, {1,0}, {0,-1}, {-1,-1}, {-1,-1}
};

float sawtooth_wave(float phase) {
    return 2.0f * phase - 1.0f;
}

float sine_wave(float phase, float mod) {
    return sinf(2.0f * M_PI * (phase + mod));
}

float sine_fm(Operator* ops[4], float frequency){
    float dubpi = 2.0f * M_PI;

    
    float a = sine_op(ops[2], frequency, 0.0f);
    float b = sine_op(ops[1], frequency, a);
    float c = sine_op(ops[0], frequency, b);

    return c;
}

float sine_op(Operator* op, float frequency, float mod){
    float phase_inc = (frequency * getParameterValue(op->ratio)) / SAMPLE_RATE;
    op->phase = fmodf(op->phase + phase_inc, 1.0f);
    float a = sinf(2 * M_PI * (op->phase + mod));
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
    op->phase = 0.0f;
    op->ratio = createParameter(paramList, "ratio", ratio, 0.25f, 30.0f);
    op->level = createParameter(paramList, "level", .5f, 0.0f, 1.0f);
    return op;
}