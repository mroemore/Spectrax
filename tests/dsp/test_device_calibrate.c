/* test_device_calibrate.c — the device's calibration.
 *
 * The calibration is what turns criterion B ("budgets from a measurement")
 * from a claim into a mechanism, and it runs on the user's machine the
 * first time they start the app. Two things about it are worth pinning:
 *
 * - it must not disturb the app. The measurement renders a voice, and a
 *   measurement that left the app's own voice pool triggered would make the
 *   app start with a note already sounding.
 * - it must be reproducible enough to be useful. A measurement that varies
 *   wildly between runs would make the budget arbitrary.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "device_calibrate.h"
#include "device_budget.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define ASSERT_EQ(a, b, m) do { if((a) != (b)) { \
	printf("FAIL: %s (%ld, expected %ld)\n", m, (long)(a), (long)(b)); return 1; } } while(0)

typedef struct {
	VoiceManager *vm;
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
} CalEnv;

static int calSetup(CalEnv *e) {
	memset(e, 0, sizeof(*e));
	e->sp = createSamplePool();
	e->wtp = createWavetablePool();
	if(!e->sp || !e->wtp) {
		return 1;
	}
	initPresetBank(&e->pb);
	Preset p;
	initDefaultFmPreset(&p);
	addPresetToBank(&e->pb, p);
	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = 1;
	settings.defaultVoiceCount = 2;
	settings.defaultBPM = 120;
	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	if(!e->vm) {
		return 1;
	}
	return 0;
}

static void calTeardown(CalEnv *e) {
	if(e->vm) {
		freeVoiceManager(e->vm);
		e->vm = NULL;
	}
	if(e->sp) {
		freeSamplePool(e->sp);
		e->sp = NULL;
	}
	if(e->wtp) {
		freeWavetablePool(e->wtp);
		e->wtp = NULL;
	}
}

/* The measurement must leave the app's voice pool exactly as it was. */
static int test_the_measurement_leaves_the_pool_alone(void) {
	CalEnv e;
	if(calSetup(&e) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	/* Record the pool's state. */
	int count = e.vm->voiceCount[0];
	int active[16];
	int note0[16];
	int note1[16];
	for(int i = 0; i < count && i < 16; i++) {
		active[i] = e.vm->voicePools[0][i]->active;
		note0[i] = e.vm->voicePools[0][i]->note[0];
		note1[i] = e.vm->voicePools[0][i]->note[1];
	}
	long nanos = deviceCalibrateMeasureVoiceCost(e.vm, 0);
	if(nanos <= 0) {
		printf("FAIL: the measurement returned %ld\n", nanos);
		calTeardown(&e);
		return 1;
	}
	ASSERT_EQ(e.vm->voiceCount[0], count, "the pool's size is unchanged");
	for(int i = 0; i < count && i < 16; i++) {
		if(e.vm->voicePools[0][i]->active != active[i]) {
			printf("FAIL: the measurement changed voice %d's active flag\n", i);
			calTeardown(&e);
			return 1;
		}
		if(e.vm->voicePools[0][i]->note[0] != note0[i] ||
		   e.vm->voicePools[0][i]->note[1] != note1[i]) {
			printf("FAIL: the measurement changed voice %d's note\n", i);
			calTeardown(&e);
			return 1;
		}
	}
	calTeardown(&e);
	printf("PASS test_the_measurement_leaves_the_pool_alone\n");
	return 0;
}

/* The measurement must be positive and in a plausible range: a voice costs
 * tens of nanoseconds per sample on a fast machine and a few thousand on a
 * slow one. A measurement of zero would mean the voice rendered nothing. */
static int test_the_measurement_is_plausible(void) {
	CalEnv e;
	if(calSetup(&e) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	long nanos = deviceCalibrateMeasureVoiceCost(e.vm, 0);
	if(nanos <= 0) {
		printf("FAIL: the measurement returned %ld\n", nanos);
		calTeardown(&e);
		return 1;
	}
	if(nanos > 1000000L) {
		printf("FAIL: the measurement returned %ld ns per sample, which is not a voice\n", nanos);
		calTeardown(&e);
		return 1;
	}
	calTeardown(&e);
	printf("PASS test_the_measurement_is_plausible\n");
	return 0;
}

/* The calibration writes a profile, and the next calibration reads it
 * instead of measuring again. */
static int test_the_calibration_is_written_and_reused(void) {
	char dir[256];
	snprintf(dir, sizeof(dir), "/tmp/spectrax-calibrate-%d", (int)getpid());
	mkdir(dir, 0755);
	CalEnv e;
	if(calSetup(&e) != 0) {
		printf("FAIL: could not build the scene\n");
		rmdir(dir);
		return 1;
	}
	DeviceBudget first;
	ASSERT_TRUE(deviceCalibrate(dir, e.vm, 0, &first), "the calibration measures the device");
	ASSERT_TRUE(first.nanosPerVoiceSample > 0, "the budget carries the measurement");
	ASSERT_TRUE(first.voicesPerChannel >= 1, "the budget allows a voice");

	/* The profile is on disk. */
	DeviceBudget stored;
	ASSERT_TRUE(deviceBudgetLoad(dir, &stored), "the profile is written");
	ASSERT_EQ(stored.nanosPerVoiceSample, first.nanosPerVoiceSample, "the profile carries the measurement");

	/* The second calibration reuses it: the stored measurement is what
	 * comes back, not a fresh one. */
	DeviceBudget second;
	ASSERT_TRUE(deviceCalibrate(dir, e.vm, 0, &second), "the second calibration succeeds");
	ASSERT_EQ(second.nanosPerVoiceSample, first.nanosPerVoiceSample,
	          "the second calibration reuses the stored measurement");

	calTeardown(&e);
	rmdir(dir);
	printf("PASS test_the_calibration_is_written_and_reused\n");
	return 0;
}

/* A device with no storeable profile still gets a budget: the calibration
 * is a convenience, not a requirement. */
static int test_an_unwritable_profile_still_yields_a_budget(void) {
	char dir[256];
	snprintf(dir, sizeof(dir), "/tmp/spectrax-calibrate-ro-%d", (int)getpid());
	mkdir(dir, 0755);
	CalEnv e;
	if(calSetup(&e) != 0) {
		printf("FAIL: could not build the scene\n");
		rmdir(dir);
		return 1;
	}
	/* Point the profile at a path that cannot be written: a directory
	 * inside a non-existent parent. */
	DeviceBudget budget;
	ASSERT_TRUE(deviceCalibrate("/proc/nonexistent/spectrax", e.vm, 0, &budget),
	           "the calibration still measures");
	ASSERT_TRUE(budget.nanosPerVoiceSample > 0, "the budget carries the measurement");
	ASSERT_TRUE(budget.voicesPerChannel >= 1, "the budget allows a voice");
	calTeardown(&e);
	rmdir(dir);
	printf("PASS test_an_unwritable_profile_still_yields_a_budget\n");
	return 0;
}

int main(void) {
	initModSystem();
	int fails = 0;
	fails += test_the_measurement_leaves_the_pool_alone();
	fails += test_the_measurement_is_plausible();
	fails += test_the_calibration_is_written_and_reused();
	fails += test_an_unwritable_profile_still_yields_a_budget();
	if(fails) {
		fprintf(stderr, "%d device-calibrate test(s) failed\n", fails);
		return 1;
	}
	printf("ALL device-calibrate tests passed\n");
	return 0;
}
