/* gui_inst_sample.c -- the SAMPLE instrument's control panel: the dial
 * rows plus the waveform visualiser node.
 * Cross-module state/helpers: see gui_internal.h / gui_inst_internal.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dstruct.h"
#include "raylib.h"
#include "gui.h"
#include "graph_gui.h"
#include "input.h"
#include "modsystem.h"
#include "notes.h"
#include "voice.h"
#include "io/preset_io.h"
#include "sample.h"
#include "io.h"
#include "gui_internal.h"
#include "gui_inst_internal.h"

/* Scratch capacity for the per-column peak envelope. The instrument
 * screen renders at 640px wide, so this covers any node width; if a node
 * is ever wider the envelope is built at this resolution and stretched. */
#define WAVEFORM_MAX_COLS 1024
#define WAVEFORM_NAME_FONT 9
#define WAVEFORM_TAG_FONT 9
#define WAVEFORM_ZOOM_FACTOR 8.0f

void drawSampleWaveLinesNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!arrangerMixRing) {
		return;
	}
	drawSampleWaveLines(arrangerMixRing, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
	DrawTextEx(pixelFont, "VLINE", (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, cs.vline);
}


void drawSampleWavePolylineNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!arrangerMixRing) {
		return;
	}
	drawSampleWavePolyline(arrangerMixRing, (Rectangle){ gn->x, gn->y, gn->w, gn->h });
	DrawTextEx(pixelFont, "POLY", (Vector2){ gn->x + 2, gn->y + 2 }, 9, 1, cs.poly);
}


static void drawSampleWaveformGuiNode(void *self);

/* The waveform's cursor movement -- defined below, with the options
 * menu. */
static bool modLineWaveNav(void *self, int keymapping);

/* The points are written to the instrument's loop parameters. */
static void writePointsToParameters(SampleWaveformGuiNode *swgn);

/* The instrument's waveform node, or NULL. The points live on it. */
static SampleWaveformGuiNode *findWaveformNode(Instrument *inst);

/* Find a node by name in a graph. */
static GuiNode *findNodeByName(GuiNode *n, const char *name) {
	if(!n) {
		return NULL;
	}
	if(n->name && strcmp(n->name, name) == 0) {
		return n;
	}
	if(n->items) {
		ListElement *le = n->items->head;
		for(int i = 0; i < n->itemCount && le; i++, le = le->next) {
			GuiNode *hit = findNodeByName(*(GuiNode **)le->data, name);
			if(hit) {
				return hit;
			}
		}
	}
	return NULL;
}

/* The points are the source of truth while they are being edited, so the
 * START and END buttons' captions are refreshed from them -- otherwise
 * they keep showing the value the point had when the panel was built,
 * and the user cannot see where the point went. */
static void refreshPointCaptions(SampleWaveformGuiNode *swgn) {
	if(!swgn || !swgn->instrument) {
		return;
	}
	Graph *g = getSelectedInstGraph();
	if(!g || !g->root) {
		return;
	}
	int rate = (swgn->instrument->id.sampler.sample
	            && swgn->instrument->id.sampler.sample->sampleRate > 0)
	           ? swgn->instrument->id.sampler.sample->sampleRate
	           : SPECTRAX_ENGINE_RATE;
	char buf[48];
	GuiNode *start = findNodeByName(g->root, "START");
	if(start) {
		snprintf(buf, sizeof(buf), "START %.3f",
		         (float)swgn->points.loopStart.frame * 1000.0f / (float)rate);
		guiNodeSetText(start, buf);
	}
	GuiNode *end = findNodeByName(g->root, "END");
	if(end) {
		snprintf(buf, sizeof(buf), "END %.3f",
		         (float)swgn->points.loopEnd.frame * 1000.0f / (float)rate);
		guiNodeSetText(end, buf);
	}
}

/* Task 8: the START and END buttons select their point on the
 * waveform. The chord (FUNCTION held) reaches this from the instrument
 * screen's input handler, which checks the FUNCTION-held case before
 * the generic EDIT dispatch. */
static void cbSelectLoopStart(void *ctx) {
	SampleWaveformGuiNode *swgn = findWaveformNode((Instrument *)ctx);
	if(!swgn) {
		return;
	}
	samplerPointsSelect(&swgn->points, PT_LOOP_START);
	/* Selecting a loop point that did not exist creates it, so the
	 * instrument must hear about it -- otherwise the screen shows a
	 * point the voice is not playing. */
	writePointsToParameters(swgn);
	refreshPointCaptions(swgn);
}

