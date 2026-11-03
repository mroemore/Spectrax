#ifndef SPECTRAX_VOICE_GOVERNOR_H
#define SPECTRAX_VOICE_GOVERNOR_H

#include <stdbool.h>

/* The runtime governor: how many voices the engine may have sounding, and
 * how that number moves.
 *
 * The device's budget (src/device_budget.h) says what the machine can do
 * at all -- measured once, at startup. The governor is the *live* half: it
 * watches the callback's cost and widens or narrows the number of voices
 * that may sound, so a song that gets busier than the budget assumed does
 * not turn into a dropout.
 *
 * Two rules make this safe under criterion C (a voice's output is a pure
 * function of its settings):
 *
 * - The governor only ever changes *how many* voices sound. It never
 *   touches a voice's parameters, its phase or its envelope, so a voice
 *   that survives the governor's decision sounds exactly as it would have
 *   without it.
 * - The steal is pinned and deterministic: the *oldest* voice on the
 *   channel goes, never a random one and never the newest. The same
 *   sequence of notes therefore always steals the same voice, on every
 *   platform and every run.
 *
 * The governor's decisions are about *this* machine's load, so they are
 * not part of the canonical output: the golden render's scene has one
 * voice, and a voice's own samples do not depend on how many other voices
 * are sounding (that is what tests/dsp/test_voice_isolation.c pins).
 */

#define GOVERNOR_MAX_VOICES 16

typedef struct {
	/* How many voices may sound at once on a channel. */
	int limit;
	/* The largest the limit may grow to. */
	int ceiling;
	/* The smallest it may shrink to: at least one voice, or the engine
	 * would go silent rather than merely degraded. */
	int floor;
	/* The fraction of the callback's deadline above which the governor
	 * narrows the limit, and the fraction below which it widens it. The
	 * gap between them is the hysteresis: the limit does not oscillate
	 * around the threshold. */
	double highWatermark;
	double lowWatermark;
	/* The measured callback cost, as a fraction of the deadline, and the
	 * smoothed value the decision is made on. */
	double lastUtilization;
	double smoothedUtilization;
	/* How many notes have been asked for and refused because the limit
	 * was reached. */
	unsigned long refused;
	/* How many voices have been stolen to make room. */
	unsigned long stolen;
	/* The age of the voice stolen last, for the tests. */
	long lastStolenAge;
	/* Whether the governor is in force. When it is not, the limit stays
	 * where it is and the measurements are ignored.
	 *
	 * This exists for the golden render: that test compares a machine's
	 * output with another machine's, so the number of voices it plays
	 * must not depend on how fast the machine is. The governor is a load
	 * policy -- exactly the thing the golden render must not include. */
	bool enabled;
} VoiceGovernor;

/* The governor's defaults: no limit below the ceiling, and the
 * watermarks at the fractions of the deadline that mean "too busy" and
 * "comfortable". */
void governorDefaults(VoiceGovernor *g);

/* The governor's state for a device whose budget says `voices` voices may
 * sound at once. */
void governorInit(VoiceGovernor *g, int voices);

/* Report the callback's cost for the buffer just rendered.
 *
 * `utilization` is the fraction of the deadline the callback used (0.0 for
 * an idle callback, 1.0 for one that exactly filled its time). The
 * smoothed value moves towards it, and the limit follows the smoothed
 * value: it widens when the engine is comfortable and narrows when it is
 * not. */
void governorObserve(VoiceGovernor *g, double utilization);

/* The current limit: how many voices may sound at once on a channel. */
int governorLimit(const VoiceGovernor *g);

/* Put the governor in force, or take it out of force. */
void governorSetEnabled(VoiceGovernor *g, bool enabled);

/* Note that a note was refused because the limit was reached. */
void governorNoteRefused(VoiceGovernor *g);

/* Note that a voice was stolen to make room, and how old it was. */
void governorNoteStolen(VoiceGovernor *g, long age);

/* The age of the voice that should be stolen to make room: the oldest
 * sounding voice on the channel, which is the one that has been sounding
 * longest. The ages come from the caller, because the governor does not
 * know about voices.
 *
 * Returns -1 when there is nothing to steal. */
/* The ages a caller should offer: how long each sounding voice has been
 * sounding, in samples. The oldest (the largest) goes first.
 *
 * `finished` says which of those voices have already finished -- their
 * envelope has run out, so they are silent but still hold their slot.
 * A finished voice is taken before a sounding one: the corpus's MPC60
 * rule is "no choking. Repeated strikes of a drum are all voiced -
 * earlier strikes are not cut off", so a voice that is still sounding
 * must not be sacrificed while a silent one is available.
 *
 * The order is total and pinned: finished before sounding, and within
 * each group the oldest (the largest age) first. The same set of voices
 * therefore always picks the same victim, on every platform and run. */
long governorPickVictim(const long *ages, const bool *finished, int count);

#define GOVERNOR_VICTIM_NONE (-1)

#endif
