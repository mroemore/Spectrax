/*
 * test_modsystem.c — verify the modulation system (LFO / envelope / random /
 * parameter routing) built under fil-c.
 *
 * PRE-EXISTING BUGS documented by this file (behavior is asserted as-is, with
 * a comment at each site):
 *
 *  1. initMod() (src/modsystem.c:456) ignores its `generate` argument and
 *     ALWAYS installs generateEnvelope. LFOs/random mods therefore never get
 *     their real generator from createLFO/createRandom; processModulations()
 *     would call generateEnvelope() on LFO memory (UB). Tests that need
 *     processModulations() end-to-end explicitly reassign the generator, and
 *     one test asserts the buggy assignment.
 *  2. setParameterMaxValue() (src/modsystem.c:173) has an inverted check
 *     (`max < minValue`): raising max is silently ignored, lowering it below
 *     min is accepted.
 *  3. createConnection() (src/modsystem.c:188) ignores the `amount` argument
 *     (always creates a 1.0 "mod amount" param), and processModulations()
 *     (src/modsystem.c:673) never consults conn->amount at all.
 *  4. Mod output params are created with range [0,1] (src/modsystem.c:455),
 *     so bipolar generator outputs (sine/square/ramp/random) are clamped to
 *     0 on their negative half.
 *  5. Callback change detection uses fabs(fabs(old)-fabs(clamped))
 *     (src/modsystem.c:148), so a change that crosses zero is not detected.
 *  6. incParameterBaseValue() uses abs() on a float (src/modsystem.c:445) —
 *     truncation means |rel| in (1,2) uses the FINE increment.
 *  7. Envelope `loop` is never honored by generateEnvelope().
 *  8. addEnvelopeStage() with duration <= 0 clamps the duration param to the
 *     0.001 minimum, so a "sustain" stage cannot actually have 0 duration.
 *  9. clearParamList()/clearModList() zero the count but never free
 *     (the free loops are commented out) — memory leak by design.
 * 10. incrementConnectionType()/decrementConnectionType() are declared in
 *     modsystem.h but have no implementation anywhere (link error if used).
 * 11. initRandFromPreset()/saveRandPreset() are empty no-ops.
 * 12. generateEnvelope() reads wt->data[index0+1] one element past a 1024
 *     entry table on the stage-completing sample (src/modsystem.c:403) — an
 *     out-of-bounds read that lands inside the pool buffer and is discarded.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "modsystem.h"

extern WavetablePool *envTables; /* global pool created by initModSystem() */

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

