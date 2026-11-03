#ifndef SPECTRAX_DEVICE_CALIBRATE_H
#define SPECTRAX_DEVICE_CALIBRATE_H

#include <stdbool.h>

#include "device_budget.h"
#include "voice.h"

/* The device's calibration: measuring what this machine can do, and turning
 * that into the budget it gets.
 *
 * The measurement renders one voice through the same path the audio
 * callback uses and times it. It uses a *scratch* voice -- one allocated for
 * the measurement and freed afterwards -- so the app's own voice pool is
 * exactly as it was: no voice left active, no note left triggered.
 *
 * The calibration is a first-run thing: the measurement is written to the
 * profile, and later starts read it instead of measuring again. */

/* Measure the cost of one voice's synthesis, in nanoseconds per output
 * sample. Returns 0 when the measurement could not be taken. */
long deviceCalibrateMeasureVoiceCost(VoiceManager *vm, int channel);

/* The deadline the measurement is judged against: the callback's buffer
 * duration at the engine's rate. */
int deviceCalibrateDeadlineNanos(void);

/* Calibrate the device.
 *
 * Loads the stored profile; if the device has not been measured, measures
 * it, derives the budget and writes the profile. `out` always receives the
 * budget in force -- the stored one, or the freshly measured one.
 *
 * Returns true when the budget in `out` came from a measurement (this run
 * or an earlier one), false when the device could not be measured and the
 * defaults are in force. */
bool deviceCalibrate(const char *configDir, VoiceManager *vm, int channel, DeviceBudget *out);

#endif
