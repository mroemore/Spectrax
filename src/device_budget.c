#include "device_budget.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The profile's format version, so a future change can be told apart from
 * a file written by an older build. */
#define DEVICE_BUDGET_PROFILE_VERSION 1
#define DEVICE_BUDGET_PROFILE_NAME "device-profile.json"

/* The voice types the engine can actually make a sound with. GRAIN's
 * generate is a stub that returns silence and SPECTRAL reads an
 * uninitialised buffer, so neither is offered: a voice type that cannot
 * make a sound is not a budget question, it is a missing feature. */
#define DEVICE_BUDGET_DEFAULT_VOICE_TYPES \
	(DEVICE_BUDGET_VOICE_SAMPLE | DEVICE_BUDGET_VOICE_FM | DEVICE_BUDGET_VOICE_BLEP)

/* The fractions of the deadline and of the memory the budget may use. */
#define DEVICE_BUDGET_HEADROOM_NUM 3
#define DEVICE_BUDGET_HEADROOM_DEN 4
#define DEVICE_BUDGET_RAM_NUM 1
#define DEVICE_BUDGET_RAM_DEN 8

/* The ranges the derivation clamps into. */
#define DEVICE_BUDGET_MIN_VOICES 1
#define DEVICE_BUDGET_MAX_VOICES 16
#define DEVICE_BUDGET_MIN_CHANNELS 1
#define DEVICE_BUDGET_MAX_CHANNELS 16

void deviceBudgetDefaults(DeviceBudget *out) {
	if(!out) {
		return;
	}
	memset(out, 0, sizeof(*out));
	out->voicesPerChannel = 8;
	out->enabledChannels = 16;
	out->bufferFrames = DEVICE_BUDGET_BUFFER_MID;
	out->voiceTypes = DEVICE_BUDGET_DEFAULT_VOICE_TYPES;
	out->samplePoolBytes = 32L * 1000L * 1000L;
	out->nanosPerVoiceSample = 0;
}

/* How many voices fit in the deadline. */
static long deriveVoices(long nanosPerVoiceSample, int deadlineNanos, int headroom, int bufferFrames) {
	if(nanosPerVoiceSample <= 0 || deadlineNanos <= 0) {
		return DEVICE_BUDGET_MIN_VOICES;
	}
	if(headroom < 1) {
		headroom = 1;
	}
	if(headroom > 100) {
		headroom = 100;
	}
	/* The usable part of the deadline, in nanoseconds. */
	long usable = ((long)deadlineNanos * headroom) / 100;
	if(usable <= 0) {
		return DEVICE_BUDGET_MIN_VOICES;
	}
	/* The cost of one voice over a whole buffer, not per sample: the
	 * deadline is a buffer's duration. */
	long perVoice = nanosPerVoiceSample * (long)bufferFrames;
	if(perVoice <= 0) {
		return DEVICE_BUDGET_MIN_VOICES;
	}
	long voices = usable / perVoice;
	if(voices < DEVICE_BUDGET_MIN_VOICES) {
		voices = DEVICE_BUDGET_MIN_VOICES;
	}
	if(voices > DEVICE_BUDGET_MAX_VOICES) {
		voices = DEVICE_BUDGET_MAX_VOICES;
	}
	return voices;
}

/* The sample pool's ceiling: a fraction of the device's memory. */
static long deriveSamplePool(long ramBytes) {
	if(ramBytes <= 0) {
		return 32L * 1000L * 1000L;
	}
	long pool = (ramBytes * DEVICE_BUDGET_RAM_NUM) / DEVICE_BUDGET_RAM_DEN;
	if(pool < 1024L * 1024L) {
		pool = 1024L * 1024L;
	}
	return pool;
}

/* The buffer size: the one that lets the allowed voices fit the
 * deadline. */
static int deriveBuffer(long nanosPerVoiceSample, int voices) {
	/* The buffer size is chosen so the deadline covers the voices the
	 * budget allows: a slower device with fewer voices can afford a
	 * longer buffer, because the buffer's cost is per voice per buffer.
	 * The size is a latency knob, so this choice changes the latency and
	 * nothing else. */
	if(nanosPerVoiceSample <= 0) {
		return DEVICE_BUDGET_BUFFER_MID;
	}
	/* 256 frames at 48000 is 5.33 ms. */
	long perBuffer = nanosPerVoiceSample * (long)DEVICE_BUDGET_BUFFER_MID * (long)voices;
	if(perBuffer > 2666666L) {
		return DEVICE_BUDGET_BUFFER_MAX;
	}
	return DEVICE_BUDGET_BUFFER_MID;
}