#define ASSERT_FALSE(cond) do { \
	if ((cond)) { \
		fprintf(stderr, "FAIL %s:%d: expected FALSE: %s\n", \
		        __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#define ASSERT_NULL(ptr) do { \
	if ((ptr) != NULL) { \
		fprintf(stderr, "FAIL %s:%d: expected NULL\n", __FILE__, __LINE__); \
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

#define ASSERT_STREQ(actual, expected) do { \
	if (strcmp((actual), (expected)) != 0) { \
		fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
		        __FILE__, __LINE__, (expected), (actual)); \
		return 1; \
	} \
} while (0)

/* ---- cleanup helpers ---------------------------------------------------
 *
 * Params live in the ParamList AND are owned by whoever created them (mods,
 * connections, or the test). cleanupModSystem() frees mods and the params
 * they own; freeParamList() frees everything left. To avoid double frees we
 * first drop mod-owned params out of the ParamList array.
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

static void unlist_mod_owned_params(ParamList *pl, ModList *ml) {
	if (!pl || !ml) return;
	for (int i = 0; i < ml->count; i++) {
		Mod *m = ml->mods[i];
		if (!m) continue;
		switch (m->type) {
			case MT_LFO: {
				LFO *l = (LFO *)m;
				remove_param_from_list(pl, l->base.output);
				remove_param_from_list(pl, l->rate);
				remove_param_from_list(pl, l->phase);
				break;
			}
			case MT_RND: {
				Random *r = (Random *)m;
				remove_param_from_list(pl, r->base.output);
				remove_param_from_list(pl, r->rate);
				remove_param_from_list(pl, r->phase);
				break;
			}
			case MT_ENV: {
				Envelope *e = (Envelope *)m;
				remove_param_from_list(pl, e->base.output);
				for (int j = 0; j < e->stageCount; j++) {
					remove_param_from_list(pl, e->stages[j].duration);
					remove_param_from_list(pl, e->stages[j].curvature);
				}
				break;
			}
			default:
				remove_param_from_list(pl, m->output);
				break;
		}
	}
}

/* Free a test's entire object graph exactly once. */
static void free_test_lists(ParamList *pl, ModList *ml) {
	if (pl) unlist_mod_owned_params(pl, ml);
	if (ml) cleanupModSystem(ml); /* mods + mod-owned params + ModList */
	if (pl) freeParamList(pl);    /* remaining params + conn nodes + ParamList */
}

/* ---- param list / parameter basics ------------------------------------ */

static int test_create_param_list(void) {
	ParamList *pl = createParamList();
	ASSERT_NOT_NULL(pl);
	ASSERT_EQ(pl->count, 0);
	freeParamList(pl);
	printf("PASS test_create_param_list\n");
	return 0;
}

static int test_add_parameter(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameter(pl, "gain", 0.7f, 0.0f, 1.0f);
	ASSERT_NOT_NULL(p);
	ASSERT_EQ(pl->count, 1);
	ASSERT_PTR_EQ(pl->params[0], p);
	ASSERT_STREQ(p->name, "gain");
	ASSERT_NEAR(p->baseValue, 0.7f, 1e-6f);
	ASSERT_NEAR(p->currentValue, 0.7f, 1e-6f);
	ASSERT_NEAR(p->minValue, 0.0f, 1e-6f);
	ASSERT_NEAR(p->maxValue, 1.0f, 1e-6f);
	ASSERT_NEAR(p->fineIncrement, 0.01f, 1e-6f);
	ASSERT_NEAR(p->coarseIncrement, 0.10f, 1e-6f);
	ASSERT_EQ(p->modulator_count, 0);
	ASSERT_NULL(p->modulators);
	/* initial value outside range gets clamped at creation */
	Parameter *q = createParameter(pl, "clamped", 5.0f, 0.0f, 1.0f);
	ASSERT_NEAR(q->baseValue, 1.0f, 1e-6f);
	ASSERT_NEAR(q->currentValue, 1.0f, 1e-6f);
	ASSERT_EQ(pl->count, 2);
	freeParamList(pl);
	printf("PASS test_add_parameter\n");
	return 0;
}

static int g_cb_count;

static void on_param_change(void *data) {
	(void)data;
	g_cb_count++;
}

static int test_parameter_ex_pro(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameterEx(pl, "ex", 0.5f, 0.0f, 1.0f, 0.02f, 0.3f);
	ASSERT_NEAR(p->fineIncrement, 0.02f, 1e-6f);
	ASSERT_NEAR(p->coarseIncrement, 0.3f, 1e-6f);

	g_cb_count = 0;
	Parameter *c = createParameterPro(pl, "pro", 0.5f, -1.0f, 1.0f,
	                                  0.01f, 0.1f, &g_cb_count, on_param_change);
	ASSERT_NEAR(c->fineIncrement, 0.01f, 1e-6f);
	ASSERT_NEAR(c->coarseIncrement, 0.1f, 1e-6f);
	ASSERT_PTR_EQ(c->onChange.cbData, &g_cb_count);
	ASSERT_PTR_EQ((void *)c->onChange.cbFunc, (void *)on_param_change);

	/* change fires callback */
	setParameterValue(c, 0.6f);
	ASSERT_EQ(g_cb_count, 1);
	/* same value does not fire */
	setParameterValue(c, 0.6f);
	ASSERT_EQ(g_cb_count, 1);
	/* BUG (modsystem.c:148): change detection compares |old| vs |clamped|,
	 * so a sign-crossing change (0.6 -> -0.6) does NOT fire the callback. */
	setParameterValue(c, -0.6f);
	ASSERT_EQ(g_cb_count, 1);
	/* magnitude change still fires */
	setParameterValue(c, 0.0f);
	ASSERT_EQ(g_cb_count, 2);

	freeParamList(pl);
	printf("PASS test_parameter_ex_pro\n");
	return 0;
}

static int test_set_parameter_value_clamps(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameter(pl, "p", 0.5f, 0.0f, 1.0f);

	setParameterValue(p, 0.75f);
	ASSERT_NEAR(getParameterValue(p), 0.75f, 1e-6f);
	/* below min clamps to min */
	setParameterValue(p, -3.0f);
	ASSERT_NEAR(getParameterValue(p), 0.0f, 1e-6f);
	ASSERT_NEAR(p->baseValue, 0.5f, 1e-6f); /* base untouched by setParameterValue */
	/* above max clamps to max */
	setParameterValue(p, 4.0f);
	ASSERT_NEAR(getParameterValue(p), 1.0f, 1e-6f);

	freeParamList(pl);
	printf("PASS test_set_parameter_value_clamps\n");
	return 0;
}

static int test_set_parameter_base_value(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameter(pl, "p", 0.5f, 0.0f, 1.0f);
	setParameterValue(p, 0.9f);

	setParameterBaseValue(p, 0.3f);
	ASSERT_NEAR(p->baseValue, 0.3f, 1e-6f);
	/* currentValue is NOT changed by setParameterBaseValue (actual behavior) */
	ASSERT_NEAR(getParameterValue(p), 0.9f, 1e-6f);
	/* base also clamps */
	setParameterBaseValue(p, 99.0f);
	ASSERT_NEAR(p->baseValue, 1.0f, 1e-6f);

	freeParamList(pl);
	printf("PASS test_set_parameter_base_value\n");
	return 0;
}

static int test_set_min_max_values(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameter(pl, "p", 0.5f, 0.0f, 1.0f);

	setParameterMinValue(p, -2.0f);
	ASSERT_NEAR(p->minValue, -2.0f, 1e-6f);
	/* min >= max is rejected */
	setParameterMinValue(p, 5.0f);
	ASSERT_NEAR(p->minValue, -2.0f, 1e-6f);

	/* BUG (modsystem.c:173): setParameterMaxValue checks `max < minValue`,
	 * so raising the max is silently ignored... */
	setParameterMaxValue(p, 10.0f);
	ASSERT_NEAR(p->maxValue, 1.0f, 1e-6f);
	/* ...and lowering it BELOW min is accepted. */
	setParameterMaxValue(p, -5.0f);
	ASSERT_NEAR(p->maxValue, -5.0f, 1e-6f);

	freeParamList(pl);
	printf("PASS test_set_min_max_values\n");
	return 0;
}

static int test_get_parameter_value_int(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameter(pl, "p", 0.0f, -100.0f, 100.0f);
	setParameterValue(p, 2.4f);
	ASSERT_EQ(getParameterValueAsInt(p), 2);
	setParameterValue(p, 2.6f);
	ASSERT_EQ(getParameterValueAsInt(p), 3);
	setParameterValue(p, -2.6f);
	ASSERT_EQ(getParameterValueAsInt(p), -3);
	freeParamList(pl);
	printf("PASS test_get_parameter_value_int\n");
	return 0;
}

static int test_modify_parameter_values(void) {
	ParamList *pl = createParamList();
	Parameter *p = createParameter(pl, "p", 0.5f, -10.0f, 10.0f);

	modifyParameterValue(p, 1.0f);
	ASSERT_NEAR(getParameterValue(p), 1.5f, 1e-6f);
	modifyParameterBaseValue(p, 1.0f);
	ASSERT_NEAR(p->baseValue, 1.5f, 1e-6f);

	/* incParameterBaseValue: |rel| <= 1 (after abs() truncation) -> fine */
	incParameterBaseValue(p, 0.5f);
	ASSERT_NEAR(p->baseValue, 1.51f, 1e-6f);
	/* |rel| > 1 -> coarse */
	incParameterBaseValue(p, 2.5f);
	ASSERT_NEAR(p->baseValue, 1.61f, 1e-6f);
	/* negative -> coarse downward */
	incParameterBaseValue(p, -2.5f);
	ASSERT_NEAR(p->baseValue, 1.51f, 1e-6f);
	/* BUG (modsystem.c:445): abs() on a float truncates, so |rel| in (1,2)
	 * uses the fine increment instead of coarse (1.5f -> (int)1 -> fine). */
	incParameterBaseValue(p, 1.5f);
	ASSERT_NEAR(p->baseValue, 1.52f, 1e-6f);

	freeParamList(pl);
	printf("PASS test_modify_parameter_values\n");
	return 0;
}

/* ---- mod list / LFO ---------------------------------------------------- */

static int test_create_mod_list(void) {
	ModList *ml = createModList();
	ASSERT_NOT_NULL(ml);
	ASSERT_EQ(ml->count, 0);
	free(ml);
	printf("PASS test_create_mod_list\n");
	return 0;
}

static int test_create_lfo(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();

	LFO *lfo = createLFO(pl, ml, 0, 2.0f, LS_SIN, "mylfo");
	ASSERT_NOT_NULL(lfo);
	ASSERT_EQ(ml->count, 1);
	ASSERT_PTR_EQ(ml->mods[0], (Mod *)lfo);
	ASSERT_EQ(lfo->base.type, MT_LFO);
	ASSERT_EQ(lfo->shape, LS_SIN);
	ASSERT_STREQ(lfo->base.name, "mylfo");
	ASSERT_NOT_NULL(lfo->base.output);
	ASSERT_NOT_NULL(lfo->rate);
	ASSERT_NOT_NULL(lfo->phase);
	ASSERT_NEAR(lfo->rate->baseValue, 2.0f, 1e-6f);
	ASSERT_NEAR(lfo->phase->baseValue, 0.0f, 1e-6f);

	/* BUG (modsystem.c:456): initMod ignores the generate argument and
	 * always installs generateEnvelope, even for LFOs. */
	ASSERT_EQ(lfo->base.generate == generateEnvelope, true);

	free_test_lists(pl, ml);
	printf("PASS test_create_lfo\n");
	return 0;
}

static int test_update_lfo_phase(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 2.0f, LS_SIN, "lfo");

	updateMod((Mod *)lfo, 0.25f);
	ASSERT_NEAR(getParameterValue(lfo->phase), 0.5f, 1e-6f);
	/* the wrap is `>= 1.0`, so phase never holds 1.0: it wraps to 0 */
	updateMod((Mod *)lfo, 0.25f);
	ASSERT_NEAR(getParameterValue(lfo->phase), 0.0f, 1e-6f);
	updateMod((Mod *)lfo, 0.25f);
	ASSERT_NEAR(getParameterValue(lfo->phase), 0.5f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_update_lfo_phase\n");
	return 0;
}

/* ---- generator functions (called directly: initMod bug bypassed) ------- */

static int test_generate_sine(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 2.0f, LS_SIN, "lfo");

	/* sin(2*pi*0.25) = 1 */
	setParameterValue(lfo->phase, 0.25f);
	generateSine(lfo);
	ASSERT_NEAR(lfo->base.output->baseValue, 1.0f, 1e-4f);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 1.0f, 1e-4f);
	/* sin(2*pi*0.5) = 0 */
	setParameterValue(lfo->phase, 0.5f);
	generateSine(lfo);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 0.0f, 1e-4f);

	free_test_lists(pl, ml);
	printf("PASS test_generate_sine\n");
	return 0;
}

