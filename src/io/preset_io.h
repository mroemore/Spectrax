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

/* Task 8: save into a specific bank slot. If `slot` is blank (>= the
 * bank's filled count) the new preset fills it; if the name already
 * exists it returns PRESET_EXISTS (caller opens the overwrite modal);
 * otherwise it appends (same as saveInstrumentAsPreset). Used so the
 * user can save a fresh preset into the blank slot they've parked on. */
PresetFileResult saveInstrumentAsPresetToSlot(Instrument *inst, const char *name, const char *dir, int slot);

/* Task 8: overwrite-confirmation path. Like saveInstrumentAsPreset, but:
 *   - skips the in-bank dedup check (returns PRESET_OK even if a preset
 *     with `name` already exists);
 *   - replaces the existing bank slot at the same name instead of
 *     appending, so the bank doesn't grow on overwrite;
 *   - still writes the file in-place (savePresetFile opens with "wb",
 *     which truncates by default).
 * The caller (the overwrite modal) is responsible for confirming with
 * the user before invoking this. */
PresetFileResult saveInstrumentAsPresetOverwrite(Instrument *inst, const char *name, const char *dir);

#endif
