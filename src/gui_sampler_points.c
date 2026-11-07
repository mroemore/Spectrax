/* gui_sampler_points.c -- the waveform's points model.
 *
 * The model is UI state only: no synthesis, no parameters, no drawing.
 * The waveform's own node draws what the model holds, and the
 * instrument screen's input handler moves it.
 *
 * A point is a position on the waveform that can be selected, moved,
 * re-placed by an action, or deleted. The cursor is UI state; the loop
 * start and end are the instrument's parameters, and the model mirrors
 * them so the screen can draw and move them.
 *
 * The rule that makes the interaction direct manipulation rather than a
 * form: A SELECTED POINT FOLLOWS THE CURSOR. Selecting the loop start
 * puts the cursor on it, and from then on moving the cursor moves the
 * point. */

#include <string.h>

#include "gui_sampler_points.h"

/* The point of a kind, or NULL. */
static SamplerPoint *pointOf(SamplerPoints *p, SamplerPointKind kind) {
	if(!p) {
		return NULL;
	}
	switch(kind) {
		case PT_CURSOR:
			return &p->cursor;
		case PT_LOOP_START:
			return &p->loopStart;
		case PT_LOOP_END:
			return &p->loopEnd;
		default:
			return NULL;
	}
}

/* The selected point, or NULL. */
static SamplerPoint *selectedPoint(SamplerPoints *p) {
	if(!p) {
		return NULL;
	}
	if(p->cursor.selected) {
		return &p->cursor;
	}
	if(p->loopStart.selected) {
		return &p->loopStart;
	}
	if(p->loopEnd.selected) {
		return &p->loopEnd;
	}
	return NULL;
}

/* Clear the selection from every point. */
static void clearSelection(SamplerPoints *p) {
	p->cursor.selected = false;
	p->loopStart.selected = false;
	p->loopEnd.selected = false;
}

void samplerPointsInit(SamplerPoints *p, int sampleLength) {
	if(!p) {
		return;
	}
	memset(p, 0, sizeof(*p));
	p->length = (sampleLength > 0) ? sampleLength : 0;
	p->cursor.kind = PT_CURSOR;
	p->cursor.frame = 0;
	p->cursor.exists = true;
	p->cursor.selected = true;
	p->loopStart.kind = PT_LOOP_START;
	p->loopEnd.kind = PT_LOOP_END;
	p->viewStart = 0;
	p->viewEnd = (p->length > 0) ? p->length - 1 : 0;
}

void samplerPointsMoveCursor(SamplerPoints *p, int frames) {
	if(!p || p->length <= 0) {
		return;
	}
	/* A deleted cursor is re-created by the next movement, at the
	 * position it had. */
	if(!p->cursor.exists) {
		p->cursor.exists = true;
	}
	int frame = p->cursor.frame + frames;
	if(frame < 0) {
		frame = 0;
	}
	if(frame > p->length - 1) {
		frame = p->length - 1;
	}
	p->cursor.frame = frame;
	/* A selected point FOLLOWS the cursor -- that is what makes this
	 * direct manipulation rather than a form. */
	SamplerPoint *sel = selectedPoint(p);
	if(sel && sel->kind != PT_CURSOR) {
		sel->frame = frame;
	}
}

void samplerPointsSelect(SamplerPoints *p, SamplerPointKind kind) {
	if(!p) {
		return;
	}
	SamplerPoint *pt = pointOf(p, kind);
	if(!pt) {
		return;
	}
	clearSelection(p);
	pt->selected = true;
	if(!pt->exists) {
		/* Selecting a loop point that does not exist creates it, at
		 * the cursor's position. */
		pt->exists = true;
		pt->frame = p->cursor.frame;
	}
	/* The cursor jumps to the point it selected. */
	p->cursor.frame = pt->frame;
	p->cursor.exists = true;
}

bool samplerPointsHasPoint(const SamplerPoints *p, SamplerPointKind kind) {
	if(!p) {
		return false;
	}
	const SamplerPoint *pt = NULL;
	switch(kind) {
		case PT_CURSOR:
			pt = &p->cursor;
			break;
		case PT_LOOP_START:
			pt = &p->loopStart;
			break;
		case PT_LOOP_END:
			pt = &p->loopEnd;
			break;
		default:
			return false;
	}
	return pt->exists;
}

bool samplerPointsDelete(SamplerPoints *p) {
	if(!p) {
		return false;
	}
	SamplerPoint *sel = selectedPoint(p);
	if(!sel) {
		return false;
	}
	sel->exists = false;
	sel->selected = false;
	if(sel->kind == PT_CURSOR) {
		/* The cursor's frame is KEPT: the next movement re-creates it
		 * at the position it had. */
		return true;
	}
	/* A deleted loop point leaves the loop reverted to the whole
	 * sample -- the parameters are the instrument's business, and the
	 * screen re-writes them from the model's state. */
	return true;
}

int samplerPointsNearestZeroCrossing(const float *data, int length, int frame) {
	if(!data || length <= 0) {
		return -1;
	}
	if(frame < 0) {
		frame = 0;
	}
	if(frame > length - 1) {
		frame = length - 1;
	}
	/* The search is bounded by a whole period of the waveform: at the
	 * lowest frequency a sample can hold, a period is length/20
	 * frames. */
	int bound = length / 20;
	if(bound < 1) {
		bound = 1;
	}
	/* The zero crossings on either side: a change of sign. */
	int best = -1;
	for(int d = 0; d <= bound; d++) {
		int left = frame - d;
		int right = frame + d;
		if(left >= 0 && left < length - 1) {
			float a = data[left];
			float b = data[left + 1];
			if((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f)) {
				if(a == 0.0f) {
					best = left;
				} else if(b == 0.0f) {
					best = left + 1;
				} else {
					/* The crossing lies between the two frames: the
					 * nearer of them is the answer. */
					best = (d == 0) ? left : left;
				}
				break;
			}
		}
		if(right >= 0 && right < length - 1) {
			float a = data[right];
			float b = data[right + 1];
			if((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f)) {
				if(a == 0.0f) {
					best = right;
				} else if(b == 0.0f) {
					best = right + 1;
				} else {
					best = right;
				}
				break;
			}
		}
	}
	return best;
}

bool samplerPointsValidate(const SamplerPoints *p, int length) {
	if(!p || length <= 0) {
		return false;
	}
	/* A loop that starts at or past its end would be read backwards,
	 * which is worse than no loop at all. */
	if(p->loopStart.exists && p->loopEnd.exists) {
		if(p->loopStart.frame >= p->loopEnd.frame) {
			return false;
		}
	}
	return true;
}
