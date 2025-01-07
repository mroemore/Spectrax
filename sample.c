#include "sample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

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

Sample load_wav_sample(const char *filename, FILE *log_file)
{
	Sample sample = {NULL, 0, 0};		// Initialize sample with default values
	FILE *file = fopen(filename, "rb"); // Open the file in binary read mode
	if (!file)
	{
		fprintf(stderr, "Failed to open sample file: %s\n", filename); // Error if file cannot be opened
		return sample;
	}

	WAVHeader header;
	if (fread(&header, sizeof(WAVHeader), 1, file) != 1)
	{
		fprintf(stderr, "Failed to read WAV header\n"); // Error if header cannot be read
		fclose(file);
		return sample;
	}

	if (strncmp(header.chunkID, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0)
	{
		fprintf(stderr, "Invalid or unsupported WAV file format\n"); // Error if file is not a valid WAV
		fclose(file);
		return sample;
	}

	// Log the extracted metadata
	fprintf(log_file, "Loaded WAV file: %s\n", filename);
	fprintf(log_file, "Chunk ID: %.4s\n", header.chunkID);
	fprintf(log_file, "Chunk Size: %u\n", header.chunkSize);
	fprintf(log_file, "Format: %.4s\n", header.format);
	fprintf(log_file, "Subchunk1 ID: %.4s\n", header.subchunk1ID);
	fprintf(log_file, "Subchunk1 Size: %u\n", header.subchunk1Size);
	fprintf(log_file, "Audio Format: %u\n", header.audioFormat);
	fprintf(log_file, "Number of Channels: %u\n", header.numChannels);
	fprintf(log_file, "Sample Rate: %u\n", header.sampleRate);
	fprintf(log_file, "Byte Rate: %u\n", header.byteRate);
	fprintf(log_file, "Block Align: %u\n", header.blockAlign);
	fprintf(log_file, "Bits Per Sample: %u\n", header.bitsPerSample);
	fprintf(log_file, "Subchunk2 ID: %.4s\n", header.subchunk2ID);
	fprintf(log_file, "Subchunk2 Size: %u\n", header.subchunk2Size);

	// Ensure the "data" subchunk is found
	if (strncmp(header.subchunk2ID, "data", 4) != 0)
	{
		fprintf(stderr, "Failed to find data subchunk\n");
		fclose(file);
		return sample;
	}

	sample.sample_rate = header.sampleRate;
	sample.length = header.subchunk2Size / (header.bitsPerSample / 4);
	sample.bit = header.bitsPerSample * header.numChannels;
	sample.data = (float *)malloc(sample.length * sizeof(float));
	if (!sample.data)
	{
		fprintf(stderr, "Failed to allocate memory for sample data\n");
		fclose(file);
		return sample;
	}

	// Read and convert PCM data to float
	if (header.bitsPerSample == 8)
	{
		uint8_t *pcm_data = (uint8_t *)malloc(header.subchunk2Size);
		if (fread(pcm_data, 1, header.subchunk2Size, file) != header.subchunk2Size)
		{
			fprintf(stderr, "Failed to read WAV data\n");
			free(pcm_data);
			fclose(file);
			return sample;
		}
		for (uint32_t i = 0; i < sample.length; i++)
		{
			float value = 0.0f;
			for (uint16_t ch = 0; ch < header.numChannels; ch++)
			{
				value += (pcm_data[i * header.numChannels + ch] - 128) / 128.0f;
			}
			sample.data[i] = value / header.numChannels;
		}
		free(pcm_data);
	}
	else if (header.bitsPerSample == 16)
	{
		int16_t *pcm_data = (int16_t *)malloc(header.subchunk2Size);
		if (fread(pcm_data, sizeof(int16_t), header.subchunk2Size / 2, file) != header.subchunk2Size / 2)
		{
			fprintf(stderr, "Failed to read WAV data\n");
			free(pcm_data);
			fclose(file);
			return sample;
		}
		for (uint32_t i = 0; i < sample.length; i++)
		{
			float value = 0.0f;
			for (uint16_t ch = 0; ch < header.numChannels; ch++)
			{
				value += pcm_data[i * header.numChannels + ch] / 32768.0f;
			}
			sample.data[i] = value / header.numChannels;
		}
		free(pcm_data);
	}
	else
	{
		fprintf(stderr, "Unsupported bit depth: %d\n", header.bitsPerSample);
		fclose(file);
		return sample;
	}

	fclose(file);
	return sample;
}

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
