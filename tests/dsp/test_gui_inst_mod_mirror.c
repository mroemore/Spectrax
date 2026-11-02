/* gui_inst_mod.c's envelope mirror uses two different index spaces.
 *
 * addRuntimeSource writes the new Mod into envelopes[envelopeCount], which is a
 * SOURCE position (envelopes[] is indexed by "how many envelopes exist").
 * removeSource clears envelopes[mi] where mi = modIndexAt(modList, srcIndex),
 * which is a modList position -- and modList interleaves attenuators, so those
 * two spaces diverge as soon as anything has been appended.
 *
 * Concretely: a runtime source lands at modList index 11 (past the seven
 * attenuators) but is mirrored at envelopes[3]. Removing it computes mi = 11,
 * which is >= MAX_ENVELOPES, so the `if(mi < MAX_ENVELOPES)` guard skips the
 * clear entirely and envelopes[3] keeps pointing at freed memory. The next
 * addRuntimeSource calls rebuildInstrumentGraph(), which walks envelopes[].
 *
 * Run under ASan or valgrind: a clean run without them would only mean the
 * freed block was not reused yet. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "gui_inst_internal.h"
#include "graph_gui.h"
#include "input.h"
#include "io.h"
#include "modsystem.h"
#include "sample.h"
#include "sequencer.h"
#include "voice.h"
#include "wavetable.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

#define TEST_SAMPLE 1024

int main(void) {
	SamplePool *sp = createSamplePool();
	ASSERT_TRUE(sp != NULL, "createSamplePool");
	float *buf = (float *)malloc(TEST_SAMPLE * sizeof(float));
	ASSERT_TRUE(buf != NULL, "sample buffer");
	for(int i = 0; i < TEST_SAMPLE; i++) {
		buf[i] = sinf(2.0f * (float)M_PI * (float)i / (float)TEST_SAMPLE);
	}
	loadSample(sp, "sine", buf, 24, SAMPLE_RATE, TEST_SAMPLE);
	free(buf);

	WavetablePool *wtp = createWavetablePool();
	ASSERT_TRUE(wtp != NULL, "createWavetablePool");

	PresetBank pb;
	initPresetBank(&pb);
	Preset p;
	initDefaultFmPreset(&p);
	addPresetToBank(&pb, p);

	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = 1;
	settings.defaultVoiceCount = 1;
	settings.defaultBPM = 120;
	VoiceManager *vm = createVoiceManager(&settings, sp, wtp, &pb);
	ASSERT_TRUE(vm != NULL, "createVoiceManager");

	ParamList *pl = createParamList();
	Arranger *arr = (Arranger *)calloc(1, sizeof(Arranger));
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int r = 0; r < MAX_SONG_LENGTH; r++) {
			arr->song[ch][r] = -1;
		}
		arr->playhead_indices[ch] = -1;
	}
	arr->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	arr->tempoSettings.swing = createParameterPro(pl, "Swing", 0.0f, 0.0f, 1.0f,
	                                            0.01f, 0.1f, NULL, NULL);
	PatternList *pats = (PatternList *)calloc(1, sizeof(PatternList));
	pats->pattern_count = 1;
	pats->patterns[0].pattern_size = 8;

	int selected = 0;
	initModSystem();
	createInstrumentGui(vm, &selected, 0);
	Instrument *inst = vm->instruments[0];
	ASSERT_TRUE(inst != NULL, "instrument 0");

	int sourcesBefore = modSourceCount(inst->modList);
	int envCountBefore = inst->envelopeCount;

	/* Add a runtime source. It is appended to the modList, i.e. after the
	 * attenuators, so its modList index is far larger than the envelopes[]
	 * slot it gets mirrored into. */
	addRuntimeSource(inst);
	int addedPos = modSourceCount(inst->modList) - 1;
	int addedIdx = modIndexAt(inst->modList, addedPos);
	int mirrorSlot = envCountBefore;
	fprintf(stderr, "DBG addedPos=%d addedIdx=%d mirrorSlot=%d envelopeCount=%d\n",
	        addedPos, addedIdx, mirrorSlot, inst->envelopeCount);
	ASSERT_TRUE(addedIdx != mirrorSlot,
	            "the two index spaces have diverged (that is the point)");
	ASSERT_TRUE(inst->envelopes[mirrorSlot] != NULL,
	            "the new source was mirrored into envelopes[]");

	Mod *mirrored = inst->envelopes[mirrorSlot];

	/* Remove it again. removeSource clears envelopes[addedIdx], but addedIdx
	 * is >= MAX_ENVELOPES, so the guard skips and the mirror keeps a pointer
	 * to freed memory. */
	removeSource(inst, addedPos);
	fprintf(stderr, "DBG after remove: modList.count=%d sources=%d envelopeCount=%d\n",
	        inst->modList->count, modSourceCount(inst->modList),
	        inst->envelopeCount);

	/* The dangling slot is still populated, and it points at a Mod that has
	 * been freed. Reading a field is the cheapest possible use-after-free
	 * probe; ASan reports it as heap-use-after-free on the read. */
	if(inst->envelopes[mirrorSlot] != NULL) {
		fprintf(stderr, "FAIL envelopes[%d] still holds %p after the source was "
		        "removed (mirrored %p)\n",
		        mirrorSlot, (void *)inst->envelopes[mirrorSlot], (void *)mirrored);
		fprintf(stderr, "     reading it to prove it is dangling...\n");
		volatile ModType t = inst->envelopes[mirrorSlot]->type;
		(void)t;
		fprintf(stderr, "FAIL mirror slot was not cleared\n");
		return 1;
	}

	/* The next structural change walks envelopes[] via rebuildInstrumentGraph,
	 * so drive one: this is where the stale pointer becomes a crash. */
	addRuntimeSource(inst);

	printf("PASSED (envelope mirror indices agree)\n");
	free(pats);
	freeParamList(pl);
	free(arr);
	freeSamplePool(sp);
	freeWavetablePool(wtp);
	freeInstrumentGui();
	return 0;
}