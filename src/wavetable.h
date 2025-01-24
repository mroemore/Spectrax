#ifndef WAVETABLE_H
#define WAVETABLE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_WAVETABLES 128
#define MAX_WTPOOL_BYTES 1600000

typedef struct {
    float *data;
    char* name;
    int length;
} Wavetable;

typedef struct { 
    char* data;
    Wavetable** tables; 
    size_t memoryUsed;
    int tableSizes[MAX_WAVETABLES];
    int tableCount;
} WavetablePool;

WavetablePool* createWavetablePool();
void freeWavetablePool(WavetablePool* wtp);
void loadWavetable(WavetablePool* wtp, char* name, float* data, size_t length);

#endif