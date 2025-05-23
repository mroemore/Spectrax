#include <stdlib.h>
#include <string.h>
#include "io.h"
#include "modsystem.h"

static int writeChunkHeader(FILE *file, const char *id) {
	return fwrite(id, 1, 4, file) == 4;
}

static int readAndVerifyChunkHeader(FILE *file, const char *expected) {
	char header[4];
	if(fread(header, 1, 4, file) != 4) return 0;
	return memcmp(header, expected, 4) == 0;
}

DirectoryList *createDirectoryList() {
	DirectoryList *list = (DirectoryList *)malloc(sizeof(DirectoryList));
	list->file_paths = NULL;
	list->count = 0;
	return list;
}

void freeDirectoryList(DirectoryList *list) {
	for(size_t i = 0; i < list->count; i++) {
		free(list->file_paths[i]);
	}
	free(list->file_paths);
	list->file_paths = NULL;
	list->count = 0;
}

void populateDirectoryList(DirectoryList *list, const char *dirPath) {
#ifdef _WIN32
	WIN32_FIND_DATA findFileData;
	HANDLE hFind;
	char searchPath[MAX_PATH];
	snprintf(searchPath, MAX_PATH, "%s\\*", dirPath);

	hFind = FindFirstFile(searchPath, &findFileData);
	if(hFind == INVALID_HANDLE_VALUE) {
		perror("Failed to open directory");
		return;
	}

	do {
		if(strcmp(findFileData.cFileName, ".") == 0 || strcmp(findFileData.cFileName, "..") == 0) {
			continue; // Skip current and parent directory entries
		}

		// Allocate memory for the file path
		char *filepath = (char *)malloc(MAX_PATH);
		if(!filepath) {
			perror("Failed to allocate memory");
			FindClose(hFind);
			return;
		}

		// Construct the full file path
		snprintf(filepath, MAX_PATH, "%s\\%s", dirPath, findFileData.cFileName);
		printf("PATH:\n");
		printf("%s\n", filepath);
		// Add the file path to the list
		list->file_paths = (char **)realloc(list->file_paths, (list->count + 1) * sizeof(char *));
		if(!list->file_paths) {
			perror("Failed to reallocate memory");
			free(filepath);
			FindClose(hFind);
			return;
		}

		list->file_paths[list->count] = filepath;
		list->count++;
	} while(FindNextFile(hFind, &findFileData) != 0);

	FindClose(hFind);
#else
	// Unix-like directory traversal
	DIR *dir = opendir(dirPath);
	if(!dir) {
		perror("Failed to open directory");
		return;
	}

	struct dirent *entry;
	while((entry = readdir(dir)) != NULL) {
		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue; // Skip current and parent directory entries
		}

		// Allocate memory for the file path
		char *filepath = (char *)malloc(1024);
		if(!filepath) {
			perror("Failed to allocate memory");
			closedir(dir);
			return;
		}

		// Construct the full file path
		snprintf(filepath, 1024, "%s/%s", dirPath, entry->d_name);

		// Add the file path to the list
		list->file_paths = (char **)realloc(list->file_paths, (list->count + 1) * sizeof(char *));
		if(!list->file_paths) {
			perror("Failed to reallocate memory");
			free(filepath);
			closedir(dir);
			return;
		}

		list->file_paths[list->count] = filepath;
		list->count++;
	}

	closedir(dir);
#endif
}

void loadPresetsFromDirectory(const char *dirPath, PresetBank *pb) {
	DirectoryList *dirList = createDirectoryList();
	populateDirectoryList(dirList, dirPath);

	for(int i = 0; i < dirList->count; i++) {
		loadPresetFile(dirList->file_paths[i], pb);
	}

	freeDirectoryList(dirList);
}

