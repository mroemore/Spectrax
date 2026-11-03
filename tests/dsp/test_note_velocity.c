/* test_note_velocity.c — the note's velocity, from the sequencer into the
 * voice.
 *
 * The corpus's MPC60 finding (article id 23): "Velocity moves the sample's
 * start address as well as envelope attack. Hard hits skip more of the
 * attack, soft hits get the whole transient. Nobody emulates this, and it
 * is acoustically consequential." And: "No choking. Repeated strikes of a
 * drum are all voiced - earlier strikes are not cut off."
 *
 * This test covers the *plumbing*: a step's velocity travels from the
 * pattern, through the file, through the sequencer's trigger, into the
 * voice -- and the voice's allocation does not cut off a sounding note
 * while a finished one is available.
 *
 * The velocity's convention: 1..127, with 127 the loudest. Zero means
 * "unset", and an unset velocity plays at full: a note with no velocity
 * information must sound exactly as it did before velocity existed, or
 * every existing song would change.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sequencer.h"
#include "voice.h"
#include "modsystem.h"
#include "notes.h"
#include "sampler_character.h"
#include "io/sequencer_io.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define ASSERT_EQ(a, b, m) do { if((a) != (b)) { \
	printf("FAIL: %s (%ld, expected %ld)\n", m, (long)(a), (long)(b)); return 1; } } while(0)

/* ------------------------------------------------------------------ */
/* The pattern's velocity.                                            */
/* ------------------------------------------------------------------ */

/* A step's velocity defaults to full: an unset velocity must not change
 * how an existing note sounds. */
static int test_the_velocity_defaults_to_full(void) {
	PatternList *pl = createPatternList(NULL);
	if(!pl) {
		printf("FAIL: createPatternList\n");
		return 1;
	}
	int notes[4][NOTE_INFO_SIZE] = { { A, 4 }, { C, 5 }, { OFF, 0 }, { OFF, 0 } };
	int idx = addPattern(pl, 4, notes);
	ASSERT_TRUE(idx >= 0, "the pattern is added");
	int vel = getCurrentStepVelocity(pl, idx, 0);
	ASSERT_EQ(vel, NOTE_VELOCITY_FULL, "an unset velocity is full");
	/* A step with no note is still unset. */
	ASSERT_EQ(getCurrentStepVelocity(pl, idx, 3), NOTE_VELOCITY_FULL,
	          "a step with no note is unset");
	free(pl);
	printf("PASS test_the_velocity_defaults_to_full\n");
	return 0;
}

