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

VoiceManager *createVoiceManager(Settings *settings, SamplePool *sp, WavetablePool *wtp, PresetBank *pb) {
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
		init_instrument(&vm->instruments[i], VOICE_TYPE_SAMPLE, sp, pb);
		vm->instruments[i]->vm = vm;
		/* Task 6: the boot preset is a stand-in for "no preset yet"
		 * (presetBank starts empty — patches[0].name is uninitialised).
		 * Zero the name so applyInstrumentPreset's trailing
		 * markPresetLoaded call skips the snapshot capture and leaves
		 * loaded.name empty (no baseline). */
		if(pb->presetCount == 0) {
			pb->patches[0].name[0] = '\0';
		}
		applyInstrumentPreset(vm->instruments[i], pb->patches[0]);
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
	// Ownership: v->paramList owns ALL parameters (frequency, volume,
	// mod outputs, LFO rate/phase, operator outLevels). v->modList owns
	// the mod structs (Envelope/LFO embed Mod base as their first member,
	// so freeing &base frees the whole struct). FM operator ratio/level
	// params are instrument-owned (createParamPointerOperator) and the
	// sample is pool-owned; neither is freed here.

	// Free mod structs only — their output params are owned by paramList.
	for(int i = 0; i < v->modList->count; i++) {
		free(v->modList->mods[i]);
	}
	free(v->modList);

	// Free all remaining params (frequency, volume, mod outputs, LFO
	// rate/phase, operator outLevels) exactly once.
	freeParamList(v->paramList);

	// Free operator structs only — feedbackAmount/ratio/level are
	// instrument-owned, outLevel is owned by paramList (already freed).
	switch(v->type) {
		case VOICE_TYPE_FM:
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				free(v->vd.fm.operators[i]);
			}
			break;
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
	out.L = getSampleValueFwd(currentVoice->vd.sampler.samplePool->samples[sampleIndex], &currentVoice->vd.sampler.samplePosition, phaseIncrement, loop);
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

OutVal generateGranular(Voice *currentVoice, float phaseIncrement, float frequency) {
	OutVal out;
	printf("ERROR: stub generate func.\n\n");
	out.L = 0;
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

	out = currentVoice->generate(currentVoice, phaseIncrement, frequency);

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
			voice->generate = generateBlep;
			break;

		case VOICE_TYPE_SAMPLE:
			voice->vd.sampler.sample = inst->id.sampler.sample;
			voice->vd.sampler.samplePosition = 0.0f; // Initialize sample position
			voice->vd.sampler.samplePool = inst->id.sampler.sp;
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
			voice->generate = generateSample;
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
			voice->generate = generateFM;
			break;
		case VOICE_TYPE_GRAIN:
			voice->vd.granular.granularProcessor = createGranularProcessor(inst->id.sampler.sample);
			voice->generate = generateGranular;
			break;
		case VOICE_TYPE_SPECTRAL:
			voice->vd.spectral.sample = inst->id.sampler.sample;
			voice->vd.spectral.samplePosition = 0.0f; // Initialize sample position
			addModulation(voice->paramList, &voice->envelope[0]->base, voice->volume, 1.0f, MO_MUL);
			voice->generate = generateSpectral;
		default:
			break;
	}
	voice->filter = createFilter(kTransposeCanonical, secondOrderLPF, 250.0f, 10.0f);
}

void initDefaultFmPreset(Preset *p) {
	Preset p1 = (Preset){
		.voiceType = VOICE_TYPE_FM,
		.pd.fm.selectedAlgorithm = 0,
		.modSettingsCount = 4
	};
	for(int i = 0; i < MAX_FM_OPERATORS; i++) {
		p1.pd.fm.ops[i].feedbackAmount = 0.0;
		p1.pd.fm.ops[i].level = 0.25;
		p1.pd.fm.ops[i].outLevel = 1.0;
		p1.pd.fm.ops[i].ratio = 1.0;
	}
	p1.pd.fm.ops[1].ratio = 2.0;
	p1.pd.fm.ops[2].ratio = 3.0;
	p1.pd.fm.ops[3].ratio = 5.0;

	for(int i = 0; i < p1.modSettingsCount; i++) {
		initADPresetData(&p1.modSettings[i], 0.1f, 4.5f, 0.75f, 0.75f);
	}
	*p = p1;
}

