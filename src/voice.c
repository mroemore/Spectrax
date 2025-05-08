#include "voice.h"
#include "fft.h"
#include "kiss_fft.h"
#include "kiss_fftr.h"
#include "notes.h"
#include "blit_synth.h"
#include "modsystem.h"
#include "sample.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

VoiceManager *createVoiceManager(Settings *settings, SamplePool *sp, WavetablePool *wtp) {
	// printf("creating voiceManager.\n");

	VoiceManager *vm = (VoiceManager *)malloc(sizeof(VoiceManager));
	if(!vm) {
		fprintf(stderr, "Failed to allocate memory for VoiceManager\n");
		return NULL;
	}

	vm->wavetablePool = wtp;
	vm->samplePool = sp;
	vm->enabledChannels = settings->enabledChannels;

	// Initialize voiceCount to 0 for all channels
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		vm->voiceCount[i] = 0;
	}

	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		init_instrument(&vm->instruments[i], settings->voiceTypes[i], sp);
		initVoicePool(vm, i, settings->defaultVoiceCount, vm->instruments[i]);
		vm->voiceAllocation[i] = VA_FREE_OR_ZERO;
	}
	return vm;
}

void freeVoiceManager(VoiceManager *vm) {
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		for(int j = 0; j < vm->voiceCount[i]; j++) {
			freeVoice(vm->voicePools[i][j]);
		}
	}

	free(vm);
}

void freeVoice(Voice *v) { // TO-DO: free grain
	freeModList(v->modList);
	freeParamList(v->paramList);
	freeParameter(v->volume);
	for(int i = 0; i < v->envCount; i++) {
		freeEnvelope(v->envelope[i]);
	}
	for(int i = 0; i < v->lfoCount; i++) {
		freeLFO(v->lfo[i]);
	}
	switch(v->type) {
		case VOICE_TYPE_FM:
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				freeOperator(v->vd.fm.operators[i]);
			}
			break;
		case VOICE_TYPE_SAMPLE:
			freeSample(v->vd.sampler.sample);
			break;
		case VOICE_TYPE_BLEP:
		default:
			break;
	}

	free(v);
}

OutVal generateFM(Voice *currentVoice, float phaseIncrement, float frequency) {
	OutVal out;
	out.L = sineFmAlgo(currentVoice->vd.fm.operators, frequency, getParameterValueAsInt(currentVoice->instrumentRef->id.fm.selectedAlgorithm));
	out.R = out.L;
	return out;
}

OutVal generateSample(Voice *currentVoice, float phaseIncrement, float frequency) {
	OutVal out;
	int sampleIndex = getParameterValueAsInt(currentVoice->instrumentRef->id.sampler.sampleIndex);
	bool loop = getParameterValueAsInt(currentVoice->instrumentRef->id.sampler.loopSample);
	out.L = getSampleValueFwd(currentVoice->vd.sampler.samplePool->samples[sampleIndex], &currentVoice->vd.sampler.samplePosition, phaseIncrement, SAMPLE_RATE, loop, SPT_FORWARD);
	out.L *= 0.5;
	out.R = out.L;
	return out;
}

OutVal generateBlep(Voice *currentVoice, float phaseIncrement, float frequency) {
	OutVal out;
	int shape = getParameterValueAsInt(currentVoice->instrumentRef->id.blep.shape);
	switch(shape) {
		case BLEP_RAMP:
			out.L = blep_saw(currentVoice->leftPhase, phaseIncrement);
			out.L *= 0.025;
			break;
		case BLEP_SQUARE:
			out.L = blep_square(currentVoice->leftPhase, phaseIncrement);
			out.L *= 0.025;
			break;
		case BLEP_SINE:
			out.L = noblep_sine(currentVoice->leftPhase);
			out.L *= 0.5;
			break;
	}
	out.R = out.L;
	return out;
}

