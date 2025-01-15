#include "voice.h"
#include "notes.h"
#include "blit_synth.h"
#include "modsystem.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

VoiceManager* createVoiceManager(Settings* settings, SamplePool* sp, WavetablePool* wtp) {
    VoiceManager* vm = (VoiceManager*)malloc(sizeof(VoiceManager));
    if (vm == NULL) {
        fprintf(stderr, "Failed to allocate memory for VoiceManager\n");
        return NULL;
    }

    vm->wavetablePool = wtp;
    vm->samplePool = sp;
    vm->enabledChannels = settings->enabledChannels;

    // Initialize voiceCount to 0 for all channels
    for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
        vm->voiceCount[i] = 0;
    }

    for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
        init_instrument(&vm->instruments[i], settings->voiceTypes[i], sp);
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

OutVal generateVoice(VoiceManager* vm, Voice* currentVoice, float phaseIncrement, float frequency){
    OutVal out;
    float L = 0.0f;
    float R = 0.0f;
    switch(currentVoice->type){
        case VOICE_TYPE_BLEP:
            L = blep_square(currentVoice->leftPhase, phaseIncrement);
            out = (OutVal){L, L};
            break;
        case VOICE_TYPE_FM:
            L = sineFmAlgo(currentVoice->source.operators, frequency, getParameterValueAsInt(currentVoice->instrumentRef->selectedAlgorithm));
            out = (OutVal){L, L};
            break;
        case VOICE_TYPE_SAMPLE:
            L = getSampleValue(currentVoice->source.sample, &currentVoice->samplePosition, phaseIncrement, SAMPLE_RATE, 0);
            out = (OutVal){L, L};
            break;
        default:
            break;
    }
    return out;
}

void initVoicePool(VoiceManager* vm, int channelIndex, int voiceCount, Instrument* inst){
    if(channelIndex >= MAX_SEQUENCER_CHANNELS || channelIndex < 0) {
        printf("out of bounds!\n");
    }
    if(voiceCount >= MAX_VOICES_PER_CHANNEL) voiceCount = MAX_VOICES_PER_CHANNEL;
    
    vm->voiceCount[channelIndex] = 0;
    
    for(int i = 0; i < voiceCount; i++){
        printf("allocating voice %i of %i (type %i) for channel %i\n", i+1, voiceCount, inst->voiceType, channelIndex);
        vm->voicePools[channelIndex][i] = (Voice*)malloc(sizeof(Voice));
        if (vm->voicePools[channelIndex][i] == NULL) {
            fprintf(stderr, "Failed to allocate memory for voice %d in channel %d\n", i, channelIndex);
            return;
        }
        initialize_voice(vm->voicePools[channelIndex][i], inst);
        vm->voiceCount[channelIndex]++;
    }

    printf("voice count of %i for channel %i, from starting input of %i", vm->voiceCount[channelIndex], channelIndex, voiceCount);
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

int selectSample(VoiceManager* vm, int channelIndex, int sampleIndex){
    if(sampleIndex < 0 || sampleIndex >= vm->samplePool->sampleCount){
        sampleIndex %= vm->samplePool->sampleCount - 1;
    }

    vm->instruments[channelIndex]->sample = vm->samplePool->samples[sampleIndex];
    return sampleIndex;
}

void incrementSampleParam(Parameter* sampleIndex, float delta){
    int index = getParameterValueAsInt(sampleIndex);
    index += (int)delta;

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
    voice->instrumentRef = inst;
    voice->frequency = createParameter(voice->paramList, "frequency", 440.0f, 0.001f, 20000.0f);
    voice->samplesElapsed = 0;
    voice->active = 0;
    voice->volume = createParameter(voice->paramList, "volume", 1.0f, 0.0f, 1.0f);
    voice->type = inst->voiceType;
    printf("active: %i\n", voice->active);
    voice->envCount = inst->envelopeCount;
    voice->lfoCount = inst->lfoCount;
    for(int i = 0; i < voice->envCount; i++){
        voice->envelope[i] = createParamPointerAD(
            voice->paramList,
            voice->modList, 
            inst->envelopes[i]->stages[0].duration, 
            inst->envelopes[i]->stages[1].duration, 
            inst->envelopes[i]->stages[0].curvature, 
            inst->envelopes[i]->stages[1].curvature, 
            "ADp"
        );
    }

    switch(voice->type){
        case VOICE_TYPE_BLEP:
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            addModulation(voice->paramList, &voice->envelope[1]->base, voice->frequency, 400.5f, MO_ADD);
            break;

        case VOICE_TYPE_SAMPLE:
            voice->source.sample = inst->sample;
            voice->samplePosition = 0.0f; // Initialize sample position
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            break;

        case VOICE_TYPE_FM:
            voice->source.operators[0] = createParamPointerOperator(voice->paramList, inst->ops[0]->feedbackAmount, inst->ops[0]->ratio, inst->ops[0]->level);
            voice->source.operators[1] = createParamPointerOperator(voice->paramList, inst->ops[1]->feedbackAmount, inst->ops[1]->ratio, inst->ops[1]->level);
            voice->source.operators[2] = createParamPointerOperator(voice->paramList, inst->ops[2]->feedbackAmount, inst->ops[2]->ratio, inst->ops[2]->level);
            voice->source.operators[3] = createParamPointerOperator(voice->paramList, inst->ops[3]->feedbackAmount, inst->ops[3]->ratio, inst->ops[3]->level);
            voice->samplePosition = 0.0f; // Initialize sample position
            for(int i = 0; i < voice->envCount; i++) {
                addModulation(voice->paramList, &voice->envelope[i]->base, voice->source.operators[i]->level, 1.0f, MO_MUL);
            }
            addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
            break;
        default:
            break;
    }
}


void init_instrument(Instrument** instrument, VoiceType vt, SamplePool* samplePool) {
    *instrument = (Instrument*)malloc(sizeof(Instrument));
    (*instrument)->modList = createModList();
    (*instrument)->paramList = createParamList();
    switch(vt){
        case VOICE_TYPE_BLEP:
            (*instrument)->envelopeCount = 2;
            (*instrument)->lfoCount = 0;
            break;
        case VOICE_TYPE_SAMPLE:
            (*instrument)->envelopeCount = 1;
            (*instrument)->lfoCount = 0;
            (*instrument)->sample = samplePool->samples[0];
            (*instrument)->sampleIndex = createParameterEx((*instrument)->paramList, "algo", 0, 0, (float)samplePool->sampleCount, 1.0f, 10.0f);
            break;
        case VOICE_TYPE_FM:
            (*instrument)->envelopeCount = 4;
            (*instrument)->lfoCount = 0;
            (*instrument)->selectedAlgorithm = createParameterEx((*instrument)->paramList, "algo", 0, 0, ALGO_COUNT, 1.0f, 10.0f);
            for(int i = 0; i < MAX_FM_OPERATORS; i++){
                (*instrument)->ops[i] = createOperator((*instrument)->paramList, (float)i+1);
            }
            break;
    
    }
    for(int i = 0; i < (*instrument)->envelopeCount; i++){
        (*instrument)->envelopes[i] = createAD((*instrument)->paramList, (*instrument)->modList, .25f, .25f, "AD1");
    }
    
    (*instrument)->voiceType = vt;
}