/* Re-initialize every voice on the channel that owns `instrument`,
 * so that voice envelope/FM-param aliases point at the NEW instrument
 * params (not freed ones). Used after applyInstrumentPreset at runtime.
 * Frees each voice with freeVoice and re-runs initialize_voice on a
 * fresh malloc'd Voice struct — matches freeVoice's ownership model. */
void rebuildVoicesForInstrument(VoiceManager *vm, Instrument *instrument) {
	if(!vm || !instrument) {
		return;
	}
	for(int ch = 0; ch < vm->enabledChannels; ch++) {
		if(vm->instruments[ch] != instrument) {
			continue;
		}
		for(int i = 0; i < vm->voiceCount[ch]; i++) {
			Voice *v = vm->voicePools[ch][i];
			freeVoice(v);
			v = (Voice *)malloc(sizeof(Voice));
			vm->voicePools[ch][i] = v;
			if(v) {
				initialize_voice(v, instrument);
			}
		}
		return;
	}
}

/* Task 2: proper teardown of an Instrument that was allocated via
 * init_instrument. Frees the modList (Mod entries + list struct) and
 * the paramList (Parameter structs + list struct), then the
 * Instrument itself.
 *
 * IMPORTANT: do NOT call freeVoice here — the voices that point back
 * at this instrument are owned by VoiceManager's voicePools[][] and
 * are either being torn down (initVoicePool re-allocates fresh ones)
 * or being re-pointed at the new instrument
 * (rebuildVoicesForInstrument re-initializes them in place). The
 * voices own their OWN paramList/modList (allocated by initialize_voice),
 * but they ALIAS some params from inst->paramList (envelope stage
 * durations, FM operator ratio/level/feedback via createParamPointerOperator).
 * Those aliases become stale the moment we free inst->paramList; the
 * caller must guarantee either (a) the voices are about to be
 * re-initialized (via initVoicePool or rebuildVoicesForInstrument)
 * or (b) inst->rebuilding is set so the audio thread skips the
 * channel. Both paths do so via setInstrumentVoiceType /
 * setChannelVoiceCount below. */
static void freeInstrument(Instrument *inst) {
	if(!inst) return;
	/* Tear down modList first — clearModList frees only the Mod/Envelope/
	 * LFO/Random struct itself; the inner Params (output, stage duration,
	 * stage curvature, rate, phase) are owned by paramList and freed by the
	 * following clearParamList. cleanupModSystem would double-free them.
	 * Order matches applyInstrumentPreset's boot-time reset. */
	clearModList(inst->modList);
	clearParamList(inst->paramList);
	free(inst->modList);
	free(inst->paramList);
	free(inst);
}

/* Task 2: instrument-chip "type swap" — replace the channel's
 * Instrument with a fresh one of the requested VoiceType (defaults,
 * no preset loaded). Returns false for invalid args, invalid VoiceType,
 * or init_instrument failure. The OLD instrument's rebuilding flag is
 * set BEFORE the swap (audio thread sees rebuilding=true and skips the
 * channel); the FRESH instrument's flag is cleared AFTER
 * rebuildVoicesForInstrument finishes (audio thread resumes on the
 * new instrument). */
bool setInstrumentVoiceType(VoiceManager *vm, int channel, VoiceType vt) {
	if(!vm || channel < 0 || channel >= vm->enabledChannels) return false;
	if(vt != VOICE_TYPE_SAMPLE && vt != VOICE_TYPE_FM && vt != VOICE_TYPE_BLEP) return false;
	Instrument *old = vm->instruments[channel];
	if(old) old->rebuilding = true;
	Instrument *fresh = NULL;
	/* The Instrument carries a presetBank pointer and a back-pointer to
	 * vm; the samplePool is sourced from vm (Instrument itself has no
	 * samplePool field — only id.sampler.sp does, and that's only valid
	 * for VOICE_TYPE_SAMPLE). For FM/BLEP the samplePool arg is unused
	 * by init_instrument, so vm->samplePool is always the right input. */
	init_instrument(&fresh, vt, vm->samplePool,
		old ? old->presetBank : NULL);
	if(!fresh) {
		/* init failed — undo the rebuilding flag so the audio thread
		 * resumes on the old instrument. */
		if(old) old->rebuilding = false;
		return false;
	}
	fresh->vm = vm;
	if(old) {
		fresh->presetBank = old->presetBank;
	}
	vm->instruments[channel] = fresh;
	freeInstrument(old);
	rebuildVoicesForInstrument(vm, fresh);
	fresh->rebuilding = false;
	return true;
}