static void cbSelectLoopEnd(void *ctx) {
	SampleWaveformGuiNode *swgn = findWaveformNode((Instrument *)ctx);
	if(!swgn) {
		return;
	}
	samplerPointsSelect(&swgn->points, PT_LOOP_END);
	writePointsToParameters(swgn);
	refreshPointCaptions(swgn);
}

/* Cycle the waveform view mode FIT -> LOOP -> ZOOM -> FIT. Wired to the
 * VIEW action button; the generic actionCb dispatch fires it on KM_EDIT. */
static void cbCycleWaveView(void *ctx) {
	SampleWaveformGuiNode *swgn = (SampleWaveformGuiNode *)ctx;
	if(!swgn) {
		return;
	}
	swgn->viewMode = (WaveformViewMode)((swgn->viewMode + 1) % WF_VIEW_COUNT);
}

static SampleWaveformGuiNode *createSampleWaveformGuiNode(int x, int y, int w, int h, int padding, NodeAlignment na, const char *name, bool selected, Instrument *inst, Parameter *loopStart, Parameter *loopEnd) {
	SampleWaveformGuiNode *swgn = malloc(sizeof(SampleWaveformGuiNode));
	GuiNode *gn = (GuiNode *)swgn;
	if(!initGuiNode(gn, x, y, w, h, padding, na, name, 1, selected)) {
		printf("SampleWaveformGuiNode init problem, returning NULL.\n");
		return NULL;
	}

	swgn->instrument = inst;
	swgn->bgColour = cs.waveformBg;
	swgn->wfColour = cs.waveform;
	swgn->wfAltColour = cs.waveformAlt;
	swgn->loopStart = loopStart;
	swgn->loopEnd = loopEnd;
	swgn->viewMode = WF_VIEW_FIT;
	/* The points: the cursor at the start, and the loop points where
	 * the instrument's parameters say they are. */
	int sampleLength = (inst && inst->id.sampler.sample) ? inst->id.sampler.sample->length : 0;
	samplerPointsInit(&swgn->points, sampleLength);
	if(inst && inst->id.sampler.loopStartIndex && inst->id.sampler.loopEndIndex) {
		swgn->points.loopStart.exists = true;
		swgn->points.loopStart.frame = (int)getParameterValue(inst->id.sampler.loopStartIndex);
		swgn->points.loopEnd.exists = true;
		swgn->points.loopEnd.frame = (int)getParameterValue(inst->id.sampler.loopEndIndex);
	}

	gn->drawable = true;
	gn->draw = drawSampleWaveformGuiNode;
	/* The waveform takes the left and right arrows: they move its
	 * cursor. */
	gn->customNav = modLineWaveNav;
	return swgn;
}

/* The instrument's waveform node, or NULL. The points live on it, and
 * the START and END buttons need them. */
static SampleWaveformGuiNode *findWaveformNode(Instrument *inst);

/* Task 9: the options menu -- the context menu on the selected point.
 *
 * It holds exactly two things: NEAREST ZERO CROSSING, which puts the
 * point where the waveform crosses zero so a loop does not click, and
 * the CROSSFADE, a DIAL ROW (the app's instrument for a number) that
 * sets the crossfade's length in milliseconds.
 *
 * The menu is a layer over the instrument screen. */
static void cbPointNearestZeroCrossing(void *ctx) {
	SampleWaveformGuiNode *swgn = (SampleWaveformGuiNode *)ctx;
	if(!swgn || !swgn->instrument) {
		return;
	}
	Sample *src = swgn->instrument->id.sampler.sample;
	if(!src || !src->data || src->length <= 0) {
		return;
	}
	/* The point that moves is the SELECTED one. */
	SamplerPoint *pt = NULL;
	if(swgn->points.loopStart.selected) {
		pt = &swgn->points.loopStart;
	} else if(swgn->points.loopEnd.selected) {
		pt = &swgn->points.loopEnd;
	} else {
		pt = &swgn->points.cursor;
	}
	int z = samplerPointsNearestZeroCrossing(src->data, src->length, pt->frame);
	if(z >= 0) {
		pt->frame = z;
		swgn->points.cursor.frame = z;
		/* The point moved, so the loop moved with it. */
		writePointsToParameters(swgn);
	}
}

static void cbPointOptionsBack(void *ctx) {
	(void)ctx;
	InstrumentGui *ig = getInstrumentGui();
	if(ig) {
		popTopNonPassiveLayerFree(&ig->overlayLayers);
	}
}

/* Task 8: the points ARE the loop.
 *
 * The model holds the points; the instrument's loop parameters are the
 * source of truth, and the points are written to them. Every gesture
 * that moves a point goes through here, so the loop the voice plays is
 * always the loop on the screen.
 *
 * A loop whose start is at or past its end would be read backwards, so
 * the write is REFUSED in that case: the points keep their positions and
 * the instrument keeps the last good loop. */