OutVal generateSpectral(Voice *currentVoice, float phaseIncrement, float frequency) {
	OutVal out;
	currentVoice->vd.spectral.samplePosition += phaseIncrement;
	int spFloor = (int)currentVoice->vd.spectral.samplePosition;
	int spCeil = spFloor + 1;
	spCeil = spCeil > currentVoice->vd.spectral.spectralDataSize ? spFloor : spCeil;
	float frac = currentVoice->vd.spectral.samplePosition - spFloor;
	out.L = currentVoice->vd.spectral.spectralData[spFloor];
	out.L *= 0.5;
	out.R = out.L;
	return out;
}

OutVal generateVoice(VoiceManager *vm, Voice *currentVoice, float phaseIncrement, float frequency) {
	OutVal out;
	float L = 0.0f;
	float R = 0.0f;
	int shape = 0;
	int sampleIndex = 0;
	int loop = 0;
	int detuneVoiceCount = getParameterValueAsInt(currentVoice->instrumentRef->detuneVoiceCount);
	float detuneSpreadIncrement = getParameterValue(currentVoice->instrumentRef->detuneSpread) / detuneVoiceCount;
	int detuneAmountIncrement = getParameterValueAsInt(currentVoice->instrumentRef->detuneRange) / detuneVoiceCount;
	float detunePan = 50.0 - (detuneSpreadIncrement * (detuneVoiceCount / 2.0f));
	float detuneFreqInc = detuneAmountIncrement * (frequency / 100.0);
	float detuneFreq = frequency - (detuneAmountIncrement * (detuneVoiceCount / 2.0f));
	// switch(currentVoice->type) {
	// 	case VOICE_TYPE_BLEP:
	// 		shape = getParameterValueAsInt(currentVoice->instrumentRef->shape);
	// 		switch(shape) {
	// 			case BLEP_RAMP:
	// 				L = blep_saw(currentVoice->leftPhase, phaseIncrement);
	// 				L *= 0.025;
	// 				break;
	// 			case BLEP_SQUARE:
	// 				L = blep_square(currentVoice->leftPhase, phaseIncrement);
	// 				L *= 0.025;
	// 				break;
	// 			case BLEP_SINE:
	// 				L = noblep_sine(currentVoice->leftPhase);
	// 				L *= 0.5;
	// 				break;
	// 		}
	// 		out = (OutVal){ L, L };
	// 		break;
	// 	case VOICE_TYPE_FM:
	// 		L = sineFmAlgo(currentVoice->source.operators, frequency, getParameterValueAsInt(currentVoice->instrumentRef->selectedAlgorithm));
	// 		out = (OutVal){ L, L };
	// 		// for(int i = 0; i < detuneVoiceCount; i++) {
	// 		// 	float dts = sineFmAlgo(currentVoice->source.operators, detuneFreq, getParameterValueAsInt(currentVoice->instrumentRef->selectedAlgorithm));
	// 		// 	detuneFreq += detuneFreqInc;
	// 		// 	out.L += dts * detunePan;
	// 		// 	out.R += dts * (1.0 - detunePan);
	// 		// 	detunePan += detuneSpreadIncrement;
	// 		// }
	// 		// out.L /= detuneVoiceCount;
	// 		// out.R /= detuneVoiceCount;
	// 		break;
	// 	case VOICE_TYPE_SAMPLE:
	// 		sampleIndex = getParameterValueAsInt(currentVoice->instrumentRef->sampleIndex);
	// 		loop = getParameterValueAsInt(currentVoice->instrumentRef->loopSample);
	// 		L = getSampleValueFwd(vm->samplePool->samples[sampleIndex], &currentVoice->samplePosition, phaseIncrement, SAMPLE_RATE, loop, SPT_FORWARD);
	// 		L *= 0.5;
	// 		out = (OutVal){ L, L };
	// 		break;
	// 	case VOICE_TYPE_GRAIN:
	// 		out = granularProcess(currentVoice->source.granularProcessor, phaseIncrement);
	// 	case VOICE_TYPE_SPECTRAL:
	// 		currentVoice->samplePosition += phaseIncrement;
	// 		int spFloor = (int)currentVoice->samplePosition;
	// 		int spCeil = spFloor + 1;
	// 		spCeil = spCeil > currentVoice->instrumentRef->spectralDataSize ? spFloor : spCeil;
	// 		float frac = currentVoice->samplePosition - spFloor;
	// 		L = currentVoice->instrumentRef->spectralData[spFloor];
	// 		L *= 0.5;
	// 		out = (OutVal){ L, L };
	// 	default:
	// 		break;
	// }
	float pan = getParameterValue(currentVoice->instrumentRef->panning);
	// out.L = currentVoice->filter->biquad->processSample(currentVoice->filter->biquad, out.L);
	out.R *= pan;
	out.L *= 1.0 - pan;
	return out;
}

