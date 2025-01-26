#include "filters.h"

BiquadFilter* createBiquadFilter(BiquadType type){
    BiquadFilter* bf = (BiquadFilter*)malloc(sizeof(BiquadFilter));
    if(!bf){
        printf("error: could not allocate memory for BiquadFilter, returning NULL.\n");
        return NULL;
    }
    bf->type = type;
    for(int i = 0; i < state_count; i++){
        bf->states[i] = 0.0f;
    }
    switch(type){
        case kDirect:
            bf->processSample = processKDirect;
            break;
        case kCanonical:
            bf->processSample = processKCanonical;
            break;
        case kTransposeDirect:
            bf->processSample = processKTransposeDirect;
            break;
        case kTransposeCanonical:
            bf->processSample = processKTransposeCanonical;
            break;
    }

    return bf;
}

int checkFLoatUnderflow(float* val){
    int undeflowPresent = 0;
    if(*val > 0.0 && *val < SMALLEST_POS_FLOAT){
        undeflowPresent = 1;
        *val = 0.0f;
    }
    if(*val < 0.0 && *val > SMALLEST_NEG_FLOAT){
        undeflowPresent = 1;
        *val = 0.0f;
    }
    return undeflowPresent;
}

float processKDirect(BiquadFilter* bf, float xn){
    float yn = 
        bf->coefficients[a0] * xn +
        bf->coefficients[a1] * bf->states[x_z1] - 
        bf->coefficients[a2] * bf->states[x_z2] -
        bf->coefficients[b1] * bf->states[y_z1] -
        bf->coefficients[b2] * bf->states[y_z2];
    
    bf->states[x_z2] = bf->states[x_z1];
    bf->states[x_z1] = xn;

    bf->states[y_z2] = bf->states[y_z1];
    bf->states[y_z1] = yn;

    return (yn * bf->coefficients[c0]) + (xn * bf->coefficients[d0]);
}

float processKCanonical(BiquadFilter* bf, float xn){
    float wn = xn-(bf->coefficients[b1]*bf->states[x_z1]) - (bf->coefficients[b2]*bf->states[x_z2]);
    float yn =  bf->coefficients[a0] * wn + 
                bf->coefficients[a1] * bf->states[x_z1] +
                bf->coefficients[a2] * bf->states[x_z2];
    
    bf->states[x_z2] = bf->states[x_z1];
    bf->states[x_z1] = wn;

    return (yn * bf->coefficients[c0]) + (xn * bf->coefficients[d0]);
}

float processKTransposeDirect(BiquadFilter* bf, float xn){
    float wn = xn + bf->states[y_z1];
    float yn = bf->coefficients[a0] * wn + bf->states[x_z1];

    checkFLoatUnderflow(&yn);

    bf->states[y_z1] = bf->states[y_z2] - bf->coefficients[b1] * wn;
    bf->states[y_z2] = -bf->coefficients[b2] * wn;

    bf->states[x_z1] = bf->states[x_z2] - bf->coefficients[a1] * wn;
    bf->states[x_z2] = -bf->coefficients[a2] * wn;

    return (yn * bf->coefficients[c0]) + (xn * bf->coefficients[d0]);
}

float processKTransposeCanonical(BiquadFilter* bf, float xn){
    float yn = bf->coefficients[a0] * xn + bf->states[x_z1];

    checkFLoatUnderflow(&yn);

    bf->states[x_z1] =  bf->coefficients[a1] * xn -
                        bf->coefficients[b1] * yn + bf->states[x_z2];
    bf->states[x_z2] = bf->coefficients[a2] * xn - bf->coefficients[b2] * yn;

    return (yn * bf->coefficients[c0]) + (xn * bf->coefficients[d0]);
}

Filter* createFilter(BiquadType bfType, FilterType fType, float freq, float q){
    BiquadFilter* bf = createBiquadFilter(bfType);
    Filter* flt = (Filter*)malloc(sizeof(Filter));
    if(!flt){
        printf("error: could not allocate memory for Filter, returning NULL");
        return NULL;
    }

    flt->biquad = bf;
    flt->type = fType;
    flt->q = q;    
    
    float angle = 0.0f, d = 0.0f, anglesin = 0.0f, anglecos = 0.0f, beta = 0.0f, theta = 0.0f;

    switch(fType){
        case secondOrderLPF:
            angle = (TWOPI * freq) / PA_SR;
            d = 1 / q;
            anglesin = sinf(angle);
            anglecos = cosf(angle);
            beta = 0.5f * (
                (1.0f - (d / 2.0f) * anglesin) /
                (1.0f + (d / 2.0f) * anglesin)
            );
            theta = (0.5f + beta) * anglecos;
            flt->biquad->coefficients[a1] = (0.5f + beta - theta);
            flt->biquad->coefficients[a0] = flt->biquad->coefficients[a1] / 2.0f;
            flt->biquad->coefficients[a2] = flt->biquad->coefficients[a0];
            flt->biquad->coefficients[b1] = -2.0f * theta;
            flt->biquad->coefficients[b2] = 2.0f * beta;
            flt->biquad->coefficients[c0] = 1.0f;
            flt->biquad->coefficients[d0] = 0.0f;
            break;
        case secondOrderHPF:
            angle = (TWOPI * freq) / PA_SR;
            d = 1 / q;
            anglesin = sinf(angle);
            anglecos = cosf(angle);
            beta = 0.5f * (
                (1.0f - (d / 2.0f) * anglesin) /
                (1.0f + (d / 2.0f) * anglesin)
            );
            theta = (0.5f + beta) * anglecos;
            flt->biquad->coefficients[a1] = -(0.5f + beta + theta);
            flt->biquad->coefficients[a0] = (0.5f + beta + theta) / 2.0f;
            flt->biquad->coefficients[a2] = (0.5f + beta + theta);
            flt->biquad->coefficients[b1] = -2.0f * theta;
            flt->biquad->coefficients[b2] = 2.0f * beta;
            flt->biquad->coefficients[c0] = 1.0f;
            flt->biquad->coefficients[d0] = 0.0f;
            break;
        default:
            printf("error: invalid Filter type, returning NULL");
            return NULL;
            break;
    }

    return flt;
}


// flt->biquad->coefficients[a0] = (0.5f + beta - theta) / 2.0f;
// flt->biquad->coefficients[a1] = ;
// flt->biquad->coefficients[a2];
// flt->biquad->coefficients[b1];
// flt->biquad->coefficients[b2];
// flt->biquad->coefficients[c0];
// flt->biquad->coefficients[d0];