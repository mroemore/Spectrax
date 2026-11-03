#include "device_calibrate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The fraction of the deadline the budget may use. The rest covers the
 * mixing, the visualisation and PortAudio's own overhead. */
#define DEVICE_CALIBRATE_HEADROOM 75

/* The measurement renders this many frames. It is long enough that the
 * per-frame overhead (the clock calls) is negligible, and short enough that
 * the first run is not noticeable. */
#define DEVICE_CALIBRATE_FRAMES 8192

/* The available memory, in bytes. The sample pool's ceiling is a fraction
 * of it. Zero when the platform does not say. */
static long availableRamBytes(void) {
#if defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
	long pages = sysconf(_SC_AVPHYS_PAGES);
	long pageSize = sysconf(_SC_PAGESIZE);
	if(pages <= 0 || pageSize <= 0) {
		return 0;
	}
	return pages * pageSize;
#else
	return 0;
#endif
}

int deviceCalibrateDeadlineNanos(void) {
	/* The callback's buffer duration at the engine's rate: 256 frames at
	 * 48000 is 5.33 ms. */
	return (int)(1000000000L * 256L / SPECTRAX_ENGINE_RATE);
}

long deviceCalibrateMeasureVoiceCost(VoiceManager *vm, int channel) {
	if(!vm || channel < 0 || channel >= MAX_SEQUENCER_CHANNELS) {
		return 0;
	}
	Instrument *inst = vm->instruments[channel];
	if(!inst) {
		return 0;
	}
	/* A scratch voice, so the app's own pool is untouched: the
	 * measurement triggers a note, and a triggered note in the live pool
	 * would sound at the start of the session. */
	Voice *v = (Voice *)malloc(sizeof(Voice));
	if(!v) {
		return 0;
	}
	memset(v, 0, sizeof(*v));
	initialize_voice(v, inst);
	int note[NOTE_INFO_SIZE] = { A, 4 };
	triggerVoice(v, note, NOTE_VELOCITY_FULL);

	struct timespec t0;
	struct timespec t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for(int f = 0; f < DEVICE_CALIBRATE_FRAMES; f++) {
		syncVoiceGraph(v);
		processModulations(v->paramList, v->modList, 1.0f / (float)SPECTRAX_ENGINE_RATE);
		OutVal o = generateVoice(vm, v, 440.0f / (float)SPECTRAX_ENGINE_RATE, 440.0f);
		(void)o;
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	long nanos = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);

	freeVoice(v);
	return nanos / DEVICE_CALIBRATE_FRAMES;
}

bool deviceCalibrate(const char *configDir, VoiceManager *vm, int channel, DeviceBudget *out) {
	DeviceBudget budget;
	deviceBudgetDefaults(&budget);
	bool stored = deviceBudgetLoad(configDir, &budget);
	if(stored && budget.nanosPerVoiceSample > 0) {
		/* Already measured on this device. */
		if(out) {
			*out = budget;
		}
		return true;
	}
	long nanos = deviceCalibrateMeasureVoiceCost(vm, channel);
	if(nanos <= 0) {
		if(out) {
			*out = budget;
		}
		return false;
	}
	DeviceBudget measured;
	deviceBudgetDerive(&measured, nanos, deviceCalibrateDeadlineNanos(), DEVICE_CALIBRATE_HEADROOM,
	                   availableRamBytes(), 0);
	/* The profile is a convenience, not a requirement: a device whose
	 * config directory is read-only still runs, with the budget this run
	 * derived. */
	deviceBudgetStore(configDir, &measured);
	if(out) {
		*out = measured;
	}
	return true;
}
