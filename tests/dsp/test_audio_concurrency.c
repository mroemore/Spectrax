/* test_audio_concurrency.c — the audio/GUI-thread race that the render lock
 * exists to prevent.
 *
 * The scenario is not hypothetical: src/audio.c's comment records a real
 * production segfault. An instrument type-swap frees and re-mallocs the Voice
 * structs while the audio thread is iterating voicePools[j][v] in the middle
 * of a buffer, so without the lock the `rebuilding` flag leaves a
 * check-then-use window. That was fixed by adding g_audioLock, and this test
 * is what keeps the fix honest.
 *
 * Thread layout mirrors production: one thread runs renderAudioBuffer() in a
 * loop (the PortAudio callback's job), the other performs the reconfiguration
 * the GUI thread performs (setInstrumentVoiceType's type-cycle). Both take
 * g_audioLock, so a correct build is silent under ThreadSanitizer and Helgrind.
 *
 * What this can and cannot prove:
 *   - It DOES prove no race is reported for the interleavings it happens to
 *     produce, and it runs the real code path rather than a mock.
 *   - It does NOT prove the absence of races. TSan reports what it observes;
 *     an interleaving this two-thread loop never reaches stays hidden. Treat a
 *     clean run as "no evidence of a race", not "no race".
 *
 * No window and no audio device: with g_probeTempo false (the default)
 * renderAudioBuffer makes no raylib calls at all. */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "audio.h"
#include "fft.h"
#include "io.h"
#include "modsystem.h"
#include "notes.h"
#include "sample.h"
#include "sequencer.h"
#include "voice.h"
#include "wavetable.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define AUDIO_CHANNELS   1
#define VOICE_COUNT      2
#define FRAMES           64
#define AUDIO_ITERS      400
#define GUI_ITERS        40
#define TEST_SAMPLE_LEN  2048

static paTestData g_data;
static VoiceManager *g_vm;

/* --- thread bodies ------------------------------------------------- */

static void *audio_thread(void *arg) {
	(void)arg;
	static float buf[FRAMES * 2];
	for(int i = 0; i < AUDIO_ITERS; i++) {
		renderAudioBuffer(&g_data, buf, FRAMES);
	}
	return NULL;
}

static void *typecycle_thread(void *arg) {
	(void)arg;
	/* The exact production gesture from the audio.c comment: cycle the
	 * instrument's voice type, which frees the old Instrument and its
	 * voices and builds fresh ones. */
	const VoiceType cycle[] = { VOICE_TYPE_FM, VOICE_TYPE_BLEP, VOICE_TYPE_SAMPLE };
	for(int i = 0; i < GUI_ITERS; i++) {
		setInstrumentVoiceType(g_vm, 0, cycle[i % 3]);
	}
	return NULL;
}

static void *rebuild_thread(void *arg) {
	(void)arg;
	/* The other reconfiguration path: re-point the voice pool at the
	 * instrument's current param/mod lists. */
	for(int i = 0; i < GUI_ITERS; i++) {
		pthread_mutex_lock(&g_audioLock);
		Instrument *inst = g_vm->instruments[0];
		inst->rebuilding = true;
		rebuildVoicesForInstrument(g_vm, inst);
		inst->rebuilding = false;
		pthread_mutex_unlock(&g_audioLock);
	}
	return NULL;
}

/* --- environment --------------------------------------------------- */

