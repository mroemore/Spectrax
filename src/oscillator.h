#ifndef OSCILLATOR_H
#define OSCILLATOR_H

#include "modsystem.h"

#define SAMPLE_RATE (44100)


typedef struct {
    float (*generate)(float phase, float increment);
} Oscillator;

typedef struct {
    float (*generate)(float phase, float increment);
    float phase;
    Parameter* ratio;
    Parameter* level;
} Operator;

float sawtooth_wave(float phase);
float sine_wave(float phase,  float mod);
float square_wave(float phase);
float band_limited_sawtooth(float phase, float increment);
float band_limited_square(float phase, float increment);
float sine_fm(Operator* ops[4], float frequency);
float sine_op(Operator* op, float frequency, float mod);
Operator* createOperator(ParamList* paramList, float ratio);

#endif // OSCILLATOR_H