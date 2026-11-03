#include "sample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include "settings.h"

SamplePool *createSamplePool(void) {
	return createSamplePoolWithCapacity(MAX_SAMPLE_POOL_BYTES);
}

SamplePool *createSamplePoolWithCapacity(size_t capacityBytes) {
	// printf("creating sample pool.\n");

	SamplePool *sp = (SamplePool *)malloc(sizeof(SamplePool));
	if(!sp) {
		printf("could not allocate memory for sample pool struct.\n");
		return NULL;
	}
	/* The arena is NOT allocated here. The pool used to malloc the whole
	 * ceiling up front -- 32 MB by default -- whether the device had one
	 * sample in it or none. On a 128 MB handheld that is a quarter of the
	 * RAM, held for the app's whole life, and the pool is the largest
	 * single allocation the engine makes.
	 *
	 * It is allocated when the first sample arrives and grown as more
	 * arrive, up to the ceiling. */
	sp->sampleData = NULL;
	sp->memoryUsed = 0;
	sp->allocatedBytes = 0;
	sp->capacityBytes = capacityBytes;
	sp->samples = malloc(sizeof(Sample *) * MAX_LOADED_SAMPLES);
	sp->sampleCount = 0;
	sp->maxSamples = MAX_LOADED_SAMPLES;
	if(!sp->samples) {
		free(sp);
		printf("could not allocate memory for sample within pool.\n");
		return NULL;
	}
	printf("\t-> DONE.\n");
	return sp;
}

/* Make room for `needed` more bytes in the arena, growing it if it must.
 *
 * The arena is one block, so growing it moves the memory every loaded
 * sample points into: the samples' data pointers have to be fixed up
 * afterwards, or they would point into the freed block.
 *
 * Returns false when the pool's ceiling would be passed, or when the
 * allocation fails. */
static bool samplePoolReserve(SamplePool *sp, size_t needed) {
	if(sp->memoryUsed + needed > sp->capacityBytes) {
		return false;
	}
	if(sp->sampleData && sp->memoryUsed + needed <= sp->allocatedBytes) {
		return true;
	}
	/* Grow geometrically, so a pool that receives a hundred samples does
	 * not reallocate a hundred times. */
	size_t want = sp->allocatedBytes ? sp->allocatedBytes : 65536;
	while(want < sp->memoryUsed + needed) {
		if(want > sp->capacityBytes / 2) {
			want = sp->capacityBytes;
			break;
		}
		want *= 2;
	}
	if(want > sp->capacityBytes) {
		want = sp->capacityBytes;
	}
	char *grown = (char *)realloc(sp->sampleData, want);
	if(!grown) {
		return false;
	}
	/* Fix up the samples that pointed into the old block. The offsets
	 * are computed from the old base, before it was replaced. */
	for(size_t i = 0; i < sp->sampleCount; i++) {
		Sample *s = sp->samples[i];
		if(s && s->data && sp->sampleData) {
			size_t offset = (size_t)((char *)s->data - sp->sampleData);
			s->data = (float *)(grown + offset);
		}
	}
	sp->sampleData = grown;
	sp->allocatedBytes = want;
	return true;
}

void freeSamplePool(SamplePool *sp) {
	if(!sp) return;

	for(size_t i = 0; i < sp->sampleCount; i++) {
		/* Sample.data points into the pool arena (freed below); only the
		 * separately-allocated name is owned here. */
		free(sp->samples[i]->name);
		free(sp->samples[i]);
	}
	free(sp->samples);
	free(sp->sampleData);
	free(sp);
}

void loadSample(SamplePool *sp, const char *name, float *data, int bit, int sampleSr, int length) {
	if(sp->sampleCount >= sp->maxSamples) {
		printf("Error: Maximum number of samples reached ().\n");
		return;
	}
	size_t dataSize = length * sizeof(float);
	/* The pool arena is a fixed MAX_SAMPLE_POOL_BYTES block; refuse a sample
	 * that would run past the end instead of overflowing it. Also refuse once
	 * the sample table is full — the previous code wrote samples[maxSamples]
	 * out of bounds (found by P3 review). */
	if(length < 0 || sp->memoryUsed + dataSize > sp->capacityBytes) {
		printf("Sample pool exhausted\n");
		return;
	}
	if(!samplePoolReserve(sp, dataSize)) {
		printf("Sample pool exhausted\n");
		return;
	}
	if(sp->sampleCount >= sp->maxSamples) {
		printf("Max sample count reached\n");
		return;
	}
	float *sampleData = (float *)((char *)sp->sampleData + sp->memoryUsed);

	memcpy(sampleData, data, dataSize);

	Sample *sample = (Sample *)malloc(sizeof(Sample));
	if(!sample) return;

	sample->data = sampleData;

	sample->name = (char *)malloc(strlen(name) + 1);
	if(!sample->name) {
		free(sample);
		return;
	}
	strcpy(sample->name, name);
	sample->bit = bit;
	sample->length = length;
	sample->sampleRate = sampleSr;

	printf("adding sample of %i length, %i bit\n", sample->length, sample->bit);

	sp->samples[sp->sampleCount] = sample;
	sp->sampleCount++;
	sp->memoryUsed += dataSize;

	printf("%zu samples, %zu memoryUsed \n", sp->sampleCount, sp->memoryUsed);
}