static void writePointsToParameters(SampleWaveformGuiNode *swgn) {
	if(!swgn || !swgn->instrument) {
		return;
	}
	Instrument *inst = swgn->instrument;
	if(!inst->id.sampler.loopStartIndex || !inst->id.sampler.loopEndIndex) {
		return;
	}
	if(!swgn->points.loopStart.exists || !swgn->points.loopEnd.exists) {
		return;
	}
	if(!samplerPointsValidate(&swgn->points, swgn->points.length)) {
		return;
	}
	setParameterValue(inst->id.sampler.loopStartIndex, (float)swgn->points.loopStart.frame);
	setParameterValue(inst->id.sampler.loopEndIndex, (float)swgn->points.loopEnd.frame);
	refreshPointCaptions(swgn);
	/* The crossfade's ceiling follows the LOOP's length, not the
	 * sample's: the spec's range is "0 to the loop's length". */
	if(inst->id.sampler.crossfade && inst->id.sampler.sample
	   && inst->id.sampler.sample->sampleRate > 0) {
		int loopFrames = swgn->points.loopEnd.frame - swgn->points.loopStart.frame;
		float loopMs = (float)loopFrames * 1000.0f / (float)inst->id.sampler.sample->sampleRate;
		setParameterMaxValue(inst->id.sampler.crossfade, loopMs);
	}
}

/* Task 8: the waveform's cursor movement.
 *
 * LEFT and RIGHT move the cursor one column of the current view, and
 * the SELECTED point follows it -- which is what makes the interaction
 * direct manipulation rather than a form. The points are the model's;
 * the instrument's loop parameters are written from them, so the loop the
 * voice plays is the loop on the screen.
 *
 * The node takes the arrows itself: a waveform's cursor IS the thing
 * being moved, so the graph must not navigate away from it. */
static bool modLineWaveNav(void *self, int keymapping) {
	SampleWaveformGuiNode *swgn = (SampleWaveformGuiNode *)self;
	if(!swgn) {
		return false;
	}
	if(keymapping != KM_LEFT && keymapping != KM_RIGHT) {
		return false;
	}
	/* One column of the current view: the view spans the whole sample
	 * in FIT, so a column is a step of the sample divided by the node's
	 * width -- and at least one frame, so the cursor can always move. */
	int length = swgn->points.length;
	if(length <= 0) {
		return false;
	}
	int width = swgn->base.w;
	if(width <= 0) {
		width = 1;
	}
	int step = length / width;
	if(step < 1) {
		step = 1;
	}
	if(keymapping == KM_LEFT) {
		step = -step;
	}
	samplerPointsMoveCursor(&swgn->points, step);
	/* The loop the voice plays is the loop on the screen. */
	writePointsToParameters(swgn);
	return true;
}

/* Open the options menu for the waveform's selected point.
 *
 * It is opened by SELECT+EDIT on the waveform -- SELECT for the
 * context menu, EDIT to open it -- so it does not collide with the
 * gestures that are already on the wavefrom. */
static void cbOpenPointOptions(void *ctx) {
	SampleWaveformGuiNode *swgn = (SampleWaveformGuiNode *)ctx;
	InstrumentGui *ig = getInstrumentGui();
	if(!swgn || !ig || !swgn->instrument) {
		return;
	}
	Graph *g = createGraph(na_vertical);
	if(!g || !g->root) {
		free(g);
		return;
	}
	/* NEAREST ZERO CROSSING. */
	GuiNode *zero = createActionBtnGuiNode(0, 0, 100, 100, 1, na_horizontal, "OPT_ZERO", 0,
	                                      cbPointNearestZeroCrossing, swgn);
	if(zero) {
		guiNodeSetText(zero, "NEAREST ZERO CROSSING");
		appendItem(g->root, zero, 1);
	}
	/* The CROSSFADE: a dial row, bound to the instrument's crossfade
	 * parameter, so turning it changes the crossfade and the waveform's
	 * band grows as it turns. */
	if(swgn->instrument->id.sampler.crossfade) {
		GuiNode *xf = createDialGuiNode(0, 0, 100, 100, 1, na_horizontal, "OPT_XFADE", 0,
		                                incParameterBaseValue, swgn->instrument->id.sampler.crossfade);
		if(xf) {
			appendItem(g->root, xf, 1);
		}
	}
	GuiNode *back = createActionBtnGuiNode(0, 0, 100, 100, 1, na_horizontal, "OPT_BACK", 0,
	                                       cbPointOptionsBack, NULL);
	if(back) {
		guiNodeSetText(back, "BACK");
		appendItem(g->root, back, 1);
	}
	changeGraphSelection(g, zero ? zero : back);
	Layer *layer = createLayer(g, 0, 0, SCREEN_W, SCREEN_H, "POINTOPT", true, true);
	if(!layer) {
		freeGuiNode(g->root);
		free(g);
		return;
	}
	pushLayer(&ig->overlayLayers, layer);
}