void initVoicePool(VoiceManager *vm, int channelIndex, int voiceCount, Instrument *inst) {
	if(channelIndex >= MAX_SEQUENCER_CHANNELS || channelIndex < 0) {
		printf("out of bounds!\n");
	}
	if(voiceCount >= MAX_VOICES_PER_CHANNEL) voiceCount = MAX_VOICES_PER_CHANNEL;

	vm->voiceCount[channelIndex] = 0;

	for(int i = 0; i < voiceCount; i++) {
		// printf("allocating voice %i of %i (type %i) for channel %i\n", i + 1, voiceCount, inst->voiceType, channelIndex);
		vm->voicePools[channelIndex][i] = (Voice *)malloc(sizeof(Voice));
		if(vm->voicePools[channelIndex][i] == NULL) {
			fprintf(stderr, "Failed to allocate memory for voice %d in channel %d\n", i, channelIndex);
			return;
		}
		initialize_voice(vm->voicePools[channelIndex][i], inst);
		vm->voiceCount[channelIndex]++;
	}

	// printf("voice count of %i for channel %i, from starting input of %i", vm->voiceCount[channelIndex], channelIndex, voiceCount);
}

Voice *getFreeVoice(VoiceManager *vm, int seqChannel) {
	int voiceIndex = 0;
	switch(vm->voiceAllocation[seqChannel]) {
		case VA_FREE_OR_ZERO:
			for(int i = 0; i < vm->voiceCount[seqChannel]; i++) {
				if(vm->voicePools[seqChannel][i]->active == 0) {
					voiceIndex = i;
				}
			}
			break;
		default:
			break;
	}
	// printf("returning voice %i of channel %i\n", voiceIndex, seqChannel);
	return vm->voicePools[seqChannel][voiceIndex];
}

int selectSample(VoiceManager *vm, int channelIndex, int sampleIndex) {
	if(sampleIndex < 0 || sampleIndex >= vm->samplePool->sampleCount) {
		sampleIndex %= vm->samplePool->sampleCount - 1;
	}

	vm->instruments[channelIndex]->id.sampler.sample = vm->samplePool->samples[sampleIndex];
	return sampleIndex;
}

void incrementSampleParam(Parameter *sampleIndex, float delta) {
	int index = getParameterValueAsInt(sampleIndex);
	index += (int)delta;
}

void triggerVoice(Voice *voice, int note[NOTE_INFO_SIZE]) {
	voice->note[0] = note[0];
	voice->note[1] = note[1];
	voice->leftPhase = 0.0f;
	voice->rightPhase = 0.0f;
	voice->samplesElapsed = 0;
	voice->active = 1;
	for(int e = 0; e < voice->envCount; e++) {
		triggerEnvelope(voice->envelope[e]);
	}
}

