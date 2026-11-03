/* test_voice_governor.c -- the runtime governor.
 *
 * The governor is the live half of criterion B: the device's budget says
 * what the machine can do, and the governor watches the callback's cost
 * and widens or narrows the number of voices that may sound.
 *
 * Two properties matter and are pinned here:
 *
 * - The decisions are hysteretic. A limit that oscillates around the
 *   threshold would make the engine's voice count flap, which is audible
 *   and which would make the golden render's scene (one voice) depend on
 *   the machine it runs on.
 * - The steal is pinned. The oldest voice goes, never a random one and
 *   never the newest, so the same notes always steal the same voice.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voice_governor.h"
#include "voice.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define ASSERT_EQ(a, b, m) do { if((a) != (b)) { \
	printf("FAIL: %s (%ld, expected %ld)\n", m, (long)(a), (long)(b)); return 1; } } while(0)

/* The defaults give the engine every voice it has. */
static int test_the_defaults_permit_everything(void) {
	VoiceGovernor g;
	governorDefaults(&g);
	ASSERT_TRUE(g.limit >= 1, "the limit is at least one voice");
	ASSERT_TRUE(g.ceiling >= g.limit, "the ceiling is at least the limit");
	ASSERT_TRUE(g.floor >= 1, "the floor is at least one voice");
	ASSERT_TRUE(g.highWatermark > g.lowWatermark, "the watermarks are ordered");
	printf("PASS test_the_defaults_permit_everything\n");
	return 0;
}

/* The limit follows the measured cost. */
static int test_the_limit_follows_the_load(void) {
	VoiceGovernor g;
	governorInit(&g, 8);
	ASSERT_EQ(g.limit, 8, "the limit starts at the budget's voices");

	/* A comfortable callback widens the limit towards the ceiling. */
	for(int i = 0; i < 200; i++) {
		governorObserve(&g, 0.1);
	}
	ASSERT_TRUE(g.limit >= 8, "a comfortable engine keeps its voices");

	/* A callback that overruns narrows it. */
	for(int i = 0; i < 200; i++) {
		governorObserve(&g, 0.95);
	}
	ASSERT_TRUE(g.limit < 8, "an overloaded engine gives voices back");
	ASSERT_TRUE(g.limit >= g.floor, "the limit never falls below the floor");
	printf("PASS test_the_limit_follows_the_load\n");
	return 0;
}

/* The decision is hysteretic: a load that sits between the watermarks
 * leaves the limit alone, and a load that oscillates around a watermark
 * does not flap the limit. */
static int test_the_limit_does_not_oscillate(void) {
	VoiceGovernor g;
	governorInit(&g, 8);
	/* Settle into a comfortable state. */
	for(int i = 0; i < 100; i++) {
		governorObserve(&g, 0.2);
	}
	int settled = g.limit;
	/* A load between the watermarks changes nothing. */
	for(int i = 0; i < 100; i++) {
		governorObserve(&g, (g.highWatermark + g.lowWatermark) / 2.0);
	}
	ASSERT_EQ(g.limit, settled, "a load between the watermarks changes nothing");

	/* A load that oscillates across the high watermark does not flap:
	 * the limit moves at most one step per observation and only after
	 * the smoothed value crosses. */
	int changes = 0;
	int previous = g.limit;
	for(int i = 0; i < 200; i++) {
		governorObserve(&g, (i % 2) ? 0.99 : 0.3);
		if(g.limit != previous) {
			changes++;
			previous = g.limit;
		}
	}
	/* The smoothed value stays low, so the limit must not have moved at
	 * all: the oscillation is in the instantaneous value, and the
	 * decision is made on the smoothed one. */
	ASSERT_EQ(changes, 0, "an oscillating load does not move the limit");
	printf("PASS test_the_limit_does_not_oscillate\n");
	return 0;
}

/* The floor and the ceiling bind. */
static int test_the_limits_bind(void) {
	VoiceGovernor g;
	governorInit(&g, 4);
	for(int i = 0; i < 1000; i++) {
		governorObserve(&g, 1.0);
	}
	ASSERT_TRUE(g.limit >= g.floor, "the floor holds under a permanent overload");
	for(int i = 0; i < 1000; i++) {
		governorObserve(&g, 0.0);
	}
	ASSERT_TRUE(g.limit <= g.ceiling, "the ceiling holds under a permanent idle");
	printf("PASS test_the_limits_bind\n");
	return 0;
}

