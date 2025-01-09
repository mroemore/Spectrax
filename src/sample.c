#include "sample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

SamplePool* createSamplePool(){
    SamplePool* sp = (SamplePool*)malloc(sizeof(SamplePool));
    sp->sampleData = (char*)malloc(MAX_SAMPLE_POOL_BYTES);
    if(!sp->sampleData){
        free(sp);
        return NULL;
    }

    sp->memoryUsed = 0;
    sp->samples = malloc(sizeof(Sample*) * MAX_LOADED_SAMPLES);
	sp->sampleCount = 0;
    sp->maxSamples = MAX_LOADED_SAMPLES;
    if(!sp->samples){
        free(sp->sampleData);
        free(sp->samples);
        free(sp);
        return NULL;
    }

    return sp;
}

void freeSamplePool(SamplePool* sp){
	if(!sp) return;

	for(size_t i = 0; i < sp->sampleCount; i++){
		free(sp->samples[i]);
	}
	free(sp->samples);
	free(sp->sampleData);
	free(sp);
}

void loadSample(SamplePool* sp, const char* name, float* data, int bit, size_t length){
    if (sp->sampleCount >= sp->maxSamples) {
        printf("Error: Maximum number of samples reached ().\n");
        return;
    }
    size_t dataSize = length * sizeof(float);
    if(sp->memoryUsed + dataSize > MAX_SAMPLE_POOL_BYTES){
        printf("sample memory maxed out.\n");
        return;
    }
    float* sampleData = (float*)(sp->sampleData + sp->memoryUsed);
    memcpy(sampleData, data, dataSize);

    Sample* sample = (Sample*)malloc(sizeof(Sample));
    if(!sample) return;
    
    sample->data = sampleData;
    sample->name = (char*)malloc(strlen(name)+1);
	sample->bit = bit;
	strcpy(sample->name, name);
    sample->length = length;

    sp->samples[sp->sampleCount] = sample;
    sp->sampleCount++;
    sp->memoryUsed += dataSize;
}

void freeSample(Sample *sample)
{
	if (sample->data)
	{
		free(sample->data);
		sample->data = NULL;
	}
	sample->length = 0;
	sample->sampleRate = 0;
}

float getSampleValue(Sample *sample, float *samplePosition, float phaseIncrement, int paSr, int loop) {
    // Validate the sample
    if (!sample || !sample->data || sample->length <= 0) {
        fprintf(stderr, "Invalid sample or data\n");
        return 0.0f;
    }
	float adjusted_phase_increment = phaseIncrement * (paSr / (sample->sampleRate /  sample->bit)*2);
	*samplePosition += adjusted_phase_increment;

	if (*samplePosition >= sample->length) {
		if(loop){
			*samplePosition -= sample->length;
		} else {
			*samplePosition = sample->length-1;
		}
	}
    // Calculate the wavetable indices and interpolation fraction
    int indexFloor = (int)*samplePosition;
    int indexCeil = (indexFloor + 1) % sample->length; // Wrap around at the end
    float frac = *samplePosition - indexFloor;

    // Perform linear interpolation between indexFloor and indexCeil
    float value = sample->data[indexFloor] * (1.0f - frac) + sample->data[indexCeil] * frac;
	if(*samplePosition >= sample->length-2){
		return 0;
	}
    return value;
}
