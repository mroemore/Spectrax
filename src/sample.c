#include "sample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

SamplePool* createSamplePool(){
	printf("creating sample pool.\n");

    SamplePool* sp = (SamplePool*)malloc(sizeof(SamplePool));
	if(!sp){
        printf("could not allocate memory for sample pool struct.\n");
        return NULL;
    }
    sp->sampleData = (char*)malloc(MAX_SAMPLE_POOL_BYTES);
    if(!sp->sampleData){
        free(sp);
        printf("could not allocate memory for sample pool data.\n");
        return NULL;
    }

    sp->memoryUsed = 0;
    sp->samples = malloc(sizeof(Sample*) * MAX_LOADED_SAMPLES);
	sp->sampleCount = 0;
    sp->maxSamples = MAX_LOADED_SAMPLES;
    if(!sp->samples){
        free(sp->sampleData);
        free(sp);
        printf("could not allocate memory for sample within pool.\n");
        return NULL;
    }
	printf("\t-> DONE.\n");
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

void loadSample(SamplePool* sp, const char* name, float* data, int bit, int sampleSr, int length){
    if (sp->sampleCount >= sp->maxSamples) {
        printf("Error: Maximum number of samples reached ().\n");
        return;
    }
    size_t dataSize = length * sizeof(float);
    float* sampleData = (float*)((char*)sp->sampleData + sp->memoryUsed);

    memcpy(sampleData, data, dataSize);

    Sample* sample = (Sample*)malloc(sizeof(Sample));
    if(!sample) return;
    
    sample->data = sampleData;
	
	sample->name = (char*)malloc(strlen(name)+1);
	strcpy(sample->name, name);
	sample->bit = bit;
    sample->length = length;
	sample->sampleRate = sampleSr;

	printf("adding sample of %i length, %i bit\n", sample->length, sample->bit);

    sp->samples[sp->sampleCount] = sample;
    sp->sampleCount++;
    sp->memoryUsed += dataSize;

	printf("%i samples, %i memoryUsed \n", sp->sampleCount, sp->memoryUsed);
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