static int test_generate_sine_negative_clamped(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 2.0f, LS_SIN, "lfo");

	/* BUG (modsystem.c:455): the mod output param has range [0,1], so the
	 * negative half of the sine is clamped to 0 instead of going to -1. */
	setParameterValue(lfo->phase, 0.75f);
	generateSine(lfo);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 0.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_generate_sine_negative_clamped\n");
	return 0;
}

static int test_generate_square(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SQU, "lfo");

	setParameterValue(lfo->phase, 0.25f);
	generateSquare(lfo);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 1.0f, 1e-6f);
	/* BUG: negative half (-1.0) clamped to 0 by the [0,1] output range */
	setParameterValue(lfo->phase, 0.75f);
	generateSquare(lfo);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 0.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_generate_square\n");
	return 0;
}

static int test_generate_ramp_clamped(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_RMP, "lfo");

	/* BUG: generateRamp produces (phase-1)*2, which is <= 0 for the whole
	 * phase range; with the [0,1] output range the ramp is always 0. */
	setParameterValue(lfo->phase, 0.5f);
	generateRamp(lfo);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 0.0f, 1e-6f);
	setParameterValue(lfo->phase, 1.0f);
	generateRamp(lfo);
	ASSERT_NEAR(getParameterValue(lfo->base.output), 0.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_generate_ramp_clamped\n");
	return 0;
}

