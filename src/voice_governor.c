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

void governorSetCeiling(VoiceGovernor *g, int voices) {
	if(!g) {
		return;
	}
	if(voices < 1) {
		voices = 1;
	}
	if(voices > GOVERNOR_MAX_VOICES) {
		voices = GOVERNOR_MAX_VOICES;
	}
	g->ceiling = voices;
	/* The limit is the ceiling minus the narrowing, so a pool that grew
	 * allows more voices without handing back the ones the governor took
	 * for the load. */
	g->limit = voices - g->narrowing;
	if(g->limit < g->floor) {
		g->limit = g->floor;
	}
	if(g->limit > g->ceiling) {
		g->limit = g->ceiling;
	}
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
		/* The machine is struggling: take a voice away. */
		if(g->narrowing < g->ceiling - g->floor) {
			g->narrowing++;
		}
	} else if(g->smoothedUtilization < g->lowWatermark) {
		/* It is comfortable: give one back. */
		if(g->narrowing > 0) {
			g->narrowing--;
		}
	}
	g->limit = g->ceiling - g->narrowing;
	if(g->limit < g->floor) {
		g->limit = g->floor;
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

void governorNoteStolen(VoiceGovernor *g, long age) {
	if(g) {
		g->stolen++;
		g->lastStolenAge = age;
	}
}

long governorPickVictim(const long *ages, const bool *finished, int count) {
	if(!ages || count <= 0) {
		return GOVERNOR_VICTIM_NONE;
	}
	long best = -1;
	long bestAge = -1;
	bool bestFinished = false;
	for(int i = 0; i < count; i++) {
		bool isFinished = finished ? finished[i] : false;
		/* A finished voice goes first: it is silent, so cutting it costs
		 * nothing audible, while a sounding voice's note is cut off. */
		if(best < 0 || (isFinished && !bestFinished) ||
		   (isFinished == bestFinished && ages[i] > bestAge)) {
			bestAge = ages[i];
			bestFinished = isFinished;
			best = i;
		}
	}
	return best;
}
