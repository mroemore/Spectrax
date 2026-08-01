/*
 * test_fm_synth.c — direct unit tests for the FM synth functions in
 * src/oscillator.c (sine_fm, sineFmAlgo, sine_op) and the Operator
 * factories/cleanup (createOperator, createParamPointerOperator,
 * freeOperator).  Built under fil-c.
 *
 * PRE-EXISTING BUGS documented by this file (behavior is asserted as-is, with
 * a comment at each site):
 *
 *  1. createOperator() (src/oscillator.c:65-77) never creates or assigns
 *     op->outLevel — the struct field is left as uninitialized malloc
 *     garbage, so calling sine_op() on such an operator dereferences a
 *     garbage pointer (crash/UB).  The header comment says each Operator has
 *     feedbackAmount/ratio/level/outLevel, but only 3 params are made.
 *     Tests that render audio therefore assign op->outLevel by hand
 *     (make_op) and a lifecycle test asserts pl->count == 3.
 *  2. createOperator()/createParamPointerOperator() never assign the
 *     Operator.generate function pointer (src/oscillator.c:65-91).  It stays
 *     uninitialized; the field is not part of the render path (sine_op is
 *     called directly), so this is latent, not asserted on garbage.
 *  3. freeOperator() (src/oscillator.c:93-96) has no NULL guard: passing
 *     NULL dereferences op->ratio and crashes.  It also frees op->ratio and
 *     op->level even for createParamPointerOperator() operators that do NOT
 *     own those parameters, and it leaks op->feedbackAmount (and
 *     op->outLevel for pointer operators).  freeOperator(NULL) is therefore
 *     NOT executed here — it would kill the test binary.
 *  4. sine_op() (src/oscillator.c:54) computes feedbackLevel but never uses
 *     it — the feedbackAmount parameter has NO audible effect.  Asserted
 *     below (high-feedback output == zero-feedback output).
 *  5. sine_op() never writes op->currentVal / op->lastVal / op->modVal; the
 *     caller (sineFmAlgo) owns that state.  Asserted below.
 *  6. sine_fm() (src/oscillator.c:15-20) has no NULL checks and ignores
 *     ops[3].  sine_fm(NULL) crashes; documented, not executed.
 *  7. sineFmAlgo() (src/oscillator.c:22-23) has no bounds check on
 *     `algorithm`: algo outside [0, ALGO_COUNT-1] reads fm_algorithm out of
 *     bounds (UB).  Documented, not executed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "oscillator.h"

/* ---- local test macros (no Unity) ------------------------------------- */

#define ASSERT_NEAR(actual, expected, tol) do { \
	float _a = (float)(actual); \
	float _e = (float)(expected); \
	if (fabsf(_a - _e) > (tol)) { \
		fprintf(stderr, "FAIL %s:%d: expected %.6f, got %.6f (tol %.6f)\n", \
		        __FILE__, __LINE__, (double)_e, (double)_a, (double)(float)(tol)); \
		return 1; \
	} \
} while (0)

#define ASSERT_EQ(actual, expected) do { \
	long long _a = (long long)(actual); \
	long long _e = (long long)(expected); \
	if (_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
		        __FILE__, __LINE__, _e, _a); \
		return 1; \
	} \
} while (0)

#define ASSERT_TRUE(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: expected TRUE: %s\n", \
		        __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
	if ((ptr) == NULL) { \
		fprintf(stderr, "FAIL %s:%d: expected non-NULL\n", __FILE__, __LINE__); \
		return 1; \
	} \
} while (0)

#define ASSERT_PTR_EQ(actual, expected) do { \
	const void *_a = (const void *)(actual); \
	const void *_e = (const void *)(expected); \
	if (_a != _e) { \
		fprintf(stderr, "FAIL %s:%d: expected ptr %p, got %p\n", \
		        __FILE__, __LINE__, _e, _a); \
		return 1; \
	} \
} while (0)

