#ifndef BLIT_SYNTH_H
#define BLIT_SYNTH_H

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> // Include for malloc
#include <string.h> // Include for memset
#include "settings.h"

#define NO_BLIT false

typedef struct {
    float dac_center;
    float dac_max;
    float osc_val;
    float osc_val_diff;
    float freq;
    float blit_buffer[16];
    float impulse_buffer[16];
} BlitParams;

BlitParams* init_blit();
float blit_synth(float phase, float increment, BlitParams* bp);
float poly_blep(float phaseIncrement, float t);
float blep_square(float phase, float increment);
float blep_saw(float phase, float increment);
float blep_tri(float phase, float increment);
float noblep_sine(float phase);

#endif 