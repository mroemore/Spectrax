#include "voice.h"
#include "notes.h"
#include "blit_synth.h"
#include "modsystem.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

VoiceManager* createVoiceManager(Settings* settings){
    VoiceManager* vm = (VoiceManager*)malloc(sizeof(VoiceManager));
    for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
        initVoicePool(vm, i, settings->defaultVoiceCount, settings->voiceTypes[i]);
        vm->voiceAllocation[i] = VA_FREE_OR_ZERO;
    }

    return vm;
}

void initVoicePool(VoiceManager* vm, int seqChannel, int voiceCount, VoiceType vt){
    if(seqChannel >= MAX_SEQUENCER_CHANNELS - 1 || seqChannel < 0) seqChannel %= MAX_SEQUENCER_CHANNELS - 1;
    if(voiceCount >= MAX_VOICES_PER_CHANNEL) voiceCount = MAX_VOICES_PER_CHANNEL;

    for(int i = 0; i < voiceCount; i++){
        vm->voicePools[seqChannel][i] = (Voice*)malloc(sizeof(Voice));
        initialize_voice(vm->voicePools[seqChannel][i], vt, NULL);
        vm->voiceCount[seqChannel]++;
    }
}

Voice* getFreeVoice(VoiceManager* vm, int seqChannel){
    int voiceIndex = 0;
    switch(vm->voiceAllocation[seqChannel]){
        case VA_FREE_OR_ZERO:
            for(int i = 0; i < vm->voiceCount[seqChannel]; i++){
                if(vm->voicePools[seqChannel][i]->active == 0){
                    voiceIndex = i;
                }
            }
            break;
        default:
            break;
    }
    
    return vm->voicePools[seqChannel][voiceIndex];
}

void triggerVoice(Voice* voice, int note[NOTE_INFO_SIZE]){
    voice->note[0] = note[0];
    voice->note[1] = note[1];
    voice->left_phase = 0.0f;
    voice->right_phase = 0.0f;
    voice->samples_elapsed = 0;
    voice->active = 1;
    for(int e = 0; e < voice->env_count; e++){
        triggerEnvelope(voice->envelope[e]);
    }
}

void initialize_voice(Voice *voice, VoiceType voiceType, Sample* sample) {
    voice->left_phase = 0.0f;
    voice->right_phase = 0.0f;
    voice->note[0] = OFF;
    voice->note[1] = 0;
    voice->paramList = createParamList();
    voice->modList = createModList();
    voice->frequency = createParameter(voice->paramList, "frequency", 440.0f, 0.001f, 20000.0f);
    voice->samples_elapsed = 0;
    voice->instrument_index = 0;
    voice->active = 0;
    voice->volume = createParameter(voice->paramList, "volume", 1.0f, 0.0f, 1.0f);
    voice->type = voiceType;

    switch(voice->type){
        case VOICE_TYPE_BLEP:
            voice->env_count = 2;
            voice->envelope[0] = createAD(voice->paramList, voice->modList, .25f, .25f, "AD1");
            voice->envelope[1] = createAD(voice->paramList, voice->modList, .25f, .25f, "AD2");
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            addModulation(voice->paramList, &voice->envelope[1]->base, voice->frequency, 400.5f, MO_ADD);
            break;

        case VOICE_TYPE_SAMPLE:
            voice->source.sample = *sample;
            voice->env_count = 1;
            voice->sample_position = 0.0f; // Initialize sample position
            voice->envelope[0] = createADSR(voice->paramList, voice->modList, 0.5f, 1.2f, 1.5f, 1.1f, "ADSR1");
            break;

        case VOICE_TYPE_FM:
            voice->source.operators[0] = createOperator(voice->paramList, 1.0f);
            voice->source.operators[1] = createOperator(voice->paramList, 2.0f);
            voice->source.operators[2] = createOperator(voice->paramList, 4.0f);
            voice->source.operators[3] = createOperator(voice->paramList, 3.0f);
            voice->sample_position = 0.0f; // Initialize sample position
            voice->env_count = 4;
            for(int i = 0; i < voice->env_count; i++) {
                voice->envelope[i] = createAD(voice->paramList, voice->modList, 0.05f, 1.2f, "ADSR1");
                addModulation(voice->paramList, &voice->envelope[i]->base, voice->source.operators[i]->level, 1.0f, MO_MUL);
            }
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            break;
        default:
            break;
    }
}

void init_instrument(Instrument* instrument, Sample* sample) {
    instrument = (Instrument*)malloc(sizeof(Instrument));
    instrument->sample = sample;
}