/* Task 2: instrument-chip "voice-count resize" — re-allocate the
 * channel's voice pool to `count` (clamped 1..MAX_VOICES_PER_CHANNEL
 * by the caller; initVoicePool additionally caps at
 * MAX_VOICES_PER_CHANNEL but does not floor to 1, so we reject 0
 * here rather than silently passing it through). Returns false for
 * invalid args / out-of-range count. The instrument's rebuilding flag
 * is set during the resize so the audio thread skips the channel
 * while voices are being torn down and re-initialized.
 *
 * NOTE: the existing initVoicePool only re-allocates the first
 * `count` slots; any voices beyond the new count in voicePools[ch][]
 * are NOT freed. Callers that shrink the pool may leak the tail.
 * Acceptable for the chip UI (chips always grow, never shrink in
 * practice) and matches the pre-existing pool semantics. */
bool setChannelVoiceCount(VoiceManager *vm, int channel, int count) {
	if(!vm || channel < 0 || channel >= vm->enabledChannels) return false;
	if(count < 1 || count > MAX_VOICES_PER_CHANNEL) return false;
	Instrument *inst = vm->instruments[channel];
	if(inst) inst->rebuilding = true;
	initVoicePool(vm, channel, count, inst);
	if(inst) inst->rebuilding = false;
	return true;
}

