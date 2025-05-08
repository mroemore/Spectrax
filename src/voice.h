#ifndef VOICE_H
#define VOICE_H
#include <stdlib.h>
#include "kiss_fft.h"
#include "settings.h"
#include "oscillator.h"
#include "sample.h"
#include "notes.h"
#include "modsystem.h"
#include "blit_synth.h"
#include "filters.h"
#include "fft.h"

#define MAX_LFOS 8
#define MAX_ENVELOPES 6
#define MAX_FM_OPERATORS 4
#define MAX_DETUNE 16

typedef enum {
	VOICE_TYPE_SAMPLE,
	VOICE_TYPE_FM,
	VOICE_TYPE_BLEP,
	VOICE_TYPE_GRAIN,
	VOICE_TYPE_SPECTRAL,
	VOICE_TYPE_COUNT
} VoiceType;

typedef struct {
	float L;
	float R;
} OutVal;

#define GRANULAR_BUFFER_SIZE 441000 // 10 seconds
#define GRAIN_COUNT 16
#define GRAIN_WINDOW_SIZE 1024

typedef struct Voice Voice;
typedef float (*GenerateSample)(Voice *currentVoice, float phaseIncrement, float frequency);

typedef struct {
	ParamList *paramList;
	ModList *modList;
	float buffer[GRANULAR_BUFFER_SIZE];
	float grainWindow[GRAIN_WINDOW_SIZE];
	int windowIndex[GRAIN_WINDOW_SIZE];
	int writeHead;
	Parameter *grainStartPos[GRAIN_COUNT];
	float grainReadPos[GRAIN_COUNT];
	Parameter *grainVelocity;
	Parameter *grainMs;
	Parameter *volume;
	Sample *sample;
	Envelope *mainEnv;
	Envelope *grainEnvs[GRAIN_COUNT];

} GranularProcessor;

GranularProcessor *createGranularProcessor(Sample *s);
OutVal granularProcess(GranularProcessor *gp, float phaseIncrement);

typedef struct {
	Sample *sample;
} GranularInstrumentData;

typedef struct {
	Parameter *shape;
} BlepInstrumentData;

typedef struct {
	Operator *ops[MAX_FM_OPERATORS];
	Parameter *selectedAlgorithm;
} FmInstrumentData;

typedef struct {
	Sample *sample;
	Parameter *playbackSpeed;
	SamplePool *sp;
	float *spectralData;
	int spectralDataSize;
} SpectralInstrumentData;

typedef struct {
	Sample *sample;
	SamplePool *sp;
	Parameter *bitDepth;
	Parameter *sampleRate;
	Parameter *loopSample;
	Parameter *sampleIndex;
	Parameter *playbackType;
	Parameter *loopStartIndex;
	Parameter *loopEndIndex;
	GetSampleFunc getSample;
} SamplerInstrumentData;

typedef struct {
	ModList *modList;
	ParamList *paramList;
	Envelope *envelopes[MAX_ENVELOPES];
	int envelopeCount;
	int lfoCount;
	float volumeAttenuation;
	VoiceType voiceType;
	Parameter *detuneVoiceCount;
	Parameter *detuneRange;
	Parameter *detuneSpread;
	Parameter *panning;
	union {
		SamplerInstrumentData sampler;
		FmInstrumentData fm;
		SpectralInstrumentData spectral;
		BlepInstrumentData blep;
		GranularInstrumentData granular;
	} id;
} Instrument;

typedef struct {
	Oscillator oscillator;
} BlepVoiceData;

typedef struct {
	Operator *operators[MAX_FM_OPERATORS];
} FmVoiceData;

typedef struct {
	Sample *sample;
	float *spectralData;
	float samplePosition;
	int spectralDataSize;
} SpectralVoiceData;

typedef struct {
	Sample *sample;
	float samplePosition; // Position in the sample data
	GenerateSample generate;
	SamplePool *samplePool;
} SamplerVoiceData;

typedef struct {
	GranularProcessor *granularProcessor;
} GranularVoiceData;

struct Voice {
	VoiceType type;
	float leftPhase;
	float rightPhase;
	float detunePhase[MAX_DETUNE];
	int note[2];
	int samplesElapsed;
	int active;
	ParamList *paramList;
	ModList *modList;
	int envCount;
	int lfoCount;
	Envelope *envelope[4];
	LFO *lfo[2];
	Parameter *frequency;
	Parameter *volume;
	Instrument *instrumentRef;
	union {
		FmVoiceData fm;
		BlepVoiceData blep;
		SpectralVoiceData spectral;
		SamplerVoiceData sampler;
		GranularVoiceData granular;
	} vd;
	Filter *filter;
};

typedef enum {
	VA_FREE_OR_ZERO,
	VA_FREE_OR_OLDEST,
	VA_ROUND_ROBIN,
	VA_RANDOM
} AllocationBehaviour;

typedef struct {
	Voice *voicePools[MAX_SEQUENCER_CHANNELS][MAX_VOICES_PER_CHANNEL];
	Instrument *instruments[MAX_SEQUENCER_CHANNELS];
	VoiceType voiceTypes[MAX_SEQUENCER_CHANNELS];
	int voiceCount[MAX_SEQUENCER_CHANNELS];
	int enabledChannels;
	WavetablePool *wavetablePool;
	SamplePool *samplePool;
	AllocationBehaviour voiceAllocation[MAX_SEQUENCER_CHANNELS];
} VoiceManager;

VoiceManager *createVoiceManager(Settings *settings, SamplePool *sp, WavetablePool *wtp);
void initVoicePool(VoiceManager *vm, int channelIndex, int voiceCount, Instrument *inst);
void initVoiceManager(VoiceManager *vm, SamplePool *sp);
void freeVoice(Voice *v);
void freeVoiceManager(VoiceManager *vm);
Voice *getFreeVoice(VoiceManager *vm, int seqChannel);
void changeVoiceType(VoiceManager *vm, int seqChannel, VoiceType type);
void triggerVoice(Voice *voice, int note[NOTE_INFO_SIZE]);
OutVal generateVoice(VoiceManager *vm, Voice *currentVoice, float phaseIncrement, float frequency);

void initialize_voice(Voice *voice, Instrument *inst);
void initialize_voice_sample(Voice *voice, Sample sample, int voice_id, ModList *modList);
void initialize_voice_blep(Voice *voice, int voice_id, ModList *modList);
void initialize_voice_fm(Voice *voice, int voice_id, ModList *modList);
void init_instrument(Instrument **instrument, VoiceType vt, SamplePool *samplePool);
void setSamplePlaybackFunction(void *instrument);
void updateSampleReferences(void *instrument);
#endif // VOICE_H