void freeSample(Sample *sample) {
	if(sample->data) {
		free(sample->data);
		sample->data = NULL;
	}
	sample->length = 0;
	sample->sampleRate = 0;
}

float getSampleValueFwd(Sample *sample, float *samplePosition, float phaseIncrement, int loop) {
	// Validate the sample
	if(!sample || !sample->data || sample->length <= 0) {
		fprintf(stderr, "Invalid sample or data\n");
		return 0.0f;
	}
	float adjusted_phase_increment = phaseIncrement * (PA_SR / ((float)sample->sampleRate / sample->bit) * 2);
	*samplePosition += adjusted_phase_increment;

	if(*samplePosition >= sample->length) {
		if(loop) {
			*samplePosition -= sample->length;
		} else {
			*samplePosition = sample->length - 1;
		}
	}
	// Calculate the wavetable indices and interpolation fraction
	int indexFloor = (int)*samplePosition;
	int indexCeil = (indexFloor + 1) % sample->length; // Wrap around at the end
	float frac = *samplePosition - indexFloor;

	// Perform linear interpolation between indexFloor and indexCeil
	float value = sample->data[indexFloor] * (1.0f - frac) + sample->data[indexCeil] * frac;
	if(*samplePosition >= sample->length - 2) {
		return 0;
	}
	return value;
}

void samplePeakEnvelope(const Sample *sample, float lo, float hi, int cols, float *outMin, float *outMax) {
	if(!sample || !sample->data || sample->length <= 0 || cols <= 0 || !outMin || !outMax) {
		return;
	}
	float len = (float)sample->length;
	if(lo < 0.0f) {
		lo = 0.0f;
	}
	if(hi > len) {
		hi = len;
	}
	if(hi <= lo) {
		/* Empty/backwards window: fall back to a one-sample span at the
		 * clamped low edge so the per-column span is never zero. */
		hi = lo + 1.0f;
		if(hi > len) {
			hi = len;
			lo = len - 1.0f;
		}
	}
	float span = hi - lo;
	for(int c = 0; c < cols; c++) {
		int i0 = (int)(lo + span * (float)c / (float)cols);
		int i1 = (int)(lo + span * (float)(c + 1) / (float)cols);
		if(i0 < 0) {
			i0 = 0;
		}
		if(i0 > sample->length - 1) {
			i0 = sample->length - 1;
		}
		if(i1 > sample->length) {
			i1 = sample->length;
		}
		if(i1 <= i0) {
			/* Narrower than one sample per column: include i0 alone. */
			i1 = i0 + 1;
		}
		float mn = sample->data[i0];
		float mx = sample->data[i0];
		for(int i = i0; i < i1; i++) {
			float v = sample->data[i];
			if(v < mn) {
				mn = v;
			}
			if(v > mx) {
				mx = v;
			}
		}
		outMin[c] = mn;
		outMax[c] = mx;
	}
}

float getSampleValueRev(Sample *sample, float *samplePosition, float phaseIncrement, int loop) {
	// Validate the sample
	if(!sample || !sample->data || sample->length <= 0) {
		fprintf(stderr, "Invalid sample or data\n");
		return 0.0f;
	}
	float adjusted_phase_increment = phaseIncrement * (PA_SR / ((float)sample->sampleRate / sample->bit) * 2);
	*samplePosition -= adjusted_phase_increment;

	if(*samplePosition < 0) {
		if(loop) {
			*samplePosition += sample->length;
		} else {
			*samplePosition = 0;
		}
	}
	// Calculate the wavetable indices and interpolation fraction
	int indexFloor = (int)*samplePosition;
	int indexCeil = (indexFloor + 1) % sample->length; // Wrap around at the end
	float frac = *samplePosition - indexFloor;

	// Perform linear interpolation between indexFloor and indexCeil
	return sample->data[indexFloor] * (1.0f - frac) + sample->data[indexCeil] * frac;
}
