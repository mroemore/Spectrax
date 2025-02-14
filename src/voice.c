#include "voice.h"
#include "notes.h"
#include "blit_synth.h"
#include "modsystem.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

VoiceManager* createVoiceManager(Settings* settings, SamplePool* sp, WavetablePool* wtp) {
    printf("creating voiceManager.\n");

    VoiceManager* vm = (VoiceManager*)malloc(sizeof(VoiceManager));
    if (!vm) {
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
	printf("\t-> DONE.\n");
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

void freeVoice(Voice* v){ //TO-DO: free grain
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
    int shape = 0;
    int sampleIndex = 0;

    switch(currentVoice->type){
        case VOICE_TYPE_BLEP:
            shape = getParameterValueAsInt(currentVoice->instrumentRef->shape);
            switch(shape){
                case BLEP_RAMP:
                    L = blep_saw(currentVoice->leftPhase, phaseIncrement);
                    L *= 0.025;
                    break;
                case BLEP_SQUARE:
                    L = blep_square(currentVoice->leftPhase, phaseIncrement);
                    L *= 0.025;
                    break;
                case BLEP_SINE:
                    L = noblep_sine(currentVoice->leftPhase);
                    L*=0.5;
                    break;
            }
            out = (OutVal){L, L};
            break;
        case VOICE_TYPE_FM:
            L = sineFmAlgo(currentVoice->source.operators, frequency, getParameterValueAsInt(currentVoice->instrumentRef->selectedAlgorithm));
            out = (OutVal){L, L};
            break;
        case VOICE_TYPE_SAMPLE:
            sampleIndex = getParameterValueAsInt(currentVoice->instrumentRef->sampleIndex);
            L = getSampleValue(vm->samplePool->samples[sampleIndex], &currentVoice->samplePosition, phaseIncrement, SAMPLE_RATE, 0);
            int bitDepthDiff = 24 - getParameterValueAsInt(currentVoice->instrumentRef->bitDepth);
            int intVal = (int)(L*1000000000000000000000000);
            intVal >> bitDepthDiff;
            intVal << bitDepthDiff;
            L = (float)intVal / 1000000000000000000000000;

            out = (OutVal){L, L};
            break;
        case VOICE_TYPE_GRAIN:
            out = granularProcess(currentVoice->source.granularProcessor, phaseIncrement);
        default:
            break;
    }
    out.L = currentVoice->filter->biquad->processSample(currentVoice->filter->biquad, out.L);
    out.R = out.L;
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
        case VOICE_TYPE_GRAIN:
            voice->source.granularProcessor = createGranularProcessor(inst->sample); 
            break;
        default:
            break;
    }
    voice->filter = createFilter(kTransposeCanonical, secondOrderLPF, 250.0f, 10.0f);
}


void init_instrument(Instrument** instrument, VoiceType vt, SamplePool* samplePool) {
    printf("init instrument\n");
    *instrument = (Instrument*)malloc(sizeof(Instrument));
    if(!*instrument){
        printf("could not allocate memory for instrument in init_instrument.\n");
        return;
    }
    (*instrument)->modList = createModList();
    if(!(*instrument)->modList){
        printf("modList creation failed in init_instrument.\n");
        return;
    }
    (*instrument)->paramList = createParamList();
    if(!(*instrument)->paramList){
        printf("paramList creation failed in init_instrument.\n");
    }
    switch(vt){
        case VOICE_TYPE_BLEP:
            (*instrument)->envelopeCount = 2;
            (*instrument)->lfoCount = 0;
            (*instrument)->shape = createParameterEx((*instrument)->paramList, "shape", 0.0f, 0.0f, (float)BLEP_SHAPE_COUNT - 1, 1.0, 10.0);
            break;
        case VOICE_TYPE_SAMPLE:
            (*instrument)->envelopeCount = 1;
            (*instrument)->lfoCount = 0;
            (*instrument)->sample = samplePool->samples[0];
            (*instrument)->bitDepth = createParameterEx((*instrument)->paramList, "bitdepth", 24.0f, 8.0f, 24.0f, 1.0f, 4.0f);
            (*instrument)->sampleRate = createParameterEx((*instrument)->paramList, "bitrate", 44100.0f, 2000.0f, 44100.0f, 100.0f, 1000.0f);
            (*instrument)->sampleIndex = createParameterEx((*instrument)->paramList, "algo", 0, 0, (float)samplePool->sampleCount, 1.0f, 10.0f);
            break;
        case VOICE_TYPE_FM:
            (*instrument)->envelopeCount = 4;
            (*instrument)->lfoCount = 0;
            (*instrument)->selectedAlgorithm = createParameterEx((*instrument)->paramList, "algo", 0, 0, ALGO_COUNT, 1.0f, 10.0f);
            for(int i = 0; i < MAX_FM_OPERATORS; i++){
                (*instrument)->ops[i] = createOperator((*instrument)->paramList, (float)i+1);
            }
        case VOICE_TYPE_GRAIN:
            (*instrument)->sample = samplePool->samples[2];
            (*instrument)->envelopeCount = 1;
            
            break;
    
    }
    for(int i = 0; i < (*instrument)->envelopeCount; i++){
        (*instrument)->envelopes[i] = createAD((*instrument)->paramList, (*instrument)->modList, .25f, .25f, "AD1");
    }
    
    (*instrument)->voiceType = vt;
}

GranularProcessor* createGranularProcessor(Sample* s){
    GranularProcessor* gp = (GranularProcessor*)malloc(sizeof(GranularProcessor));
    if(!gp) return NULL;
    gp->paramList = createParamList();
    if(!gp->paramList) {
        free(gp);
        return NULL;
    }
    gp->modList = createModList();
    if(!gp->modList) {
        free(gp->paramList);
        free(gp);
        return NULL;
    }
    gp->grainVelocity = createParameter(gp->paramList, "gVel", 0.333f, 0.001f, 100.0f);
    gp->volume = createParameter(gp->paramList, "gVol", 1.0f, 0.0f, 1.0f);
    gp->writeHead = 0;
    gp->mainEnv = createAD(gp->paramList, gp->modList, 0.05, 10.5, "gEnv");
    gp->sample = s;
    for(int i = 0; i < GRAIN_WINDOW_SIZE; i++){
        gp->grainWindow[i] = sin((i/GRAIN_WINDOW_SIZE) * TWO_PI);
        gp->windowIndex[i] = 0;
    }
    for(int i = 0; i < GRAIN_COUNT; i++){
        float startPos = rand() * GRANULAR_BUFFER_SIZE/4;
        gp->grainStartPos[i] = createParameter(gp->paramList, "gPos", rand() * GRANULAR_BUFFER_SIZE/4, 0.0f, (float)GRANULAR_BUFFER_SIZE);
        gp->grainReadPos[i] = startPos;
    }

    return gp;
}

OutVal granularProcess(GranularProcessor* gp, float phaseIncrement) {
    OutVal result = {0.0f, 0.0f};

    for (int i = 0; i < GRAIN_COUNT; i++) {
        float adjusted_phaseinc_sample = phaseIncrement * (SAMPLE_RATE / (float)gp->sample->sampleRate);
        gp->grainReadPos[i] += adjusted_phaseinc_sample;
        gp->windowIndex[i] += phaseIncrement;

        if (gp->windowIndex[i] >= GRAIN_WINDOW_SIZE) {
            gp->windowIndex[i] -= GRAIN_WINDOW_SIZE;
        }

        if (gp->grainReadPos[i] >= gp->sample->length) {
            gp->grainReadPos[i] -= gp->sample->length;
        }

        int indexFloor = (int)gp->grainReadPos[i];
        int sIndexCeil = (indexFloor + 1) % gp->sample->length; // Wrap around at the end
        int wIndexFloor = gp->windowIndex[i];
        int wIndexCeil = (wIndexFloor + 1) % GRAIN_WINDOW_SIZE; // Wrap around at the end
        float frac = gp->grainReadPos[i] - indexFloor;

        // Perform linear interpolation between indexFloor and indexCeil
        float windowVal = gp->grainWindow[wIndexFloor] * (1.0f - frac) + gp->grainWindow[wIndexCeil] * frac;
        float value = gp->sample->data[indexFloor] * (1.0f - frac) + gp->sample->data[sIndexCeil] * frac;

        result.L += value * windowVal;
    }

    result.L /= GRAIN_COUNT; // Normalize by the number of grains
    result.R = result.L;

    return result;
}