#ifndef SPECTRAX_DEVICE_BUDGET_H
#define SPECTRAX_DEVICE_BUDGET_H

#include <stdbool.h>
#include <stddef.h>

/* The device's budget: how much the engine may do.
 *
 * These are *budgets*, not quality tiers. Every one of them is
 * output-neutral: a voice's samples are a pure function of its settings
 * (criterion C), so the only thing a smaller budget can do is refuse
 * work -- allocate fewer voices, leave channels idle, allow fewer voice
 * types, cap the sample pool. Nothing here may change how a voice
 * sounds.
 *
 * The numbers come from a measurement of *this* device, taken by the app
 * itself on its first run and stored beside the user's settings. The
 * device's static facts (the CPU, the RAM, the display) live in
 * devices/<id>.toml; this is the measured half.
 */

/* The callback buffer sizes the app may choose from, in frames. The
 * buffer size is a latency knob, not a parameter of the sound: the
 * engine's control rate is scheduled in samples, so all three produce
 * identical output. */
#define DEVICE_BUDGET_BUFFER_MIN 128
#define DEVICE_BUDGET_BUFFER_MID 256
#define DEVICE_BUDGET_BUFFER_MAX 512

/* The voice types the app may offer, as a bitmask over VoiceType. */
#define DEVICE_BUDGET_VOICE_SAMPLE (1 << 0)
#define DEVICE_BUDGET_VOICE_FM (1 << 1)
#define DEVICE_BUDGET_VOICE_BLEP (1 << 2)
#define DEVICE_BUDGET_VOICE_GRAIN (1 << 3)
#define DEVICE_BUDGET_VOICE_SPECTRAL (1 << 4)

typedef struct {
	/* How many voices a channel may allocate. */
	int voicesPerChannel;
	/* How many channels may be active at once. */
	int enabledChannels;
	/* The callback buffer size, in frames. */
	int bufferFrames;
	/* The permitted voice types, as a bitmask over VoiceType. */
	int voiceTypes;
	/* The sample pool's ceiling, in bytes. */
	long samplePoolBytes;
	/* The measured cost of one voice's render, in nanoseconds per
	 * sample. Zero when the device has not been measured. */
	long nanosPerVoiceSample;
} DeviceBudget;

/* The defaults, for a device that has not been measured: the values the
 * desktop uses. */
void deviceBudgetDefaults(DeviceBudget *out);

/* The voice types this device may offer.
 *
 * This is the engine's copy of the budget's mask, so the engine can
 * refuse a voice type the device does not permit even when the request
 * does not come from the GUI (a preset that names one, a script, a
 * saved session). The GUI greys the others out; this is what makes the
 * refusal real.
 *
 * It is a module-level setting rather than a parameter of every call,
 * because it is a property of the device, set once at startup. */
void deviceBudgetSetPermittedVoiceTypes(int mask);
int deviceBudgetPermittedVoiceTypes(void);

/* The budget's bit for a voice type. The engine's VoiceType enum and the
 * budget's mask are separate enumerations, so the mapping lives in one
 * place rather than being assumed to match. */
int deviceBudgetMaskForVoiceType(int voiceType);

/* Derive a budget from a measurement.
 *
 * `nanosPerVoiceSample` is the measured cost of one voice's synthesis
 * per output sample; `deadlineNanos` is the callback's budget (the
 * buffer's duration in nanoseconds); `voices` is how many voices would
 * fit in that budget at the given per-voice cost.
 *
 * `headroom` is the fraction of the deadline the budget may use (0.75
 * leaves a quarter for the mixing, the visualisation and the PortAudio
 * overhead).
 *
 * `ramBytes` is the device's available memory, and the sample pool's
 * ceiling is a fraction of it.
 *
 * The result is clamped into ranges the engine can actually honour. */
void deviceBudgetDerive(DeviceBudget *out, long nanosPerVoiceSample, int deadlineNanos,
                         int headroom, long ramBytes, int measuredVoices);

/* The profile's path inside the user's config directory. */
void deviceBudgetProfilePath(char *out, size_t outSize, const char *configDir);

/* Read the profile. Returns false when there is none (the device has not
 * been measured) or when it cannot be parsed; `out` is then left with the
 * defaults. */
bool deviceBudgetLoad(const char *configDir, DeviceBudget *out);

/* Write the profile. Returns false when it cannot be written (a
 * read-only profile directory is a normal outcome on some handhelds, and
 * the app carries on with the defaults). */
bool deviceBudgetStore(const char *configDir, const DeviceBudget *budget);

#endif