void deviceBudgetDerive(DeviceBudget *out, long nanosPerVoiceSample, int deadlineNanos,
                         int headroom, long ramBytes, int measuredVoices) {
	if(!out) {
		return;
	}
	deviceBudgetDefaults(out);
	if(nanosPerVoiceSample > 0) {
		out->nanosPerVoiceSample = nanosPerVoiceSample;
		out->voicesPerChannel = (int)deriveVoices(nanosPerVoiceSample, deadlineNanos, headroom,
		                                                DEVICE_BUDGET_BUFFER_MID);
		out->bufferFrames = deriveBuffer(nanosPerVoiceSample, out->voicesPerChannel);
	}
	if(ramBytes > 0) {
		out->samplePoolBytes = deriveSamplePool(ramBytes);
	}
	if(measuredVoices > 0 && measuredVoices < out->voicesPerChannel) {
		/* The measurement is the floor: if the device was measured with
		 * fewer voices than the arithmetic allows, that is what it
		 * managed. */
		out->voicesPerChannel = measuredVoices;
	}
}

void deviceBudgetProfilePath(char *out, size_t outSize, const char *configDir) {
	if(!out || outSize == 0) {
		return;
	}
	out[0] = '\0';
	if(!configDir || !configDir[0]) {
		return;
	}
	snprintf(out, outSize, "%s/%s", configDir, DEVICE_BUDGET_PROFILE_NAME);
}

/* The profile is a small JSON object. The tree already vendors cjson, so
 * no new dependency is needed. */
#include "cJSON.h"

bool deviceBudgetLoad(const char *configDir, DeviceBudget *out) {
	if(!out) {
		return false;
	}
	deviceBudgetDefaults(out);
	char path[1024];
	deviceBudgetProfilePath(path, sizeof(path), configDir);
	if(!path[0]) {
		return false;
	}
	FILE *f = fopen(path, "r");
	if(!f) {
		return false;
	}
	/* The whole file, not one line: cJSON's output is pretty-printed and
	 * spans several lines. */
	char *text = NULL;
	size_t cap = 0;
	ssize_t len = 0;
	for(;;) {
		if((size_t)len + 1024 > cap) {
			size_t want = cap ? cap * 2 : 4096;
			while((size_t)len + 1024 > want) {
				want *= 2;
			}
			char *bigger = (char *)realloc(text, want);
			if(!bigger) {
				free(text);
				fclose(f);
				return false;
			}
			text = bigger;
			cap = want;
		}
		size_t got = fread(text + len, 1, 1024, f);
		len += (ssize_t)got;
		if(got < 1024) {
			break;
		}
	}
	fclose(f);
	if(len <= 0 || !text) {
		free(text);
		return false;
	}
	text[len] = '\0';
	cJSON *root = cJSON_Parse(text);
	free(text);
	if(!root) {
		return false;
	}
	bool ok = false;
	cJSON *version = cJSON_GetObjectItem(root, "version");
	if(cJSON_IsNumber(version) && version->valueint == DEVICE_BUDGET_PROFILE_VERSION) {
		cJSON *v = cJSON_GetObjectItem(root, "voices_per_channel");
		cJSON *c = cJSON_GetObjectItem(root, "enabled_channels");
		cJSON *b = cJSON_GetObjectItem(root, "buffer_frames");
		cJSON *t = cJSON_GetObjectItem(root, "voice_types");
		cJSON *s = cJSON_GetObjectItem(root, "sample_pool_bytes");
		cJSON *n = cJSON_GetObjectItem(root, "nanos_per_voice_sample");
		if(cJSON_IsNumber(v) && cJSON_IsNumber(c) && cJSON_IsNumber(b) &&
		   cJSON_IsNumber(t) && cJSON_IsNumber(s) && cJSON_IsNumber(n)) {
			out->voicesPerChannel = v->valueint;
			out->enabledChannels = c->valueint;
			out->bufferFrames = b->valueint;
			out->voiceTypes = t->valueint;
			out->samplePoolBytes = (long)s->valuedouble;
			out->nanosPerVoiceSample = (long)n->valuedouble;
			ok = true;
		}
	}
	cJSON_Delete(root);
	return ok;
}

bool deviceBudgetStore(const char *configDir, const DeviceBudget *budget) {
	if(!budget) {
		return false;
	}
	char path[1024];
	deviceBudgetProfilePath(path, sizeof(path), configDir);
	if(!path[0]) {
		return false;
	}
	cJSON *root = cJSON_CreateObject();
	if(!root) {
		return false;
	}
	cJSON_AddNumberToObject(root, "version", DEVICE_BUDGET_PROFILE_VERSION);
	cJSON_AddNumberToObject(root, "voices_per_channel", budget->voicesPerChannel);
	cJSON_AddNumberToObject(root, "enabled_channels", budget->enabledChannels);
	cJSON_AddNumberToObject(root, "buffer_frames", budget->bufferFrames);
	cJSON_AddNumberToObject(root, "voice_types", budget->voiceTypes);
	cJSON_AddNumberToObject(root, "sample_pool_bytes", (double)budget->samplePoolBytes);
	cJSON_AddNumberToObject(root, "nanos_per_voice_sample", (double)budget->nanosPerVoiceSample);
	char *text = cJSON_Print(root);
	cJSON_Delete(root);
	if(!text) {
		return false;
	}
	FILE *f = fopen(path, "w");
	if(!f) {
		free(text);
		return false;
	}
	fputs(text, f);
	fclose(f);
	free(text);
	return true;
}
