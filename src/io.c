#include <stdlib.h>
#include <string.h>
#include "io.h"

Sample load_wav_sample(const char *filename){
	Sample sample = {NULL, 0, 0};		// Initialize sample with default values
	FILE *file = fopen(filename, "rb"); // Open the file in binary read mode
	if (!file)
	{
		printf("Failed to open sample file: %s\n", filename); // Error if file cannot be opened
		return sample;
	}

	WAVHeader header;
	if (fread(&header, sizeof(WAVHeader), 1, file) != 1)
	{
		printf("Failed to read WAV header\n"); // Error if header cannot be read
		fclose(file);
		return sample;
	}

	if (strncmp(header.chunkID, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0)
	{
		printf("Invalid or unsupported WAV file format\n"); // Error if file is not a valid WAV
		fclose(file);
		return sample;
	}

	// Log the extracted metadata
	// fprintf(log_file, "Loaded WAV file: %s\n", filename);
	// fprintf(log_file, "Chunk ID: %.4s\n", header.chunkID);
	// fprintf(log_file, "Chunk Size: %u\n", header.chunkSize);
	// fprintf(log_file, "Format: %.4s\n", header.format);
	// fprintf(log_file, "Subchunk1 ID: %.4s\n", header.subchunk1ID);
	// fprintf(log_file, "Subchunk1 Size: %u\n", header.subchunk1Size);
	// fprintf(log_file, "Audio Format: %u\n", header.audioFormat);
	// fprintf(log_file, "Number of Channels: %u\n", header.numChannels);
	// fprintf(log_file, "Sample Rate: %u\n", header.sampleRate);
	// fprintf(log_file, "Byte Rate: %u\n", header.byteRate);
	// fprintf(log_file, "Block Align: %u\n", header.blockAlign);
	// fprintf(log_file, "Bits Per Sample: %u\n", header.bitsPerSample);
	// fprintf(log_file, "Subchunk2 ID: %.4s\n", header.subchunk2ID);
	// fprintf(log_file, "Subchunk2 Size: %u\n", header.subchunk2Size);

	// Ensure the "data" subchunk is found
	if (strncmp(header.subchunk2ID, "data", 4) != 0)
	{
		printf("Failed to find data subchunk\n");
		fclose(file);
		return sample;
	}

	sample.sample_rate = header.sampleRate;
	sample.length = header.subchunk2Size / (header.bitsPerSample / 4);
	sample.bit = header.bitsPerSample * header.numChannels;
	sample.data = (float *)malloc(sample.length * sizeof(float));
	if (!sample.data)
	{
		printf("Failed to allocate memory for sample data\n");
		fclose(file);
		return sample;
	}

	// Read and convert PCM data to float
	if (header.bitsPerSample == 8)
	{
		uint8_t *pcm_data = (uint8_t *)malloc(header.subchunk2Size);
		if (fread(pcm_data, 1, header.subchunk2Size, file) != header.subchunk2Size)
		{
			printf("Failed to read WAV data\n");
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
			printf("Failed to read WAV data\n");
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
		printf("Unsupported bit depth: %d\n", header.bitsPerSample);
		fclose(file);
		return sample;
	}

	fclose(file);
	return sample;
}

FileResult saveSettings(const char* filename, Settings* settings) {
    FILE* file = fopen(filename, "wb");
    if (!file) return FILE_ERROR_OPEN;
    
    const char magic[] = "SET1";
    fwrite(magic, 1, 4, file);
    fwrite(settings, sizeof(Settings), 1, file);
    
    fclose(file);
    return FILE_OK;
}

FileResult loadSettings(const char* filename, Settings* settings) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FILE_ERROR_OPEN;
    
    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "SET1", 4) != 0) {
        fclose(file);
        return FILE_ERROR_FORMAT;
    }
    
    if (fread(settings, sizeof(Settings), 1, file) != 1) {
        fclose(file);
        return FILE_ERROR_READ;
    }
    
    fclose(file);
    return FILE_OK;
}

FileResult saveColourScheme(const char* filename, ColourScheme* colourScheme) {
    FILE* file = fopen(filename, "wb");
    if (!file) return FILE_ERROR_OPEN;
    
    const char magic[] = "CSC1";
    fwrite(magic, 1, 4, file);
    fwrite(colourScheme, sizeof(ColourScheme), 1, file);
    
    fclose(file);
    return FILE_OK;
}

FileResult loadColourScheme(const char* filename, ColourScheme* colourScheme) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FILE_ERROR_OPEN;
    
    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "CSC1", 4) != 0) {
        fclose(file);
        return FILE_ERROR_FORMAT;
    }
    
    if (fread(colourScheme, sizeof(ColourScheme), 1, file) != 1) {
        fclose(file);
        return FILE_ERROR_READ;
    }
    
    fclose(file);
    return FILE_OK;
}

