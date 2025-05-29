#ifndef PRESETIO_H
#define PRESETIO_H

#include "../io.h"
#include "../voice.h"

void loadPresetsFromDirectory(const char *dirPath, PresetBank *pb);
PresetFileResult savePresetFile(const char *filename, Preset *preset);
PresetFileResult loadPresetFile(const char *filename, PresetBank *pb);

#endif