static int make_env(void) {
	memset(&g_data, 0, sizeof(g_data));

	SamplePool *sp = createSamplePool();
	ASSERT_TRUE(sp != NULL, "createSamplePool");
	float *sbuf = (float *)malloc(TEST_SAMPLE_LEN * sizeof(float));
	for(int i = 0; i < TEST_SAMPLE_LEN; i++) {
		sbuf[i] = sinf(2.0f * (float)M_PI * (float)i / (float)TEST_SAMPLE_LEN);
	}
	loadSample(sp, "sine", sbuf, 24, SPECTRAX_ENGINE_RATE, TEST_SAMPLE_LEN);
	free(sbuf);

	WavetablePool *wtp = createWavetablePool();
	ASSERT_TRUE(wtp != NULL, "createWavetablePool");

	initPresetBank(&g_data.presetBank);
	Preset p;
	initDefaultFmPreset(&p);
	addPresetToBank(&g_data.presetBank, p);

	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = AUDIO_CHANNELS;
	settings.defaultVoiceCount = VOICE_COUNT;
	settings.defaultBPM = 120;

	g_vm = createVoiceManager(&settings, sp, wtp, &g_data.presetBank);
	ASSERT_TRUE(g_vm != NULL, "createVoiceManager");

	g_data.samplePool = sp;
	g_data.wavetablePool = wtp;
	g_data.voiceManager = g_vm;
	g_data.globalParameters = createParamList();
	g_data.modList = createModList();
	ASSERT_TRUE(g_data.globalParameters != NULL, "createParamList");

	/* createArranger needs a global param list for the BPM/swing params and
	 * reads settings->defaultBPM. NULL appState is fine: it only stores the
	 * pointer for later callbacks. */
	g_data.arranger = createArranger(&settings, g_vm, NULL, g_data.globalParameters);
	ASSERT_TRUE(g_data.arranger != NULL, "createArranger");
	g_data.arranger->enabledChannels = AUDIO_CHANNELS;
	g_data.arranger->playing = true;
	g_data.arranger->tempoSettings.samplesPerEvenStep = 64;
	g_data.arranger->tempoSettings.samplesPerOddStep = 64;
	g_data.arranger->visibleStart = 0;

	g_data.patternList = createPatternList(NULL);
	ASSERT_TRUE(g_data.patternList != NULL, "createPatternList");
	/* One 4-step pattern on channel 0 so advanceSequencerStep has something
	 * to fire and triggerSequencerNote allocates voices. */
	static int notes[4][NOTE_INFO_SIZE];
	for(int i = 0; i < 4; i++) {
		notes[i][0] = i;   /* note index */
		notes[i][1] = 4;   /* octave */
	}
	int pat = addPattern(g_data.patternList, 4, notes);
	ASSERT_TRUE(pat == 0, "addPattern");
	g_data.arranger->song[0][0] = pat;

	g_data.sequencer = createSequencer(g_data.arranger);
	ASSERT_TRUE(g_data.sequencer != NULL, "createSequencer");

	initFFT(&g_data.spectrogram.fft, 512, FRAMES, 1, false, false);

	/* Envelope generation reads the module-global envTables by a hardcoded
	 * table index (modsystem.c generateEnvelope: tIdx = 8), so it must be
	 * populated before the first processModulations — same prerequisite
	 * test_voice.c calls out. */
	initModSystem();
	return 0;
}

/* --- tests --------------------------------------------------------- */

/* Single-threaded smoke: the extracted function must produce audio before we
 * ask two threads to race on it. A crash here is a plain bug, not a race. */
static int test_render_single_threaded(void) {
	static float buf[FRAMES * 2];
	memset(buf, 0, sizeof(buf));
	for(int i = 0; i < 8; i++) {
		renderAudioBuffer(&g_data, buf, FRAMES);
	}
	/* The master volume must be observable in the output path. */
	audioSetMasterVolume(0.0f);
	renderAudioBuffer(&g_data, buf, FRAMES);
	for(int i = 0; i < FRAMES * 2; i++) {
		ASSERT_TRUE(buf[i] == 0.0f, "volume 0 must silence the buffer");
	}
	audioSetMasterVolume(1.0f);
	renderAudioBuffer(&g_data, buf, FRAMES);

	printf("PASS test_render_single_threaded\n");
	return 0;
}

typedef void *(*thread_fn)(void *);

static int run_pair(thread_fn gui_fn, const char *name) {
	pthread_t audio, gui;
	pthread_create(&audio, NULL, audio_thread, NULL);
	pthread_create(&gui, NULL, gui_fn, NULL);
	pthread_join(audio, NULL);
	pthread_join(gui, NULL);
	printf("PASS %s\n", name);
	return 0;
}

static int test_typecycle_race(void) {
	return run_pair(typecycle_thread, "test_typecycle_race");
}

static int test_rebuild_race(void) {
	return run_pair(rebuild_thread, "test_rebuild_race");
}

/* Master volume and the tempo probe are module state in audio.c touched by
 * both the argv/input path and the render loop; read them from the GUI
 * thread while the audio thread renders. */
static int test_volume_race(void) {
	pthread_t audio;
	pthread_create(&audio, NULL, audio_thread, NULL);
	for(int i = 0; i < 1000; i++) {
		audioSetMasterVolume((float)(i % 101) / 100.0f);
		(void)audioGetMasterVolume();
	}
	pthread_join(audio, NULL);
	audioSetMasterVolume(1.0f);
	printf("PASS test_volume_race\n");
	return 0;
}

int main(void) {
	int rc = 0;
	if(make_env() != 0) {
		return 1;
	}
	rc |= test_render_single_threaded();
	rc |= test_typecycle_race();
	rc |= test_rebuild_race();
	rc |= test_volume_race();
	if(rc != 0) {
		fprintf(stderr, "FAILED (audio concurrency)\n");
		return 1;
	}
	printf("PASSED (audio concurrency)\n");
	return 0;
}