#ifndef VOICE_H
#define VOICE_H
#include <stdlib.h>
#include "settings.h"
#include "oscillator.h"
#include "sample.h"
#include "notes.h"
#include "modsystem.h"
#include "blit_synth.h"



#define MAX_LFOS 8
#define MAX_ENVELOPES 8
#define MAX_FM_OPERATORS 4

typedef enum {
    VOICE_TYPE_SAMPLE,
    VOICE_TYPE_FM,
    VOICE_TYPE_BLEP,
    VOICE_TYPE_COUNT
} VoiceType;

typedef struct {
	float L;
	float R;
} OutVal;

typedef struct {
    Sample* sample;
    ModList* modList;
    ParamList* paramList;
    Envelope* envelopes[MAX_ENVELOPES];
    int envelopeCount; 
    int lfoCount; 
    VoiceType voiceType;
    Parameter* selectedAlgorithm;
    Parameter* sampleIndex;
    Operator* ops[MAX_FM_OPERATORS];
} Instrument;

typedef struct {
    float leftPhase;
    float rightPhase;
    int samplesElapsed;
    int active;
    ParamList* paramList;
    ModList* modList;
    Parameter* volume;
    VoiceType type;
    Instrument* instrumentRef;
    union {
        Oscillator oscillator;
        Sample *sample;
        Operator *operators[MAX_FM_OPERATORS];
    } source;
    float samplePosition; // Position in the sample data
    int note[2];
    Parameter* frequency;
    int envCount;
    int lfoCount;
    Envelope* envelope[4];
    LFO * lfo[2];
} Voice;

typedef enum {
    VA_FREE_OR_ZERO,
    VA_FREE_OR_OLDEST,
    VA_ROUND_ROBIN,
    VA_RANDOM
} AllocationBehaviour;

typedef struct {
    Voice* voicePools[MAX_SEQUENCER_CHANNELS][MAX_VOICES_PER_CHANNEL];
    Instrument* instruments[MAX_SEQUENCER_CHANNELS];
    VoiceType voiceTypes[MAX_SEQUENCER_CHANNELS];
    int voiceCount[MAX_SEQUENCER_CHANNELS];
    int enabledChannels;
    WavetablePool* wavetablePool;
    SamplePool* samplePool;
    AllocationBehaviour voiceAllocation[MAX_SEQUENCER_CHANNELS];
} VoiceManager;


VoiceManager* createVoiceManager(Settings* settings, SamplePool* sp, WavetablePool* wtp);
void initVoicePool(VoiceManager* vm, int channelIndex, int voiceCount, Instrument* inst);
void initVoiceManager(VoiceManager* vm, SamplePool* sp);
void freeVoice(Voice* v);
void freeVoiceManager(VoiceManager* vm);
Voice* getFreeVoice(VoiceManager* vm, int seqChannel);
void changeVoiceType(VoiceManager* vm, int seqChannel, VoiceType type);
void triggerVoice(Voice* voice, int note[NOTE_INFO_SIZE]);
OutVal generateVoice(VoiceManager* vm, Voice* currentVoice, float phaseIncrement, float frequency);

void initialize_voice(Voice *voice, Instrument* inst);
void initialize_voice_sample(Voice *voice, Sample sample, int voice_id, ModList* modList);
void initialize_voice_blep(Voice *voice, int voice_id, ModList* modList);
void initialize_voice_fm(Voice *voice, int voice_id, ModList* modList);
void init_instrument(Instrument** instrument, VoiceType vt, SamplePool* samplePool);
#endif // VOICE_H