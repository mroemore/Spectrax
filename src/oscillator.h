#ifndef OSCILLATOR_H
#define OSCILLATOR_H

#include "modsystem.h"
#include <stddef.h>
#include <string.h>

/* The engine's default rate. The actual rate is runtime state (g_sampleRate):
 * a handheld's ALSA device need not accept 44100 at all -- the RG35XX SP's
 * exposes only 192000 (hw), a broken INT_MAX (sysdefault) and 48000 (dmix) --
 * so the app runs at whatever rate the opened stream reports, and every phase
 * increment is computed against that. This macro is only the fallback used
 * before a stream exists. */
#define SAMPLE_RATE_DEFAULT (44100)

extern float g_sampleRate;
#define SAMPLE_RATE (g_sampleRate)
#define BLEP_SIZE 32
#define ALGO_SIZE 6
#define ALGO_COUNT 7
#define OP_COUNT 4

typedef enum {
	BLEP_SINE,
	BLEP_SQUARE,
	BLEP_RAMP,
	BLEP_SHAPE_COUNT
} BlepShape;

typedef struct {
	float (*generate)(float phase, float increment);
} Oscillator;

typedef struct {
	float feedbackAmount;
	float ratio;
	float level;
	float outLevel;
	float pitch; /* continuous Hz offset (added on top of fundamental*ratio) */
} OperatorData;

typedef struct {
	float (*generate)(float phase, float increment);
	int generated;
	float phase;
	float phase_increment;
	float currentVal;
	float lastVal;
	float modVal;
	Parameter *feedbackAmount;
	Parameter *ratio;
	Parameter *level;
	Parameter *outLevel;
	Parameter *pitch; /* continuous Hz offset on top of fundamental*ratio */
} Operator;

static int fm_algorithm[ALGO_COUNT * ALGO_SIZE][2] = {
	{ 3, 2 },
	{ 2, 1 },
	{ 1, 0 },
	{ 0, -1 },
	{ -1, -1 },
	{ -1, -1 },
	{ 3, 2 },
	{ 2, 0 },
	{ 1, 0 },
	{ 0, -1 },
	{ -1, -1 },
	{ -1, -1 },
	{ 3, 1 },
	{ 2, 1 },
	{ 1, 0 },
	{ 0, -1 },
	{ -1, -1 },
	{ -1, -1 },
	{ 3, 1 },
	{ 3, 2 },
	{ 2, 1 },
	{ 1, 0 },
	{ 0, -1 },
	{ -1, -1 },
	{ 3, 0 },
	{ 2, 0 },
	{ 1, 0 },
	{ 0, -1 },
	{ -1, -1 },
	{ -1, -1 },
	{ 3, -1 },
	{ 2, -1 },
	{ 1, -1 },
	{ 0, -1 },
	{ -1, -1 },
	{ -1, -1 },
	{ 3, 2 },
	{ 2, 1 },
	{ 1, 0 },
	{ 2, 0 },
	{ 0, -1 },
	{ -1, -1 },
};

float sawtooth_wave(float phase);
float sine_wave(float phase, float mod);
float square_wave(float phase);
float band_limited_sawtooth(float phase, float increment);
float band_limited_square(float phase, float increment);
float sine_fm(Operator *ops[4], float frequency);
float sineFmAlgo(Operator *ops[OP_COUNT], float frequency, int algorithm);
float sine_op(Operator *op, float frequency, float mod);
Operator *createOperator(ParamList *paramList, float ratio);
Operator *createParamPointerOperator(ParamList *paramList, Parameter *fbamt, Parameter *ratio, Parameter *level);
Operator *createVoiceOperator(ParamList *paramList, const Operator *proto);
void syncOperatorFromInstrument(Operator *voiceOp, const Operator *instOp);
float opFrequencyAt(const Operator *op, float fundamental);
void freeOperator(Operator *op);

#endif // OSCILLATOR_H
