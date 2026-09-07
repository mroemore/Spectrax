#include "oscillator.h"
#include "modsystem.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

float sawtooth_wave(float phase) {
	return 2.0f * phase - 1.0f;
}

float sine_wave(float phase, float mod) {
	return sinf(TWO_PI * (phase + mod));
}

float sine_fm(Operator *ops[4], float frequency) {
	float a = sine_op(ops[2], frequency, 0.0f);
	float b = sine_op(ops[1], frequency, a);
	float c = sine_op(ops[0], frequency, b);
	return c;
}

float sineFmAlgo(Operator *ops[], float frequency, int algorithm) {
	int algoOffset = algorithm * ALGO_SIZE;
	float out = 0.0f;
	for(int i = 0; i < OP_COUNT; i++) {
		ops[i]->lastVal = ops[i]->currentVal;
		ops[i]->currentVal = 0.0f;
		ops[i]->modVal = 0.0f;
		ops[i]->generated = 0;
	}

	for(int i = algoOffset; i < algoOffset + ALGO_SIZE; i++) {
		int modDstIndex = fm_algorithm[i][1];
		int modSrcIndex = fm_algorithm[i][0];
		if(modSrcIndex == -1) {
			continue;
		}
		if(!ops[modSrcIndex]->generated) {
			ops[modSrcIndex]->currentVal = sine_op(ops[modSrcIndex], frequency, ops[modSrcIndex]->modVal);
			ops[modSrcIndex]->generated = 1;
		}
		if(modDstIndex == -1) {
			out += ops[modSrcIndex]->currentVal;
		} else {
			ops[modDstIndex]->modVal += ops[modSrcIndex]->currentVal;
		}
	}

	return out;
}

float sine_op(Operator *op, float frequency, float mod) {
	float phase_inc = opFrequencyAt(op, frequency) / SAMPLE_RATE;
	float feedbackLevel = getParameterValue(op->feedbackAmount) * op->lastVal;
	op->phase = fmodf(op->phase + phase_inc, 1.0f);
	float a = sinf(TWO_PI * (op->phase + mod));
	float lvl = getParameterValue(op->outLevel) * getParameterValue(op->level);
	return a * lvl;
}

float square_wave(float phase) {
	return phase < 0.5f ? 1.0f : -1.0f;
}

Operator *createOperator(ParamList *paramList, float ratio) {
	Operator *op = (Operator *)malloc(sizeof(Operator));
	op->generated = 0;
	op->phase = 0.0f;
	op->phase_increment = 0.0f;
	op->currentVal = 0.0f;
	op->lastVal = 0.0f;
	op->modVal = 0.0f;
	op->feedbackAmount = createParameterEx(paramList, "feedback", 0.0f, 0.0f, 1.0f, 0.01f, 0.10f);
	op->ratio = createParameterEx(paramList, "ratio", ratio, 0.25f, 30.0f, 0.01f, 1.0f);
	op->level = createParameter(paramList, "level", 0.1f, 0.0f, 1.0f);
	op->outLevel = createParameter(paramList, "outLevel", 0.5f, 0.0f, 1.0f);
	op->pitch = createParameterEx(paramList, "pitch", 0.0f, -1000.0f, 1000.0f, 0.1f, 1.0f);
	op->generate = sine_wave;
	return op;
}

Operator *createParamPointerOperator(ParamList *paramList, Parameter *fbamt, Parameter *ratio, Parameter *level) {
	Operator *op = (Operator *)malloc(sizeof(Operator));
	op->generated = 0;
	op->phase = 0.0f;
	op->currentVal = 0.0f;
	op->lastVal = 0.0f;
	op->modVal = 0.0f;
	op->feedbackAmount = fbamt;
	op->ratio = ratio;
	op->level = level;
	op->outLevel = createParameter(paramList, "outLevel", 0.5f, 0.0f, 1.0f);
	op->pitch = createParameterEx(paramList, "pitch", 0.0f, -1000.0f, 1000.0f, 0.1f, 1.0f);
	return op;
}

void freeOperator(Operator *op) {
	freeParameter(op->ratio);
	freeParameter(op->level);
	free(op);
}

/* Task 2.1: a voice-owned FM operator. Creates fresh feedback/ratio/level/
 * outLevel params in the voice's paramList (via createOperator) and copies
 * the instrument operator's current baseValues so a fresh voice matches the
 * dials. Unlike createParamPointerOperator this does NOT alias the
 * instrument params — the voice owns its storage and the per-buffer base
 * sync (syncOperatorFromInstrument) keeps it live. */
Operator *createVoiceOperator(ParamList *paramList, const Operator *proto) {
	Operator *op = createOperator(paramList, 1.0f);
	if(!op) {
		return NULL;
	}
	syncOperatorFromInstrument(op, proto);
	return op;
}

void syncOperatorFromInstrument(Operator *voiceOp, const Operator *instOp) {
	if(!voiceOp || !instOp) {
		return;
	}
	setParameterBaseValue(voiceOp->feedbackAmount, instOp->feedbackAmount->baseValue);
	setParameterBaseValue(voiceOp->ratio, instOp->ratio->baseValue);
	setParameterBaseValue(voiceOp->level, instOp->level->baseValue);
	setParameterBaseValue(voiceOp->outLevel, instOp->outLevel->baseValue);
	setParameterBaseValue(voiceOp->pitch, instOp->pitch->baseValue);
}

float opFrequencyAt(const Operator *op, float fundamental) {
	return fundamental * getParameterValue(op->ratio) + getParameterValue(op->pitch);
}
