#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdint.h>

#define MAX_SAMPLE_POOL_BYTES 64000000
#define MAX_LOADED_SAMPLES 1024

#pragma pack(push, 1)
typedef struct
{
	char chunkID[4];		// "RIFF"
	uint32_t chunkSize;		// Size of the entire file minus 8 bytes
	char format[4];			// "WAVE"
	char subchunk1ID[4];	// "fmt "
	uint32_t subchunk1Size; // Size of the fmt chunk (in bytes)
	uint16_t audioFormat;	// Audio format (1 for PCM)
	uint16_t numChannels;	// Number of channels
	uint32_t sampleRate;	// Sampling rate
	uint32_t byteRate;		// Byte rate = SampleRate * NumChannels * BitsPerSample/8
	uint16_t blockAlign;	// Block align = NumChannels * BitsPerSample/8
	uint16_t bitsPerSample; // Bits per sample
	char subchunk2ID[4];	// "data"
	uint32_t subchunk2Size; // Size of the data chunk (in bytes)
} WAVHeader;
#pragma pack(pop)

typedef struct {
    float *data;
    char* name;
    int length;
    int sampleRate;
    int bit;
} Sample;

typedef struct {
    char* sampleData;
    size_t memoryUsed;
    Sample** samples;
    size_t sampleCount;
    size_t maxSamples;
} SamplePool;


void loadSample(SamplePool* sp, const char* name, float* data, int bit, int sampleSr, int length);
SamplePool* createSamplePool();
void freeSamplePool(SamplePool* sp);
void freeSample(Sample *sample);
float getSampleValue(Sample *sample, float *samplePosition, float phaseIncrement, int paSr, int loop);

#endif