/* ---- helpers -----------------------------------------------------------
 *
 * make_op(): createOperator() leaves op->outLevel uninitialized (BUG #1,
 * oscillator.c:65-77) so rendering would crash.  For signal tests, patch in
 * an outLevel of 1.0 and set level to 1.0 so the operator produces a pure
 * sine (amplitude 1).  ParamList-placed params are used exactly like the
 * real app would.
 */

static void remove_param_from_list(ParamList *pl, Parameter *p) {
	if (!pl || !p) return;
	for (int i = 0; i < pl->count; i++) {
		if (pl->params[i] == p) {
			for (int j = i; j < pl->count - 1; j++) {
				pl->params[j] = pl->params[j + 1];
			}
			pl->count--;
			return;
		}
	}
}

static Operator *make_op(ParamList *pl, float ratio) {
	Operator *op = createOperator(pl, ratio);
	if (!op) return NULL;
	/* BUG #1 (oscillator.c:65-77): outLevel never created by createOperator.
	 * A real caller would crash in sine_op; patch it up here. */
	op->outLevel = createParameter(pl, "outLevel", 1.0f, 0.0f, 1.0f);
	setParameterValue(op->level, 1.0f);
	return op;
}

/* Cleanup matching the buggy ownership: freeOperator() frees ratio+level
 * (and the pointers stay in the ParamList), so remove them first, then let
 * freeParamList() reclaim the leftovers (feedbackAmount, outLevel). */
static void cleanup_bank(ParamList *pl, Operator **ops, int n) {
	if (!pl) return;
	for (int i = 0; i < n; i++) {
		if (!ops[i]) continue;
		Parameter *ratio = ops[i]->ratio;
		Parameter *level = ops[i]->level;
		freeOperator(ops[i]);
		remove_param_from_list(pl, ratio);
		remove_param_from_list(pl, level);
	}
	freeParamList(pl);
}

/* build a fresh 4-op bank, all ratio `r`, patched to pure-sine level. */
static void make_bank(ParamList *pl, Operator **ops, float r) {
	for (int i = 0; i < OP_COUNT; i++) {
		ops[i] = make_op(pl, r);
	}
}

/* ---- createOperator / freeOperator lifecycle -------------------------- */

static int test_create_operator_lifecycle(void) {
	ParamList *pl = createParamList();
	Operator *op = createOperator(pl, 2.0f);

	ASSERT_NOT_NULL(op);
	ASSERT_NOT_NULL(op->feedbackAmount);
	ASSERT_NOT_NULL(op->ratio);
	ASSERT_NOT_NULL(op->level);
	ASSERT_NEAR(getParameterValue(op->ratio), 2.0f, 1e-6f);
	ASSERT_NEAR(getParameterValue(op->level), 0.1f, 1e-6f); /* createOperator default */
	ASSERT_NEAR(getParameterValue(op->feedbackAmount), 0.0f, 1e-6f);
	ASSERT_EQ(op->generated, 0);
	ASSERT_NEAR(op->phase, 0.0f, 1e-6f);
	ASSERT_NEAR(op->currentVal, 0.0f, 1e-6f);
	ASSERT_NEAR(op->lastVal, 0.0f, 1e-6f);
	ASSERT_NEAR(op->modVal, 0.0f, 1e-6f);
	/* BUG #1 (oscillator.c:65-77): outLevel is never created — only 3
	 * params land in the list (feedback, ratio, level).  A correct
	 * implementation would have 4. */
	ASSERT_EQ(pl->count, 3);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_create_operator_lifecycle\n");
	return 0;
}