static int test_generate_random_range(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Random *rnd = createRandom(pl, ml, 0, 1.0f, RT_SNH, "rnd");

	srand(1234);
	/* no wrap yet -> output stays at initial 0 */
	setParameterValue(rnd->phase, 0.9f);
	generateRandom(rnd);
	ASSERT_NEAR(getParameterValue(rnd->base.output), 0.0f, 1e-6f);
	/* phase wrap -> new random sample, clamped into [0,1] */
	setParameterValue(rnd->phase, 0.1f);
	generateRandom(rnd);
	ASSERT_TRUE(getParameterValue(rnd->base.output) >= 0.0f);
	ASSERT_TRUE(getParameterValue(rnd->base.output) <= 1.0f);
	/* another wrap -> another sample in range */
	setParameterValue(rnd->phase, 0.0f);
	generateRandom(rnd);
	ASSERT_TRUE(getParameterValue(rnd->base.output) >= 0.0f);
	ASSERT_TRUE(getParameterValue(rnd->base.output) <= 1.0f);

	free_test_lists(pl, ml);
	printf("PASS test_generate_random_range\n");
	return 0;
}

static int test_generate_drunk_range(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Random *rnd = createRandom(pl, ml, 0, 1.0f, RT_DRK, "drunk");

	srand(7);
	for (int i = 0; i < 50; i++) {
		setParameterValue(rnd->phase, (i % 10) / 10.0f);
		generateDrunk(rnd);
		float v = getParameterValue(rnd->base.output);
		ASSERT_TRUE(v >= 0.0f);
		ASSERT_TRUE(v <= 1.0f);
	}

	free_test_lists(pl, ml);
	printf("PASS test_generate_drunk_range\n");
	return 0;
}

/* ---- connections ------------------------------------------------------- */

