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

typedef enum {
    VOICE_TYPE_OSCILLATOR,
    VOICE_TYPE_SAMPLE,
    VOICE_TYPE_FM,
    VOICE_TYPE_BLEP
} VoiceType;

typedef enum {
    IT_OSCILLATOR,
    IT_SAMPLE,
    IT_FM,
    IT_BLEP
} InstrumentType;

typedef struct {
    float left_phase;
    float right_phase;
    int samples_elapsed;
    int active;
    ParamList* paramList;
    ModList* modList;
    Parameter* volume;
    int instrument_index;
    VoiceType type;
    union {
        Oscillator oscillator;
        Sample sample;
        Operator *operators[4];
    } source;
    float sample_position; // Position in the sample data
    int note[2];
    Parameter* frequency;
    int env_count;
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
    VoiceType voiceTypes[MAX_SEQUENCER_CHANNELS];
    int voiceCount[MAX_SEQUENCER_CHANNELS];
    AllocationBehaviour voiceAllocation[MAX_SEQUENCER_CHANNELS];
} VoiceManager;

typedef struct {
    Sample* sample;
} Instrument;

VoiceManager* createVoiceManager(Settings* settings);
void initVoicePool(VoiceManager* vm, int seqChannel, int voiceCount, VoiceType vt);
void initVoiceManager(VoiceManager* vm);
Voice* getFreeVoice(VoiceManager* vm, int seqChannel);
void changeVoiceType(VoiceManager* vm, int seqChannel, VoiceType type);
void triggerVoice(Voice* voice, int note[NOTE_INFO_SIZE]);

void initialize_voice(Voice *voice, VoiceType voiceType, Sample* sample);
void initialize_voice_sample(Voice *voice, Sample sample, int voice_id, ModList* modList);
void initialize_voice_blep(Voice *voice, int voice_id, ModList* modList);
void initialize_voice_fm(Voice *voice, int voice_id, ModList* modList);
void init_instrument(Instrument* instrument, Sample* sample);

#endif // VOICE_H