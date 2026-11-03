#include "voice_governor.h"

#include <string.h>

/* How fast the smoothed utilisation follows the measured one. A small
 * factor means the decision is made on a longer average, which is what
 * keeps the limit from flapping around a watermark. */
#define GOVERNOR_SMOOTHING 0.15

/* The default watermarks: the fraction of the deadline above which the
 * engine is "too busy" and below which it is "comfortable". The gap
 * between them is the hysteresis band. */
#define GOVERNOR_HIGH_WATERMARK 0.85
#define GOVERNOR_LOW_WATERMARK 0.60

void governorDefaults(VoiceGovernor *g) {
	if(!g) {
		return;
	}
	memset(g, 0, sizeof(*g));
	g->ceiling = GOVERNOR_MAX_VOICES;
	g->limit = GOVERNOR_MAX_VOICES;
	g->floor = 1;
	g->highWatermark = GOVERNOR_HIGH_WATERMARK;
	g->lowWatermark = GOVERNOR_LOW_WATERMARK;
	g->smoothedUtilization = 0.0;
	g->lastUtilization = 0.0;
	g->refused = 0;
	g->stolen = 0;
	g->lastStolenAge = GOVERNOR_VICTIM_NONE;
	g->enabled = true;
}

void governorInit(VoiceGovernor *g, int voices) {
	governorDefaults(g);
	if(!g) {
		return;
	}
	if(voices < 1) {
		voices = 1;
	}
	if(voices > GOVERNOR_MAX_VOICES) {
		voices = GOVERNOR_MAX_VOICES;
	}
	g->limit = voices;
	g->ceiling = voices;
	g->floor = 1;
}

void governorObserve(VoiceGovernor *g, double utilization) {
	if(!g || !g->enabled) {
		return;
	}
	if(utilization < 0.0) {
		utilization = 0.0;
	}
	g->lastUtilization = utilization;
	g->smoothedUtilization += (utilization - g->smoothedUtilization) * GOVERNOR_SMOOTHING;

	if(g->smoothedUtilization > g->highWatermark) {
		if(g->limit > g->floor) {
			g->limit--;
		}
	} else if(g->smoothedUtilization < g->lowWatermark) {
		if(g->limit < g->ceiling) {
			g->limit++;
		}
	}
}

int governorLimit(const VoiceGovernor *g) {
	if(!g) {
		return 1;
	}
	return g->limit;
}

void governorSetEnabled(VoiceGovernor *g, bool enabled) {
	if(g) {
		g->enabled = enabled;
	}
}

void governorNoteRefused(VoiceGovernor *g) {
	if(g) {
		g->refused++;
	}
}

void governorNoteStolen(VoiceGovernor *g, long age) {
	if(g) {
		g->stolen++;
		g->lastStolenAge = age;
	}
}

long governorPickVictim(const long *ages, int count) {
	if(!ages || count <= 0) {
		return GOVERNOR_VICTIM_NONE;
	}
	long best = -1;
	long bestAge = -1;
	for(int i = 0; i < count; i++) {
		/* The oldest voice -- the one that has been sounding longest --
		 * goes. A tie goes to the lowest index, so the choice does not
		 * depend on the order the voices happen to be listed in. */
		if(ages[i] > bestAge) {
			bestAge = ages[i];
			best = i;
		}
	}
	return best;
}
