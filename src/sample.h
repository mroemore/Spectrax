#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdint.h>


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
    int length;
    int sample_rate;
    int bit;
} Sample;


void free_sample(Sample *sample);
float get_sample_value(Sample *sample, float *sample_position, float phase_increment, int pa_sr, int loop);
// float get_sample_value(Sample *sample, float *sample_position, int pa_sr, float phase);

#endif // SAMPLE_H