FileResult loadColourSchemeTxt(const char* filename, Color* colourArray[], int arraySize) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FILE_ERROR_OPEN;
    
    char line[32];
    for (int i = 0; i < arraySize; i++) {
        if (!fgets(line, sizeof(line), file)) {
            fclose(file);
            return FILE_ERROR_READ;
        }
        
        int r, g, b;
        if (sscanf(line, "%d,%d,%d", &r, &g, &b) != 3 ||
            r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            fclose(file);
            return FILE_ERROR_FORMAT;
        }
        
        colourArray[i]->r = r;
        colourArray[i]->g = g;
        colourArray[i]->b = b;
    }
    
    fclose(file);
    return FILE_OK;
}

static int writeChunkHeader(FILE* file, const char* id) {
    return fwrite(id, 1, 4, file) == 4;
}

static int readAndVerifyChunkHeader(FILE* file, const char* expected) {
    char header[4];
    if (fread(header, 1, 4, file) != 4) return 0;
    return memcmp(header, expected, 4) == 0;
}
SequencerFileResult saveSequencerState(const char* filename, Arranger* arranger, PatternList* patterns) {
    FILE* file = fopen(filename, "wb");
    if (!file) return SEQ_ERROR_OPEN;

    // Write file header
    if (!writeChunkHeader(file, MAGIC_HEADER)) {
        fclose(file);
        return SEQ_ERROR_WRITE;
    }

    // Write patterns section
    if (!writeChunkHeader(file, PATTERN_SECTION)) {
        fclose(file);
        return SEQ_ERROR_WRITE;
    }

    // Write pattern count and patterns
    fwrite(&patterns->pattern_count, sizeof(int), 1, file);
    for (int i = 0; i < patterns->pattern_count; i++) {
        Pattern* p = &patterns->patterns[i];
        fwrite(&p->pattern_size, sizeof(int), 1, file);
        fwrite(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file);
    }

    // Write arranger section
    if (!writeChunkHeader(file, ARRANGER_SECTION)) {
        fclose(file);
        return SEQ_ERROR_WRITE;
    }

    // Write complete arranger struct
    fwrite(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
    fwrite(&arranger->enabledChannels, sizeof(int), 1, file);
    fwrite(&arranger->selected_x, sizeof(int), 1, file);
    fwrite(&arranger->selected_y, sizeof(int), 1, file);
    fwrite(&arranger->loop, sizeof(int), 1, file);
    fwrite(&arranger->beats_per_minute, sizeof(int), 1, file);
    fwrite(&arranger->playing, sizeof(int), 1, file);
    fwrite(&arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
    fwrite(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file);

    fclose(file);
    return SEQ_OK;
}

SequencerFileResult loadSequencerState(const char* filename, Arranger* arranger, PatternList* patterns) {
    FILE* file = fopen(filename, "rb");
    if (!file) return SEQ_ERROR_OPEN;

    if (!readAndVerifyChunkHeader(file, MAGIC_HEADER)) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    if (!readAndVerifyChunkHeader(file, PATTERN_SECTION)) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    // Read patterns
    if (fread(&patterns->pattern_count, sizeof(int), 1, file) != 1) {
        fclose(file);
        return SEQ_ERROR_READ;
    }

    if (patterns->pattern_count > MAX_PATTERNS) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    for (int i = 0; i < patterns->pattern_count; i++) {
        Pattern* p = &patterns->patterns[i];
        if (fread(&p->pattern_size, sizeof(int), 1, file) != 1 ||
            fread(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file) 
                != MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE) {
            fclose(file);
            return SEQ_ERROR_READ;
        }
    }

    // Read arranger section
    if (!readAndVerifyChunkHeader(file, ARRANGER_SECTION)) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    if (fread(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS ||
        fread(&arranger->enabledChannels, sizeof(int), 1, file) != 1 ||
        fread(&arranger->selected_x, sizeof(int), 1, file) != 1 ||
        fread(&arranger->selected_y, sizeof(int), 1, file) != 1 ||
        fread(&arranger->loop, sizeof(int), 1, file) != 1 ||
        fread(&arranger->beats_per_minute, sizeof(int), 1, file) != 1 ||
        fread(&arranger->playing, sizeof(int), 1, file) != 1 ||
        fread(&arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != 1 ||
        fread(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file) 
            != MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH) {
        fclose(file);
        return SEQ_ERROR_READ;
    }

    fclose(file);
    return SEQ_OK;
}