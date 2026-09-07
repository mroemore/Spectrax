#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "preset_io.h"

/* V1 had no name field; the body starts at voiceType (offset depends on
 * alignment of the new `name[33]` field, so use offsetof, not a literal). */
#define OLD_PRESET_DATA_OFFSET offsetof(Preset, voiceType)
#define OLD_PRESET_SIZE (sizeof(Preset) - OLD_PRESET_DATA_OFFSET)

/* V2 (IPB2) predated the per-op `pitch` float. The pitch field interleaves
 * inside each OperatorData (it follows outLevel), so the old body cannot be
 * read into the current struct by truncation — every op's fields would
 * misalign. We mirror the old layout exactly and copy field-by-field, so
 * pitch defaults to 0.0 ("no offset"). */
typedef struct {
	float feedbackAmount;
	float ratio;
	float level;
	float outLevel;
} OperatorDataV2;

typedef struct {
	OperatorDataV2 ops[MAX_FM_OPERATORS];
	int selectedAlgorithm;
} FmPatchV2;

typedef struct {
	char name[33];
	VoiceType voiceType;
	ModPreset modSettings[MAX_ENVELOPES + MAX_LFOS];
	int modSettingsCount;
	union {
		SamplerPatch sampler;
		FmPatchV2 fm;
		BlepPatch blep;
	} pd;
} PresetV2;

static int cmpPresetPaths(const void *a, const void *b) {
	const char *const *pa = a;
	const char *const *pb = b;
	return strcmp(*pa, *pb);
}
void loadPresetsFromDirectory(const char *dirPath, PresetBank *pb) {
	DirectoryList *dirList = createDirectoryList();
	populateDirectoryList(dirList, dirPath);

	/* Sort the directory entries so the bank order is deterministic
	 * regardless of readdir order (the load-list UI sorts the same way). */
	if(dirList->count > 1) {
		qsort(dirList->file_paths, dirList->count, sizeof(char *), cmpPresetPaths);
	}

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
	if(!writeChunkHeader(file, PRESET_MAGIC_HEADER_V3)) {
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
	if(readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER_V3)) {
		if(fread(&preset, sizeof(Preset), 1, file) != 1) {
			fclose(file);
			return PRESET_ERROR_READ;
		}
		fclose(file);
		addPresetToBank(pb, preset);
		return PRESET_OK;
	}
	/* V2: same struct minus the per-op `pitch` field. */
	fclose(file);
	file = fopen(filename, "rb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}
	if(!readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER_V2)) {
		/* fall through to V1 handling below */
	} else {
		/* The struct begins immediately after the 4-byte magic. Read the
		 * V2-sized body into a layout-mirror struct (pitch absent). */
		PresetV2 v2;
		memset(&v2, 0, sizeof(v2));
		if(fread(&v2, sizeof(v2), 1, file) != 1) {
			fclose(file);
			return PRESET_ERROR_READ;
		}
		fclose(file);
		/* Copy the common fields, then the FM ops field-by-field (pitch
		 * stays 0). */
		Preset migrated;
		memset(&migrated, 0, sizeof(migrated));
		memcpy(migrated.name, v2.name, sizeof(migrated.name));
		migrated.voiceType = v2.voiceType;
		memcpy(migrated.modSettings, v2.modSettings, sizeof(migrated.modSettings));
		migrated.modSettingsCount = v2.modSettingsCount;
		switch(migrated.voiceType) {
			case VOICE_TYPE_FM:
				migrated.pd.fm.selectedAlgorithm = v2.pd.fm.selectedAlgorithm;
				for(int i = 0; i < MAX_FM_OPERATORS; i++) {
					migrated.pd.fm.ops[i].feedbackAmount = v2.pd.fm.ops[i].feedbackAmount;
					migrated.pd.fm.ops[i].ratio = v2.pd.fm.ops[i].ratio;
					migrated.pd.fm.ops[i].level = v2.pd.fm.ops[i].level;
					migrated.pd.fm.ops[i].outLevel = v2.pd.fm.ops[i].outLevel;
					migrated.pd.fm.ops[i].pitch = 0.0f;
				}
				break;
			case VOICE_TYPE_SAMPLE:
				migrated.pd.sampler = v2.pd.sampler;
				break;
			case VOICE_TYPE_BLEP:
				migrated.pd.blep = v2.pd.blep;
				break;
			default:
				break;
		}
		/* V2 bodies were already name-bearing; just re-add (pitch stays 0). */
		addPresetToBank(pb, migrated);
		return PRESET_OK;
	}
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

/* Check whether a preset with exactly the name `name` is in `pb`. The
 * stored patch slots are 32 bytes wide and zero-padded past the NUL by
 * strncpy, so we compare up to the shorter string's length: short names
 * in the bank stop at their own NUL (so "alpha" doesn't match
 * "alphabet"), and the bank is otherwise untouched. Replaces the old
 * fixed-width strncmp which let "Xm1  " match a stored "Xm1" via the
 * trailing NULs. */
bool presetNameExists(PresetBank *pb, const char *name) {
	for(int i = 0; i < pb->presetCount; i++) {
		if(strcmp(pb->patches[i].name, name) == 0) {
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

/* Task 8: save into a specific bank slot. If the slot is blank (index
 * >= filled count), fill it in place instead of appending -- this is
 * the "edit + name + save into the blank slot you're parked on" path.
 * If the name already exists in the bank, refuse with PRESET_EXISTS so
 * the caller can open the overwrite modal. Otherwise append (same as
 * saveInstrumentAsPreset). */
PresetFileResult saveInstrumentAsPresetToSlot(Instrument *inst, const char *name, const char *dir, int slot) {
	if(!inst || !name || !dir) {
		return PRESET_ERROR_FORMAT;
	}
	if(slot < 0 || slot >= PRESET_BANK_SLOTS) {
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
	if(slot >= inst->presetBank->presetCount) {
		/* Blank slot: fill it. presetCount only advances past the new
		 * highest filled index (a gap is fine for now). */
		inst->presetBank->patches[slot] = p;
		if(slot >= inst->presetBank->presetCount) {
			inst->presetBank->presetCount = slot + 1;
		}
	} else {
		/* Already-filled slot with a new name: append so both remain
		 * addressable (matches saveInstrumentAsPreset). */
		addPresetToBank(inst->presetBank, p);
	}
	return PRESET_OK;
}

/* Task 8: overwrite-confirmation save path. Mirrors saveInstrumentAsPreset
 * but skips the EXISTS guard AND replaces the existing bank slot at the
 * same name instead of appending. On a missing-name slot we still append
 * (consistent with the original behaviour: this function never errors on
 * a duplicate, it just does the right thing either way). */
PresetFileResult saveInstrumentAsPresetOverwrite(Instrument *inst, const char *name, const char *dir) {
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
	/* Write the file first. savePresetFile opens "wb", which truncates
	 * an existing file in-place; no need to unlink() first. */
	PresetFileResult r = savePresetFile(path, &p);
	if(r != PRESET_OK) {
		return r;
	}
	/* Replace the bank slot if one already exists at this name, else
	 * append (matches saveInstrumentAsPreset for the no-prior-entry case). */
	int replaced = 0;
	for(int i = 0; i < inst->presetBank->presetCount; i++) {
		if(strcmp(inst->presetBank->patches[i].name, name) == 0) {
			inst->presetBank->patches[i] = p;
			replaced = 1;
			break;
		}
	}
	if(!replaced) {
		addPresetToBank(inst->presetBank, p);
	}
	return PRESET_OK;
}