/* Open the options menu for the instrument's waveform. Called by the
 * instrument screen's input handler when SELECT+EDIT lands on the
 * waveform. */
void guiOpenPointOptions(Instrument *inst) {
	SampleWaveformGuiNode *swgn = findWaveformNode(inst);
	if(!swgn) {
		return;
	}
	cbOpenPointOptions(swgn);
}

/* Task 8: note that FUNCTION was pressed. The SEQUENCE gesture is
 * FUNCTION pressed-and-released before EDIT, and the flag that says so
 * lives on the points model. */
bool samplerPointsNoteFunctionPress(Instrument *inst) {
	SampleWaveformGuiNode *swgn = findWaveformNode(inst);
	if(!swgn) {
		return false;
	}
	swgn->points.functionPressed = true;
	return true;
}

/* Task 8: the SEQUENCE gesture -- FUNCTION was pressed and released
 * since the last EDIT, so the selected point is deleted.
 *
 * The flag lives on the points model, not in the input's history ring:
 * that ring is appended to only when something QUERIES a key's
 * just-pressed state, so its contents depend on the query order rather
 * than on the player's hands.
 *
 * Returns true when a point was deleted. */
bool samplerPointsDeleteSelected(Instrument *inst) {
	SampleWaveformGuiNode *swgn = findWaveformNode(inst);
	if(!swgn) {
		return false;
	}
	if(!swgn->points.functionPressed) {
		return false;
	}
	/* The flag is consumed by the gesture that reads it. */
	swgn->points.functionPressed = false;
	if(!samplerPointsDelete(&swgn->points)) {
		return false;
	}
	/* A deleted loop point leaves the loop reverted to the whole sample,
	 * and the instrument's parameters must say so -- otherwise the voice
	 * keeps playing the loop that is no longer on the screen. */
	if(swgn->instrument) {
		Instrument *i = swgn->instrument;
		if(!swgn->points.loopStart.exists && i->id.sampler.loopStartIndex) {
			setParameterValue(i->id.sampler.loopStartIndex, 0.0f);
		}
		if(!swgn->points.loopEnd.exists && i->id.sampler.loopEndIndex) {
			int len = (i->id.sampler.sample) ? i->id.sampler.sample->length : 1;
			setParameterValue(i->id.sampler.loopEndIndex, (float)(len - 1));
		}
	}
	return true;
}
static SampleWaveformGuiNode *findWaveformNode(Instrument *inst) {
	if(!inst) {
		return NULL;
	}
	Graph *g = getSelectedInstGraph();
	if(!g || !g->root) {
		return NULL;
	}
	/* The waveform node is the one named "WFRM". */
	GuiNode *n = g->root;
	if(!n) {
		return NULL;
	}
	/* A small recursive walk for the named node. */
	GuiNode *stack[64];
	int top = 0;
	stack[top++] = n;
	while(top > 0) {
		GuiNode *cur = stack[--top];
		if(cur && cur->name && strcmp(cur->name, "WFRM") == 0) {
			return (SampleWaveformGuiNode *)cur;
		}
		if(cur && cur->items) {
			ListElement *le = cur->items->head;
			for(int i = 0; i < cur->itemCount && le && top < 64; i++, le = le->next) {
				stack[top++] = *(GuiNode **)le->data;
			}
		}
	}
	return NULL;
}


/* The bake's rate, or the engine's when there is no bake. The
 * crossfade's length is in milliseconds, and the frame count it means
 * depends on the rate. */
static int bakedRateOf(Instrument *inst) {
	if(inst && inst->id.sampler.baked && inst->id.sampler.baked->rate > 0) {
		return inst->id.sampler.baked->rate;
	}
	if(inst && inst->id.sampler.sample && inst->id.sampler.sample->sampleRate > 0) {
		return inst->id.sampler.sample->sampleRate;
	}
	return SPECTRAX_ENGINE_RATE;
}

/* Draw the crossfade's band and its fade curve. `gain` is 1 for the
 * copy fading IN (the head) and 0 for the copy fading OUT (the tail);
 * the curve is the fade between them. */
