#ifndef PRESETIO_H
#define PRESETIO_H

#include "../io.h"
#include "../voice.h"

void loadPresetsFromDirectory(const char *dirPath, PresetBank *pb);
PresetFileResult savePresetFile(const char *filename, Preset *preset);
PresetFileResult loadPresetFile(const char *filename, PresetBank *pb);

/* Task 5: user-facing save path. */
void sanitizePresetFilename(const char *name, char *out, size_t outSize);
bool presetNameExists(PresetBank *pb, const char *name);
PresetFileResult saveInstrumentAsPreset(Instrument *inst, const char *name, const char *dir);

#endif