void initialize_voice(Voice *voice, Instrument *inst) {
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
	// printf("active: %i\n", voice->active);
	voice->envCount = inst->envelopeCount;
	voice->lfoCount = inst->lfoCount;
	for(int i = 0; i < voice->envCount; i++) {
		voice->envelope[i] = createParamPointerAD(
		  voice->paramList,
		  voice->modList,
		  inst->envelopes[i]->stages[0].duration,
		  inst->envelopes[i]->stages[1].duration,
		  inst->envelopes[i]->stages[0].curvature,
		  inst->envelopes[i]->stages[1].curvature,
		  "ADp");
	}
	for(int i = 0; i < MAX_DETUNE; i++) {
		voice->detunePhase[i] = 0.0f;
	}

	switch(voice->type) {
		case VOICE_TYPE_BLEP:
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
			addModulation(voice->paramList, &voice->envelope[1]->base, voice->frequency, 400.5f, MO_ADD);
			break;

		case VOICE_TYPE_SAMPLE:
			voice->vd.sampler.sample = inst->id.sampler.sample;
			voice->vd.sampler.samplePosition = 0.0f; // Initialize sample position
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
			break;

		case VOICE_TYPE_FM:
			voice->vd.fm.operators[0] = createParamPointerOperator(voice->paramList, inst->id.fm.ops[0]->feedbackAmount, inst->id.fm.ops[0]->ratio, inst->id.fm.ops[0]->level);
			voice->vd.fm.operators[1] = createParamPointerOperator(voice->paramList, inst->id.fm.ops[1]->feedbackAmount, inst->id.fm.ops[1]->ratio, inst->id.fm.ops[1]->level);
			voice->vd.fm.operators[2] = createParamPointerOperator(voice->paramList, inst->id.fm.ops[2]->feedbackAmount, inst->id.fm.ops[2]->ratio, inst->id.fm.ops[2]->level);
			voice->vd.fm.operators[3] = createParamPointerOperator(voice->paramList, inst->id.fm.ops[3]->feedbackAmount, inst->id.fm.ops[3]->ratio, inst->id.fm.ops[3]->level);

			addModulation(voice->paramList, &voice->envelope[0]->base, voice->vd.fm.operators[0]->outLevel, 1.0f, MO_MUL);
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->vd.fm.operators[1]->outLevel, 1.0f, MO_MUL);
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->vd.fm.operators[2]->outLevel, 1.0f, MO_MUL);
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->vd.fm.operators[3]->outLevel, 1.0f, MO_MUL);
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
			break;
		case VOICE_TYPE_GRAIN:
			voice->vd.granular.granularProcessor = createGranularProcessor(inst->id.sampler.sample);
			break;
		case VOICE_TYPE_SPECTRAL:
			voice->vd.spectral.sample = inst->id.sampler.sample;
			voice->vd.sampler.samplePosition = 0.0f; // Initialize sample position
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
		default:
			break;
	}
	voice->filter = createFilter(kTransposeCanonical, secondOrderLPF, 250.0f, 10.0f);
}

