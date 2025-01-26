#ifndef FILTERS_H
#define FILTERS_H

#define SMALLEST_POS_FLOAT 1.175494351e-38         /* min positive value */
#define SMALLEST_NEG_FLOAT -1.175494351e-38         /* min negative value */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "settings.h"

typedef struct BiquadFilter BiquadFilter;
typedef float (*BiquadProcessor)(BiquadFilter* bf, float xn);

typedef enum {
    a0,
    a1,
    a2,
    b1,
    b2,
    c0,
    d0,
    coeff_count
} FilterCoeff;

typedef enum {
    x_z1,
    x_z2,
    y_z1,
    y_z2,
    state_count
} FilterState;

typedef enum {
    kDirect,
    kCanonical,
    kTransposeDirect,
    kTransposeCanonical,
    biquad_count
} BiquadType;

typedef enum {
    secondOrderLPF,
    secondOrderHPF,
    filter_count
} FilterType;

struct BiquadFilter {
    float coefficients[coeff_count];
    float states[state_count];
    BiquadType type;
    BiquadProcessor processSample;
};

typedef struct {
    FilterType type;
    BiquadFilter* biquad;
    float q;
} Filter;

typedef struct {
    float terminalResistace;
    float openTerminalResistance;
    float sourceResistance;
    
} WdfAdaptor;

BiquadFilter* createBiquadFilter(BiquadType type);
void resetState(BiquadFilter* bf);

int checkFLoatUnderflow(float* val);

float processKDirect(BiquadFilter* bf, float xn);
float processKCanonical(BiquadFilter* bf, float xn);
float processKTransposeDirect(BiquadFilter* bf, float xn);
float processKTransposeCanonical(BiquadFilter* bf, float xn);

Filter* createFilter(BiquadType bfType, FilterType fType, float freq, float q);

#endif