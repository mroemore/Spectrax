#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "preset_io.h"

#define OLD_PRESET_SIZE (sizeof(Preset) - 33) /* V1 had no name field */

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
	if(!writeChunkHeader(file, PRESET_MAGIC_HEADER_V2)) {
		fclose(file);
		return PRESET_ERROR_WRITE;
	}
	if(fwrite(preset, sizeof(Preset), 1, file) != 1) {
		fclose(file);
		return PRESET_ERROR_WRITE;
	}
	fclose(file);
	return PRESET_OK;
}

PresetFileResult loadPresetFile(const char *filename, PresetBank *pb) {
	Preset preset;
	memset(&preset, 0, sizeof(preset));
	FILE *file = fopen(filename, "rb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}
	if(readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER_V2)) {
		if(fread(&preset, sizeof(Preset), 1, file) != 1) {
			fclose(file);
			return PRESET_ERROR_READ;
		}
		fclose(file);
		addPresetToBank(pb, preset);
		return PRESET_OK;
	}
	/* V1: old magic, struct without the name field */
	fclose(file);
	file = fopen(filename, "rb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}
	if(!readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER)) {
		fclose(file);
		return PRESET_ERROR_FORMAT;
	}
	if(fread(((char *)&preset) + 33, OLD_PRESET_SIZE, 1, file) != 1) {
		fclose(file);
		return PRESET_ERROR_READ;
	}
	fclose(file);
	/* derive the name from a copy of the filename (don't mutate the
	 * caller's string), then auto-migrate to V2 in place */
	char fname_copy[512];
	strncpy(fname_copy, filename, sizeof(fname_copy) - 1);
	fname_copy[sizeof(fname_copy) - 1] = '\0';
	const char *base = strrchr(fname_copy, '/');
	base = base ? base + 1 : fname_copy;
	char *dot = strrchr(base, '.');
	if(dot) {
		*dot = '\0';
	}
	strncpy(preset.name, base, sizeof(preset.name) - 1);
	preset.name[sizeof(preset.name) - 1] = '\0';
	savePresetFile(filename, &preset);
	addPresetToBank(pb, preset);
	return PRESET_OK;
}
