#ifndef FX_H
#define FX_H

#include "sample.h"

#define GRANULAR_BUFFER_SIZE 441000 //10 seconds
#define GRAIN_COUNT 8

typedef struct {
    float buffer[GRANULAR_BUFFER_SIZE];
    int writeHead;
    int grainPos[GRAIN_COUNT];
} GranularProcessor;

void granularProcess(GranularProcessor* gp, float sample);

#endif