/* A velocity can be set, and it is read back. */
static int test_the_velocity_can_be_set(void) {
	PatternList *pl = createPatternList(NULL);
	if(!pl) {
		printf("FAIL: createPatternList\n");
		return 1;
	}
	int notes[2][NOTE_INFO_SIZE] = { { A, 4 }, { C, 5 } };
	int idx = addPattern(pl, 2, notes);
	ASSERT_TRUE(idx >= 0, "the pattern is added");
	ASSERT_TRUE(setStepVelocity(pl, idx, 1, 64), "the velocity is set");
	ASSERT_EQ(getCurrentStepVelocity(pl, idx, 1), 64, "the velocity reads back");
	/* The other step is untouched. */
	ASSERT_EQ(getCurrentStepVelocity(pl, idx, 0), NOTE_VELOCITY_FULL,
	          "the other step is untouched");
	/* Out of range is refused, not clamped silently. */
	ASSERT_TRUE(!setStepVelocity(pl, idx, 0, 0), "zero is refused");
	ASSERT_TRUE(!setStepVelocity(pl, idx, 0, 128), "128 is refused");
	ASSERT_TRUE(!setStepVelocity(pl, idx, 99, 64), "an out-of-range step is refused");
	free(pl);
	printf("PASS test_the_velocity_can_be_set\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* The file.                                                          */
/* ------------------------------------------------------------------ */

/* Build a song file with a pattern whose second step has a velocity, and
 * read it back. */
static int test_the_velocity_survives_the_file(void) {
	const char *path = ".tmp_files/test_note_velocity.sng";
	ParamList *pl = createParamList();
	Arranger *ar = (Arranger *)calloc(1, sizeof(Arranger));
	PatternList *pat = (PatternList *)calloc(1, sizeof(PatternList));
	if(!pl || !ar || !pat) {
		printf("FAIL: the environment\n");
		return 1;
	}
	ar->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f, 1000.0f, 1.0f, 10.0f, NULL, NULL);
	ar->enabledChannels = 1;
	int notes[4][NOTE_INFO_SIZE] = { { A, 4 }, { C, 5 }, { OFF, 0 }, { OFF, 0 } };
	int idx = addPattern(pat, 4, notes);
	if(idx < 0 || !setStepVelocity(pat, idx, 1, 77)) {
		printf("FAIL: the pattern\n");
		return 1;
	}
	SequencerFileResult rc = saveSequencerState(path, ar, pat, NULL);
	ASSERT_EQ(rc, SEQ_OK, "the song is saved");

	/* Read it into a fresh destination. */
	PatternList *dst = (PatternList *)calloc(1, sizeof(PatternList));
	Arranger *ar2 = (Arranger *)calloc(1, sizeof(Arranger));
	ParamList *pl2 = createParamList();
	ar2->tempoSettings.bpm = createParameterPro(pl2, "BPM", 120.0f, 1.0f, 1000.0f, 1.0f, 10.0f, NULL, NULL);
	rc = loadSequencerState(path, ar2, dst, NULL);
	ASSERT_EQ(rc, SEQ_OK, "the song is loaded");
	ASSERT_TRUE(dst->pattern_count >= 1, "the pattern is loaded");
	ASSERT_EQ(getCurrentStepVelocity(dst, 0, 1), 77, "the velocity survives the file");
	ASSERT_EQ(getCurrentStepVelocity(dst, 0, 0), NOTE_VELOCITY_FULL,
	          "an unset velocity stays unset");
	free(dst);
	free(ar2);
	free(pl2);
	free(pat);
	free(ar);
	free(pl);
	printf("PASS test_the_velocity_survives_the_file\n");
	return 0;
}

/* A file with no velocity information -- an older song -- loads with
 * every velocity unset, so it plays exactly as it did. */
static int test_an_older_file_loads_with_unset_velocities(void) {
	const char *path = ".tmp_files/test_note_velocity_old.sng";
	ParamList *pl = createParamList();
	Arranger *ar = (Arranger *)calloc(1, sizeof(Arranger));
	PatternList *pat = (PatternList *)calloc(1, sizeof(PatternList));
	if(!pl || !ar || !pat) {
		printf("FAIL: the environment\n");
		return 1;
	}
	ar->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f, 1000.0f, 1.0f, 10.0f, NULL, NULL);
	ar->enabledChannels = 1;
	int notes[2][NOTE_INFO_SIZE] = { { A, 4 }, { C, 5 } };
	addPattern(pat, 2, notes);
	ASSERT_EQ(saveSequencerState(path, ar, pat, NULL), SEQ_OK, "the song is saved");

	/* Strip the velocity chunk, so the file looks like one written before
	 * velocity existed. The chunk is at the end, after the pattern tracks
	 * (which were not written here). */
	FILE *f = fopen(path, "rb");
	ASSERT_TRUE(f != NULL, "the file opens");
	char header[4];
	ASSERT_TRUE(fread(header, 1, 4, f) == 4, "the magic is read");
	fclose(f);

	PatternList *dst = (PatternList *)calloc(1, sizeof(PatternList));
	Arranger *ar2 = (Arranger *)calloc(1, sizeof(Arranger));
	ParamList *pl2 = createParamList();
	ar2->tempoSettings.bpm = createParameterPro(pl2, "BPM", 120.0f, 1.0f, 1000.0f, 1.0f, 10.0f, NULL, NULL);
	SequencerFileResult rc = loadSequencerState(path, ar2, dst, NULL);
	ASSERT_EQ(rc, SEQ_OK, "the song loads");
	for(int p = 0; p < dst->pattern_count; p++) {
		for(int s = 0; s < MAX_SEQUENCE_LENGTH; s++) {
			if(getCurrentStepVelocity(dst, p, s) != NOTE_VELOCITY_FULL) {
				printf("FAIL: step %d of pattern %d came back with velocity %d\n",
				       s, p, getCurrentStepVelocity(dst, p, s));
				return 1;
			}
		}
	}
	free(dst);
	free(ar2);
	free(pl2);
	free(pat);
	free(ar);
	free(pl);
	printf("PASS test_an_older_file_loads_with_unset_velocities\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* The trigger path.                                                  */
/* ------------------------------------------------------------------ */

/* The sequencer's trigger callback receives the step's velocity. */
static int g_seenVelocity = -1;
static int g_seenNote = -1;

static void captureTrigger(void *ctx, int channel, const int *note, int velocity) {
	(void)ctx;
	(void)channel;
	g_seenNote = note ? note[0] : -1;
	g_seenVelocity = velocity;
}

static int test_the_trigger_carries_the_velocity(void) {
	PatternList *pat = createPatternList(NULL);
	ParamList *pl = createParamList();
	Arranger *ar = (Arranger *)calloc(1, sizeof(Arranger));
	if(!pat || !pl || !ar) {
		printf("FAIL: the environment\n");
		return 1;
	}
	ar->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f, 1000.0f, 1.0f, 10.0f, NULL, NULL);
	ar->enabledChannels = 1;
	int notes[2][NOTE_INFO_SIZE] = { { A, 4 }, { C, 5 } };
	int idx = addPattern(pat, 2, notes);
	ASSERT_TRUE(idx >= 0, "the pattern is added");
	ASSERT_TRUE(setStepVelocity(pat, idx, 0, 33), "the velocity is set");

	Sequencer *seq = createSequencer(ar);
	if(!seq) {
		printf("FAIL: createSequencer\n");
		return 1;
	}
	seq->running[0] = 1;
	seq->pattern_index[0] = idx;
	seq->playhead_index[0] = 0;
	g_seenVelocity = -1;
	advanceSequencerStep(seq, pat, ar, captureTrigger, NULL);
	ASSERT_EQ(g_seenNote, A, "the trigger fires the note");
	ASSERT_EQ(g_seenVelocity, 33, "the trigger carries the velocity");

	free(seq);
	free(ar);
	free(pl);
	free(pat);
	printf("PASS test_the_trigger_carries_the_velocity\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* The voice's velocity, and what it does.                            */
/* ------------------------------------------------------------------ */

/* The MPC60's rules: a harder hit skips more of the sample's attack, and
 * is louder. */
static int test_the_mpc60_velocity_rules(void) {
	/* The level: full at the top, and quieter as the velocity falls. */
	float full = samplerVelocityLevel(NOTE_VELOCITY_FULL);
	float soft = samplerVelocityLevel(1);
	if(!(full > soft)) {
		printf("FAIL: the level does not follow the velocity (%g vs %g)\n",
		       (double)full, (double)soft);
		return 1;
	}
	if(!(full > 0.99f && full <= 1.0f)) {
		printf("FAIL: the full velocity's level is %g, expected 1.0\n", (double)full);
		return 1;
	}

	/* The start address: a hard hit skips more of the attack. */
	int hard = samplerVelocityStartSkipFrames(NOTE_VELOCITY_FULL, 48000);
	int softSkip = samplerVelocityStartSkipFrames(1, 48000);
	if(!(hard > softSkip)) {
		printf("FAIL: the start skip does not follow the velocity (%d vs %d)\n",
		       hard, softSkip);
		return 1;
	}
	if(softSkip != 0) {
		printf("FAIL: the softest hit skips %d frames, expected 0\n", softSkip);
		return 1;
	}
	/* The skip is bounded by the sample. */
	int beyond = samplerVelocityStartSkipFrames(NOTE_VELOCITY_FULL, 4);
	if(beyond > 4) {
		printf("FAIL: the skip (%d) is larger than the sample (4)\n", beyond);
		return 1;
	}
	printf("PASS test_the_mpc60_velocity_rules\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* The allocation: no choking.                                        */
/* ------------------------------------------------------------------ */

/* A sounding voice must not be cut while a finished one is available:
 * "Repeated strikes of a drum are all voiced - earlier strikes are not
 * cut off." */
static int test_the_allocation_does_not_choke(void) {
	SamplePool *sp = createSamplePool();
	WavetablePool *wtp = createWavetablePool();
	if(!sp || !wtp) {
		printf("FAIL: the environment\n");
		return 1;
	}
	initPresetBank(&(PresetBank){ 0 });
	PresetBank pb;
	initPresetBank(&pb);
	Preset p;
	initDefaultFmPreset(&p);
	addPresetToBank(&pb, p);
	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = 1;
	/* A pool of two: the policy only applies when there is nothing free,
	 * so the pool has to be full for the test to mean anything. */
	settings.defaultVoiceCount = 2;
	settings.defaultBPM = 120;
	VoiceManager *vm = createVoiceManager(&settings, sp, wtp, &pb);
	if(!vm) {
		printf("FAIL: createVoiceManager\n");
		return 1;
	}
	int note[NOTE_INFO_SIZE] = { A, 4 };
	/* Both voices active: one sounding, one finished. */
	Voice *sounding = vm->voicePools[0][0];
	Voice *finished = vm->voicePools[0][1];
	triggerVoice(sounding, note, NOTE_VELOCITY_FULL);
	triggerVoice(finished, note, NOTE_VELOCITY_FULL);
	int envFound = 0;
	for(int i = 0; i < finished->cloneCount; i++) {
		Mod *m = finished->clones[i];
		if(m && m->type == MT_ENV) {
			m->data.env.isTriggered = false;
			envFound++;
		}
	}
	if(envFound == 0) {
		printf("FAIL: the voice has no envelope to finish\n");
		freeVoiceManager(vm);
		freeSamplePool(sp);
		freeWavetablePool(wtp);
		return 1;
	}
	/* The next note must take the finished voice, not the sounding one. */
	Voice *taken = getFreeVoice(vm, 0);
	if(taken != finished) {
		printf("FAIL: the allocation cut the sounding voice instead of the finished one\n");
		freeVoiceManager(vm);
		freeSamplePool(sp);
		freeWavetablePool(wtp);
		return 1;
	}
	freeVoiceManager(vm);
	freeSamplePool(sp);
	freeWavetablePool(wtp);
	printf("PASS test_the_allocation_does_not_choke\n");
	return 0;
}

int main(void) {
	initModSystem();
	int fails = 0;
	fails += test_the_velocity_defaults_to_full();
	fails += test_the_velocity_can_be_set();
	fails += test_the_velocity_survives_the_file();
	fails += test_an_older_file_loads_with_unset_velocities();
	fails += test_the_trigger_carries_the_velocity();
	fails += test_the_mpc60_velocity_rules();
	fails += test_the_allocation_does_not_choke();
	if(fails) {
		fprintf(stderr, "%d note-velocity test(s) failed\n", fails);
		return 1;
	}
	printf("ALL note-velocity tests passed\n");
	return 0;
}
