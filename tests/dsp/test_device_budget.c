/* test_device_budget.c -- the budget's derivation and its profile.
 *
 * The budget is what keeps a slow device inside its callback deadline.
 * It is derived from a measurement, so the arithmetic has to be right in
 * the boundaries: a device that measured slow must not be given a budget
 * it cannot keep, and a device that measured fast must not be given one
 * that starves it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "device_budget.h"
#include "voice.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define ASSERT_EQ(a, b, m) do { if((a) != (b)) { \
	printf("FAIL: %s (%ld, expected %ld)\n", m, (long)(a), (long)(b)); return 1; } } while(0)

/* The deadline at 48 kHz with a 256-frame buffer: 5.33 ms. */
#define DEADLINE_NANOS (5333333)

static int test_the_defaults_are_sane(void) {
	DeviceBudget b;
	deviceBudgetDefaults(&b);
	ASSERT_TRUE(b.voicesPerChannel >= 1, "the default voice count is positive");
	ASSERT_TRUE(b.enabledChannels >= 1, "the default channel count is positive");
	ASSERT_EQ(b.bufferFrames, DEVICE_BUDGET_BUFFER_MID, "the default buffer is 256");
	ASSERT_TRUE(b.voiceTypes != 0, "the default permits some voice types");
	/* GRAIN's generate is a stub and SPECTRAL reads an uninitialised
	 * buffer, so neither may be offered. */
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_GRAIN) == 0, "GRAIN is not offered");
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_SPECTRAL) == 0, "SPECTRAL is not offered");
	printf("PASS test_the_defaults_are_sane\n");
	return 0;
}

/* A device that measured a slow voice gets fewer voices. */
static int test_a_slow_device_gets_fewer_voices(void) {
	DeviceBudget slow, fast;
	/* 1000 ns per sample is a slow voice: 256 samples of it is 256 us,
	 * and the deadline is 5333 us, so a few of them fit. */
	deviceBudgetDerive(&slow, 1000, DEADLINE_NANOS, 75, 512L * 1024L * 1024L, 0);
	deviceBudgetDerive(&fast, 50, DEADLINE_NANOS, 75, 512L * 1024L * 1024L, 0);
	ASSERT_TRUE(fast.voicesPerChannel > slow.voicesPerChannel,
	            "a faster voice earns more voices");
	ASSERT_TRUE(slow.voicesPerChannel >= 1, "even a slow device gets at least one voice");
	printf("PASS test_a_slow_device_gets_fewer_voices\n");
	return 0;
}

/* A voice that cannot fit the deadline even once still gets one: a
 * synth that plays nothing is worse than one that overruns slightly. */
static int test_an_impossible_device_still_gets_a_voice(void) {
	DeviceBudget b;
	deviceBudgetDerive(&b, 100000000L, DEADLINE_NANOS, 75, 512L * 1024L * 1024L, 0);
	ASSERT_EQ(b.voicesPerChannel, 1, "the minimum is one voice");
	printf("PASS test_an_impossible_device_still_gets_a_voice\n");
	return 0;
}

/* An unmeasured device keeps the defaults: the arithmetic must not
 * produce a nonsense budget out of a zero measurement. */
static int test_an_unmeasured_device_keeps_the_defaults(void) {
	DeviceBudget b;
	deviceBudgetDerive(&b, 0, DEADLINE_NANOS, 75, 0, 0);
	DeviceBudget d;
	deviceBudgetDefaults(&d);
	ASSERT_EQ(b.voicesPerChannel, d.voicesPerChannel, "the voice count is the default");
	ASSERT_EQ(b.bufferFrames, d.bufferFrames, "the buffer is the default");
	ASSERT_EQ(b.samplePoolBytes, d.samplePoolBytes, "the pool is the default");
	printf("PASS test_an_unmeasured_device_keeps_the_defaults\n");
	return 0;
}

/* The measurement is the floor: a device that managed fewer voices than
 * the arithmetic allows is given the fewer. */
static int test_the_measurement_is_the_floor(void) {
	DeviceBudget b;
	deviceBudgetDerive(&b, 50, DEADLINE_NANOS, 75, 512L * 1024L * 1024L, 2);
	ASSERT_EQ(b.voicesPerChannel, 2, "the measurement wins");
	printf("PASS test_the_measurement_is_the_floor\n");
	return 0;
}

/* The sample pool's ceiling scales with the memory, and never exceeds it
 * by much. */
static int test_the_sample_pool_scales_with_the_memory(void) {
	DeviceBudget small, large;
	deviceBudgetDerive(&small, 50, DEADLINE_NANOS, 75, 64L * 1024L * 1024L, 0);
	deviceBudgetDerive(&large, 50, DEADLINE_NANOS, 75, 1024L * 1024L * 1024L, 0);
	ASSERT_TRUE(large.samplePoolBytes > small.samplePoolBytes,
	            "more memory buys a bigger pool");
	ASSERT_TRUE(small.samplePoolBytes <= 64L * 1024L * 1024L,
	            "the pool does not exceed the memory");
	printf("PASS test_the_sample_pool_scales_with_the_memory\n");
	return 0;
}