static int test_create_operator_ratio_clamped(void) {
	ParamList *plHi = createParamList();
	Operator *hi = createOperator(plHi, 100.0f);
	ASSERT_NEAR(getParameterValue(hi->ratio), 30.0f, 1e-6f); /* max */
	ParamList *plLo = createParamList();
	Operator *lo = createOperator(plLo, 0.05f);
	ASSERT_NEAR(getParameterValue(lo->ratio), 0.25f, 1e-6f); /* min */

	cleanup_bank(plHi, &hi, 1);
	cleanup_bank(plLo, &lo, 1);
	printf("PASS test_create_operator_ratio_clamped\n");
	return 0;
}

static int test_create_param_pointer_operator(void) {
	ParamList *pl = createParamList();
	Parameter *fb = createParameter(pl, "fb", 0.3f, 0.0f, 1.0f);
	Parameter *ratio = createParameter(pl, "ratio", 1.5f, 0.0f, 10.0f);
	Parameter *level = createParameter(pl, "level", 0.8f, 0.0f, 1.0f);

	Operator *op = createParamPointerOperator(pl, fb, ratio, level);
	ASSERT_NOT_NULL(op);
	ASSERT_PTR_EQ(op->feedbackAmount, fb);
	ASSERT_PTR_EQ(op->ratio, ratio);
	ASSERT_PTR_EQ(op->level, level);
	ASSERT_NOT_NULL(op->outLevel); /* the only param this factory creates */
	ASSERT_NEAR(getParameterValue(op->outLevel), 0.5f, 1e-6f);
	ASSERT_EQ(pl->count, 4); /* fb, ratio, level, outLevel */
	ASSERT_EQ(op->generated, 0);
	ASSERT_NEAR(op->phase, 0.0f, 1e-6f);
	ASSERT_NEAR(op->currentVal, 0.0f, 1e-6f);
	ASSERT_NEAR(op->lastVal, 0.0f, 1e-6f);
	ASSERT_NEAR(op->modVal, 0.0f, 1e-6f);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_create_param_pointer_operator\n");
	return 0;
}

static int test_free_operator_smoke(void) {
	/* createOperator + freeOperator: must not crash (leaks feedbackAmount,
	 * but that is the pre-existing behavior — see file header bug #3). */
	ParamList *pl = createParamList();
	Operator *op = createOperator(pl, 1.0f);
	cleanup_bank(pl, &op, 1);

	/* createParamPointerOperator + freeOperator: frees the passed-in ratio
	 * and level (which it does not own), leaks outLevel.  The test mirrors
	 * that ownership so cleanup happens exactly once. */
	ParamList *pl2 = createParamList();
	Parameter *fb = createParameter(pl2, "fb", 0.0f, 0.0f, 1.0f);
	Parameter *ratio = createParameter(pl2, "ratio", 1.0f, 0.0f, 10.0f);
	Parameter *level = createParameter(pl2, "level", 1.0f, 0.0f, 1.0f);
	Operator *op2 = createParamPointerOperator(pl2, fb, ratio, level);
	cleanup_bank(pl2, &op2, 1);

	printf("PASS test_free_operator_smoke\n");
	return 0;
}

/* freeOperator(NULL): NOT executed.  freeOperator() (oscillator.c:93-96)
 * dereferences op->ratio with no NULL check, so passing NULL segfaults the
 * test binary.  Verified separately by scratch program (see report). */
static int test_free_operator_null_crashes(void) {
	printf("SKIP test_free_operator_null_crashes (documented crash, oscillator.c:93)\n");
	return 0;
}

/* ---- sine_op ----------------------------------------------------------- */

static int test_sine_op_phase_advance_and_output(void) {
	ParamList *pl = createParamList();
	Operator *op = make_op(pl, 1.0f);
	const float f = 440.0f;
	const float p = f / (float)SAMPLE_RATE; /* phase_inc for ratio 1 */

	float s1 = sine_op(op, f, 0.0f);
	ASSERT_NEAR(op->phase, p, 1e-6f);
	ASSERT_NEAR(s1, sinf(TWO_PI * p), 1e-6f);

	float s2 = sine_op(op, f, 0.0f);
	ASSERT_NEAR(op->phase, 2.0f * p, 1e-6f);
	ASSERT_NEAR(s2, sinf(TWO_PI * (2.0f * p)), 1e-6f);
	ASSERT_TRUE(s1 != s2); /* phase advanced, so the waveform moved */

	cleanup_bank(pl, &op, 1);
	printf("PASS test_sine_op_phase_advance_and_output\n");
	return 0;
}

