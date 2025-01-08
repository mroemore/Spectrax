#include "sample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

void free_sample(Sample *sample)
{
	if (sample->data)
	{
		free(sample->data);
		sample->data = NULL;
	}
	sample->length = 0;
	sample->sample_rate = 0;
}

float get_sample_value(Sample *sample, float *sample_position, float phase_increment, int pa_sr, int loop) {
    // Validate the sample
    if (!sample || !sample->data || sample->length <= 0) {
        fprintf(stderr, "Invalid sample or data\n");
        return 0.0f;
    }
	float adjusted_phase_increment = phase_increment * (pa_sr / (sample->sample_rate /  sample->bit)*2);
	*sample_position += adjusted_phase_increment;

	if (*sample_position >= sample->length) {
		if(loop){
			*sample_position -= sample->length;
		} else {
			*sample_position = sample->length-1;
		}
	}
    // Calculate the wavetable indices and interpolation fraction
    int indexFloor = (int)*sample_position;
    int indexCeil = (indexFloor + 1) % sample->length; // Wrap around at the end
    float frac = *sample_position - indexFloor;

    // Perform linear interpolation between indexFloor and indexCeil
    float value = sample->data[indexFloor] * (1.0f - frac) + sample->data[indexCeil] * frac;
	if(*sample_position >= sample->length-2){
		return 0;
	}
    return value;
}