/* The counters record what the governor did. */
static int test_the_counters_record_what_happened(void) {
	VoiceGovernor g;
	governorInit(&g, 4);
	ASSERT_EQ(g.stolen, 0, "nothing stolen to start with");
	governorNoteStolen(&g, 4711);
	ASSERT_EQ(g.stolen, 1, "a steal is counted");
	ASSERT_EQ(g.lastStolenAge, 4711, "the stolen voice's age is recorded");
	printf("PASS test_the_counters_record_what_happened\n");
	return 0;
}

/* A governor that is out of force ignores the load: the limit stays where
 * it was put. This is what keeps the golden render independent of how
 * fast the machine it runs on is. */
static int test_a_disabled_governor_ignores_the_load(void) {
	VoiceGovernor g;
	governorInit(&g, 8);
	governorSetEnabled(&g, false);
	for(int i = 0; i < 500; i++) {
		governorObserve(&g, 1.0);
	}
	ASSERT_EQ(governorLimit(&g), 8, "a disabled governor does not narrow the limit");
	governorSetEnabled(&g, true);
	for(int i = 0; i < 500; i++) {
		governorObserve(&g, 1.0);
	}
	ASSERT_TRUE(governorLimit(&g) < 8, "an enabled one does");
	printf("PASS test_a_disabled_governor_ignores_the_load\n");
	return 0;
}

/* The steal picks the oldest voice: the one that has been sounding
 * longest. */
static int test_the_steal_picks_the_oldest(void) {
	long ages[4] = { 100, 5000, 200, 900 };
	/* The oldest is the largest age. */
	ASSERT_EQ(governorPickVictim(ages, NULL, 4), 1, "the oldest voice is stolen");
	printf("PASS test_the_steal_picks_the_oldest\n");
	return 0;
}

/* With one voice, that voice is the victim. */
static int test_the_steal_picks_the_only_voice(void) {
	long ages[1] = { 42 };
	ASSERT_EQ(governorPickVictim(ages, NULL, 1), 0, "the only voice is stolen");
	printf("PASS test_the_steal_picks_the_only_voice\n");
	return 0;
}

/* With no voices, there is nothing to steal. */
static int test_the_steal_finds_nothing(void) {
	ASSERT_EQ(governorPickVictim(NULL, NULL, 0), GOVERNOR_VICTIM_NONE, "no voice, no victim");
	long ages[2] = { 1, 2 };
	ASSERT_EQ(governorPickVictim(ages, NULL, 2) >= 0, 1, "there is a victim");
	printf("PASS test_the_steal_finds_nothing\n");
	return 0;
}

/* The victim is pinned: the same ages always pick the same voice, and a
 * tie goes to the lowest index (so the choice does not depend on the
 * order the voices happen to be listed in). */
static int test_the_steal_is_pinned(void) {
	long ages[3] = { 100, 100, 50 };
	ASSERT_EQ(governorPickVictim(ages, NULL, 3), 0, "a tie goes to the lowest index");
	long other[3] = { 50, 100, 100 };
	ASSERT_EQ(governorPickVictim(other, NULL, 3), 1, "the oldest is the same voice whatever the order");
	printf("PASS test_the_steal_is_pinned\n");
	return 0;
}


/* ------------------------------------------------------------------ */
/* The governor's place in the engine: the voice allocation.          */
/* ------------------------------------------------------------------ */

typedef struct {
	VoiceManager *vm;
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
} GovEnv;

static int govSetup(GovEnv *e, int voices) {
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
	settings.defaultVoiceCount = voices;
	settings.defaultBPM = 120;
	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	if(!e->vm) {
		return 1;
	}
	return 0;
}

