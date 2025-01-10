#include "voice.h"
#include "notes.h"
#include "blit_synth.h"
#include "modsystem.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

VoiceManager* createVoiceManager(Settings* settings, SamplePool* sp){
    VoiceManager* vm = (VoiceManager*)malloc(sizeof(VoiceManager));
    for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
        init_instrument(vm->instruments[i], settings->voiceTypes[i], sp->samples[3]);
        initVoicePool(vm, i, settings->defaultVoiceCount, vm->instruments[i]);
        vm->voiceAllocation[i] = VA_FREE_OR_ZERO;
    }

    return vm;
}

void freeVoiceManager(VoiceManager* vm){
    for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
        for(int j = 0; j < vm->voiceCount[i]; j++){
            freeVoice(vm->voicePools[i][j]);
        }
    }

    free(vm);
}

void freeVoice(Voice* v){
    freeModList(v->modList);
    freeParamList(v->paramList);
    freeParameter(v->volume);
    for(int i = 0; i < v->envCount; i++){
        freeEnvelope(v->envelope[i]);
    }
    for(int i = 0; i < v->lfoCount; i++){
        freeLFO(v->lfo[i]);
    }
    switch(v->type){
        case VOICE_TYPE_FM:
            for(int i = 0; i < MAX_FM_OPERATORS; i++){
                freeOperator(v->source.operators[i]);
            }
            break;
        case VOICE_TYPE_SAMPLE:
            freeSample(v->source.sample);
            break;
        case VOICE_TYPE_BLEP:
        default:
            break;
    }

    free(v);
}

void initVoicePool(VoiceManager* vm, int channelIndex, int voiceCount, Instrument* inst){
    if(channelIndex >= MAX_SEQUENCER_CHANNELS - 1 || channelIndex < 0) channelIndex %= MAX_SEQUENCER_CHANNELS;
    if(voiceCount >= MAX_VOICES_PER_CHANNEL) voiceCount = MAX_VOICES_PER_CHANNEL;
    vm->voiceCount[channelIndex] = 0;
    
    for(int i = 0; i < voiceCount; i++){
        printf("allocating voice %i of %i (type %i) for channel %i\n", i, voiceCount, inst->voiceType, channelIndex);
        vm->voicePools[channelIndex][i] = (Voice*)malloc(sizeof(Voice));
        initialize_voice(vm->voicePools[channelIndex][i], inst);
        vm->voiceCount[channelIndex]++;
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
    printf("returning voice %i of channel %i\n", voiceIndex, seqChannel);
    return vm->voicePools[seqChannel][voiceIndex];
}

void triggerVoice(Voice* voice, int note[NOTE_INFO_SIZE]){
    voice->note[0] = note[0];
    voice->note[1] = note[1];
    voice->leftPhase = 0.0f;
    voice->rightPhase = 0.0f;
    voice->samplesElapsed = 0;
    voice->active = 1;
    for(int e = 0; e < voice->envCount; e++){
        triggerEnvelope(voice->envelope[e]);
    }
}

void initialize_voice(Voice *voice, Instrument* inst) {
    voice->leftPhase = 0.0f;
    voice->rightPhase = 0.0f;
    voice->note[0] = OFF;
    voice->note[1] = 0;
    voice->paramList = createParamList();
    voice->modList = createModList();
    voice->frequency = createParameter(voice->paramList, "frequency", 440.0f, 0.001f, 20000.0f);
    voice->samplesElapsed = 0;
    voice->active = 0;
    voice->volume = createParameter(voice->paramList, "volume", 1.0f, 0.0f, 1.0f);
    voice->type = inst->voiceType;

    switch(voice->type){
        case VOICE_TYPE_BLEP:
            voice->envCount = 2;
            voice->lfoCount = 0;
            voice->envelope[0] = createAD(voice->paramList, voice->modList, .25f, .25f, "AD1");
            voice->envelope[1] = createAD(voice->paramList, voice->modList, .25f, .25f, "AD2");
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            addModulation(voice->paramList, &voice->envelope[1]->base, voice->frequency, 400.5f, MO_ADD);
            break;

        case VOICE_TYPE_SAMPLE:
            voice->source.sample = inst->sample;
            voice->envCount = 1;
            voice->lfoCount = 0;
            voice->samplePosition = 0.0f; // Initialize sample position
            voice->envelope[0] = createAD(voice->paramList, voice->modList, 0.15f, 1.2f, "AD1");
            break;

        case VOICE_TYPE_FM:
            voice->source.operators[0] = createOperator(voice->paramList, 1.0f);
            voice->source.operators[1] = createOperator(voice->paramList, 2.0f);
            voice->source.operators[2] = createOperator(voice->paramList, 4.0f);
            voice->source.operators[3] = createOperator(voice->paramList, 3.0f);
            voice->samplePosition = 0.0f; // Initialize sample position
            voice->envCount = 4;
            voice->lfoCount = 0;
            for(int i = 0; i < voice->envCount; i++) {
                voice->envelope[i] = createAD(voice->paramList, voice->modList, 0.05f, 1.2f, "ADSR1");
                addModulation(voice->paramList, &voice->envelope[i]->base, voice->source.operators[i]->level, 1.0f, MO_MUL);
            }
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            break;
        default:
            break;
    }
}


void init_instrument(Instrument* instrument, VoiceType vt, Sample* sample) {
    instrument = (Instrument*)malloc(sizeof(Instrument));
    instrument->sample = sample;
    instrument->voiceType = vt;
}