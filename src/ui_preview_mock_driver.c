#include "ui_preview_mock.h"

#include <stdio.h>
#include <string.h>

#include "dataviz.h"
#include "graph_gui.h"
#include "gui.h"
#include "gui_internal.h"
#include "modsystem.h"
#include "sequencer.h"
#include "vizfx.h"

/* --- the driver ---------------------------------------------------------- */

#include <string.h>

#include "dataviz.h"
#include "graph_gui.h"
#include "gui_internal.h"
#include "gui.h"
#include "modsystem.h"
#include "sequencer.h"
#include "vizfx.h"

#define PREVIEW_MOCK_SUBJECTS 3

#define PREVIEW_MOCK_SUBJECTS 3
#define PREVIEW_MOCK_DESTS 3

/* The instrument the preview shows may be FM, a sampler, granular...: naming
 * parameters by string would only ever light up one kind, and routing into a
 * parameter that has no dial would show nothing. Destinations are therefore
 * taken from the dials the instrument screen actually draws, offset by the
 * subject so each hold shows a different set. */
#define PREVIEW_MOCK_DIALS 64

static int mockCollectDials(GuiNode *node, Parameter **out, int max, int n) {
	if(!node || n >= max) {
		return n;
	}
	if(node->draw && node->p
	   && (node->draw == drawDialGuiNode || node->draw == drawDiscreteDialGuiNode)) {
		out[n++] = node->p;
	}
	if(node->items) {
		ListElement *le = node->items->head;
		while(le && n < max) {
			GuiNode *child = *(GuiNode **)le->data;
			n = mockCollectDials(child, out, max, n);
			le = le->next;
		}
	}
	return n;
}

static Mod *gMockLfos[PREVIEW_MOCK_SUBJECTS];
static Parameter *gMockDests[PREVIEW_MOCK_DESTS];
static float gMockSign[PREVIEW_MOCK_DESTS];
static int gMockDestCount = 0;
static int gMockSubject = -1;

/* Channel 0, deliberately: the preview's first instrument panel is always
 * the first channel of its voice type (see previewBuildPanels), and driving
 * the arranger's cursor channel instead would animate a screen no panel
 * draws. */
#define PREVIEW_MOCK_CHANNEL 0

static Instrument *mockInstrument(ApplicationState *appState) {
	InstrumentGui *ig = getInstrumentGui();
	if(!ig || !ig->vm || !appState) {
		return NULL;
	}
	if(PREVIEW_MOCK_CHANNEL >= ig->vm->enabledChannels) {
		return NULL;
	}
	return ig->vm->instruments[PREVIEW_MOCK_CHANNEL];
}

/* Routes subject `subject`: drops whatever the previous subject left on its
 * sources, then connects the patch through addModulation, which is what makes
 * a dial draw a modulation segment. Amounts are a fraction of the
 * destination's own range, so a segment is visible without pushing the dial
 * out of it. */
static void mockApplySubject(Instrument *inst, int subject) {
	if(!inst || !inst->paramList || !inst->modList) {
		return;
	}
	if(subject < 0 || subject >= PREVIEW_MOCK_SUBJECTS) {
		subject = 0;
	}
	InstrumentGui *ig = getInstrumentGui();
	int ch = PREVIEW_MOCK_CHANNEL;
	Parameter *dials[PREVIEW_MOCK_DIALS];
	int dialCount = 0;
	if(ig && ch >= 0 && ig->instrumentScreenGraphs[ch]
	   && ig->instrumentScreenGraphs[ch]->root) {
		dialCount = mockCollectDials(ig->instrumentScreenGraphs[ch]->root,
		                             dials, PREVIEW_MOCK_DIALS, 0);
	}

	for(int i = 0; i < PREVIEW_MOCK_SUBJECTS; i++) {
		if(gMockLfos[i]) {
			removeModulationsForSource(inst->paramList, inst->modList, gMockLfos[i]);
		}
	}
	gMockDestCount = 0;
	if(dialCount <= 0) {
		gMockSubject = subject;
		return;
	}
	for(int k = 0; k < PREVIEW_MOCK_DESTS; k++) {
		Parameter *dest = dials[(subject * PREVIEW_MOCK_DESTS + k) % dialCount];
		if(!dest || !gMockLfos[k] || !(dest->maxValue > dest->minValue)) {
			continue;
		}
		float span = dest->maxValue - dest->minValue;
		float sign = (k % 2) ? -1.0f : 1.0f;
		/* The route is real, so the modulation screen and the mod strips
		 * list the source; the amount stays inside the attenuator's own
		 * 0..2 scale. */
		addModulation(inst->paramList, inst->modList, gMockLfos[k], dest,
		              span * 0.30f * sign, MO_ADD);
		gMockDests[gMockDestCount] = dest;
		gMockSign[gMockDestCount] = sign;
		gMockDestCount++;
	}
	gMockSubject = subject;
}