void applyInstrumentPreset(Instrument *instrument, Preset p) {
	/* Task 8: gate the audio thread out while we tear down + rebuild the
	 * param/mod lists. The PortAudio callback reads these lists every
	 * buffer; freeing them from the GUI thread while it iterates is a
	 * use-after-free. The flag is cleared at the end of this function;
	 * callers that ALSO rebuild the voice pool wrap that separately. */
	if(instrument) {
		instrument->rebuilding = true;
	}
	clearModList(instrument->modList);
	clearParamList(instrument->paramList);
	instrument->voiceType = p.voiceType;
	switch(p.voiceType) {
		case VOICE_TYPE_FM:
			instrument->id.fm.selectedAlgorithm = createParameterEx(instrument->paramList, "algo", 0, 0, ALGO_COUNT, 1.0f, 1.0f);
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				instrument->id.fm.ops[i] = createOperator(instrument->paramList, 1);
				setParameterBaseValue(instrument->id.fm.ops[i]->ratio, p.pd.fm.ops[i].ratio);
				setParameterValue(instrument->id.fm.ops[i]->ratio, p.pd.fm.ops[i].ratio);
				setParameterBaseValue(instrument->id.fm.ops[i]->level, p.pd.fm.ops[i].level);
				setParameterValue(instrument->id.fm.ops[i]->level, p.pd.fm.ops[i].level);
				setParameterBaseValue(instrument->id.fm.ops[i]->feedbackAmount, p.pd.fm.ops[i].feedbackAmount);
				setParameterValue(instrument->id.fm.ops[i]->feedbackAmount, p.pd.fm.ops[i].feedbackAmount);
				setParameterBaseValue(instrument->id.fm.ops[i]->outLevel, p.pd.fm.ops[i].outLevel);
				setParameterValue(instrument->id.fm.ops[i]->outLevel, p.pd.fm.ops[i].outLevel);
			}
			setParameterBaseValue(instrument->id.fm.selectedAlgorithm, (float)p.pd.fm.selectedAlgorithm);
			setParameterValue(instrument->id.fm.selectedAlgorithm, (float)p.pd.fm.selectedAlgorithm);
			break;
		case VOICE_TYPE_SAMPLE:
			instrument->id.sampler.sp = (instrument->vm) ? instrument->vm->samplePool : NULL;
			instrument->id.sampler.sample = (instrument->id.sampler.sp && instrument->id.sampler.sp->sampleCount > 0) ? instrument->id.sampler.sp->samples[0] : NULL;
			instrument->id.sampler.getSampleValue = getSampleValueFwd;
			int sl = (instrument->id.sampler.sample) ? instrument->id.sampler.sample->length : 1;
			int smax = (instrument->id.sampler.sp && instrument->id.sampler.sp->sampleCount > 0) ? instrument->id.sampler.sp->sampleCount - 1 : 0;
			instrument->id.sampler.bitDepth = createParameterEx(instrument->paramList, "bitdepth", (float)p.pd.sampler.bitDepth, 8.0f, 24.0f, 1.0f, 4.0f);
			instrument->id.sampler.sampleRate = createParameterEx(instrument->paramList, "bitrate", (float)p.pd.sampler.sampleRate, 2000.0f, 44100.0f, 100.0f, 1000.0f);
			instrument->id.sampler.sampleIndex = createParameterPro(instrument->paramList, "sample", (float)p.pd.sampler.sampleIndex, 0, (float)smax, 1.0f, 10.0f, instrument, updateSampleReferences);
			instrument->id.sampler.loopSample = createParameterEx(instrument->paramList, "loop", (float)p.pd.sampler.loopSample, 0, 1.0, 1.0f, 1.0f);
			instrument->id.sampler.playbackType = createParameterPro(instrument->paramList, "playback", (float)p.pd.sampler.playbackType, 0, (float)SPT_COUNT, 1.0f, 10.0f, instrument, setSamplePlaybackFunction);
			instrument->id.sampler.loopStartIndex = createParameterEx(instrument->paramList, "loop start", (float)p.pd.sampler.loopStartIndex, 0, (float)sl, 100.0f, 1000.0f);
			instrument->id.sampler.loopEndIndex = createParameterEx(instrument->paramList, "loop end", (float)p.pd.sampler.loopEndIndex, 1.0f, (float)sl, 100.0f, 1000.0f);
			break;
		case VOICE_TYPE_BLEP:
			instrument->id.blep.shape = createParameterEx(instrument->paramList, "shape", (float)p.pd.blep.shape, 0.0f, (float)BLEP_SHAPE_COUNT - 1, 1.0, 10.0);
			break;
		default:
			break;
	}
	instrument->envelopeCount = 0;
	instrument->lfoCount = 0;
	for(int i = 0; i < p.modSettingsCount; i++) {
		switch(p.modSettings[i].type) {
			case MT_ENV:
				instrument->envelopes[instrument->envelopeCount] = malloc(sizeof(Envelope));
				initEnvelopeFromPreset(&p.modSettings[i], instrument->envelopes[instrument->envelopeCount], instrument->paramList, instrument->modList);
				instrument->envelopeCount++;
				break;
			case MT_LFO: {
				LFO *lfo = (LFO *)malloc(sizeof(LFO));
				if(lfo) {
					initLfoFromPreset(&p.modSettings[i].md.lfo, lfo, instrument->paramList, instrument->modList);
					instrument->lfoCount++;
				}
				break;
			}
			case MT_RND: {
				Random *rnd = (Random *)malloc(sizeof(Random));
				if(rnd) {
					initRandFromPreset(&p.modSettings[i].md.rand, rnd, instrument->paramList, instrument->modList);
				}
				break;
			}
			default:
				break;
		}
	}
	/* coreEnvelopeCount locks in the post-preset envelope count so
	 * rebuildVoicesForInstrument / downstream layout code can rely on
	 * the same value the preset set up. */
	instrument->coreEnvelopeCount = instrument->envelopeCount;

	/* Re-create the persistent instrument params (panning, detune
	 * controls) that clearParamList just freed along with the preset
	 * params. init_instrument creates them once; without this they dangle
	 * and generateVoice's detuneVoiceCount division crashes. */
	instrument->panning = createParameterEx(instrument->paramList, "panning", 0.5f, 0.0f, 1.0f, 0.01f, 0.1f);
	instrument->detuneVoiceCount = createParameterEx(instrument->paramList, "detuneVoices", 4.0f, 0.0f, MAX_DETUNE, 1.0f, 1.0f);
	instrument->detuneRange = createParameterEx(instrument->paramList, "detuneAmt", 10.0f, 1.0f, 100.0f, 1.00f, 10.0f);
	instrument->detuneSpread = createParameterEx(instrument->paramList, "detuneSpread", 10.0f, 0.0f, 50.0f, 1.0f, 5.0f);

	/* Re-create the preset selector param. clearParamList freed it along
	 * with everything else; without this the PRESET dial reads a dangling
	 * param whose range reads as [0,0], making drawDialGuiNode divide by
	 * zero (angle = NaN) and grinding the renderer to a halt. The caller
	 * (cb_setInstrumentPreset) writes the applied index into it afterwards. */
	instrument->selectedPresetIndex = createParameterPro(instrument->paramList, "preset", 0.0f, 0.0f,
		(float)(PRESET_BANK_SLOTS - 1), 1.0, 1.0, instrument, cb_setInstrumentPreset);
	/* Task 6: stamp the snapshot with the loaded preset's identity so
	 * the dirty bit flips to clean. Search the bank by name (rather
	 * than passing the index around) because some call sites — notably
	 * initVoices() at boot — apply a preset without an index at hand.
	 * Falls back to index=-1 if the preset hasn't been appended yet
	 * (the common case during init is "the first bank slot", and
	 * consumers should walk the bank by name anyway). */
	markPresetLoaded(instrument, p.name);
	if(instrument) {
		instrument->rebuilding = false;
	}
}