PresetFileResult savePresetFile(const char *filename, Preset *preset) {
	FILE *file = fopen(filename, "wb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}

	if(!writeChunkHeader(file, PRESET_MAGIC_HEADER)) {
		fclose(file);
		return PRESET_ERROR_WRITE;
	}
	fwrite(preset, sizeof(Preset), 1, file);
	fclose(file);
	return PRESET_OK;
}

PresetFileResult loadPresetFile(const char *filename, PresetBank *pb) {
	Preset preset;
	FILE *file = fopen(filename, "rb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}

	if(!readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER)) {
		fclose(file);
		return PRESET_ERROR_FORMAT;
	}

	if(fread(&preset, sizeof(Preset), 1, file) != 1) {
		fclose(file);
		return PRESET_ERROR_READ;
	}
	fclose(file);

	addPresetToBank(pb, preset);
	return PRESET_OK;
}

void loadSamplesfromDirectory(const char *path, SamplePool *sp) {
	DirectoryList *dirList = createDirectoryList();
	populateDirectoryList(dirList, "resources/samples/");

	for(int i = 0; i < dirList->count; i++) {
		load_wav_sample(dirList->file_paths[i], sp);
	}

	freeDirectoryList(dirList);
}

void load_wav_sample(const char *filename, SamplePool *sp) {
	FILE *file = fopen(filename, "rb"); // Open the file in binary read mode
	if(!file) {
		printf("Failed to open sample file: %s\n", filename); // Error if file cannot be opened
		return;
	}

	WAVHeader header;
	if(fread(&header, sizeof(WAVHeader), 1, file) != 1) {
		printf("Failed to read WAV header\n"); // Error if header cannot be read
		fclose(file);
		return;
	}

	if(strncmp(header.chunkID, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0) {
		printf("Invalid or unsupported WAV file format\n"); // Error if file is not a valid WAV
		fclose(file);
		return;
	}
	// Ensure the "data" subchunk is found
	if(strncmp(header.subchunk2ID, "data", 4) != 0) {
		printf("Failed to find data subchunk\n");
		fclose(file);
		return;
	}

	// sample.sampleRate = header.sampleRate;
	// sample.length = header.subchunk2Size / (header.bitsPerSample / 4);
	// sample.bit = header.bitsPerSample * header.numChannels;
	// sample.data = (float *)malloc(sample.length * sizeof(float));
	// if (!sample.data)
	// {
	// 	printf("Failed to allocate memory for sample data\n");
	// 	fclose(file);
	// 	return sample;
	// }

	// Read and convert PCM data to float
	int bit = header.bitsPerSample * header.numChannels;
	int length = header.subchunk2Size / (header.bitsPerSample / 4);
	float *data = (float *)malloc(sizeof(float) * length);
	if(header.bitsPerSample == 8) {
		uint8_t *pcm_data = (uint8_t *)malloc(header.subchunk2Size);
		if(fread(pcm_data, 1, header.subchunk2Size, file) != header.subchunk2Size) {
			printf("Failed to read WAV data\n");
			free(pcm_data);
			free(data);
			fclose(file);
			return;
		}
		for(uint32_t i = 0; i < length; i++) {
			float value = 0.0f;
			for(uint16_t ch = 0; ch < header.numChannels; ch++) {
				value += (pcm_data[i * header.numChannels + ch] - 128) / 128.0f;
			}
			data[i] = value / header.numChannels;
		}
		printf("Copied data (first 10 samples):\n");
		for(int i = 0; i < 10; i++) {
			printf("%f ", data[i]);
		}
		loadSample(sp, filename, data, header.bitsPerSample * header.numChannels, header.sampleRate, length);
		free(pcm_data);
		// free(data);
	} else if(header.bitsPerSample == 16) {
		int16_t *pcm_data = (int16_t *)malloc(header.subchunk2Size);
		if(fread(pcm_data, sizeof(int16_t), header.subchunk2Size / 2, file) != header.subchunk2Size / 2) {
			printf("Failed to read WAV data\n");
			free(pcm_data);
			free(data);
			fclose(file);
			return;
		}
		for(uint32_t i = 0; i < length; i++) {
			float value = 0.0f;
			for(uint16_t ch = 0; ch < header.numChannels; ch++) {
				value += pcm_data[i * header.numChannels + ch] / 32768.0f;
			}
			data[i] = value / header.numChannels;
		}
		printf("Copied data (first 10 samples):\n");
		for(int i = 0; i < 10; i++) {
			printf("%f ", data[i]);
		}
		loadSample(sp, filename, data, header.bitsPerSample * header.numChannels, header.sampleRate, length);
		free(pcm_data);
		// free(data);
	} else {
		printf("Unsupported bit depth: %d\n", header.bitsPerSample);
		fclose(file);
		return;
	}

	fclose(file);
}

FileResult saveSettings(const char *filename, Settings *settings) {
	FILE *file = fopen(filename, "wb");
	if(!file) return FILE_ERROR_OPEN;

	const char magic[] = "SET1";
	fwrite(magic, 1, 4, file);
	fwrite(settings, sizeof(Settings), 1, file);

	fclose(file);
	return FILE_OK;
}

FileResult loadSettings(const char *filename, Settings *settings) {
	FILE *file = fopen(filename, "rb");
	if(!file) return FILE_ERROR_OPEN;

	char magic[4];
	if(fread(magic, 1, 4, file) != 4 || memcmp(magic, "SET1", 4) != 0) {
		fclose(file);
		return FILE_ERROR_FORMAT;
	}

	if(fread(settings, sizeof(Settings), 1, file) != 1) {
		fclose(file);
		return FILE_ERROR_READ;
	}

	fclose(file);
	return FILE_OK;
}

FileResult saveColourScheme(const char *filename, ColourScheme *colourScheme) {
	FILE *file = fopen(filename, "wb");
	if(!file) return FILE_ERROR_OPEN;

	const char magic[] = "CSC1";
	fwrite(magic, 1, 4, file);
	fwrite(colourScheme, sizeof(ColourScheme), 1, file);

	fclose(file);
	return FILE_OK;
}

FileResult loadColourScheme(const char *filename, ColourScheme *colourScheme) {
	FILE *file = fopen(filename, "rb");
	if(!file) return FILE_ERROR_OPEN;

	char magic[4];
	if(fread(magic, 1, 4, file) != 4 || memcmp(magic, "CSC1", 4) != 0) {
		fclose(file);
		return FILE_ERROR_FORMAT;
	}

	if(fread(colourScheme, sizeof(ColourScheme), 1, file) != 1) {
		fclose(file);
		return FILE_ERROR_READ;
	}

	fclose(file);
	return FILE_OK;
}

FileResult loadColourSchemeTxt(const char *filename, Color *colourArray[], int arraySize) {
	FILE *file = fopen(filename, "rb");
	if(!file) return FILE_ERROR_OPEN;

	char line[32];
	for(int i = 0; i < arraySize; i++) {
		if(!fgets(line, sizeof(line), file)) {
			fclose(file);
			return FILE_ERROR_READ;
		}

		int r, g, b;
		if(sscanf(line, "%d,%d,%d", &r, &g, &b) != 3 ||
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

SequencerFileResult saveSequencerState(const char *filename, Arranger *arranger, PatternList *patterns) {
	FILE *file = fopen(filename, "wb");
	if(!file) return SEQ_ERROR_OPEN;

	// Write file header
	if(!writeChunkHeader(file, SEQ_MAGIC_HEADER)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write patterns section
	if(!writeChunkHeader(file, PATTERN_SECTION)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write pattern count and patterns
	fwrite(&patterns->pattern_count, sizeof(int), 1, file);
	for(int i = 0; i < patterns->pattern_count; i++) {
		Pattern *p = &patterns->patterns[i];
		fwrite(&p->pattern_size, sizeof(int), 1, file);
		fwrite(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file);
	}

	// Write arranger section
	if(!writeChunkHeader(file, ARRANGER_SECTION)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write complete arranger struct
	fwrite(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
	fwrite(&arranger->enabledChannels, sizeof(int), 1, file);
	fwrite(&arranger->selected_x, sizeof(int), 1, file);
	fwrite(&arranger->selected_y, sizeof(int), 1, file);
	fwrite(&arranger->tempoSettings.loop, sizeof(int), 1, file);
	int bpm = getParameterValueAsInt(arranger->tempoSettings.bpm);
	fwrite(&bpm, sizeof(int), 1, file);
	fwrite(&arranger->playing, sizeof(int), 1, file);
	// fwrite(arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
	fwrite(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file);

	fclose(file);
	return SEQ_OK;
}

SequencerFileResult loadSequencerState(const char *filename, Arranger *arranger, PatternList *patterns) {
	FILE *file = fopen(filename, "rb");
	if(!file) return SEQ_ERROR_OPEN;

	if(!readAndVerifyChunkHeader(file, SEQ_MAGIC_HEADER)) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	if(!readAndVerifyChunkHeader(file, PATTERN_SECTION)) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	// Read patterns
	if(fread(&patterns->pattern_count, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error reading pattern count\n");
		return SEQ_ERROR_READ;
	}

	if(patterns->pattern_count > MAX_PATTERNS) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	for(int i = 0; i < patterns->pattern_count; i++) {
		Pattern *p = &patterns->patterns[i];
		if(fread(&p->pattern_size, sizeof(int), 1, file) != 1 ||
		   fread(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file) != MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE) {
			fclose(file);
			return SEQ_ERROR_READ;
			printf("error reading pattern data\n");
		}
	}

	// Read arranger section
	if(!readAndVerifyChunkHeader(file, ARRANGER_SECTION)) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	if(fread(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS) {
		fclose(file);
		printf("error playhread\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->enabledChannels, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error enabledchans\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->selected_x, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error selx\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->selected_y, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error sely\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->tempoSettings.loop, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error loop\n");
		return SEQ_ERROR_READ;
	}
	int bpm;
	if(fread(&bpm, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error bpm\n");
		return SEQ_ERROR_READ;
	}
	setParameterBaseValue(arranger->tempoSettings.bpm, bpm);

	if(fread(&arranger->playing, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error ply\n");
		return SEQ_ERROR_READ;
	}
	// if(fread(arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS) {
	// 	fclose(file);
	// 	printf("error voicetypes\n");
	// 	return SEQ_ERROR_READ;
	// }
	if(fread(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file) != MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH) {
		fclose(file);
		printf("error reading arranger data.\n");

		return SEQ_ERROR_READ;
	}

	fclose(file);
	return SEQ_OK;
}