void init_instrument(Instrument **instrument, VoiceType vt, SamplePool *samplePool) {
	// printf("init instrument\n");
	*instrument = (Instrument *)malloc(sizeof(Instrument));
	if(!*instrument) {
		printf("could not allocate memory for instrument in init_instrument.\n");
		return;
	}
	(*instrument)->modList = createModList();
	if(!(*instrument)->modList) {
		printf("modList creation failed in init_instrument.\n");
		return;
	}
	(*instrument)->paramList = createParamList();
	if(!(*instrument)->paramList) {
		printf("paramList creation failed in init_instrument.\n");
	}
	switch(vt) {
		case VOICE_TYPE_BLEP:
			(*instrument)->envelopeCount = 2;
			(*instrument)->lfoCount = 0;
			(*instrument)->id.blep.shape = createParameterEx((*instrument)->paramList, "shape", 0.0f, 0.0f, (float)BLEP_SHAPE_COUNT - 1, 1.0, 10.0);
			break;
		case VOICE_TYPE_SAMPLE:
			(*instrument)->envelopeCount = 1;
			(*instrument)->lfoCount = 0;
			(*instrument)->id.sampler.sp = samplePool;
			(*instrument)->id.sampler.sample = samplePool->samples[0];
			(*instrument)->id.sampler.getSample = getSampleValueFwd;
			(*instrument)->id.sampler.bitDepth = createParameterEx((*instrument)->paramList, "bitdepth", 24.0f, 8.0f, 24.0f, 1.0f, 4.0f);
			(*instrument)->id.sampler.sampleRate = createParameterEx((*instrument)->paramList, "bitrate", 44100.0f, 2000.0f, 44100.0f, 100.0f, 1000.0f);
			(*instrument)->id.sampler.sampleIndex = createParameterPro((*instrument)->paramList, "sample", 0, 0, (float)samplePool->sampleCount - 1, 1.0f, 10.0f, *instrument, updateSampleReferences);
			(*instrument)->id.sampler.loopSample = createParameterEx((*instrument)->paramList, "loop", 0, 0, 1.0, 1.0f, 1.0f);
			(*instrument)->id.sampler.playbackType = createParameterPro((*instrument)->paramList, "playback", 0, 0, (float)SPT_COUNT, 1.0f, 10.0f, *instrument, setSamplePlaybackFunction);
			(*instrument)->id.sampler.loopStartIndex = createParameterEx((*instrument)->paramList, "loop start", 0, 0, (float)samplePool->samples[0]->length, 100.0f, 1000.0f);
			(*instrument)->id.sampler.loopEndIndex = createParameterEx((*instrument)->paramList, "loop end", (float)samplePool->samples[0]->length - 1.0f, 1.0f, (float)samplePool->samples[0]->length, 100.0f, 1000.0f);
			break;
		case VOICE_TYPE_FM:
			(*instrument)->envelopeCount = 4;
			(*instrument)->lfoCount = 0;
			(*instrument)->id.fm.selectedAlgorithm = createParameterEx((*instrument)->paramList, "algo", 0, 0, ALGO_COUNT, 1.0f, 10.0f);
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				(*instrument)->id.fm.ops[i] = createOperator((*instrument)->paramList, 1);
			}
			setParameterBaseValue((*instrument)->id.fm.ops[1]->ratio, 2.0);
			setParameterBaseValue((*instrument)->id.fm.ops[2]->ratio, 3.0);
			setParameterBaseValue((*instrument)->id.fm.ops[3]->ratio, 4.0);
			break;
		case VOICE_TYPE_GRAIN:
			(*instrument)->id.granular.sample = samplePool->samples[2];
			(*instrument)->envelopeCount = 1;
			break;
		case VOICE_TYPE_SPECTRAL:
			(*instrument)->envelopeCount = 1;
			(*instrument)->lfoCount = 0;
			(*instrument)->id.spectral.sample = samplePool->samples[1];
			(*instrument)->id.spectral.playbackSpeed = createParameterEx((*instrument)->paramList, "playbackSpeed", 0.5f, 0.0f, 1.0f, 0.01f, 0.1f);
			Fft fft;
			int fftSize = 2048;
			initFFT(&fft, fftSize, 256, 5, false, true);
			float sampleFreq = 261.625;
			float phaseInc = sampleFreq / SAMPLE_RATE;
			for(int i = 0; i < samplePool->samples[0]->length; i++) {
				float spos = (float)i;
				float s = getSampleValueFwd(samplePool->samples[1], &spos, phaseInc, SAMPLE_RATE, 0, SPT_FORWARD);
				s *= 0.5;
				pushFrameToFFT(&fft, s);
				processFFTData(&fft);
			}
			(*instrument)->id.spectral.spectralDataSize = fft.rowCount * fft.fftSize;
			(*instrument)->id.spectral.spectralData = calloc(fft.rowCount * fft.fftSize / 4, sizeof(float));
			kiss_fftr_cfg icfg = kiss_fftr_alloc(fftSize, 1, 0, 0);
			for(int i = 0; i < fft.rowCount / 4; i++) {
				kiss_fftri(icfg, &fft.cpxvals[i * fft.freqCount * 4], &(*instrument)->id.spectral.spectralData[i * fft.fftSize]);
			}
			break;
	}
	(*instrument)->panning = createParameterEx((*instrument)->paramList, "panning", 0.5f, 0.0f, 1.0f, 0.01f, 0.1f);
	(*instrument)->detuneVoiceCount = createParameterEx((*instrument)->paramList, "detuneVoices", 4.0f, 0.0f, MAX_DETUNE, 1.0f, 1.0f);
	(*instrument)->detuneRange = createParameterEx((*instrument)->paramList, "detuneAmt", 10.0f, 1.0f, 100.0f, 1.00f, 10.0f);
	(*instrument)->detuneSpread = createParameterEx((*instrument)->paramList, "detuneSpread", 10.0f, 0.0f, 50.0f, 1.0f, 5.0f);

	for(int i = 0; i < (*instrument)->envelopeCount; i++) {
		(*instrument)->envelopes[i] = createAD((*instrument)->paramList, (*instrument)->modList, .25f, 4.25f, "AD1");
	}

	(*instrument)->voiceType = vt;
}