/* Task 6: stamp `inst->loaded` with the on-disk identity so the
 * dirty flag clears. Called from applyInstrumentPreset (load
 * refresh) and from the save paths in gui.c (initial save in
 * guiSavePreset and the overwrite-confirmation branch in
 * handlePresetUiInput — both commit on the same "live state now
 * matches disk" semantic).
 *
 * `name` is the on-disk identity to record. Pass NULL/"" to skip
 * the capture (the boot path applies an uninitialised Preset at
 * bank slot 0, and we don't want to record a garbage snapshot
 * there). `name` is also copied into inst->loaded.name so callers
 * don't have to walk the bank later. */
void markPresetLoaded(Instrument *inst, const char *name) {
	if(!inst || !name || !name[0]) {
		return;
	}
	/* Snapshot the live instrument state. presetFromInstrument reads
	 * every dial on the current voice type and serialises it into
	 * a Preset — we keep the result in inst->loaded.snapshot so the
	 * next dial-arrow edit can diff against it. (The dirty bit
	 * alone doesn't capture what changed; the snapshot is the
	 * baseline.) */
	inst->loaded.snapshot = presetFromInstrument(inst);
	strncpy(inst->loaded.snapshot.name, name, sizeof(inst->loaded.snapshot.name) - 1);
	inst->loaded.snapshot.name[sizeof(inst->loaded.snapshot.name) - 1] = '\0';
	strncpy(inst->loaded.name, name, sizeof(inst->loaded.name) - 1);
	inst->loaded.name[sizeof(inst->loaded.name) - 1] = '\0';
	/* Capture completes the sync-to-disk transaction: dirty flips
	 * to clean so the next edit starts a new diff. */
	inst->loaded.dirty = false;
}

/* Task 6: single-source-of-truth dirty check. Returns true only
 * when the live state has been edited since the last load/save.
 * The flag is always meaningful once markPresetLoaded has run at
 * least once (loaded.snapshot.name[0] != '\0'); a fresh instrument
 * with name[0] == '\0' has no baseline and is never considered
 * dirty. Consumers like the load-list gate in gui.c use this so
 * they don't push the confirm modal on a brand-new instrument the
 * user hasn't touched yet. */
bool isInstrumentDirty(const Instrument *inst) {
	if(!inst) {
		return false;
	}
	if(inst->loaded.name[0] == '\0') {
		return false;
	}
	return inst->loaded.dirty;
}

