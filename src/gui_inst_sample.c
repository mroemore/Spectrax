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

	gn->drawable = true;
	gn->draw = drawSampleWaveformGuiNode;
	return swgn;
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

	float len = (float)s->length;
	float loopStart = (float)getParameterValueAsInt(swgn->loopStart);
	float loopEnd = (float)getParameterValueAsInt(swgn->loopEnd);
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
	GuiNode *loopStart = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "START", 0, incParameterBaseValue, inst->id.sampler.loopStartIndex);
	GuiNode *loopEnd = createDialGuiNode(0, 0, 100, 100, 5, na_horizontal, "END", 0, incParameterBaseValue, inst->id.sampler.loopEndIndex);
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