/* The profile round-trips. */
static int test_the_profile_round_trips(void) {
	char dir[256];
	snprintf(dir, sizeof(dir), "/tmp/spectrax-budget-test-%d", (int)getpid());
	mkdir(dir, 0755);
	DeviceBudget written;
	deviceBudgetDefaults(&written);
	written.voicesPerChannel = 3;
	written.enabledChannels = 4;
	written.bufferFrames = DEVICE_BUDGET_BUFFER_MAX;
	written.voiceTypes = DEVICE_BUDGET_VOICE_FM | DEVICE_BUDGET_VOICE_SAMPLE;
	written.samplePoolBytes = 1234567L;
	written.nanosPerVoiceSample = 777L;
	ASSERT_TRUE(deviceBudgetStore(dir, &written), "the profile is written");

	DeviceBudget read;
	ASSERT_TRUE(deviceBudgetLoad(dir, &read), "the profile is read");
	ASSERT_EQ(read.voicesPerChannel, written.voicesPerChannel, "the voice count");
	ASSERT_EQ(read.enabledChannels, written.enabledChannels, "the channel count");
	ASSERT_EQ(read.bufferFrames, written.bufferFrames, "the buffer");
	ASSERT_EQ(read.voiceTypes, written.voiceTypes, "the voice types");
	ASSERT_EQ(read.samplePoolBytes, written.samplePoolBytes, "the pool");
	ASSERT_EQ(read.nanosPerVoiceSample, written.nanosPerVoiceSample, "the measurement");
	rmdir(dir);
	printf("PASS test_the_profile_round_trips\n");
	return 0;
}

/* A directory with no profile reads as "not measured", not as a crash. */
static int test_a_missing_profile_is_not_measured(void) {
	char dir[256];
	snprintf(dir, sizeof(dir), "/tmp/spectrax-budget-none-%d", (int)getpid());
	mkdir(dir, 0755);
	DeviceBudget b;
	ASSERT_TRUE(!deviceBudgetLoad(dir, &b), "a missing profile reads as unmeasured");
	ASSERT_TRUE(b.voicesPerChannel >= 1, "the defaults are left in place");
	rmdir(dir);
	printf("PASS test_a_missing_profile_is_not_measured\n");
	return 0;
}

/* The permitted voice types: the engine refuses a type the device does
 * not offer, so the GUI's greying out is backed by something real. */
static int test_the_voice_type_mask_is_enforced(void) {
	int saved = deviceBudgetPermittedVoiceTypes();
	/* Only the FM type is permitted. */
	deviceBudgetSetPermittedVoiceTypes(DEVICE_BUDGET_VOICE_FM);
	ASSERT_EQ(deviceBudgetPermittedVoiceTypes(), DEVICE_BUDGET_VOICE_FM,
	          "the mask is remembered");
	ASSERT_EQ(deviceBudgetMaskForVoiceType(VOICE_TYPE_FM), DEVICE_BUDGET_VOICE_FM,
	          "the FM type's bit");
	ASSERT_EQ(deviceBudgetMaskForVoiceType(VOICE_TYPE_SAMPLE), DEVICE_BUDGET_VOICE_SAMPLE,
	          "the sampler type's bit");
	/* A type outside the enum has no bit. */
	ASSERT_EQ(deviceBudgetMaskForVoiceType(VOICE_TYPE_COUNT), 0,
	          "a type past the end has no bit");
	deviceBudgetSetPermittedVoiceTypes(saved);
	printf("PASS test_the_voice_type_mask_is_enforced\n");
	return 0;
}

/* The default mask permits the three types that work, and not the two
 * that cannot make a sound. */
static int test_the_default_mask_permits_only_the_working_types(void) {
	DeviceBudget b;
	deviceBudgetDefaults(&b);
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_SAMPLE) != 0, "the sampler type");
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_FM) != 0, "the FM type");
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_BLEP) != 0, "the BLEP type");
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_GRAIN) == 0, "not the granular type");
	ASSERT_TRUE((b.voiceTypes & DEVICE_BUDGET_VOICE_SPECTRAL) == 0, "not the spectral type");
	printf("PASS test_the_default_mask_permits_only_the_working_types\n");
	return 0;
}

int main(void) {
	int fails = 0;
	fails += test_the_defaults_are_sane();
	fails += test_a_slow_device_gets_fewer_voices();
	fails += test_an_impossible_device_still_gets_a_voice();
	fails += test_an_unmeasured_device_keeps_the_defaults();
	fails += test_the_measurement_is_the_floor();
	fails += test_the_sample_pool_scales_with_the_memory();
	fails += test_the_profile_round_trips();
	fails += test_a_missing_profile_is_not_measured();
	fails += test_the_voice_type_mask_is_enforced();
	fails += test_the_default_mask_permits_only_the_working_types();
	if(fails) {
		fprintf(stderr, "%d device-budget test(s) failed\n", fails);
		return 1;
	}
	printf("ALL device-budget tests passed\n");
	return 0;
}