static int test_sine_op_mod_is_phase_fraction(void) {
	ParamList *pl = createParamList();
	Operator *op = make_op(pl, 1.0f);
	const float f = 440.0f;
	const float p = f / (float)SAMPLE_RATE;

	/* mod is a fraction of a cycle, NOT radians.  mod=0.25 shifts the sine
	 * by a quarter cycle -> sin(2*pi*(p+0.25)) ~ 1.0.  (The naive "mod =
	 * M_PI/2" idea from the task brief would not give 1.0.) */
	float s = sine_op(op, f, 0.25f);
	ASSERT_NEAR(s, sinf(TWO_PI * (p + 0.25f)), 1e-6f);
	ASSERT_NEAR(s, 1.0f, 0.01f);

	/* mod=0.5 flips sign: sin(2*pi*(p+0.5)) = -sin(2*pi*p).  Reset phase so
	 * this measurement starts from the same cycle position as the formula. */
	op->phase = 0.0f;
	float s2 = sine_op(op, f, 0.5f);
	ASSERT_NEAR(s2, -sinf(TWO_PI * p), 1e-6f);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_sine_op_mod_is_phase_fraction\n");
	return 0;
}

static int test_sine_op_level_scaling(void) {
	ParamList *pl = createParamList();
	Operator *op = make_op(pl, 1.0f);
	const float f = 440.0f;

	/* reset phase before each measurement: the level checks must compare
	 * samples at the same cycle position (phase advances every call) */
	op->phase = 0.0f;
	float full = sine_op(op, f, 0.0f);
	/* halve outLevel -> output halves */
	setParameterValue(op->outLevel, 0.5f);
	op->phase = 0.0f;
	float half = sine_op(op, f, 0.0f);
	ASSERT_NEAR(half, 0.5f * full, 1e-6f);
	/* restore outLevel, halve level instead */
	setParameterValue(op->outLevel, 1.0f);
	setParameterValue(op->level, 0.5f);
	op->phase = 0.0f;
	float half2 = sine_op(op, f, 0.0f);
	ASSERT_NEAR(half2, 0.5f * full, 1e-6f);
	/* both halved -> quarter */
	setParameterValue(op->outLevel, 0.5f);
	op->phase = 0.0f;
	float quarter = sine_op(op, f, 0.0f);
	ASSERT_NEAR(quarter, 0.25f * full, 1e-6f);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_sine_op_level_scaling\n");
	return 0;
}

static int test_sine_op_feedback_dead_code(void) {
	ParamList *pl = createParamList();
	Operator *op = make_op(pl, 1.0f);
	const float f = 440.0f;

	op->phase = 0.0f;
	float s1 = sine_op(op, f, 0.0f);
	/* BUG #4 (oscillator.c:54): feedbackLevel is computed but never used,
	 * so a maxed feedbackAmount changes nothing.  Reset phase so the two
	 * samples are at the same cycle position. */
	op->lastVal = 0.75f; /* give feedback something to scale */
	setParameterValue(op->feedbackAmount, 1.0f);
	op->phase = 0.0f;
	float s2 = sine_op(op, f, 0.0f);
	ASSERT_NEAR(s2, s1, 1e-6f);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_sine_op_feedback_dead_code\n");
	return 0;
}

