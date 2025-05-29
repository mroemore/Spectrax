#include "reset_io.h"

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