void previewMockInit(paTestData *data, ApplicationState *appState) {
	if(!data) {
		return;
	}
	/* The visualisers start hidden; a preview with no audio would show an
	 * empty box, so turn them on for the panel window only. */
	if(!data->spectrogram.dv.enabled) {
		toggleSpectrogram(&data->spectrogram);
	}
	if(!data->timeGraph.dv.enabled) {
		toggleTimeGraph(&data->timeGraph);
	}

	Instrument *inst = mockInstrument(appState);
	if(!inst || !inst->paramList || !inst->modList) {
		return;
	}
	/* Real LFOs, not faked values: the dial segments then come from the same
	 * engine the app runs, just advanced by the preview's clock. */
	static const float rates[PREVIEW_MOCK_SUBJECTS] = { 0.5f, 1.2f, 3.0f };
	static const int shapes[PREVIEW_MOCK_SUBJECTS] = { LS_SIN, LS_RMP, LS_SQU };
	static const char *names[PREVIEW_MOCK_SUBJECTS] = { "DEMO_LFO_1", "DEMO_LFO_2", "DEMO_LFO_3" };
	for(int i = 0; i < PREVIEW_MOCK_SUBJECTS; i++) {
		gMockLfos[i] = createLFO(inst->paramList, inst->modList, i, rates[i],
		                         shapes[i], names[i]);
	}
	mockApplySubject(inst, 0);
	if(data->arranger) {
		/* A playhead is only painted while the arranger believes it is
		 * playing (arrangerCellFill gates on `playing`). */
		data->arranger->playing = true;
		for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
			data->arranger->playhead_indices[ch] = 0;
			if(data->sequencer) {
				data->sequencer->running[ch] = true;
			}
		}
	}
}

void previewMockStep(paTestData *data, ApplicationState *appState, int frame) {
	if(!data) {
		return;
	}
	Instrument *inst = mockInstrument(appState);
	if(inst && inst->paramList && inst->modList) {
		int subject = previewMockSubject(frame, PREVIEW_MOCK_SUBJECTS,
		                                 PREVIEW_MOCK_HOLD_FRAMES);
		if(subject != gMockSubject) {
			mockApplySubject(inst, subject);
		}
		/* The engine's own pass, with a fixed delta so a shot is
		 * reproducible. */
		processModulations(inst->paramList, inst->modList, 1.0f / 60.0f);
		for(int i = 0; i < inst->paramList->count; i++) {
			Parameter *p = inst->paramList->params[i];
			if(p) {
				p->currentValue = previewMockClampToRange(p->currentValue,
				                                          p->minValue, p->maxValue);
			}
		}
		/* The app advances the VOICE lists from the audio callback; the
		 * instrument's own list -- the one the instrument screen's dials
		 * point at -- is never stepped. So the preview writes each demo
		 * dial's effective value itself, from the same triangle the
		 * subject's LFO is shaped like. That is the value the ring is
		 * drawn from (baseValue -> currentValue). */
		for(int k = 0; k < gMockDestCount; k++) {
			Parameter *dest = gMockDests[k];
			if(!dest) {
				continue;
			}
			float span = dest->maxValue - dest->minValue;
			float t = (float)previewMockTriangle(frame + k * 17, 120 + k * 40,
			                                     1000, 0) / 1000.0f;
			dest->currentValue = previewMockClampToRange(
				dest->baseValue + t * span * 0.30f * gMockSign[k],
				dest->minValue, dest->maxValue);
		}
	}

	/* Visualisers: the same producers the audio callback feeds. */
	for(int i = 0; i < 256; i++) {
		pushFrameToFFT(&data->spectrogram.fft, previewMockSample(frame * 4 + i));
	}
	processFFTData(&data->spectrogram.fft);
	pushTimeGraphMeasurement(&data->timeGraph,
	                         (double)(previewMockSample(frame) * 0.5f + 0.5f));
	for(int i = 0; i < 4; i++) {
		pushBufferScrollerFrame(&data->bufferScroller, previewMockSample(frame * 4 + i));
	}

	/* Playheads. The arranger highlights the row a playing channel is on and
	 * the pattern grid needs running[ch] plus a matching pattern index. */
	if(data->arranger && data->sequencer) {
		/* Walk the visible window, not the song's length: the shipped song
		 * is short and a one-row walk would stand still. */
		int row = previewMockPlayhead(frame, ARRANGER_WINDOW_ROWS, 8);
		for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
			data->arranger->playhead_indices[ch] = row;
			data->sequencer->running[ch] = true;
			data->sequencer->playhead_index[ch] =
				previewMockPlayhead(frame, MAX_PATTERN_STEPS, 4);
		}
		if(data->patternList && appState) {
			int pat = appState->selectedPattern >= 0 ? appState->selectedPattern : 0;
			for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
				data->sequencer->pattern_index[ch] = pat;
			}
		}
	}
}