static int test_sine_op_state_not_tracked(void) {
	ParamList *pl = createParamList();
	Operator *op = make_op(pl, 1.0f);
	op->currentVal = 0.123f;
	op->lastVal = 0.456f;
	op->modVal = 0.789f;

	/* BUG #5 (oscillator.c:52-59): sine_op only advances phase and returns
	 * a value; it never writes currentVal/lastVal/modVal.  State updates are
	 * entirely the caller's responsibility. */
	float out = sine_op(op, 440.0f, 0.25f);
	(void)out;
	ASSERT_NEAR(op->currentVal, 0.123f, 1e-6f);
	ASSERT_NEAR(op->lastVal, 0.456f, 1e-6f);
	ASSERT_NEAR(op->modVal, 0.789f, 1e-6f);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_sine_op_state_not_tracked\n");
	return 0;
}

static int test_sine_op_consecutive_coherence(void) {
	ParamList *pl = createParamList();
	Operator *op = make_op(pl, 1.0f);
	const float f = 55.0f; /* low frequency: adjacent samples close */
	const float p = f / (float)SAMPLE_RATE;

	float s1 = sine_op(op, f, 0.0f);
	float s2 = sine_op(op, f, 0.0f);
	ASSERT_NEAR(op->phase, 2.0f * p, 1e-6f);
	/* adjacent samples of a 55 Hz sine differ by < 0.01; assert < 0.02 so
	 * the waveform is coherent (no jumps / no reset). */
	ASSERT_TRUE(fabsf(s2 - s1) < 0.02f);
	ASSERT_NEAR(s1, sinf(TWO_PI * p), 1e-6f);
	ASSERT_NEAR(s2, sinf(TWO_PI * (2.0f * p)), 1e-6f);

	cleanup_bank(pl, &op, 1);
	printf("PASS test_sine_op_consecutive_coherence\n");
	return 0;
}

/* ---- sine_fm (single fixed algorithm: 2 -> 1 -> 0) --------------------- */

static int test_sine_fm_ratio_one(void) {
	ParamList *pl = createParamList();
	Operator *ops[OP_COUNT];
	make_bank(pl, ops, 1.0f);
	const float f = 440.0f;
	const float p = f / (float)SAMPLE_RATE;

	float out = sine_fm(ops, f);
	/* chain: a = op2(p), b = op1(p + a), c = op0(p + b) */
	float a = sinf(TWO_PI * p);
	float b = sinf(TWO_PI * (p + a));
	float c = sinf(TWO_PI * (p + b));
	ASSERT_NEAR(out, c, 1e-6f);
	/* the three used ops advanced one phase step; ops[3] untouched */
	ASSERT_NEAR(ops[2]->phase, p, 1e-6f);
	ASSERT_NEAR(ops[1]->phase, p, 1e-6f);
	ASSERT_NEAR(ops[0]->phase, p, 1e-6f);
	ASSERT_NEAR(ops[3]->phase, 0.0f, 1e-6f);
	ASSERT_EQ(ops[3]->generated, 0);

	cleanup_bank(pl, ops, OP_COUNT);
	printf("PASS test_sine_fm_ratio_one\n");
	return 0;
}

static int test_sine_fm_varying_ratios_differ(void) {
	ParamList *pl1 = createParamList();
	Operator *ops1[OP_COUNT];
	make_bank(pl1, ops1, 1.0f);

	ParamList *pl2 = createParamList();
	Operator *ops2[OP_COUNT];
	/* sine_fm uses ops[2], ops[1], ops[0]: give them ratios 3,2,1 and
	 * ops[3] an unused ratio 4. */
	ops2[0] = make_op(pl2, 1.0f);
	ops2[1] = make_op(pl2, 2.0f);
	ops2[2] = make_op(pl2, 3.0f);
	ops2[3] = make_op(pl2, 4.0f);

	float out1 = sine_fm(ops1, 440.0f);
	float out2 = sine_fm(ops2, 440.0f);
	ASSERT_TRUE(fabsf(out2 - out1) > 1e-3f);

	cleanup_bank(pl1, ops1, OP_COUNT);
	cleanup_bank(pl2, ops2, OP_COUNT);
	printf("PASS test_sine_fm_varying_ratios_differ\n");
	return 0;
}