static void drawCrossfadeBand(const GuiNode *gn, float a, float b, float lo, float hi, float gain) {
	if(hi <= lo || b <= a) {
		return;
	}
	float x0 = gn->x + (int)(((a - lo) / (hi - lo)) * (float)gn->w);
	float x1 = gn->x + (int)(((b - lo) / (hi - lo)) * (float)gn->w);
	if(x1 <= x0) {
		return;
	}
	/* The band is shaded harder than the loop region. */
	Color band = cs.waveformAlt;
	band.a = 110;
	DrawRectangle((int)x0, gn->y, (int)(x1 - x0), gn->h, band);
	/* The fade curve: the copy's gain across the band. */
	int steps = 12;
	int mid = gn->y + gn->h / 2;
	int amp = gn->h / 3;
	for(int i = 0; i < steps; i++) {
		float t0 = (float)i / (float)steps;
		float t1 = (float)(i + 1) / (float)steps;
		/* The equal-gain law is a straight line; the equal-power law is
		 * the quarter-circle. The curve is drawn the same either way
		 * here -- the law's difference is in the AUDIO, and the band
		 * shows WHERE the fade is. */
		float v0 = gain + (1.0f - gain) * t0;
		float v1 = gain + (1.0f - gain) * t1;
		float px0 = x0 + (x1 - x0) * t0;
		float px1 = x0 + (x1 - x0) * t1;
		DrawLineEx((Vector2){ px0, (float)(mid - (int)(v0 * amp)) },
		           (Vector2){ px1, (float)(mid - (int)(v1 * amp)) }, 2.0f, cs.waveform);
	}
}

/* Draw a vertical marker at sample `pos` when it lies inside [lo, hi). */
static void drawWaveformMarker(const GuiNode *gn, Color col, float pos, float lo, float hi) {
	if(hi <= lo || pos < lo || pos >= hi) {
		return;
	}
	float t = (pos - lo) / (hi - lo);
	int x = gn->x + (int)(t * (float)gn->w);
	DrawLine(x, gn->y, x, gn->y + gn->h, col);
}