static int test_create_connection(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo");

	ModConnection *conn = createConnection(pl, (Mod *)lfo, 0.5f, MO_MUL);
	ASSERT_NOT_NULL(conn);
	ASSERT_PTR_EQ(conn->source, (Mod *)lfo);
	ASSERT_NULL(conn->next);
	ASSERT_NULL(conn->previous);
	/* BUG (modsystem.c:194): the passed amount is ignored — the amount
	 * param is always created as 1.0. */
	ASSERT_NEAR(conn->amount->baseValue, 1.0f, 1e-6f);
	ASSERT_NEAR(getParameterValue(conn->type), (float)MO_MUL, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_create_connection\n");
	return 0;
}

static int test_add_modulation(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo");
	Parameter *dest = createParameter(pl, "dest", 0.0f, -10.0f, 10.0f);

	ASSERT_EQ(addModulation(pl, (Mod *)lfo, dest, 1.0f, MO_MUL), true);
	ASSERT_EQ(dest->modulator_count, 1);
	ASSERT_NOT_NULL(dest->modulators);
	ASSERT_PTR_EQ(dest->modulators->source, (Mod *)lfo);

	ModConnection *first = dest->modulators;
	ASSERT_EQ(addModulation(pl, (Mod *)lfo, dest, 1.0f, MO_MUL), true);
	ASSERT_EQ(dest->modulator_count, 2);
	/* new connection is added at the front of the list */
	ASSERT_PTR_EQ(dest->modulators, first->previous);
	ASSERT_PTR_EQ(dest->modulators->next, first);

	free_test_lists(pl, ml);
	printf("PASS test_add_modulation\n");
	return 0;
}

/* ---- processModulations ------------------------------------------------ */

static int test_process_no_connections(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *p = createParameter(pl, "p", 3.0f, 0.0f, 10.0f);

	/* currentValue is reset to baseValue by processModulations */
	setParameterValue(p, 7.0f);
	processModulations(pl, ml, 0.1f);
	ASSERT_NEAR(getParameterValue(p), 3.0f, 1e-6f);

	/* null-list robustness */
	processModulations(pl, NULL, 0.1f);
	processModulations(NULL, NULL, 0.1f);
	ASSERT_NEAR(getParameterValue(p), 3.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_process_no_connections\n");
	return 0;
}

/* helper: run processModulations once with a sine LFO wired to `dest`. */
static LFO *wire_sine(ParamList *pl, ModList *ml, Parameter *dest,
                      float rate, float dt, ModulationOperation op, float amount) {
	LFO *lfo = createLFO(pl, ml, 0, rate, LS_SIN, "lfo");
	/* BUG workaround (modsystem.c:456): initMod ignored the generator
	 * argument, so reassign the correct generator for end-to-end use. */
	lfo->base.generate = generateSine;
	addModulation(pl, (Mod *)lfo, dest, amount, op);
	processModulations(pl, ml, dt);
	return lfo;
}

static int test_process_mul_modulation(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *dest = createParameter(pl, "dest", 0.5f, -10.0f, 10.0f);

	/* dt=0.0625, rate=2 -> phase 0.125 -> sin(pi/4)=0.7071; 0.5*0.7071=0.3536 */
	wire_sine(pl, ml, dest, 2.0f, 0.0625f, MO_MUL, 1.0f);
	ASSERT_NEAR(getParameterValue(dest), 0.5f * 0.70710678f, 1e-3f);
	/* phase 0.5 -> modValue 0 -> dest 0 */
	processModulations(pl, ml, 0.1875f);
	ASSERT_NEAR(getParameterValue(dest), 0.0f, 1e-3f);

	free_test_lists(pl, ml);
	printf("PASS test_process_mul_modulation\n");
	return 0;
}

static int test_process_add_modulation(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *dest = createParameter(pl, "dest", 1.0f, -10.0f, 10.0f);

	/* dt=0.125, rate=2 -> phase 0.25 -> modValue 1.0.
	 * BUG (modsystem.c:188,673): amount 0.25 is ignored, so the result is
	 * base + 1.0 = 2.0 instead of base + 0.25*1.0 = 1.25. */
	wire_sine(pl, ml, dest, 2.0f, 0.125f, MO_ADD, 0.25f);
	ASSERT_NEAR(getParameterValue(dest), 2.0f, 1e-3f);
	/* phase 0.5 -> modValue ~0 -> back near base */
	processModulations(pl, ml, 0.125f);
	ASSERT_NEAR(getParameterValue(dest), 1.0f, 1e-3f);

	free_test_lists(pl, ml);
	printf("PASS test_process_add_modulation\n");
	return 0;
}

static int test_process_sub_modulation(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *dest = createParameter(pl, "dest", 0.5f, -10.0f, 10.0f);

	/* phase 0.25 -> modValue 1.0 -> 0.5 - 1.0 = -0.5 */
	wire_sine(pl, ml, dest, 2.0f, 0.125f, MO_SUB, 1.0f);
	ASSERT_NEAR(getParameterValue(dest), -0.5f, 1e-3f);

	free_test_lists(pl, ml);
	printf("PASS test_process_sub_modulation\n");
	return 0;
}

static int test_process_div_modulation(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *dest = createParameter(pl, "dest", 1.0f, -10.0f, 10.0f);

	/* dt=1/12, rate=1 -> phase 1/12 -> sin(pi/6)=0.5 -> 1.0/0.5=2.0 */
	LFO *lfo = wire_sine(pl, ml, dest, 1.0f, 1.0f / 12.0f, MO_DIV, 1.0f);
	ASSERT_NEAR(getParameterValue(dest), 2.0f, 1e-3f);

	/* phase 0.5833 -> sin negative -> clamped modValue 0 -> division is
	 * skipped by the div-by-zero guard, dest stays at base. */
	processModulations(pl, ml, 0.5f);
	ASSERT_NEAR(getParameterValue(dest), 1.0f, 1e-3f);
	ASSERT_TRUE(getParameterValue(lfo->phase) > 0.5f);

	free_test_lists(pl, ml);
	printf("PASS test_process_div_modulation\n");
	return 0;
}

static int test_connection_application_order(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *dest = createParameter(pl, "dest", 10.0f, 0.0f, 100.0f);

	/* LFO1: rate 2, LFO2: rate 1, dt = 1/12.
	 * LFO1 phase 1/6  -> sin(pi/3) = 0.8660254
	 * LFO2 phase 1/12 -> sin(pi/6) = 0.5
	 * Add LFO1 first (MUL), then LFO2 (ADD) — LFO2 is at the HEAD and is
	 * applied first: (10 + 0.5) * 0.8660254 = 9.09327.
	 * If the list were applied tail-first: 10*0.8660254 + 0.5 = 9.16025. */
	LFO *lfo1 = createLFO(pl, ml, 0, 2.0f, LS_SIN, "lfo1");
	lfo1->base.generate = generateSine; /* BUG workaround, see wire_sine */
	LFO *lfo2 = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo2");
	lfo2->base.generate = generateSine; /* BUG workaround, see wire_sine */
	addModulation(pl, (Mod *)lfo1, dest, 1.0f, MO_MUL);
	addModulation(pl, (Mod *)lfo2, dest, 1.0f, MO_ADD);
	processModulations(pl, ml, 1.0f / 12.0f);

	ASSERT_NEAR(getParameterValue(dest), 10.5f * 0.8660254f, 1e-3f);
	ASSERT_NEAR(getParameterValue(dest), 9.0933f, 1e-3f);

	free_test_lists(pl, ml);
	printf("PASS test_connection_application_order\n");
	return 0;
}

static int test_process_env_modulation_end_to_end(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");
	Parameter *dest = createParameter(pl, "dest", 0.0f, -10.0f, 10.0f);
	Parameter *ctrl = createParameter(pl, "ctrl", 0.5f, 0.0f, 1.0f);

	addModulation(pl, (Mod *)env, dest, 1.0f, MO_ADD);
	triggerEnvelope(env);

	/* Each processModulations advances env time by deltaTime + generate's dt,
	 * so 300 iterations * 2 * (1/44100) = 0.0136s into a 0.05s attack. */
	for (int i = 0; i < 300; i++) {
		processModulations(pl, ml, 1.0f / PA_SR);
	}
	float modVal = getParameterValue(env->base.output);
	ASSERT_TRUE(modVal > 0.1f && modVal < 0.5f);
	ASSERT_TRUE(env->isTriggered);
	ASSERT_NEAR(getParameterValue(dest), modVal, 1e-3f);
	/* unmodulated control param untouched */
	ASSERT_NEAR(getParameterValue(ctrl), 0.5f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_process_env_modulation_end_to_end\n");
	return 0;
}

/* ---- envelope ---------------------------------------------------------- */

static int test_adsr_stage_setup(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");

	ASSERT_EQ(env->stageCount, 4);
	ASSERT_STREQ(env->stages[0].name, "A");
	ASSERT_STREQ(env->stages[1].name, "D");
	ASSERT_STREQ(env->stages[2].name, "S");
	ASSERT_STREQ(env->stages[3].name, "R");
	ASSERT_EQ(env->stages[0].isRising, true);
	ASSERT_EQ(env->stages[1].isRising, false);
	ASSERT_NEAR(env->stages[0].targetLevel, 1.0f, 1e-6f);
	ASSERT_NEAR(env->stages[1].targetLevel, 0.7f, 1e-6f);
	ASSERT_NEAR(env->stages[2].targetLevel, 0.7f, 1e-6f);
	ASSERT_NEAR(env->stages[3].targetLevel, 0.0f, 1e-6f);
	ASSERT_NEAR(env->stages[0].duration->baseValue, 0.05f, 1e-6f);
	ASSERT_NEAR(env->stages[2].duration->baseValue, 0.7f, 1e-6f);
	ASSERT_NEAR(env->stages[3].duration->baseValue, 0.1f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_adsr_stage_setup\n");
	return 0;
}

static int test_env_before_trigger(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");

	ASSERT_FALSE(env->isTriggered);
	ASSERT_NEAR(getParameterValue(env->base.output), 0.0f, 1e-6f);
	/* generate before trigger is a no-op */
	env->base.generate(env);
	ASSERT_NEAR(getParameterValue(env->base.output), 0.0f, 1e-6f);

	triggerEnvelope(env);
	ASSERT_TRUE(env->isTriggered);
	ASSERT_EQ(env->currentStageIndex, 0);
	ASSERT_NEAR(env->currentTime, 0.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_env_before_trigger\n");
	return 0;
}

/* run env->base.generate until currentStageIndex reaches target (or maxCalls) */
static int run_env_to_stage(Envelope *env, int targetStage, int maxCalls) {
	int n = 0;
	while (n < maxCalls && env->isTriggered && env->currentStageIndex < targetStage) {
		env->base.generate(env);
		n++;
	}
	return n;
}

static int test_env_attack_progress(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");

	triggerEnvelope(env);
	/* 550 calls = ~1/4 of the 0.05s attack: level ~ 0.25. The attack is
	 * 2203 calls long, so the stage is not complete yet. */
	for (int i = 0; i < 550; i++) {
		env->base.generate(env);
	}
	ASSERT_NEAR(getParameterValue(env->base.output), 0.25f, 0.02f);
	ASSERT_EQ(env->currentStageIndex, 0);

	free_test_lists(pl, ml);
	printf("PASS test_env_attack_progress\n");
	return 0;
}

static int test_env_stage_transitions_and_counts(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");

	triggerEnvelope(env);
	int calls = 0;

	/* attack: 0 -> 1.0, expected ~2203 generate() calls
	 * (a stage completes when t >= 1023/1024 of its duration) */
	calls += run_env_to_stage(env, 1, 100000);
	ASSERT_NEAR(calls, 2203, 3);
	ASSERT_NEAR(getParameterValue(env->base.output), 1.0f, 1e-6f);
	ASSERT_EQ(env->currentStageIndex, 1);

	/* decay: 1.0 -> 0.7 */
	calls = 0;
	calls += run_env_to_stage(env, 2, 100000);
	ASSERT_NEAR(calls, 2203, 3);
	ASSERT_NEAR(getParameterValue(env->base.output), 0.7f, 1e-6f);
	ASSERT_EQ(env->currentStageIndex, 2);

	/* sustain-style hold: 0.7 -> 0.7. Naive math says 30840 calls
	 * (0.7*44100*1023/1024), but the sequential float accumulator in
	 * generateEnvelope loses ~0.5 ulp per add, pushing completion to
	 * 30848. Deterministic; assert actual. */
	calls = 0;
	calls += run_env_to_stage(env, 3, 100000);
	ASSERT_NEAR(calls, 30848, 3);
	ASSERT_NEAR(getParameterValue(env->base.output), 0.7f, 1e-6f);
	ASSERT_EQ(env->currentStageIndex, 3);

	/* release: 0.7 -> 0.0, then envelope finishes */
	calls = 0;
	calls += run_env_to_stage(env, 4, 100000);
	ASSERT_NEAR(calls, 4406, 3);
	ASSERT_NEAR(getParameterValue(env->base.output), 0.0f, 1e-6f);
	ASSERT_EQ(env->currentStageIndex, 4);
	ASSERT_FALSE(env->isTriggered);

	free_test_lists(pl, ml);
	printf("PASS test_env_stage_transitions_and_counts\n");
	return 0;
}

static int test_env_loop_ignored(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createAD(pl, ml, 0.05f, 0.05f, "env");

	env->loop = true;
	triggerEnvelope(env);
	int calls = 0;
	while (env->isTriggered && calls < 50000) {
		env->base.generate(env);
		calls++;
	}
	/* BUG (modsystem.c:385): generateEnvelope never honors env->loop — the
	 * envelope stops after the last stage instead of wrapping to stage 0. */
	ASSERT_FALSE(env->isTriggered);
	ASSERT_EQ(calls, 4406); /* 2 stages of 0.05s */
	ASSERT_NEAR(getParameterValue(env->base.output), 0.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_env_loop_ignored\n");
	return 0;
}

static int test_add_envelope_stage_sustain_flag(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createEnvelope(pl, ml, "env");

	addEnvelopeStage(pl, env, true, 0.05f, 1.0f, 0.5f, "A");
	ASSERT_EQ(env->stageCount, 1);
	ASSERT_EQ(env->stages[0].isSustain, false);
	ASSERT_NEAR(env->stages[0].duration->baseValue, 0.05f, 1e-6f);

	/* BUG (modsystem.c:493,496): a <=0 duration sets isSustain=true but the
	 * duration param is clamped to the 0.001 minimum, so it is not 0. */
	addEnvelopeStage(pl, env, true, 0.0f, 0.5f, 0.5f, "SUS");
	ASSERT_EQ(env->stageCount, 2);
	ASSERT_EQ(env->stages[1].isSustain, true);
	ASSERT_NEAR(env->stages[1].duration->baseValue, 0.001f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_add_envelope_stage_sustain_flag\n");
	return 0;
}

static int test_param_pointer_envelope(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Parameter *a = createParameter(pl, "a", 0.1f, 0.001f, 10.0f);
	Parameter *d = createParameter(pl, "d", 0.2f, 0.001f, 10.0f);
	Parameter *ac = createParameter(pl, "ac", 0.8f, -1.0f, 1.0f);
	Parameter *dc = createParameter(pl, "dc", 0.2f, -1.0f, 1.0f);

	Envelope *env = createParamPointerAD(pl, ml, a, d, ac, dc, "ppad");
	ASSERT_EQ(env->stageCount, 2);
	ASSERT_PTR_EQ(env->stages[0].duration, a);
	ASSERT_PTR_EQ(env->stages[1].duration, d);
	ASSERT_PTR_EQ(env->stages[0].curvature, ac);
	ASSERT_PTR_EQ(env->stages[1].curvature, dc);
	ASSERT_NEAR(env->stages[0].targetLevel, 1.0f, 1e-6f);
	ASSERT_NEAR(env->stages[1].targetLevel, 0.0f, 1e-6f);

	free_test_lists(pl, ml);
	printf("PASS test_param_pointer_envelope\n");
	return 0;
}

/* ---- presets ----------------------------------------------------------- */

static int test_preset_data_init(void) {
	ModPreset mp;

	initADPresetData(&mp, 0.1f, 0.2f, 0.5f, 0.5f);
	ASSERT_EQ(mp.type, MT_ENV);
	ASSERT_EQ(mp.md.env.stageCount, 2);
	ASSERT_NEAR(mp.md.env.stages[0].duration, 0.1f, 1e-6f);
	ASSERT_NEAR(mp.md.env.stages[1].duration, 0.2f, 1e-6f);
	ASSERT_EQ(mp.md.env.stages[0].isRising, true);
	ASSERT_EQ(mp.md.env.stages[1].isRising, false);
	ASSERT_NEAR(mp.md.env.stages[0].targetLevel, 1.0f, 1e-6f);
	ASSERT_NEAR(mp.md.env.stages[1].targetLevel, 0.0f, 1e-6f);
	ASSERT_STREQ(mp.md.env.stages[0].name, "AD_Atk");

	initLfoPresetData(&mp, LS_SQU, 2.0f, 0.25f);
	ASSERT_EQ(mp.type, MT_LFO);
	ASSERT_EQ(mp.md.lfo.shape, LS_SQU);
	ASSERT_NEAR(mp.md.lfo.rate, 2.0f, 1e-6f);
	ASSERT_NEAR(mp.md.lfo.phase, 0.25f, 1e-6f);

	initRandPresetData(&mp, LS_SIN, 1.0f, 0.5f);
	ASSERT_EQ(mp.type, MT_RND);
	ASSERT_NEAR(mp.md.rand.rate, 1.0f, 1e-6f);
	ASSERT_NEAR(mp.md.rand.phase, 0.5f, 1e-6f);

	printf("PASS test_preset_data_init\n");
	return 0;
}

static int test_env_preset_roundtrip(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");

	EnvPresetData epd;
	saveEnvPreset(&epd, env);
	ASSERT_EQ(epd.stageCount, 4);
	ASSERT_NEAR(epd.stages[0].duration, 0.05f, 1e-6f);
	ASSERT_NEAR(epd.stages[2].targetLevel, 0.7f, 1e-6f);
	ASSERT_STREQ(epd.stages[0].name, "A");
	ASSERT_STREQ(epd.stages[3].name, "R");

	/* rebuild an envelope from the saved preset */
	ParamList *pl2 = createParamList();
	ModList *ml2 = createModList();
	Envelope *env2 = (Envelope *)malloc(sizeof(Envelope));
	ASSERT_NOT_NULL(env2);
	ModPreset mp;
	mp.type = MT_ENV;
	mp.md.env = epd;
	initEnvelopeFromPreset(&mp, env2, pl2, ml2);
	ASSERT_EQ(env2->stageCount, 4);
	ASSERT_NEAR(env2->stages[0].duration->baseValue, 0.05f, 1e-6f);
	ASSERT_NEAR(env2->stages[2].targetLevel, 0.7f, 1e-6f);
	ASSERT_NEAR(env2->stages[3].targetLevel, 0.0f, 1e-6f);
	ASSERT_STREQ(env2->stages[0].name, "A");

	free_test_lists(pl, ml);
	free_test_lists(pl2, ml2);
	printf("PASS test_env_preset_roundtrip\n");
	return 0;
}

static int test_lfo_preset_roundtrip(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 2.0f, LS_SQU, "lfo");

	LfoPresetData lpd;
	saveLfoPreset(&lpd, lfo);
	ASSERT_NEAR(lpd.rate, 2.0f, 1e-6f);
	ASSERT_EQ(lpd.shape, LS_SQU);

	ParamList *pl2 = createParamList();
	ModList *ml2 = createModList();
	LFO *lfo2 = (LFO *)malloc(sizeof(LFO));
	ASSERT_NOT_NULL(lfo2);
	initLfoFromPreset(&lpd, lfo2, pl2, ml2);
	ASSERT_NEAR(lfo2->rate->baseValue, 2.0f, 1e-6f);
	ASSERT_EQ(lfo2->shape, LS_SQU);
	/* BUG (modsystem.c:456): initLfoFromPreset -> initMod also installs
	 * generateEnvelope instead of the shape's generator. */
	ASSERT_EQ(lfo2->base.generate == generateEnvelope, true);

	free_test_lists(pl, ml);
	free_test_lists(pl2, ml2);
	printf("PASS test_lfo_preset_roundtrip\n");
	return 0;
}

static int test_rand_preset_noop(void) {
	/* BUG (modsystem.c:668-671): initRandFromPreset/saveRandPreset are
	 * empty stubs — nothing is written or read. Smoke-test they don't crash. */
	RandPresetData rpd = {0};
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Random *rnd = createRandom(pl, ml, 0, 1.0f, RT_SNH, "rnd");
	initRandFromPreset(&rpd, rnd, pl, ml);
	saveRandPreset(&rpd, rnd);
	ASSERT_TRUE(true);
	free_test_lists(pl, ml);
	printf("PASS test_rand_preset_noop\n");
	return 0;
}

/* ---- curve helpers ----------------------------------------------------- */

static int test_apply_curve(void) {
	/* curvature 0.5 == linear */
	ASSERT_NEAR(applyCurve(0.5f, 0.5f), 0.5f, 1e-6f);
	ASSERT_NEAR(applyCurve(0.25f, 0.5f), 0.25f, 1e-6f);
	ASSERT_NEAR(applyCurve(0.0f, 0.5f), 0.0f, 1e-6f);
	ASSERT_NEAR(applyCurve(1.0f, 0.5f), 1.0f, 1e-6f);
	/* inputs are clamped into [0,1] */
	ASSERT_NEAR(applyCurve(2.0f, 0.5f), 1.0f, 1e-6f);
	ASSERT_NEAR(applyCurve(-1.0f, 0.5f), 0.0f, 1e-6f);
	/* curvature 1.0 -> exponent 1+4=5 */
	ASSERT_NEAR(applyCurve(0.25f, 1.0f), powf(0.25f, 5.0f), 1e-4f);
	/* curvature 0.0 -> 1 - (1-x)^5 */
	ASSERT_NEAR(applyCurve(0.25f, 0.0f), 1.0f - powf(0.75f, 5.0f), 1e-4f);

	printf("PASS test_apply_curve\n");
	return 0;
}

static int test_generate_curve(void) {
	float data[1024];
	/* curve 0.5: straight line through (0,0)..(1,1) */
	generateCurve(data, 1024, 0.5f, 4);
	ASSERT_NEAR(data[0], 0.0f, 1e-6f);
	ASSERT_NEAR(data[512], 512.0f / 1023.0f, 1e-3f);
	ASSERT_NEAR(data[1023], 1.0f, 1e-6f);

	/* curve > 0.5 is convex (midpoint below the diagonal) */
	generateCurve(data, 1024, 0.75f, 4);
	ASSERT_NEAR(data[0], 0.0f, 1e-6f);
	ASSERT_NEAR(data[1023], 1.0f, 1e-6f);
	ASSERT_TRUE(data[512] < 0.5f);
	ASSERT_NEAR(data[512], powf(512.0f / 1023.0f, 0.75f * 2 * 4), 1e-4f);

	/* curve < 0.5 is concave (midpoint above the diagonal) */
	generateCurve(data, 1024, 0.25f, 4);
	ASSERT_TRUE(data[512] > 0.5f);

	printf("PASS test_generate_curve\n");
	return 0;
}

/* ---- clear / free ------------------------------------------------------ */

static int test_clear_lists(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo");
	(void)lfo; /* side effect: populates the mod list */
	createParameter(pl, "extra", 1.0f, 0.0f, 1.0f);
	ASSERT_EQ(pl->count, 4); /* output, rate, phase, extra */
	ASSERT_EQ(ml->count, 1);

	/* BUG (modsystem.c:65,80): clearParamList/clearModList only reset the
	 * count; the free loops are commented out, so the objects leak. */
	clearParamList(pl);
	clearModList(ml);
	ASSERT_EQ(pl->count, 0);
	ASSERT_EQ(ml->count, 0);

	/* to avoid double frees of the leaked objects, leak them intentionally */
	printf("PASS test_clear_lists\n");
	return 0;
}

static int test_free_lists_smoke(void) {
	/* build a full graph and free it exactly once via the same paths the
	 * app uses (cleanupModSystem + freeParamList, with mod-owned params
	 * removed from the list first) */
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo");
	lfo->base.generate = generateSine;
	Random *rnd = createRandom(pl, ml, 0, 2.0f, RT_SNH, "rnd");
	Envelope *env = createADSR(pl, ml, 0.05f, 0.05f, 0.7f, 0.1f, "env");
	Parameter *dest = createParameter(pl, "dest", 0.5f, 0.0f, 1.0f);
	addModulation(pl, (Mod *)lfo, dest, 1.0f, MO_ADD);
	addModulation(pl, (Mod *)env, dest, 1.0f, MO_MUL);
	(void)lfo;
	(void)rnd;

	free_test_lists(pl, ml);
	printf("PASS test_free_lists_smoke\n");
	return 0;
}

static int test_mod_system_init(void) {
	/* initModSystem() populates the global wavetable pool used by the
	 * envelope generator: 16 curve tables of 1024 samples. */
	ASSERT_NOT_NULL(envTables);
	ASSERT_EQ(envTables->tableCount, 16);
	printf("PASS test_mod_system_init\n");
	return 0;
}

/* ---- main -------------------------------------------------------------- */

int main(void) {
	initModSystem(); /* builds envTables; envelope generation needs it */

	int failed = 0;
	failed |= test_create_param_list();
	failed |= test_add_parameter();
	failed |= test_parameter_ex_pro();
	failed |= test_set_parameter_value_clamps();
	failed |= test_set_parameter_base_value();
	failed |= test_set_min_max_values();
	failed |= test_get_parameter_value_int();
	failed |= test_modify_parameter_values();
	failed |= test_create_mod_list();
	failed |= test_create_lfo();
	failed |= test_update_lfo_phase();
	failed |= test_generate_sine();
	failed |= test_generate_sine_negative_clamped();
	failed |= test_generate_square();
	failed |= test_generate_ramp_clamped();
	failed |= test_generate_random_range();
	failed |= test_generate_drunk_range();
	failed |= test_create_connection();
	failed |= test_add_modulation();
	failed |= test_process_no_connections();
	failed |= test_process_mul_modulation();
	failed |= test_process_add_modulation();
	failed |= test_process_sub_modulation();
	failed |= test_process_div_modulation();
	failed |= test_connection_application_order();
	failed |= test_process_env_modulation_end_to_end();
	failed |= test_adsr_stage_setup();
	failed |= test_env_before_trigger();
	failed |= test_env_attack_progress();
	failed |= test_env_stage_transitions_and_counts();
	failed |= test_env_loop_ignored();
	failed |= test_add_envelope_stage_sustain_flag();
	failed |= test_param_pointer_envelope();
	failed |= test_preset_data_init();
	failed |= test_env_preset_roundtrip();
	failed |= test_lfo_preset_roundtrip();
	failed |= test_rand_preset_noop();
	failed |= test_apply_curve();
	failed |= test_generate_curve();
	failed |= test_clear_lists();
	failed |= test_free_lists_smoke();
	failed |= test_mod_system_init();

	freeWavetablePool(envTables); /* initModSystem's pool */

	if (failed) {
		printf("\nSome tests FAILED.\n");
	} else {
		printf("\nAll tests PASSED.\n");
	}
	return failed;
}
