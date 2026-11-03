#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_SAMPLE_POOL_BYTES 32000000
#define MAX_LOADED_SAMPLES 1024

#pragma pack(push, 1)
typedef struct
{
	char chunkID[4];        // "RIFF"
	uint32_t chunkSize;     // Size of the entire file minus 8 bytes
	char format[4];         // "WAVE"
	char subchunk1ID[4];    // "fmt "
	uint32_t subchunk1Size; // Size of the fmt chunk (in bytes)
	uint16_t audioFormat;   // Audio format (1 for PCM)
	uint16_t numChannels;   // Number of channels
	uint32_t sampleRate;    // Sampling rate
	uint32_t byteRate;      // Byte rate = SampleRate * NumChannels * BitsPerSample/8
	uint16_t blockAlign;    // Block align = NumChannels * BitsPerSample/8
	uint16_t bitsPerSample; // Bits per sample
	char subchunk2ID[4];    // "data"
	uint32_t subchunk2Size; // Size of the data chunk (in bytes)
} WAVHeader;
#pragma pack(pop)

typedef enum {
	SPT_FORWARD,
	SPT_REVERSE,
	SPT_FORWARD_PINGPONG,
	SPT_REVERSE_PINGPONG,
	SPT_COUNT
} SamplePlaybackType;

typedef struct {
	float *data;
	char *name;
	int length;
	int sampleRate;
	int bit;
} Sample;

typedef float (*GetSampleFunc)(Sample *sample, float *samplePosition, float phaseIncrement, int loop);

typedef struct {
	/* The arena the samples' data lives in. It is allocated when the
	 * first sample arrives and grown as more arrive, up to
	 * capacityBytes -- it is not allocated up front, because the largest
	 * sample in the tree is far smaller than the ceiling and a device
	 * with one sample in it should not pay for the ceiling. */
	char *sampleData;
	/* How much of the arena is in use. */
	size_t memoryUsed;
	/* How much of the arena is allocated. */
	size_t allocatedBytes;
	/* The ceiling: the most the pool may hold. The device's budget sets
	 * it, because on a small device the pool is the largest single
	 * allocation the engine makes. */
	size_t capacityBytes;
	Sample **samples;
	size_t sampleCount;
	size_t maxSamples;
} SamplePool;

/* Create the pool. The ceiling defaults to MAX_SAMPLE_POOL_BYTES. */
SamplePool *createSamplePool(void);

/* Create the pool with a ceiling: the most it may hold, in bytes. The
 * device's budget sets it. */
SamplePool *createSamplePoolWithCapacity(size_t capacityBytes);

void loadSample(SamplePool *sp, const char *name, float *data, int bit, int sampleSr, int length);
void freeSamplePool(SamplePool *sp);
void freeSample(Sample *sample);
float getSampleValueFwd(Sample *sample, float *samplePosition, float phaseIncrement, int loop);
float getSampleValueRev(Sample *sample, float *samplePosition, float phaseIncrement, int loop);

/* Build the min/max peak envelope of the window [lo, hi) of the sample
 * across `cols` output columns. outMin/outMax must each hold `cols`
 * floats. Pure (no raylib): the sampler waveform node draws these
 * directly and the unit test drives it headlessly. Degenerate inputs
 * (NULL sample/data, non-positive length or cols, lo > hi) are no-ops
 * or clamp to the valid sample range; never divides by a zero span. */
void samplePeakEnvelope(const Sample *sample, float lo, float hi, int cols, float *outMin, float *outMax);

#endif