static void drawSampleWaveformGuiNode(void *self) {
	SampleWaveformGuiNode *swgn = (SampleWaveformGuiNode *)self;
	GuiNode *gn = (GuiNode *)swgn;
	if(gn->w <= 0 || gn->h <= 0) {
		return;
	}
	DrawRectangle(gn->x, gn->y, gn->w, gn->h, swgn->bgColour);

	Sample *s = swgn->instrument ? swgn->instrument->id.sampler.sample : NULL;
	if(!s || !s->data || s->length <= 0) {
		return;
	}

	/* Task 9: the preview shows the BAKE -- what the voice plays -- not
	 * the source. The bake is the source resampled to the machine's
	 * rate and quantized to its word, so on a character profile the
	 * drawn signal and the heard signal are visibly different. */
	Sample bakedView;
	BakedSample *baked = swgn->instrument ? swgn->instrument->id.sampler.baked : NULL;
	bool drawingBake = baked && baked->data && baked->length > 0;
	if(drawingBake) {
		bakedView.data = baked->data;
		bakedView.name = s->name;
		bakedView.length = baked->length;
		bakedView.sampleRate = baked->rate;
		bakedView.bit = baked->bits;
		s = &bakedView;
	}
	/* The drawn frames per source frame: the loop points are the
	 * SOURCE's frames, and the drawn material may be at another rate. */
	float loopScale = 1.0f;
	if(drawingBake && swgn->instrument->id.sampler.sample
	   && swgn->instrument->id.sampler.sample->length > 0) {
		loopScale = (float)baked->length / (float)swgn->instrument->id.sampler.sample->length;
	}

	float len = (float)s->length;
	float loopStart = (float)getParameterValueAsInt(swgn->loopStart) * loopScale;
	float loopEnd = (float)getParameterValueAsInt(swgn->loopEnd) * loopScale;
	float lo = 0.0f;
	float hi = len;
	switch(swgn->viewMode) {
		case WF_VIEW_LOOP:
			lo = loopStart;
			hi = loopEnd;
			break;
		case WF_VIEW_CURSOR: {
			float mid = (loopStart + loopEnd) * 0.5f;
			float half = (loopEnd - loopStart) * 0.5f / WAVEFORM_ZOOM_FACTOR;
			if(half < 1.0f) {
				half = 1.0f;
			}
			lo = mid - half;
			hi = mid + half;
			break;
		}
		case WF_VIEW_FIT:
		default:
			lo = 0.0f;
			hi = len;
			break;
	}
	if(lo < 0.0f) {
		lo = 0.0f;
	}
	if(hi > len) {
		hi = len;
	}
	if(hi <= lo) {
		hi = lo + 1.0f;
	}

	int cols = gn->w;
	if(cols > WAVEFORM_MAX_COLS) {
		cols = WAVEFORM_MAX_COLS;
	}
	float outMin[WAVEFORM_MAX_COLS];
	float outMax[WAVEFORM_MAX_COLS];
	samplePeakEnvelope(s, lo, hi, cols, outMin, outMax);

	int centerY = gn->y + gn->h / 2;
	float halfH = (float)(gn->h / 2) - 1.0f;
	if(halfH < 1.0f) {
		halfH = 1.0f;
	}
	float colScale = (float)gn->w / (float)cols;
	int bottom = gn->y + gn->h - 1;
	for(int c = 0; c < cols; c++) {
		int x = gn->x + (int)((float)c * colScale);
		int yTop = centerY - (int)(outMax[c] * halfH);
		int yBot = centerY - (int)(outMin[c] * halfH);
		if(yTop < gn->y) {
			yTop = gn->y;
		}
		if(yBot > bottom) {
			yBot = bottom;
		}
		if(yBot <= yTop) {
			/* Keep flat/silent columns visible as a single pixel. */
			yBot = (yTop < bottom) ? yTop + 1 : yTop;
		}
		DrawLine(x, yTop, x, yBot, swgn->wfColour);
	}

	drawWaveformMarker(gn, swgn->wfAltColour, loopStart, lo, hi);
	drawWaveformMarker(gn, swgn->wfAltColour, loopEnd, lo, hi);

	/* Task 8: the points. The loop region is shaded, the two loop
	 * points are drawn in green, and the cursor in the brighter colour
	 * it has everywhere else in the app. */
	if(swgn->points.loopStart.exists && swgn->points.loopEnd.exists) {
		float a = (float)swgn->points.loopStart.frame * loopScale;
		float b = (float)swgn->points.loopEnd.frame * loopScale;
		if(a > lo && a < hi && b > lo && b < hi) {
			int x0 = gn->x + (int)(((a - lo) / (hi - lo)) * (float)gn->w);
			int x1 = gn->x + (int)(((b - lo) / (hi - lo)) * (float)gn->w);
			if(x1 < x0) {
				int t = x0;
				x0 = x1;
				x1 = t;
			}
			/* The region is shaded lightly, so the waveform still reads
			 * through it. */
			Color region = cs.waveformAlt;
			region.a = 60;
			DrawRectangle(x0, gn->y, x1 - x0, gn->h, region);
		}
	}
	if(swgn->points.loopStart.exists) {
		drawWaveformMarker(gn, cs.waveformAlt, (float)swgn->points.loopStart.frame * loopScale, lo, hi);
	}
	if(swgn->points.loopEnd.exists) {
		drawWaveformMarker(gn, cs.waveformAlt, (float)swgn->points.loopEnd.frame * loopScale, lo, hi);
	}
	if(swgn->points.cursor.exists) {
		drawWaveformMarker(gn, cs.labelSelected, (float)swgn->points.cursor.frame * loopScale, lo, hi);
	}

	/* Task 9: the crossfade, drawn on the waveform. The head's and the
	 * tail's bands are shaded harder than the loop region, and the
	 * fade curve is drawn over each -- a straight line for the
	 * equal-gain law, a quarter-circle for the equal-power one. The
	 * value is written on the waveform as well, so the number and the
	 * picture agree. */
	if(swgn->instrument && swgn->instrument->id.sampler.crossfade) {
		float xfMs = getParameterValue(swgn->instrument->id.sampler.crossfade);
		if(xfMs > 0.0f) {
			int headStart = 0, headEnd = 0, tailStart = 0, tailEnd = 0;
			/* The band is in the SOURCE's frames, because the points are
			 * the source's frames. (Converting the crossfade's length
			 * with the BAKE's rate here would mix the two: the band
			 * would be drawn at the wrong width whenever the profile
			 * resamples.) */
			int srcRate = (swgn->instrument->id.sampler.sample
			               && swgn->instrument->id.sampler.sample->sampleRate > 0)
			              ? swgn->instrument->id.sampler.sample->sampleRate
			              : SPECTRAX_ENGINE_RATE;
			int xfadeSrcFrames = (int)(xfMs * (float)srcRate / 1000.0f);
			samplerPointsCrossfadeBand(&swgn->points, xfadeSrcFrames,
			                           &headStart, &headEnd, &tailStart, &tailEnd);
			if(headEnd > headStart && tailEnd > tailStart) {
				drawCrossfadeBand(gn, (float)headStart * loopScale, (float)headEnd * loopScale, lo, hi, 1.0f);
				drawCrossfadeBand(gn, (float)tailStart * loopScale, (float)tailEnd * loopScale, lo, hi, 0.0f);
			}
			char xf[32];
			snprintf(xf, sizeof(xf), "XF %.0fms", xfMs);
			BeginScissorMode(gn->x, gn->y, gn->w, gn->h);
			DrawTextEx(pixelFont, xf, (Vector2){ (float)(gn->x + gn->padding), (float)(gn->y + gn->h - 12) },
			           WAVEFORM_TAG_FONT, 1, cs.waveformAlt);
			EndScissorMode();
		}
	}

	const char *name = (s->name) ? s->name : "";
	BeginScissorMode(gn->x, gn->y, gn->w, gn->h);
	DrawTextEx(pixelFont, name, (Vector2){ (float)(gn->x + gn->padding), (float)(gn->y + gn->padding) },
	           WAVEFORM_NAME_FONT, 1, swgn->wfAltColour);
	EndScissorMode();

	const char *tag = "FIT";
	if(swgn->viewMode == WF_VIEW_LOOP) {
		tag = "LOOP";
	} else if(swgn->viewMode == WF_VIEW_CURSOR) {
		tag = "ZOOM";
	}
	float tagW = MeasureTextEx(pixelFont, tag, WAVEFORM_TAG_FONT, 1).x;
	DrawTextEx(pixelFont, tag, (Vector2){ (float)(gn->x + gn->w) - tagW - (float)gn->padding, (float)(gn->y + gn->padding) },
	           WAVEFORM_TAG_FONT, 1, swgn->wfColour);
}

