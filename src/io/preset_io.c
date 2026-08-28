#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "preset_io.h"

/* V1 had no name field; the body starts at voiceType (offset depends on
 * alignment of the new `name[33]` field, so use offsetof, not a literal). */
#define OLD_PRESET_DATA_OFFSET offsetof(Preset, voiceType)
#define OLD_PRESET_SIZE (sizeof(Preset) - OLD_PRESET_DATA_OFFSET)

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
	if(fread(((char *)&preset) + OLD_PRESET_DATA_OFFSET, OLD_PRESET_SIZE, 1, file) != 1) {
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

/* Task 5: build a safe ".ipb" filename from a user-supplied preset name.
 * Replaces characters that would break paths or shell scripts (space,
 * forward/backslash, colon, star) with '_'. Always appends ".ipb". */
void sanitizePresetFilename(const char *name, char *out, size_t outSize) {
	size_t i = 0;
	for(const char *c = name; *c && i + 5 < outSize; c++) {
		char ch = *c;
		if(ch == ' ' || ch == '/' || ch == '\\' || ch == ':' || ch == '*') {
			ch = '_';
		}
		out[i++] = ch;
	}
	strcpy(out + i, ".ipb");
}

/* Task 5: scan the in-memory PresetBank for a preset whose 32-byte
 * name slot matches `name`. Fixed-width compare (strncmp) is intentional:
 * short names leave trailing NULs in patches[].name[], and we don't want
 * "alpha" to match "alphabet". */
bool presetNameExists(PresetBank *pb, const char *name) {
	for(int i = 0; i < pb->presetCount; i++) {
		if(strncmp(pb->patches[i].name, name, 32) == 0) {
			return true;
		}
	}
	return false;
}

/* Task 5: end-to-end save path for a live Instrument. Pulls the data
 * with presetFromInstrument, fills p.name, refuses to overwrite an
 * existing preset (returns PRESET_EXISTS), then writes the file and
 * records the preset in the bank's in-memory list. */
PresetFileResult saveInstrumentAsPreset(Instrument *inst, const char *name, const char *dir) {
	if(!inst || !name || !dir) {
		return PRESET_ERROR_FORMAT;
	}
	char clean[48];
	sanitizePresetFilename(name, clean, sizeof(clean));
	char path[512];
	snprintf(path, sizeof(path), "%s%s", dir, clean);
	Preset p = presetFromInstrument(inst);
	strncpy(p.name, name, sizeof(p.name) - 1);
	p.name[sizeof(p.name) - 1] = '\0';
	if(presetNameExists(inst->presetBank, name)) {
		return PRESET_EXISTS;
	}
	PresetFileResult r = savePresetFile(path, &p);
	if(r != PRESET_OK) {
		return r;
	}
	addPresetToBank(inst->presetBank, p);
	return PRESET_OK;
}