static int test_sine_fm_does_not_track_state(void) {
	ParamList *pl = createParamList();
	Operator *ops[OP_COUNT];
	make_bank(pl, ops, 1.0f);

	float out = sine_fm(ops, 440.0f);
	(void)out;
	/* BUG #5: sine_fm calls sine_op directly and never stores outputs into
	 * the operator state; everything stays at the createOperator zeroes. */
	for (int i = 0; i < OP_COUNT; i++) {
		ASSERT_NEAR(ops[i]->currentVal, 0.0f, 1e-6f);
		ASSERT_NEAR(ops[i]->lastVal, 0.0f, 1e-6f);
		ASSERT_NEAR(ops[i]->modVal, 0.0f, 1e-6f);
	}

	cleanup_bank(pl, ops, OP_COUNT);
	printf("PASS test_sine_fm_does_not_track_state\n");
	return 0;
}

/* sine_fm(NULL): NOT executed.  sine_fm (oscillator.c:15-20) dereferences
 * ops[2] with no NULL check, so a NULL array segfaults.  Verified
 * separately by scratch program (see report). */
static int test_sine_fm_null_crashes(void) {
	printf("SKIP test_sine_fm_null_crashes (documented crash, oscillator.c:15)\n");
	return 0;
}

/* ---- sineFmAlgo (all 7 algorithms) ------------------------------------- */

static int test_sinefm_algo_all_algorithms_finite(void) {
	for (int algo = 0; algo < ALGO_COUNT; algo++) {
		ParamList *pl = createParamList();
		Operator *ops[OP_COUNT];
		make_bank(pl, ops, 1.0f);

		for (int n = 0; n < 64; n++) {
			float out = sineFmAlgo(ops, 440.0f, algo);
			ASSERT_TRUE(isfinite(out));
			/* max contribution per op is 1.0; algo 5 sums all four */
			ASSERT_TRUE(fabsf(out) <= 4.0f + 1e-4f);
		}
		cleanup_bank(pl, ops, OP_COUNT);
	}
	printf("PASS test_sinefm_algo_all_algorithms_finite\n");
	return 0;
}

static int test_sinefm_algo_algorithms_differ(void) {
	float first[ALGO_COUNT];
	for (int algo = 0; algo < ALGO_COUNT; algo++) {
		ParamList *pl = createParamList();
		Operator *ops[OP_COUNT];
		make_bank(pl, ops, 1.0f);
		first[algo] = sineFmAlgo(ops, 440.0f, algo);
		cleanup_bank(pl, ops, OP_COUNT);
	}
	for (int i = 0; i < ALGO_COUNT; i++) {
		for (int j = i + 1; j < ALGO_COUNT; j++) {
			if (fabsf(first[i] - first[j]) <= 1e-6f) {
				fprintf(stderr, "FAIL %s:%d: algorithms %d and %d produce "
				        "identical first output %.6f\n",
				        __FILE__, __LINE__, i, j, (double)first[i]);
				return 1;
			}
		}
	}
	printf("PASS test_sinefm_algo_algorithms_differ\n");
	return 0;
}

static int test_sinefm_algo_algo5_sums_ops(void) {
	/* Algorithm 5 (rows {3,-1},{2,-1},{1,-1},{0,-1}) renders each op
	 * independently into the output.  Identical ratio/level ops all produce
	 * the same first sample, so out == 4 * single-op sample. */
	ParamList *pl = createParamList();
	Operator *ops[OP_COUNT];
	make_bank(pl, ops, 1.0f);
	const float f = 440.0f;
	const float p = f / (float)SAMPLE_RATE;

	float out = sineFmAlgo(ops, f, 5);
	float single = sinf(TWO_PI * p);
	ASSERT_NEAR(out, single + single + single + single, 1e-6f);

	cleanup_bank(pl, ops, OP_COUNT);
	printf("PASS test_sinefm_algo_algo5_sums_ops\n");
	return 0;
}