void appendSampleInstControlNode(Graph *g, GuiNode *container, char *name, int weight, bool selected, Instrument *inst) {
	GuiNode *btnwrap = createGuiNode(0, 0, 100, 100, 0, na_vertical, "SAMPLE_CONTROLS", 0, 0);
	btnwrap->draw = drawWrapperNode;
	btnwrap->drawable = true;

	GuiNode *btnrow1 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_1", 0, 0);
	GuiNode *btnrow2 = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_2", 0, 0);
	/* R_3 and R_4 are the CHARACTER rows: the bake's shape (profile,
	 * law, interpolation, bit depth, sample rate) and the filter and
	 * the tuning (filter, cutoff, resonance, tune, root). They were
	 * parameters with no control until now -- the DSP had landed and
	 * nothing on the screen reached it. */
	GuiNode *charRow = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_3", 0, 0);
	GuiNode *filterRow = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_4", 0, 0);
	GuiNode *wfrow = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "R_5", 0, 0);

	GuiNode *sampleIndex = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "SAMPLE", selected, incParameterBaseValue, inst->id.sampler.sampleIndex);
	GuiNode *loop = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "LOOP", 0, incParameterBaseValue, inst->id.sampler.loopSample);
	/* Task 8: START and END stop being dials and become BUTTONS that
	 * carry the point's value. The point is what FUNCTION acts on, and
	 * the point is moved on the waveform -- not by turning a dial. The
	 * loop parameters remain the instrument's: they are saved in the
	 * preset and can be modulated. Only their PRESENTATION changes. */
	GuiNode *loopStart = createActionBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "START", 0, cbSelectLoopStart, inst);
	GuiNode *loopEnd = createActionBtnGuiNode(0, 0, 100, 100, 5, na_horizontal, "END", 0, cbSelectLoopEnd, inst);
	GuiNode *playbackType = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "PLAYBACK", 0, incParameterBaseValue, inst->id.sampler.playbackType);
	GuiNode *pan = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "PAN", 0, incParameterBaseValue, inst->panning);
	GuiNode *gain = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "GAIN", 0, incParameterBaseValue, inst->gain);
	pan->draw = drawDiscreteDialGuiNode;
	gain->draw = drawDiscreteDialGuiNode;

	/* The character row. PROFILE, LAW and INTERP pick from a fixed set,
	 * so they are discrete dials; BITS and RATE are continuous. */
	GuiNode *profile = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "PROFILE", 0, incParameterBaseValue, inst->id.sampler.profile);
	GuiNode *law = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "LAW", 0, incParameterBaseValue, inst->id.sampler.law);
	GuiNode *interp = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "INTERP", 0, incParameterBaseValue, inst->id.sampler.interp);
	GuiNode *bits = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "BITS", 0, incParameterBaseValue, inst->id.sampler.bitDepth);
	GuiNode *rate = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "RATE", 0, incParameterBaseValue, inst->id.sampler.sampleRate);
	profile->draw = drawDiscreteDialGuiNode;
	law->draw = drawDiscreteDialGuiNode;
	interp->draw = drawDiscreteDialGuiNode;

	/* The filter row. FILTER is a switch, so it is discrete; CUTOFF,
	 * RES, TUNE and ROOT are continuous. */
	GuiNode *filter = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "FILTER", 0, incParameterBaseValue, inst->id.sampler.filter);
	GuiNode *cutoff = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "CUTOFF", 0, incParameterBaseValue, inst->id.sampler.cutoff);
	GuiNode *res = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "RES", 0, incParameterBaseValue, inst->id.sampler.res);
	GuiNode *tune = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "TUNE", 0, incParameterBaseValue, inst->id.sampler.tune);
	GuiNode *root = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "ROOT", 0, incParameterBaseValue, inst->id.sampler.root);
	filter->draw = drawDiscreteDialGuiNode;

	SampleWaveformGuiNode *swgn = createSampleWaveformGuiNode(0, 0, 100, 100, 5, na_vertical, "WFRM", 0, inst, inst->id.sampler.loopStartIndex, inst->id.sampler.loopEndIndex);

	/* The buttons carry the point's value, in milliseconds. The point
	 * is what FUNCTION acts on, so the button says where the point is. */
	{
		int rate = (inst->id.sampler.sample && inst->id.sampler.sample->sampleRate > 0)
		           ? inst->id.sampler.sample->sampleRate : 1;
		char buf[48];
		float startMs = (float)getParameterValueAsInt(inst->id.sampler.loopStartIndex) * 1000.0f / (float)rate;
		float endMs = (float)getParameterValueAsInt(inst->id.sampler.loopEndIndex) * 1000.0f / (float)rate;
		snprintf(buf, sizeof(buf), "START %.3f", startMs);
		guiNodeSetText(loopStart, buf);
		snprintf(buf, sizeof(buf), "END %.3f", endMs);
		guiNodeSetText(loopEnd, buf);
	}
	GuiNode *viewBtn = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "VIEW", 0, cbCycleWaveView, swgn);
	if(selected) {
		g->selected = sampleIndex;
	}

	GuiNode *sp1 = createBlankGuiNode();
	GuiNode *sp2 = createBlankGuiNode();

	appendItem(btnrow1, sampleIndex, 1);
	appendItem(btnrow1, loop, 1);
	appendItem(btnrow1, loopStart, 1);
	appendItem(btnrow1, loopEnd, 1);
	appendItem(btnrow1, playbackType, 1);

	appendItem(btnrow2, pan, 1);
	appendItem(btnrow2, gain, 1);
	appendItem(btnrow2, viewBtn, 1);
	appendItem(btnrow2, sp1, 1);
	appendItem(btnrow2, sp2, 1);

	appendItem(charRow, profile, 1);
	appendItem(charRow, law, 1);
	appendItem(charRow, interp, 1);
	appendItem(charRow, bits, 1);
	appendItem(charRow, rate, 1);

	appendItem(filterRow, filter, 1);
	appendItem(filterRow, cutoff, 1);
	appendItem(filterRow, res, 1);
	appendItem(filterRow, tune, 1);
	appendItem(filterRow, root, 1);

	appendItem(wfrow, (GuiNode *)swgn, 1);

	appendItem(btnwrap, btnrow1, 1);
	appendItem(btnwrap, btnrow2, 1);
	appendItem(btnwrap, charRow, 1);
	appendItem(btnwrap, filterRow, 1);
	appendItem(btnwrap, wfrow, 2);

	appendItem(container, btnwrap, weight);
}

/* Test seam: walk the selected instrument graph for a node by name. */
static GuiNode *findGuiNodeNamed(GuiNode *n, const char *name) {
	if(!n) {
		return NULL;
	}
	if(n->name && strcmp(n->name, name) == 0) {
		return n;
	}
	ListElement *le = n->items ? n->items->head : NULL;
	for(; le; le = le->next) {
		GuiNode *child = *(GuiNode **)le->data;
		GuiNode *match = findGuiNodeNamed(child, name);
		if(match) {
			return match;
		}
	}
	return NULL;
}

/* Probe seam (like guiRouteLinesSource): the selected instrument's
 * waveform view mode, or -1 when the sampler panel isn't built. Lets the
 * scripted harness assert the VIEW button actually cycles the mode. */
int guiWaveformViewMode(void) {
	Graph *g = getSelectedInstGraph();
	if(!g || !g->root) {
		return -1;
	}
	GuiNode *n = findGuiNodeNamed(g->root, "WFRM");
	if(!n) {
		return -1;
	}
	return (int)((SampleWaveformGuiNode *)n)->viewMode;
}
