#ifndef SPECTRAX_MAIN_H
#define SPECTRAX_MAIN_H

#include "appstate.h"
#include "gui.h"
#include "sequencer.h"
#include "voice.h"
#include "dataviz.h"
#include "modsystem.h"
#include "vizfx.h"

/* paTestData is the application-wide runtime bundle shared by
 * the PortAudio callback, the GUI, and the input handlers.
 * Defined here so the main app (src/main.c) and any test
 * harness can both reference the same struct layout. */
typedef struct {
	int sequence_index;
	int samples_per_beat;
	int samples_elapsed;
	int active_sequencer_index;
	Settings *settings;
	Arranger *arranger;
	PatternList *patternList;
	Sequencer *sequencer;
	ModList *modList;
	ParamList *globalParameters;
	VoiceManager *voiceManager;
	SamplePool *samplePool;
	WavetablePool *wavetablePool;
	Spectrogram spectrogram;
	TimeGraph timeGraph;
	BufferScroller bufferScroller;
	MixRing mixRing;
	PresetBank presetBank;
} paTestData;

/* initialise the entire application -- GUI, voice manager, arranger,
 * sequencer, mod system, song load, instrument graph. Defined in
 * src/main.c; the harness compiles main.c with -DSPECTRAX_HARNESS
 * to suppress its own main() and just borrow initApplication(). */
void initApplication(paTestData *data, ApplicationState **appState, InstrumentGui **instrumentGui);

#endif /* SPECTRAX_MAIN_H */