static void govTeardown(GovEnv *e) {
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

/* Start a voice on the channel and age it by `age` samples, so the steal
 * policy has something to choose between. */
static Voice *govStart(GovEnv *e, int age) {
	int note[NOTE_INFO_SIZE] = { A, 4 };
	Voice *v = getFreeVoice(e->vm, 0);
	if(!v) {
		return NULL;
	}
	triggerVoice(v, note, NOTE_VELOCITY_FULL);
	v->samplesElapsed = age;
	return v;
}

/* Under the limit, a note takes a free voice. */
static int test_a_note_takes_a_free_voice(void) {
	GovEnv e;
	if(govSetup(&e, 4) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	governorInit(&e.vm->governor, 4);
	Voice *a = govStart(&e, 100);
	Voice *b = govStart(&e, 200);
	if(!a || !b) {
		printf("FAIL: could not start the voices\n");
		govTeardown(&e);
		return 1;
	}
	if(a == b) {
		printf("FAIL: two notes took the same voice\n");
		govTeardown(&e);
		return 1;
	}
	if(e.vm->governor.stolen != 0) {
		printf("FAIL: a voice was stolen while the limit allowed room\n");
		govTeardown(&e);
		return 1;
	}
	govTeardown(&e);
	printf("PASS test_a_note_takes_a_free_voice\n");
	return 0;
}

/* At the limit, a note steals a voice -- and it steals the oldest one, so
 * the choice is pinned rather than arbitrary. */
static int test_a_note_at_the_limit_steals_the_oldest(void) {
	GovEnv e;
	if(govSetup(&e, 4) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	governorInit(&e.vm->governor, 2);
	Voice *oldest = govStart(&e, 5000);
	Voice *newer = govStart(&e, 10);
	if(!oldest || !newer || oldest == newer) {
		printf("FAIL: could not start the voices\n");
		govTeardown(&e);
		return 1;
	}
	/* The third note is over the limit: it must take the oldest voice. */
	Voice *third = govStart(&e, 0);
	if(third != oldest) {
		printf("FAIL: the steal took the wrong voice (expected the oldest)\\n");
		govTeardown(&e);
		return 1;
	}
	if(e.vm->governor.stolen != 1) {
		printf("FAIL: the steal was not counted\\n");
		govTeardown(&e);
		return 1;
	}
	if(e.vm->governor.lastStolenAge != 5000) {
		printf("FAIL: the stolen voice's age was recorded as %ld, expected 5000\\n",
		       e.vm->governor.lastStolenAge);
		govTeardown(&e);
		return 1;
	}
	govTeardown(&e);
	printf("PASS test_a_note_at_the_limit_steals_the_oldest\n");
	return 0;
}

/* A steal must not touch the voice that survives it: the surviving voice
 * keeps its note, its age and its parameters, so its output is unchanged
 * (criterion C). */
static int test_a_steal_leaves_the_survivor_alone(void) {
	GovEnv e;
	if(govSetup(&e, 4) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	governorInit(&e.vm->governor, 2);
	Voice *oldest = govStart(&e, 5000);
	Voice *survivor = govStart(&e, 77);
	if(!oldest || !survivor || oldest == survivor) {
		printf("FAIL: could not start the voices\n");
		govTeardown(&e);
		return 1;
	}
	int noteBefore = survivor->note[0];
	int octaveBefore = survivor->note[1];
	float volumeBefore = getParameterValue(survivor->volume);

	/* Steal the oldest. */
	govStart(&e, 0);

	if(survivor->note[0] != noteBefore || survivor->note[1] != octaveBefore) {
		printf("FAIL: the steal changed the survivor's note\\n");
		govTeardown(&e);
		return 1;
	}
	if(getParameterValue(survivor->volume) != volumeBefore) {
		printf("FAIL: the steal changed the survivor's volume\\n");
		govTeardown(&e);
		return 1;
	}
	govTeardown(&e);
	printf("PASS test_a_steal_leaves_the_survivor_alone\n");
	return 0;
}

/* A governor that is out of force changes nothing: the pool's own
 * behaviour is used, and nothing is stolen by the governor. */
static int test_a_disabled_governor_does_not_steal(void) {
	GovEnv e;
	if(govSetup(&e, 2) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	governorInit(&e.vm->governor, 1);
	governorSetEnabled(&e.vm->governor, false);
	govStart(&e, 100);
	govStart(&e, 200);
	if(e.vm->governor.stolen != 0) {
		printf("FAIL: a disabled governor stole a voice\\n");
		govTeardown(&e);
		return 1;
	}
	govTeardown(&e);
	printf("PASS test_a_disabled_governor_does_not_steal\n");
	return 0;
}

/* The governor's ceiling follows the pool's size.
 *
 * The user dials the voice count up; the pool grows; and the governor
 * must let those voices sound. Without this the governor keeps the count
 * it was built with, the pool's dial does nothing, and a second note
 * steals the first one even though there was room for it. */
static int test_the_ceiling_follows_the_pool(void) {
	GovEnv e;
	if(govSetup(&e, 1) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	/* The governor starts at the pool's size. */
	ASSERT_EQ(governorLimit(&e.vm->governor), 1, "the governor starts at the pool's size");

	/* The user dials the voice count up to four. */
	ASSERT_TRUE(setChannelVoiceCount(e.vm, 0, 4), "the voice count is set");
	ASSERT_EQ(e.vm->voiceCount[0], 4, "the pool holds four voices");
	ASSERT_EQ(governorLimit(&e.vm->governor), 4, "the governor's limit follows the pool");

	/* Two simultaneous notes must get two voices, not the same one. */
	Voice *a = govStart(&e, 100);
	Voice *b = govStart(&e, 200);
	if(!a || !b || a == b) {
		printf("FAIL: two notes took the same voice\n");
		govTeardown(&e);
		return 1;
	}
	govTeardown(&e);
	printf("PASS test_the_ceiling_follows_the_pool\n");
	return 0;
}

/* And the other way: a pool that shrank cannot sound more voices than it
 * holds. */
static int test_a_shrunken_pool_lowers_the_limit(void) {
	GovEnv e;
	if(govSetup(&e, 4) != 0) {
		printf("FAIL: could not build the scene\n");
		return 1;
	}
	ASSERT_TRUE(setChannelVoiceCount(e.vm, 0, 2), "the voice count is set");
	ASSERT_EQ(governorLimit(&e.vm->governor), 2, "the limit follows the pool down");
	govTeardown(&e);
	printf("PASS test_a_shrunken_pool_lowers_the_limit\n");
	return 0;
}

/* The ceiling is clamped: a pool of zero voices would leave the channel
 * with nothing to sound, and more than the maximum is not a pool size
 * the engine has. */
static int test_the_ceiling_is_clamped(void) {
	VoiceGovernor g;
	governorInit(&g, 4);
	governorSetCeiling(&g, 0);
	ASSERT_EQ(g.ceiling, 1, "a ceiling of zero becomes one");
	governorSetCeiling(&g, 999);
	ASSERT_EQ(g.ceiling, GOVERNOR_MAX_VOICES, "a ceiling past the maximum is clamped");
	printf("PASS test_the_ceiling_is_clamped\n");
	return 0;
}

/* A negative utilisation is not a thing: it would be a measurement that
 * went wrong, and it must not pull the smoothed value below zero. */
static int test_a_negative_utilisation_is_clamped(void) {
	VoiceGovernor g;
	governorInit(&g, 4);
	governorObserve(&g, -0.5);
	ASSERT_TRUE(g.lastUtilization >= 0.0, "the measured value is not negative");
	printf("PASS test_a_negative_utilisation_is_clamped\n");
	return 0;
}

/* The narrowing stops at the floor: the engine always keeps at least one
 * voice, however badly the machine is doing. */
static int test_the_narrowing_stops_at_the_floor(void) {
	VoiceGovernor g;
	governorInit(&g, 2);
	for(int i = 0; i < 500; i++) {
		governorObserve(&g, 1.0);
	}
	ASSERT_EQ(governorLimit(&g), 1, "the limit is the floor");
	ASSERT_EQ(g.narrowing, 1, "the narrowing stops at the ceiling less the floor");
	printf("PASS test_the_narrowing_stops_at_the_floor\n");
	return 0;
}

int main(void) {
	initModSystem();
	int fails = 0;
	fails += test_the_defaults_permit_everything();
	fails += test_the_limit_follows_the_load();
	fails += test_the_limit_does_not_oscillate();
	fails += test_the_limits_bind();
	fails += test_the_counters_record_what_happened();
	fails += test_a_disabled_governor_ignores_the_load();
	fails += test_a_note_takes_a_free_voice();
	fails += test_a_note_at_the_limit_steals_the_oldest();
	fails += test_a_steal_leaves_the_survivor_alone();
	fails += test_a_disabled_governor_does_not_steal();
	fails += test_the_ceiling_follows_the_pool();
	fails += test_a_shrunken_pool_lowers_the_limit();
	fails += test_the_ceiling_is_clamped();
	fails += test_a_negative_utilisation_is_clamped();
	fails += test_the_narrowing_stops_at_the_floor();
	fails += test_the_steal_picks_the_oldest();
	fails += test_the_steal_picks_the_only_voice();
	fails += test_the_steal_finds_nothing();
	fails += test_the_steal_is_pinned();
	if(fails) {
		fprintf(stderr, "%d voice-governor test(s) failed\n", fails);
		return 1;
	}
	printf("ALL voice-governor tests passed\n");
	return 0;
}