Preset presetFromInstrument(Instrument *instrument) {
	Preset p;
	memset(&p, 0, sizeof(p));
	p.voiceType = instrument->voiceType;
	switch(instrument->voiceType) {
		case VOICE_TYPE_FM:
			p.pd.fm.selectedAlgorithm = getParameterValueAsInt(instrument->id.fm.selectedAlgorithm);
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				p.pd.fm.ops[i].ratio = getParameterValue(instrument->id.fm.ops[i]->ratio);
				p.pd.fm.ops[i].level = getParameterValue(instrument->id.fm.ops[i]->level);
				p.pd.fm.ops[i].feedbackAmount = getParameterValue(instrument->id.fm.ops[i]->feedbackAmount);
				p.pd.fm.ops[i].outLevel = getParameterValue(instrument->id.fm.ops[i]->outLevel);
			}
			break;
		case VOICE_TYPE_SAMPLE:
			p.pd.sampler.bitDepth = getParameterValueAsInt(instrument->id.sampler.bitDepth);
			p.pd.sampler.sampleRate = getParameterValueAsInt(instrument->id.sampler.sampleRate);
			p.pd.sampler.loopSample = getParameterValueAsInt(instrument->id.sampler.loopSample) != 0;
			p.pd.sampler.sampleIndex = getParameterValueAsInt(instrument->id.sampler.sampleIndex);
			p.pd.sampler.playbackType = (SamplePlaybackType)getParameterValueAsInt(instrument->id.sampler.playbackType);
			p.pd.sampler.loopStartIndex = getParameterValueAsInt(instrument->id.sampler.loopStartIndex);
			p.pd.sampler.loopEndIndex = getParameterValueAsInt(instrument->id.sampler.loopEndIndex);
			break;
		case VOICE_TYPE_BLEP:
			p.pd.blep.shape = getParameterValueAsInt(instrument->id.blep.shape);
			break;
		default:
			break;
	}
	int n = instrument->modList ? instrument->modList->count : 0;
	if(n > MAX_ENVELOPES + MAX_LFOS) {
		n = MAX_ENVELOPES + MAX_LFOS;
	}
	p.modSettingsCount = n;
	for(int i = 0; i < n; i++) {
		Mod *mod = instrument->modList->mods[i];
		p.modSettings[i].type = mod->type;
		switch(mod->type) {
			case MT_ENV:
				saveEnvPreset(&p.modSettings[i].md.env, (Envelope *)mod);
				break;
			case MT_LFO:
				saveLfoPreset(&p.modSettings[i].md.lfo, (LFO *)mod);
				break;
			case MT_RND:
				saveRandPreset(&p.modSettings[i].md.rand, (Random *)mod);
				break;
			default:
				break;
		}
	}
	if(instrument->selectedPresetIndex) {
		/* p.name is deliberately left empty here — the save plumbing (Task 5)
		 * fills it with the name the user typed. */
	}
	return p;
}

void cb_setInstrumentPreset(void *instrument) {
	Instrument *i = (Instrument *)instrument;
	int presetIndex = getParameterValueAsInt(i->selectedPresetIndex);
	if(presetIndex < 0 || presetIndex >= PRESET_BANK_SLOTS) {
		return;
	}
	applyInstrumentPreset(i, i->presetBank->patches[presetIndex]);
	/* applyInstrumentPreset freed and rebuilt i's paramList/envelope/operator
	 * params via clearParamList; the preset selector param it just recreated
	 * starts at 0, so reflect the index we actually applied (direct field
	 * write: firing the onChange here would recurse into this callback). */
	i->selectedPresetIndex->baseValue = (float)presetIndex;
	i->selectedPresetIndex->currentValue = (float)presetIndex;
	/* voices that previously aliased those Param structs must be rebuilt so
	 * their pointers track the new ones. The audio thread iterates the
	 * voice pool every buffer -- hold the rebuilding flag across the
	 * free/malloc so it doesn't deref a freed Voice. */
	if(i->vm) {
		i->rebuilding = true;
		rebuildVoicesForInstrument(i->vm, i);
		i->rebuilding = false;
	}
}

void initPresetBank(PresetBank *pb) {
	pb->presetCount = 0;
}

/* Task 8: a usable default FM patch for empty bank slots. The bank
 * presents PRESET_BANK_SLOTS navigable slots; slots past presetCount
 * (the on-disk presets) hold this so PREV/NEXT into a blank slot gives
 * the user something real to edit. Four operators with 1:1 ratios and
 * a flat algorithm -- a harmless init sound. */
Preset makeDefaultFmPreset(void) {
	Preset p;
	memset(&p, 0, sizeof(p));
	p.name[0] = '\0';
	p.voiceType = VOICE_TYPE_FM;
	for(int i = 0; i < MAX_FM_OPERATORS; i++) {
		p.pd.fm.ops[i].ratio = 1.0f;
		p.pd.fm.ops[i].level = 0.5f;
		p.pd.fm.ops[i].outLevel = 0.5f;
		p.pd.fm.ops[i].feedbackAmount = 0.0f;
	}
	p.pd.fm.selectedAlgorithm = 0;
	return p;
}

/* Fill slots [presetCount, PRESET_BANK_SLOTS) with the default FM patch
 * so PREV/NEXT can walk the full slot range without tripping over
 * uninitialised patches. Called after directory load + at bank init. */
void fillEmptyBankSlots(PresetBank *pb) {
	if(!pb) {
		return;
	}
	Preset def = makeDefaultFmPreset();
	for(int i = pb->presetCount; i < PRESET_BANK_SLOTS; i++) {
		pb->patches[i] = def;
	}
}

