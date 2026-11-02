/* test_engine_rate.c — the engine's rate is a constant, not a device fact.
 *
 * Criterion C: a voice's samples must be a pure function of its settings.
 * If any synthesis timebase read the opened stream's rate, the same patch
 * would play at a different pitch on every device -- the RG35XX SP's codec
 * runs at 48000 and a desktop's at 44100, which is an 8.8% difference. So
 * the engine rate is a compile-time constant (SPECTRAX_ENGINE_RATE) and the
 * device's rate is a boundary fact that only the audio device sees.
 *
 * These tests pin the observable consequences:
 *   - the constant is the canonical 48 kHz;
 *   - an FM operator's phase advance is 440 Hz / 48 kHz whatever the device
 *     is running at;
 *   - the sequencer's step cadence comes from the engine rate, not the
 *     device rate;
 *   - no file on the synthesis path may name the device rate at all.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "oscillator.h"
#include "modsystem.h"
#include "sequencer.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

/* appstate.c's setSelectedPattern calls rebuildPatternGraph(); the graph does
 * not exist in this test context, so the real symbol is stubbed (the same
 * seam test_sequencer.c uses). */
void rebuildPatternGraph(void) {
}

/* The files whose contents decide what a voice sounds like. main.c is not in
 * the list: it opens the device, so it is the one place allowed to know the
 * device's rate. */
static const char *const SYNTHESIS_PATH[] = {
	"audio.c",
	"voice.c",
	"oscillator.c",
	"modsystem.c",
	"sequencer.c",
	"sample.c",
};

static int test_engine_rate_is_the_canonical_constant(void) {
	ASSERT_TRUE(SPECTRAX_ENGINE_RATE == 48000,
	            "the canonical engine rate is 48000 Hz");
	printf("PASS test_engine_rate_is_the_canonical_constant\n");
	return 0;
}

static int test_operator_phase_ignores_the_device_rate(void) {
	initModSystem();
	ParamList *pl = createParamList();
	Operator *op = createOperator(pl, 1.0f);

	/* The device runs at whatever it runs at; the operator must not care. */
	float saved = g_deviceRate;
	g_deviceRate = 44100.0f;
	sine_op(op, 440.0f, 0.0f);
	ASSERT_TRUE(op->phase == 440.0f / (float)SPECTRAX_ENGINE_RATE,
	            "the operator phase advances by 440 Hz / the engine rate");

	/* A different device rate must not move the phase by a single ULP. */
	g_deviceRate = 192000.0f;
	op->phase = 0.0f;
	sine_op(op, 440.0f, 0.0f);
	ASSERT_TRUE(op->phase == 440.0f / (float)SPECTRAX_ENGINE_RATE,
	            "the operator phase does not depend on the device rate");

	g_deviceRate = saved;
	printf("PASS test_operator_phase_ignores_the_device_rate\n");
	return 0;
}

static int test_tempo_cadence_follows_the_engine_rate(void) {
	Settings settings;
	createSettings(&settings);
	settings.defaultBPM = 87;
	ParamList *global = createParamList();
	Arranger *arranger = createArranger(&settings, NULL, NULL, global);
	ASSERT_TRUE(arranger != NULL, "createArranger");

	/* 87 bpm, four steps to the beat: 48000 * 60 / (87 * 4) = 8275 samples.
	 * At the old device-derived rate of 44100 this was 7603 -- the tempo
	 * itself would have drifted with the device. */
	int expect = (SPECTRAX_ENGINE_RATE * 60) / (87 * 4.0f);
	ASSERT_TRUE(expect == 8275, "87 bpm is 8275 samples per step at 48 kHz");
	ASSERT_TRUE(arranger->tempoSettings.samplesPerEvenStep == expect,
	            "the step cadence comes from the engine rate");

	float saved = g_deviceRate;
	g_deviceRate = 44100.0f;
	ParamList *global2 = createParamList();
	Arranger *arranger2 = createArranger(&settings, NULL, NULL, global2);
	ASSERT_TRUE(arranger2 != NULL, "createArranger again");
	ASSERT_TRUE(arranger2->tempoSettings.samplesPerEvenStep == expect,
	            "the step cadence does not depend on the device rate");
	g_deviceRate = saved;

	printf("PASS test_tempo_cadence_follows_the_engine_rate\n");
	return 0;
}

static int test_synthesis_path_never_names_the_device_rate(void) {
	for(size_t i = 0; i < sizeof(SYNTHESIS_PATH) / sizeof(SYNTHESIS_PATH[0]); i++) {
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", SPECTRAX_SRC_DIR, SYNTHESIS_PATH[i]);
		FILE *f = fopen(path, "r");
		ASSERT_TRUE(f != NULL, "the synthesis source is readable");
		char line[1024];
		int lineno = 0;
		while(fgets(line, sizeof(line), f)) {
			lineno++;
			if(strstr(line, "g_deviceRate")) {
				fclose(f);
				printf("FAIL: %s:%i names the device rate\n", SYNTHESIS_PATH[i], lineno);
				return 1;
			}
		}
		fclose(f);
	}
	printf("PASS test_synthesis_path_never_names_the_device_rate\n");
	return 0;
}

int main(void) {
	int fails = 0;
	fails += test_engine_rate_is_the_canonical_constant();
	fails += test_operator_phase_ignores_the_device_rate();
	fails += test_tempo_cadence_follows_the_engine_rate();
	fails += test_synthesis_path_never_names_the_device_rate();
	if(fails) {
		fprintf(stderr, "%d engine-rate test(s) failed\n", fails);
		return 1;
	}
	printf("ALL engine-rate tests passed\n");
	return 0;
}