static int test_sinefm_algo_algo0_chain_state(void) {
	/* Algorithm 0 rows: {3,2},{2,1},{1,0},{0,-1}.  After one call the mod
	 * inputs accumulate along the chain and each generated op's currentVal
	 * holds its rendered sample.  (State writes are sineFmAlgo's job — BUG
	 * #5 note.) */
	ParamList *pl = createParamList();
	Operator *ops[OP_COUNT];
	make_bank(pl, ops, 1.0f);

	float out = sineFmAlgo(ops, 440.0f, 0);
	ASSERT_EQ(ops[0]->generated, 1);
	ASSERT_EQ(ops[1]->generated, 1);
	ASSERT_EQ(ops[2]->generated, 1);
	ASSERT_EQ(ops[3]->generated, 1);
	ASSERT_NEAR(out, ops[0]->currentVal, 1e-6f);
	/* lastVal was set from the (zero) currentVal at loop start */
	ASSERT_NEAR(ops[0]->lastVal, 0.0f, 1e-6f);
	/* mod inputs: op0 got op1, op1 got op2, op2 got op3, op3 got nothing */
	ASSERT_NEAR(ops[0]->modVal, ops[1]->currentVal, 1e-6f);
	ASSERT_NEAR(ops[1]->modVal, ops[2]->currentVal, 1e-6f);
	ASSERT_NEAR(ops[2]->modVal, ops[3]->currentVal, 1e-6f);
	ASSERT_NEAR(ops[3]->modVal, 0.0f, 1e-6f);

	cleanup_bank(pl, ops, OP_COUNT);
	printf("PASS test_sinefm_algo_algo0_chain_state\n");
	return 0;
}

static int test_sinefm_algo_no_feedback_stable(void) {
	ParamList *pl = createParamList();
	Operator *ops[OP_COUNT];
	make_bank(pl, ops, 1.0f);

	float vmax = 0.0f;
	for (int n = 0; n < 4410; n++) { /* 0.1 s @ 44.1 kHz */
		float out = sineFmAlgo(ops, 440.0f, 0);
		ASSERT_TRUE(isfinite(out));
		ASSERT_TRUE(fabsf(out) <= 4.0f + 1e-4f);
		if (fabsf(out) > vmax) vmax = fabsf(out);
	}
	ASSERT_TRUE(vmax > 1e-4f); /* actually produced signal */

	cleanup_bank(pl, ops, OP_COUNT);
	printf("PASS test_sinefm_algo_no_feedback_stable\n");
	return 0;
}

static int test_sinefm_algo_high_feedback_no_runaway(void) {
	/* BUG #4 (oscillator.c:54): feedback is dead code.  Even at maximum
	 * feedbackAmount the rendered output is bit-identical to feedback 0. */
	ParamList *pl0 = createParamList();
	Operator *ops0[OP_COUNT];
	make_bank(pl0, ops0, 1.0f);

	ParamList *pl1 = createParamList();
	Operator *ops1[OP_COUNT];
	make_bank(pl1, ops1, 1.0f);
	for (int i = 0; i < OP_COUNT; i++) {
		setParameterValue(ops1[i]->feedbackAmount, 1.0f);
	}

	float maxDiff = 0.0f;
	for (int n = 0; n < 256; n++) {
		float o0 = sineFmAlgo(ops0, 440.0f, 3);
		float o1 = sineFmAlgo(ops1, 440.0f, 3);
		ASSERT_TRUE(isfinite(o1));
		ASSERT_TRUE(fabsf(o1) <= 4.0f + 1e-4f);
		float d = fabsf(o1 - o0);
		if (d > maxDiff) maxDiff = d;
	}
	ASSERT_NEAR(maxDiff, 0.0f, 1e-6f);

	cleanup_bank(pl0, ops0, OP_COUNT);
	cleanup_bank(pl1, ops1, OP_COUNT);
	printf("PASS test_sinefm_algo_high_feedback_no_runaway\n");
	return 0;
}

