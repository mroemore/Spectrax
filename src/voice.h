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
    VOICE_TYPE_OSCILLATOR,
    VOICE_TYPE_SAMPLE,
    VOICE_TYPE_FM,
    VOICE_TYPE_BLEP,
    VOICE_TYPE_COUNT
} VoiceType;

typedef enum {
    IT_OSCILLATOR,
    IT_SAMPLE,
    IT_FM,
    IT_BLEP
} InstrumentType;

typedef struct {
    float leftPhase;
    float rightPhase;
    int samplesElapsed;
    int active;
    ParamList* paramList;
    ModList* modList;
    Parameter* volume;
    VoiceType type;
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
    Sample* sample;
    Envelope* envelopes[MAX_ENVELOPES]; 
    VoiceType voiceType;
} Instrument;

typedef struct {
    Voice* voicePools[MAX_SEQUENCER_CHANNELS][MAX_VOICES_PER_CHANNEL];
    Instrument* instruments[MAX_SEQUENCER_CHANNELS];
    VoiceType voiceTypes[MAX_SEQUENCER_CHANNELS];
    int voiceCount[MAX_SEQUENCER_CHANNELS];
    AllocationBehaviour voiceAllocation[MAX_SEQUENCER_CHANNELS];
} VoiceManager;


VoiceManager* createVoiceManager(Settings* settings, SamplePool* sp);
void initVoicePool(VoiceManager* vm, int channelIndex, int voiceCount, Instrument* inst);
void initVoiceManager(VoiceManager* vm, SamplePool* sp);
void freeVoice(Voice* v);
void freeVoiceManager(VoiceManager* vm);
Voice* getFreeVoice(VoiceManager* vm, int seqChannel);
void changeVoiceType(VoiceManager* vm, int seqChannel, VoiceType type);
void triggerVoice(Voice* voice, int note[NOTE_INFO_SIZE]);

void initialize_voice(Voice *voice, Instrument* inst);
void initialize_voice_sample(Voice *voice, Sample sample, int voice_id, ModList* modList);
void initialize_voice_blep(Voice *voice, int voice_id, ModList* modList);
void initialize_voice_fm(Voice *voice, int voice_id, ModList* modList);
void init_instrument(Instrument* instrument, VoiceType vt, Sample* sample);
#endif // VOICE_H