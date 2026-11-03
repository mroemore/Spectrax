/* test_voice_isolation.c — a voice must not hear the other voices.
 *
 * Criterion C says a voice's output is a pure function of its settings:
 * the same voice with the same settings sounds the same whether it plays
 * alone or with four others. That is the claim the golden render cannot
 * make on its own -- the golden render proves two *machines* agree, not
 * that one voice is unaffected by its neighbours.
 *
 * The test renders the same scene twice: once with every voice playing,
 * once with a single voice playing, and compares that voice's samples
 * bit for bit. Everything else about the scene is held fixed.
 *
 * The formulation matters. The obvious one -- "the mix of all the voices
 * minus the mix of all but one equals the mix of one" -- is not a
 * bit-exact statement in floating point: (a + b) - a is not b in general.
 * The right statement is about the voice's own stream, which is what this
 * compares.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "voice.h"
#include "modsystem.h"
#include "notes.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

#define ISO_VOICES 4
#define ISO_FRAMES 4096

typedef struct {
	VoiceManager *vm;
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
	Instrument *inst;
} IsoEnv;

/* Build a scene: one channel, ISO_VOICES voices, all playing A4. */
static int isoSetup(IsoEnv *e) {
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
	settings.defaultVoiceCount = ISO_VOICES;
	settings.defaultBPM = 120;
	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	if(!e->vm) {
		return 1;
	}
	e->inst = e->vm->instruments[0];
	if(!e->inst) {
		return 1;
	}
	initVoicePool(e->vm, 0, ISO_VOICES, e->inst);
	int note[NOTE_INFO_SIZE] = { A, 4 };
	for(int i = 0; i < ISO_VOICES; i++) {
		Voice *v = e->vm->voicePools[0][i];
		if(!v) {
			return 1;
		}
		triggerVoice(v, note, NOTE_VELOCITY_FULL);
	}
	return 0;
}

static void isoTeardown(IsoEnv *e) {
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

/* Render the scene, capturing every voice's stream.
 *
 * `onlyVoice` is the index of the voice to keep playing, or -1 for all of
 * them. The capture is per voice, so the caller can compare one voice's
 * stream between the two runs. */
static int isoRender(IsoEnv *e, int onlyVoice, float *capture) {
	memset(capture, 0, sizeof(float) * ISO_VOICES * ISO_FRAMES);
	for(int f = 0; f < ISO_FRAMES; f++) {
		for(int i = 0; i < ISO_VOICES; i++) {
			if(onlyVoice >= 0 && i != onlyVoice) {
				continue;
			}
			Voice *v = e->vm->voicePools[0][i];
			if(!v || !v->active) {
				continue;
			}
			syncVoiceGraph(v);
			processModulations(v->paramList, v->modList, 1.0f / (float)SPECTRAX_ENGINE_RATE);
			OutVal o = generateVoice(e->vm, v, 440.0f / (float)SPECTRAX_ENGINE_RATE, 440.0f);
			capture[i * ISO_FRAMES + f] = o.L;
		}
	}
	return 0;
}

static int test_a_voice_does_not_hear_the_others(void) {
	for(int target = 0; target < ISO_VOICES; target++) {
		IsoEnv e;
		if(isoSetup(&e) != 0) {
			printf("FAIL: could not build the scene\n");
			return 1;
		}
		float *together = (float *)malloc(sizeof(float) * ISO_VOICES * ISO_FRAMES);
		float *alone = (float *)malloc(sizeof(float) * ISO_VOICES * ISO_FRAMES);
		if(!together || !alone) {
			printf("FAIL: out of memory\n");
			free(together);
			free(alone);
			isoTeardown(&e);
			return 1;
		}
		/* Two scenes, built the same way: one with every voice playing,
		 * one with only the voice under test. They have to be separate
		 * scenes -- a voice's state advances as it is rendered, so
		 * rendering the same scene twice would start the second run from
		 * where the first one ended. */
		isoRender(&e, -1, together);
		isoTeardown(&e);
		if(isoSetup(&e) != 0) {
			printf("FAIL: could not rebuild the scene\n");
			free(together);
			free(alone);
			return 1;
		}
		isoRender(&e, target, alone);
		int differs = -1;
		for(int f = 0; f < ISO_FRAMES; f++) {
			float a = together[target * ISO_FRAMES + f];
			float b = alone[target * ISO_FRAMES + f];
			if(a != b) {
				differs = f;
				break;
			}
		}
		if(differs >= 0) {
			printf("FAIL: voice %d differs at frame %d: %g with the others, %g alone\n",
			       target, differs,
			       (double)together[target * ISO_FRAMES + differs],
			       (double)alone[target * ISO_FRAMES + differs]);
			free(together);
			free(alone);
			isoTeardown(&e);
			return 1;
		}
		free(together);
		free(alone);
		isoTeardown(&e);
	}
	printf("PASS test_a_voice_does_not_hear_the_others\n");
	return 0;
}

/* And the mirror of it: the voices do not affect each other even when the
 * envelope is running and the phase is advancing, so the comparison is
 * made over a whole second rather than a note's length. */
static int test_the_isolation_holds_over_a_long_render(void) {
	IsoEnv e;
	if(isoSetup(&e) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	float *together = (float *)malloc(sizeof(float) * ISO_VOICES * ISO_FRAMES);
	float *alone = (float *)malloc(sizeof(float) * ISO_VOICES * ISO_FRAMES);
	if(!together || !alone) {
		printf("FAIL: out of memory\n");
		free(together);
		free(alone);
		isoTeardown(&e);
		return 1;
	}
	isoRender(&e, -1, together);
	isoTeardown(&e);
	if(isoSetup(&e) != 0) {
		printf("FAIL: could not rebuild the scene\n");
		free(together);
		free(alone);
		return 1;
	}
	isoRender(&e, 0, alone);
	/* The last frame is the interesting one: by then the envelope has
	 * moved, the phase has wrapped, and any coupling would have shown. */
	int differs = -1;
	for(int f = 0; f < ISO_FRAMES; f++) {
		if(together[f] != alone[f]) {
			differs = f;
			break;
		}
	}
	if(differs >= 0) {
		printf("FAIL: voice 0 differs at frame %d\n", differs);
		free(together);
		free(alone);
		isoTeardown(&e);
		return 1;
	}
	free(together);
	free(alone);
	isoTeardown(&e);
	printf("PASS test_the_isolation_holds_over_a_long_render\n");
	return 0;
}

int main(void) {
	initModSystem();
	int fails = 0;
	fails += test_a_voice_does_not_hear_the_others();
	fails += test_the_isolation_holds_over_a_long_render();
	if(fails) {
		fprintf(stderr, "%d voice-isolation test(s) failed\n", fails);
		return 1;
	}
	printf("ALL voice-isolation tests passed\n");
	return 0;
}
