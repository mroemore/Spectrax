#ifndef SPECTRAX_GUI_SAMPLER_POINTS_H
#define SPECTRAX_GUI_SAMPLER_POINTS_H

#include <stdbool.h>

/* The points on the sampler's waveform.
 *
 * A point is a position on the waveform that can be selected, moved,
 * re-placed by an action, or deleted. There are three kinds: the
 * CURSOR, the LOOP START and the LOOP END.
 *
 * The cursor is UI state: it is not a parameter and is not saved. The
 * loop start and end ARE the instrument's parameters, saved in the
 * preset and modulatable -- only their PRESENTATION changes. */

/* The kinds of point. */
typedef enum {
	PT_CURSOR,
	PT_LOOP_START,
	PT_LOOP_END
} SamplerPointKind;

typedef struct {
	SamplerPointKind kind;
	/* The point's position, in the source's frames. */
	int frame;
	/* This point is the selected one. */
	bool selected;
	/* The point exists. The cursor always does (until it is deleted);
	 * the loop points exist only once they have been created. */
	bool exists;
} SamplerPoint;

/* The waveform's point state. */
typedef struct {
	SamplerPoint cursor;
	SamplerPoint loopStart;
	SamplerPoint loopEnd;
	/* The visible window, in source frames. */
	int viewStart;
	int viewEnd;
	/* The sample's length, in source frames. The cursor is clamped to
	 * it. */
	int length;
	/* FUNCTION was pressed since the last EDIT.
	 *
	 * The two FUNCTION gestures are told apart by this: FUNCTION HELD
	 * when EDIT fires is the CHORD (it selects the point the button
	 * names), and FUNCTION pressed-and-released before EDIT is the
	 * SEQUENCE (it deletes the selected point).
	 *
	 * The flag lives here rather than in the input's history ring,
	 * because that ring is appended to only when something QUERIES a
	 * key's just-pressed state -- so its contents depend on the query
	 * order rather than on the player's hands. */
	bool functionPressed;
} SamplerPoints;

/* Set the points up for a sample of `sampleLength` frames: the cursor
 * at the start, no loop points, the whole sample in view. */
void samplerPointsInit(SamplerPoints *p, int sampleLength);

/* Move the cursor by `frames` (negative is backwards), clamped to the
 * sample. A selected point follows the cursor.
 *
 * A deleted cursor is re-created by the next movement, at the position
 * it had. */
void samplerPointsMoveCursor(SamplerPoints *p, int frames);

/* Select the loop start or the loop end, creating it if it does not
 * exist (at the cursor's position), and put the cursor on it. */
void samplerPointsSelect(SamplerPoints *p, SamplerPointKind kind);

/* True when the kind's point exists (the cursor always does; the loop
 * points exist only once they have been created). */
bool samplerPointsHasPoint(const SamplerPoints *p, SamplerPointKind kind);

/* Delete the selected point. Deleting the cursor removes the cursor
 * marker; the next movement re-creates it at the position it had.
 * Deleting a loop point removes it, and the loop reverts to the whole
 * sample. */
bool samplerPointsDelete(SamplerPoints *p);

/* The nearest zero crossing to `frame`, in source frames; -1 when the
 * waveform has none within a whole period. */
int samplerPointsNearestZeroCrossing(const float *data, int length, int frame);

/* True when the points are usable: the loop must not start at or past
 * its end. A loop read backwards is worse than no loop, so the state is
 * rejected rather than normalised. */
bool samplerPointsValidate(const SamplerPoints *p, int length);

#endif