void addPresetToBank(PresetBank *pb, Preset p) {
	printf("adding preset.\n");
	if(pb->presetCount < MAX_PATCHES) {
		pb->patches[pb->presetCount] = p;
		pb->presetCount++;
	} else {
		printf("WARNING: Max patches reached, not adding patch.\n");
	}
}

void init_instrument(Instrument **instrument, VoiceType vt, SamplePool *samplePool, PresetBank *pb) {
	*instrument = (Instrument *)malloc(sizeof(Instrument));
	if(!*instrument) {
		printf("could not allocate memory for instrument in init_instrument.\n");
		return;
	}
	/* Task 6: dirty-tracking baseline. malloc leaves the bytes
	 * undefined; if we don't zero them here the first isInstrumentDirty
	 * call can read a garbage name and lie about whether the
	 * instrument has been captured yet. */
	(*instrument)->loaded.name[0] = '\0';
	(*instrument)->loaded.dirty = false;
	(*instrument)->loaded.snapshot.name[0] = '\0';
	(*instrument)->rebuilding = false;
	(*instrument)->modList = createModList();
	if(!(*instrument)->modList) {
		printf("modList creation failed in init_instrument.\n");
		return;
	}
	(*instrument)->paramList = createParamList();
	if(!(*instrument)->paramList) {
		printf("paramList creation failed in init_instrument.\n");
	}

	(*instrument)->presetBank = pb;

	(*instrument)->selectedPresetIndex = createParameterPro((*instrument)->paramList, "preset", 0.0f, 0.0f, (float)(PRESET_BANK_SLOTS - 1), 1.0, 1.0, (*instrument), cb_setInstrumentPreset);
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
			/* An empty pool is legal (createVoiceManager inits every channel
			 * as a SAMPLE instrument before the preset is applied); never
			 * deref samples[0] when nothing was loaded. */
			(*instrument)->id.sampler.sample = (samplePool->sampleCount > 0) ? samplePool->samples[0] : NULL;
			int sampleLen = (samplePool->sampleCount > 0) ? samplePool->samples[0]->length : 1;
			int sampleCountMax = (samplePool->sampleCount > 0) ? samplePool->sampleCount - 1 : 0;
			(*instrument)->id.sampler.getSampleValue = getSampleValueFwd;
			(*instrument)->id.sampler.bitDepth = createParameterEx((*instrument)->paramList, "bitdepth", 24.0f, 8.0f, 24.0f, 1.0f, 4.0f);
			(*instrument)->id.sampler.sampleRate = createParameterEx((*instrument)->paramList, "bitrate", 44100.0f, 2000.0f, 44100.0f, 100.0f, 1000.0f);
			(*instrument)->id.sampler.sampleIndex = createParameterPro((*instrument)->paramList, "sample", 0, 0, (float)sampleCountMax, 1.0f, 10.0f, *instrument, updateSampleReferences);
			(*instrument)->id.sampler.loopSample = createParameterEx((*instrument)->paramList, "loop", 0, 0, 1.0, 1.0f, 1.0f);
			(*instrument)->id.sampler.playbackType = createParameterPro((*instrument)->paramList, "playback", 0, 0, (float)SPT_COUNT, 1.0f, 10.0f, *instrument, setSamplePlaybackFunction);
			(*instrument)->id.sampler.loopStartIndex = createParameterEx((*instrument)->paramList, "loop start", 0, 0, (float)sampleLen, 100.0f, 1000.0f);
			(*instrument)->id.sampler.loopEndIndex = createParameterEx((*instrument)->paramList, "loop end", (float)(sampleLen - 1), 1.0f, (float)sampleLen, 100.0f, 1000.0f);
			break;
		case VOICE_TYPE_FM:
			(*instrument)->envelopeCount = 4;
			(*instrument)->lfoCount = 0;
			(*instrument)->id.fm.selectedAlgorithm = createParameterEx((*instrument)->paramList, "algo", 0, 0, ALGO_COUNT, 1.0f, 1.0f);
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
				float s = getSampleValueFwd(samplePool->samples[1], &spos, phaseInc, 0);
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
	(*instrument)->coreEnvelopeCount = (*instrument)->envelopeCount;
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
			i->id.sampler.getSampleValue = getSampleValueRev;
			break;
		default:
		case SPT_FORWARD:
		case SPT_FORWARD_PINGPONG:
			i->id.sampler.getSampleValue = getSampleValueFwd;
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
		gp->grainWindow[i] = sin(((float)i / GRAIN_WINDOW_SIZE) * TWO_PI);
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