/* sineFmAlgo invalid algorithm: NOT executed.  algoOffset (oscillator.c:23)
 * is not bounds-checked; algo outside [0,6] reads fm_algorithm out of
 * bounds (UB). */
static int test_sinefm_algo_invalid_algorithm(void) {
	printf("SKIP test_sinefm_algo_invalid_algorithm (documented OOB read, oscillator.c:23)\n");
	return 0;
}

/* ---- fm_algorithm matrix sanity ---------------------------------------- */

static int test_fm_algorithm_matrix_indices(void) {
	/* every src/dst is either -1 ("no source") or a valid op index 0..3 */
	for (int i = 0; i < ALGO_COUNT * ALGO_SIZE; i++) {
		for (int j = 0; j < 2; j++) {
			int idx = fm_algorithm[i][j];
			if (idx < -1 || idx >= OP_COUNT) {
				fprintf(stderr, "FAIL %s:%d: fm_algorithm[%d][%d] = %d "
				        "out of range [-1, %d]\n",
				        __FILE__, __LINE__, i, j, idx, OP_COUNT - 1);
				return 1;
			}
		}
	}
	/* each algorithm: at least one row routes a generated op to the output
	 * (dst == -1) and at least one row uses a source (src != -1) */
	for (int algo = 0; algo < ALGO_COUNT; algo++) {
		int producers = 0;
		int sources = 0;
		for (int k = 0; k < ALGO_SIZE; k++) {
			int i = algo * ALGO_SIZE + k;
			if (fm_algorithm[i][0] != -1) sources++;
			if (fm_algorithm[i][1] == -1 && fm_algorithm[i][0] != -1) producers++;
		}
		if (sources == 0) {
			fprintf(stderr, "FAIL %s:%d: algorithm %d uses no operators\n",
			        __FILE__, __LINE__, algo);
			return 1;
		}
		if (producers == 0) {
			fprintf(stderr, "FAIL %s:%d: algorithm %d never routes to output\n",
			        __FILE__, __LINE__, algo);
			return 1;
		}
	}
	printf("PASS test_fm_algorithm_matrix_indices\n");
	return 0;
}

/* ---- main -------------------------------------------------------------- */

int main(void) {
	int failed = 0;
	failed |= test_create_operator_lifecycle();
	failed |= test_create_operator_ratio_clamped();
	failed |= test_create_param_pointer_operator();
	failed |= test_free_operator_smoke();
	failed |= test_free_operator_null_crashes();
	failed |= test_sine_op_phase_advance_and_output();
	failed |= test_sine_op_mod_is_phase_fraction();
	failed |= test_sine_op_level_scaling();
	failed |= test_sine_op_feedback_dead_code();
	failed |= test_sine_op_state_not_tracked();
	failed |= test_sine_op_consecutive_coherence();
	failed |= test_sine_fm_ratio_one();
	failed |= test_sine_fm_varying_ratios_differ();
	failed |= test_sine_fm_does_not_track_state();
	failed |= test_sine_fm_null_crashes();
	failed |= test_sinefm_algo_all_algorithms_finite();
	failed |= test_sinefm_algo_algorithms_differ();
	failed |= test_sinefm_algo_algo5_sums_ops();
	failed |= test_sinefm_algo_algo0_chain_state();
	failed |= test_sinefm_algo_no_feedback_stable();
	failed |= test_sinefm_algo_high_feedback_no_runaway();
	failed |= test_sinefm_algo_invalid_algorithm();
	failed |= test_fm_algorithm_matrix_indices();

	if (failed) {
		printf("\nSome tests FAILED.\n");
	} else {
		printf("\nAll tests PASSED.\n");
	}
	return failed;
}
