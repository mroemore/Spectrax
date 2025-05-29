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