void updateSampleReferences(void *instrument) {
	Instrument *i = (Instrument *)instrument;

	if(!instrument) {
		printf("ERROR: NULL instrument pointer.\n");
		return;
	}
	if(i->voiceType != VOICE_TYPE_SAMPLE) {
		printf("ERROR: voice type is not SAMPLE: %i\n", i->voiceType);
		return;
	}

	int spidx = getParameterValueAsInt(i->id.sampler.sampleIndex);
	i->id.sampler.sample = i->id.sampler.sp->samples[spidx];
	setParameterBaseValue(i->id.sampler.loopStartIndex, 0);
	setParameterValue(i->id.sampler.loopStartIndex, 0);
	setParameterMaxValue(i->id.sampler.loopStartIndex, i->id.sampler.sample->length);
	setParameterMaxValue(i->id.sampler.loopEndIndex, i->id.sampler.sample->length);
	setParameterBaseValue(i->id.sampler.loopEndIndex, i->id.sampler.sample->length - 1.0);
	setParameterValue(i->id.sampler.loopEndIndex, i->id.sampler.sample->length - 1.0);
}

void setSamplePlaybackFunction(void *instrument) {
	Instrument *i = (Instrument *)instrument;

	if(!instrument) {
		printf("ERROR: NULL instrument pointer.\n");
		return;
	}
	if(i->voiceType != VOICE_TYPE_SAMPLE) {
		printf("ERROR: voice type is not SAMPLE: %i\n", i->voiceType);
		return;
	}

	int selectedPlaybackType = getParameterValueAsInt(i->id.sampler.playbackType);
	switch(selectedPlaybackType) {
		case SPT_REVERSE:
		case SPT_REVERSE_PINGPONG:
			i->id.sampler.getSample = getSampleValueRev;
			break;
		default:
		case SPT_FORWARD:
		case SPT_FORWARD_PINGPONG:
			i->id.sampler.getSample = getSampleValueFwd;
			break;
	}
}

GranularProcessor *createGranularProcessor(Sample *s) {
	GranularProcessor *gp = (GranularProcessor *)malloc(sizeof(GranularProcessor));
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
	for(int i = 0; i < GRAIN_WINDOW_SIZE; i++) {
		gp->grainWindow[i] = sin((i / GRAIN_WINDOW_SIZE) * TWO_PI);
		gp->windowIndex[i] = 0;
	}
	for(int i = 0; i < GRAIN_COUNT; i++) {
		float startPos = rand() * GRANULAR_BUFFER_SIZE / 4.0;
		gp->grainStartPos[i] = createParameter(gp->paramList, "gPos", rand() * GRANULAR_BUFFER_SIZE / 4.0, 0.0f, (float)GRANULAR_BUFFER_SIZE);
		gp->grainReadPos[i] = startPos;
	}

	return gp;
}

OutVal granularProcess(GranularProcessor *gp, float phaseIncrement) {
	OutVal result = { 0.0f, 0.0f };

	for(int i = 0; i < GRAIN_COUNT; i++) {
		float adjusted_phaseinc_sample = phaseIncrement * (SAMPLE_RATE / (float)gp->sample->sampleRate);
		gp->grainReadPos[i] += adjusted_phaseinc_sample;
		gp->windowIndex[i] += phaseIncrement;

		if(gp->windowIndex[i] >= GRAIN_WINDOW_SIZE) {
			gp->windowIndex[i] -= GRAIN_WINDOW_SIZE;
		}

		if(gp->grainReadPos[i] >= gp->sample->length) {
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
