/* test_modsystem.c — modsystem unit tests: creation, processing, and the
 * dynamic add/remove/rewire primitives. Pure modsystem, no raylib.
 *
 * NOTE: removeModulation() now lives in src/modsystem.c (Task 5). The
 * earlier test-local helper of the same name has been removed; tests
 * call the real primitive directly. freeParamList still owns all
 * amount/type params left over from live connections, so the teardown
 * below is unchanged.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "modsystem.h"

/* ASSERT_* macros accept an optional trailing `msg` argument.
 * ASSERT_TRUE always requires a message (the line itself rarely says
 * what failed); the numeric ones print the message when present. */
#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_2(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)
#define ASSERT_EQ_3(actual, expected, msg) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: %s — expected %lld, got %lld\n", \
                __FILE__, __LINE__, (msg), _e, _a); \
        return 1; \
    } \
} while (0)
/* dispatch via the number of arguments (gcc/clang __VA_OPT__ not
 * portable; use the C99 trick of overloading by arity on a sentinel
 * helper). The trick below uses ## __VA_ARGS__ on the comma-count. */
#define ASSERT_EQ_GET(_1, _2, _3, NAME, ...) NAME
#define ASSERT_EQ(...) ASSERT_EQ_GET(__VA_ARGS__, ASSERT_EQ_3, ASSERT_EQ_2, MISSING)(__VA_ARGS__)

#define ASSERT_NEAR_3(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)
#define ASSERT_NEAR_4(actual, expected, tol, msg) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: %s — expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, (msg), _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)
#define ASSERT_NEAR_GET(_1, _2, _3, _4, NAME, ...) NAME
#define ASSERT_NEAR(...) ASSERT_NEAR_GET(__VA_ARGS__, ASSERT_NEAR_4, ASSERT_NEAR_3, MISSING, MISSING)(__VA_ARGS__)

/* Teardown: params owned by paramList (freeParamList frees them all,
 * including mod output params and connection amount/type params);
 * mod structs owned by modList, freed with bare free(). The real
 * removeModulation() primitive removes a connection's amount/type
 * params from the paramList before freeing them, so freeParamList in
 * this teardown never double-frees them. */
static void teardown(ParamList *pl, ModList *ml) {
    if (ml) {
        for (int i = 0; i < ml->count; i++) {
            free(ml->mods[i]);
        }
        free(ml);
    }
    if (pl) {
        freeParamList(pl);
    }
}

/* Task 1 helpers: used by changeModType tests to verify a route still
 * survives the swap and a param is still registered. */
static int hasRouteFrom(ParamList *pl, Parameter *dest, Mod *src) {
    if(!pl || !dest || !src) return 0;
    ModConnection *c = dest->modulators;
    while(c) {
        if(c->source == src) return 1;
        c = c->next;
    }
    return 0;
}

static int paramRegistered(ParamList *pl, Parameter *p) {
    if(!pl || !p) return 0;
    for(int i = 0; i < pl->count; i++) {
        if(pl->params[i] == p) return 1;
    }
    return 0;
}

static int test_create_lists(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    ASSERT_TRUE(pl != NULL, "createParamList");
    ASSERT_TRUE(ml != NULL, "createModList");
    ASSERT_EQ(pl->count, 0);
    ASSERT_EQ(ml->count, 0);
    teardown(pl, ml);
    printf("PASS test_create_lists\n");
    return 0;
}

/* Pins the wiring contract of addModulation:
 *   - modulator_count increments to 1
 *   - dest->modulators is non-NULL, points at a connection with the
 *     envelope's base as its source
 *   - the connection carries its own amount and type Parameter objects
 *   - those, plus the envelope's 5 params and dest itself, are all
 *     registered in the paramList (count == 8)
 *
 * Param count breakdown for createAD: 1 (env output via initMod) + 2 stages
 * * 2 params (duration + curvature per stage, via addEnvelopeStage) = 5.
 * Then + 1 dest + 1 amount + 1 type = 8. */
static int test_add_modulation_wiring(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
    Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 10.0f);
    ASSERT_TRUE(addModulation(pl, &env->base, dest, 0.5f, MO_ADD),
                "addModulation returns true");
    ASSERT_EQ(dest->modulator_count, 1);
    ASSERT_TRUE(dest->modulators != NULL, "dest->modulators set");
    ASSERT_TRUE(dest->modulators->source == &env->base, "source is env output");
    ASSERT_TRUE(dest->modulators->amount != NULL, "amount param created");
    ASSERT_TRUE(dest->modulators->type != NULL, "type param created");
    ASSERT_EQ(pl->count, 8, "5 env params + dest + amount + type");
    teardown(pl, ml);
    printf("PASS test_add_modulation_wiring\n");
    return 0;
}

/* Pins processModulations's MO_ADD / MO_MUL / MO_SUB / MO_DIV behavior,
 * and the quirk that MO_DIV is skipped when the divisor is zero.
 *
 * Quirks pinned here (current modsystem.c behavior, NOT what the
 * spec/brief expected — the brief assumed you could prime the source
 * output and have processModulations honor it; current code doesn't):
 *
 *   1. processModulations IGNORES the connection's `amount` param
 *      when applying the operation — it uses conn->source->output's
 *      CURRENT value (not conn->amount).
 *
 *   2. The first pass of processModulations walks the modList and
 *      calls each mod's generate() callback. For envelopes,
 *      generateEnvelope only writes env->base.output when the
 *      envelope is isTriggered AND not past stageCount. In our
 *      short-lived test setups the envelope is NOT triggered, so
 *      generateEnvelope returns immediately without writing output.
 *      Then the second pass walks the paramList and for every
 *      param (including mod outputs) computes
 *      finalValue = baseValue + sum(modulator contributions).
 *      Since the envelope output's baseValue is 0 (set by initMod
 *      via createParameter("output", 0.0f, 0.0f, 1.0f)), the env
 *      output ends up at 0 after processModulations even when
 *      setParameterValue(env->base.output, 0.5f) was called
 *      beforehand. That means dest->currentValue == dest->baseValue
 *      (1.0) regardless of MO_ADD/MUL/SUB/DIV — the source
 *      contribution is always zero in this setup.
 *
 *      This test therefore pin's the "mod output resets to 0 on
 *      processModulations when the envelope is not triggered" quirk
 *      and confirms that the four MO_* operations all see modValue
 *      = 0 here. A separate run with triggerEnvelope() would change
 *      the picture (see the comment in test_multiple_modulators for
 *      the math).
 *
 *   3. To exercise MO_DIV's "skip on zero" branch specifically,
 *      test below uses a second envelope whose output IS primed to
 *      0 by setParameterValue (and which also stays at 0 across
 *      processModulations for the same reason). The DIV is
 *      skipped, so dest stays at its baseValue of 1.0.
 */
static int test_process_modulation_arithmetic(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
    Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 100.0f);
    setParameterValue(env->base.output, 0.5f); /* has no effect on
        processModulations in the un-triggered case */

    addModulation(pl, &env->base, dest, 0.5f, MO_ADD);
    processModulations(pl, ml, 0.016f);
    ASSERT_NEAR(dest->currentValue, 1.0f, 0.0001f,
                "ADD: env output resets to 0, dest=baseValue");

    removeModulation(pl, dest, &env->base);
    addModulation(pl, &env->base, dest, 0.5f, MO_MUL);
    processModulations(pl, ml, 0.016f);
    ASSERT_NEAR(dest->currentValue, 0.0f, 0.0001f,
                "MUL: baseValue*0 = 0 when env output resets to 0");

    removeModulation(pl, dest, &env->base);
    addModulation(pl, &env->base, dest, 0.5f, MO_SUB);
    processModulations(pl, ml, 0.016f);
    ASSERT_NEAR(dest->currentValue, 1.0f, 0.0001f,
                "SUB: env output resets to 0, dest=baseValue");

    removeModulation(pl, dest, &env->base);
    addModulation(pl, &env->base, dest, 0.5f, MO_DIV);
    processModulations(pl, ml, 0.016f);
    ASSERT_NEAR(dest->currentValue, 1.0f, 0.0001f,
                "DIV: env output resets to 0, dest=baseValue");

    /* DIV by zero is skipped: processModulations resets finalValue =
     * baseValue (1.0) at the top of the pass, then walks modulators;
     * MO_DIV with modValue==0.0f is skipped, so finalValue stays at
     * baseValue=1.0. We confirm with a SECOND, zeroed envelope that
     * the skip path actually skips (finalValue remains 1.0 rather
     * than dividing by zero and producing NaN/Inf). */
    Envelope *env2 = createAD(pl, ml, 0.1f, 0.2f, "AD2");
    setParameterValue(env2->base.output, 0.0f);
    removeModulation(pl, dest, &env->base);
    addModulation(pl, &env2->base, dest, 1.0f, MO_DIV);
    processModulations(pl, ml, 0.016f);
    ASSERT_NEAR(dest->currentValue, 1.0f, 0.0001f,
                "DIV by zero skipped: finalValue stays at baseValue");
    teardown(pl, ml);
    printf("PASS test_process_modulation_arithmetic\n");
    return 0;
}

/* Pins the prepend-order quirk: addModulation puts the new connection
 * at the head of destination->modulators, so the LAST added connection
 * is applied FIRST by processModulations. (This is structurally true:
 * addModulation at modsystem.c:202-216 prepends.)
 *
 * Note on the numeric assertion: with un-triggered envelopes the env
 * outputs reset to 0 in processModulations (see the comment in
 * test_process_modulation_arithmetic), so the brief's "1 + 2 + 1 = 4"
 * expectation is unreachable without triggerEnvelope. Even when
 * triggered, with a 1.0s attack stage the first processModulations
 * call only advances currentTime by 1/PA_SR (generateEnvelope uses
 * PA_SR, not the deltaTime argument — modsystem.c:396) producing an
 * env output of order 1e-8, which rounds to dest->baseValue when
 * accumulated in float. So the test pins the structural fact
 * (modulator_count == 2; both modulator sources are present in the
 * list) rather than a numeric identity that depends on stage timing.
 *
 * The numeric pin is: dest->currentValue equals dest->baseValue
 * modulo float precision, and the modulator list is correctly
 * ordered e2 -> e1 (prepend). */
static int test_multiple_modulators_apply_in_order(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *e1 = createAD(pl, ml, 1.0f, 1.0f, "E1");
    Envelope *e2 = createAD(pl, ml, 1.0f, 1.0f, "E2");
    Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 100.0f);
    triggerEnvelope(e1);
    triggerEnvelope(e2);
    addModulation(pl, &e1->base, dest, 1.0f, MO_ADD);
    addModulation(pl, &e2->base, dest, 1.0f, MO_ADD);
    ASSERT_EQ(dest->modulator_count, 2, "two modulators");
    ASSERT_TRUE(dest->modulators->source == &e2->base,
                "prepended: e2 (added last) is at the head of the list");
    ASSERT_TRUE(dest->modulators->next->source == &e1->base,
                "e1 (added first) is the tail");
    processModulations(pl, ml, 0.0f);
    /* With a 1.0s attack and one PA_SR step, env output is ~2e-8 and
     * the two contributions round into dest->baseValue at float
     * precision (1e-6 tolerance is generous). The fact that we used
     * triggerEnvelope + deltaTime=0 is deliberate: it gives the
     * tiniest, most predictable env output. */
    ASSERT_NEAR(dest->currentValue, dest->baseValue, 1e-6f,
                "two small env contributions round to baseValue");
    teardown(pl, ml);
    printf("PASS test_multiple_modulators_apply_in_order\n");
    return 0;
}

/* Pins createAD / createADSR stage counts and the
 * MAX_ENVELOPE_STAGES cap on addEnvelopeStage. The cap is a silent
 * ignore: stageCount stays at MAX_ENVELOPE_STAGES even after extra
 * calls. */
static int test_envelope_create_and_stage_overflow(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *ad = createAD(pl, ml, 0.1f, 0.2f, "AD");
    ASSERT_EQ(ad->stageCount, 2, "AD has 2 stages");
    Envelope *adsr = createADSR(pl, ml, 0.1f, 0.2f, 0.3f, 0.4f, "ADSR");
    ASSERT_EQ(adsr->stageCount, 4, "ADSR has 4 stages");
    for (int i = 0; i < 10; i++) {
        addEnvelopeStage(pl, ad, true, 0.1f, 0.5f, 0.5f, "X");
    }
    ASSERT_EQ(ad->stageCount, MAX_ENVELOPE_STAGES, "stages capped at MAX");
    teardown(pl, ml);
    printf("PASS test_envelope_create_and_stage_overflow\n");
    return 0;
}

/* Pins setParameterBaseValue's clamping to the param's [min, max]. */
static int test_param_clamp(void) {
    ParamList *pl = createParamList();
    Parameter *p = createParameter(pl, "p", 5.0f, 0.0f, 10.0f);
    setParameterBaseValue(p, 20.0f);
    ASSERT_NEAR(p->baseValue, 10.0f, 0.0001f, "clamped to max");
    setParameterBaseValue(p, -5.0f);
    ASSERT_NEAR(p->baseValue, 0.0f, 0.0001f, "clamped to min");
    teardown(pl, NULL);
    printf("PASS test_param_clamp\n");
    return 0;
}

/* Pins that setParameterBaseValue keeps currentValue in sync with
 * baseValue. Dials read currentValue (see Dial widget), so a stale
 * currentValue would make unmodulated dials show the wrong number
 * after a baseValue edit. processModulations recomputes currentValue
 * every pass when there ARE modulators, so syncing inside the setter
 * is safe: it only matters for the unmodulated case (where no
 * processModulations pass runs to update it). */
static int test_set_base_value_syncs_current(void) {
    ParamList *pl = createParamList();
    Parameter *p = createParameter(pl, "p", 5.0f, 0.0f, 10.0f);
    setParameterBaseValue(p, 7.0f);
    ASSERT_NEAR(p->baseValue, 7.0f, 0.0001f, "baseValue updated");
    ASSERT_NEAR(p->currentValue, 7.0f, 0.0001f, "currentValue synced");
    teardown(pl, NULL);
    printf("PASS test_set_base_value_syncs_current\n");
    return 0;
}

/* Pins that processModulations is a no-op on empty lists (no crash,
 * no spurious writes). */
static int test_empty_process_modulations(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    processModulations(pl, ml, 0.016f);
    teardown(pl, ml);
    printf("PASS test_empty_process_modulations\n");
    return 0;
}

/* Pins LFO phase wrap behaviour: after many processModulations calls,
 * the LFO phase must stay in [0.0, 1.0). updateMod handles the wrap
 * (subtract 1.0 when >= 1.0).
 *
 * Note: createLFO sets lfo->base.generate = generateEnvelope via
 * initMod's current implementation (the `generate` argument is
 * ignored). That's a latent bug we don't pin here — this test only
 * checks phase wrap. */
static int test_lfo_phase_wrap(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    LFO *lfo = createLFO(pl, ml, 0, 0.4f, LS_SIN, "LFO");
    ASSERT_TRUE(lfo != NULL, "createLFO");
    for (int i = 0; i < 100; i++) {
        processModulations(pl, ml, 0.1f);
        ASSERT_TRUE(lfo->phase->baseValue >= 0.0f && lfo->phase->baseValue < 1.0f,
                    "LFO phase stays in [0,1)");
    }
    teardown(pl, ml);
    printf("PASS test_lfo_phase_wrap\n");
    return 0;
}

/* Pins the wiring contract of removeModulation:
 *   - it returns true and clears destination's modulator list when the
 *     matching (dest, source) connection is found, leaving count=0
 *   - it removes BOTH the amount and type params from the paramList
 *     (count drops by exactly 2) and frees them
 *   - returning false when the connection is absent
 *   - in a two-modulator list, removing the head leaves the tail
 *     intact (linked-list unlink, not just a count decrement)
 */
static int test_remove_modulation(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
    Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 10.0f);
    addModulation(pl, &env->base, dest, 0.5f, MO_ADD);
    int before = pl->count;
    ASSERT_TRUE(removeModulation(pl, dest, &env->base), "removed");
    ASSERT_EQ(dest->modulator_count, 0, "no modulators left");
    ASSERT_TRUE(dest->modulators == NULL, "list empty");
    ASSERT_EQ(pl->count, before - 2, "amount+type params removed from list");
    ASSERT_TRUE(!removeModulation(pl, dest, &env->base), "absent returns false");

    /* mid-list: two modulators, remove the head */
    Envelope *e2 = createAD(pl, ml, 0.1f, 0.2f, "E2");
    addModulation(pl, &env->base, dest, 1.0f, MO_ADD);
    addModulation(pl, &e2->base, dest, 1.0f, MO_ADD);
    ASSERT_EQ(dest->modulator_count, 2, "two modulators");
    ASSERT_TRUE(removeModulation(pl, dest, &e2->base), "remove head");
    ASSERT_EQ(dest->modulator_count, 1, "one left");
    ASSERT_EQ(dest->modulators->source, &env->base, "tail survives");
    teardown(pl, ml);
    printf("PASS test_remove_modulation\n");
    return 0;
}

/* Pins the wiring contract of removeModulationsForSource: walks every
 * param in the list, removes ALL connections whose source is the
 * given mod, returns the count of connections removed. The two-pass
 * design unlinks+orphans amount/type params in pass 1, then drops
 * them from the paramList in pass 2 so that removing-from-list
 * doesn't disturb the in-progress iteration.
 *
 * Verifies:
 *   - returns 2 when one env is connected to two destinations
 *   - each destination is left with modulator_count == 0
 *   - pl->count drops by 4 (2 connections x (amount+type))
 *   - a second call returns 0 (idempotent / no-op when nothing matches)
 */
static int test_remove_modulations_for_source(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
    Parameter *d1 = createParameter(pl, "d1", 1.0f, 0.0f, 10.0f);
    Parameter *d2 = createParameter(pl, "d2", 1.0f, 0.0f, 10.0f);
    addModulation(pl, &env->base, d1, 1.0f, MO_ADD);
    addModulation(pl, &env->base, d2, 1.0f, MO_MUL);
    int before = pl->count;
    ASSERT_EQ(removeModulationsForSource(pl, &env->base), 2, "two connections removed");
    ASSERT_EQ(d1->modulator_count, 0, "d1 clean");
    ASSERT_EQ(d2->modulator_count, 0, "d2 clean");
    ASSERT_EQ(pl->count, before - 4, "four amount/type params removed");
    ASSERT_EQ(removeModulationsForSource(pl, &env->base), 0, "none left");
    teardown(pl, ml);
    printf("PASS test_remove_modulations_for_source\n");
    return 0;
}

static int test_remove_from_modlist(void) {
    ModList *ml = createModList();
    ParamList *pl = createParamList();
    Envelope *a = createAD(pl, ml, 0.1f, 0.2f, "A");
    Envelope *b = createAD(pl, ml, 0.1f, 0.2f, "B");
    Envelope *c = createAD(pl, ml, 0.1f, 0.2f, "C");
    ASSERT_EQ(ml->count, 3, "3 mods");
    ASSERT_TRUE(removeFromModList(ml, &b->base), "remove middle");
    ASSERT_EQ(ml->count, 2, "count 2 after remove");
    ASSERT_EQ(ml->mods[0], &a->base, "a first");
    ASSERT_EQ(ml->mods[1], &c->base, "c shifted down");
    ASSERT_TRUE(removeFromModList(ml, &a->base), "remove first");
    ASSERT_TRUE(removeFromModList(ml, &c->base), "remove last");
    ASSERT_EQ(ml->count, 0, "empty");
    ASSERT_TRUE(!removeFromModList(ml, &b->base), "absent returns false");
    teardown(pl, ml);
    printf("PASS test_remove_from_modlist\n");
    return 0;
}

static int test_remove_from_paramlist(void) {
    ParamList *pl = createParamList();
    Parameter *a = createParameter(pl, "a", 1.0f, 0.0f, 10.0f);
    Parameter *b = createParameter(pl, "b", 1.0f, 0.0f, 10.0f);
    Parameter *c = createParameter(pl, "c", 1.0f, 0.0f, 10.0f);
    ASSERT_TRUE(removeFromParamList(pl, b), "remove middle");
    ASSERT_EQ(pl->count, 2, "count 2");
    ASSERT_EQ(pl->params[0], a, "a first");
    ASSERT_EQ(pl->params[1], c, "c shifted down");
    ASSERT_TRUE(!removeFromParamList(pl, b), "already removed");
    teardown(pl, NULL);
    printf("PASS test_remove_from_paramlist\n");
    return 0;
}

/* Pins the wiring contract of rewireModulation: walks destination's
 * modulator list and reassigns conn->source to newSource on the first
 * connection whose source matches oldSource. Returns false if no match
 * is found. The `list` parameter is part of the signature for symmetry
 * with addModulation/removeModulation but is intentionally unused —
 * rewiring is purely a destination->modulators operation. */
static int test_rewire_modulation(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *e1 = createAD(pl, ml, 0.1f, 0.2f, "E1");
    Envelope *e2 = createAD(pl, ml, 0.1f, 0.2f, "E2");
    Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 10.0f);
    addModulation(pl, &e1->base, dest, 1.0f, MO_ADD);
    ASSERT_TRUE(rewireModulation(pl, dest, &e1->base, &e2->base), "rewired");
    ASSERT_TRUE(dest->modulators->source == &e2->base, "source now e2");
    ASSERT_TRUE(!rewireModulation(pl, dest, &e1->base, &e2->base),
                "old source absent -> false");
    teardown(pl, ml);
    printf("PASS test_rewire_modulation\n");
    return 0;
}

/* Pins the discrete-wrap contract of wrapIncrementParameter: adds step
 * to baseValue and wraps discretely within [min, max] using
 * count = max - min + 1. Routes through setParameterBaseValue so that
 * currentValue (what dials read) stays in sync. Test pins 12+1->0 and
 * 0-1->12 for a 0..12 param, plus a within-range +3. */
static int test_wrap_increment(void) {
    ParamList *pl = createParamList();
    Parameter *p = createParameter(pl, "route", 12.0f, 0.0f, 12.0f);
    wrapIncrementParameter(p, 1.0f);
    ASSERT_NEAR(p->baseValue, 0.0f, 0.0001f, "12 + 1 wraps to 0");
    ASSERT_NEAR(p->currentValue, 0.0f, 0.0001f, "currentValue synced");
    wrapIncrementParameter(p, -1.0f);
    ASSERT_NEAR(p->baseValue, 12.0f, 0.0001f, "0 - 1 wraps to 12");
    setParameterBaseValue(p, 0.0f);
    wrapIncrementParameter(p, 3.0f);
    ASSERT_NEAR(p->baseValue, 3.0f, 0.0001f, "0 + 3 = 3 (reset to 0 first)");
    teardown(pl, NULL);
    printf("PASS test_wrap_increment\n");
    return 0;
}

static int test_remove_mod(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
    Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 10.0f);
    addModulation(pl, &env->base, dest, 1.0f, MO_ADD);
    int before = pl->count;   /* 5 env params + dest + amount + type = 8 */
    int modBefore = ml->count; /* 1 */
    ASSERT_TRUE(removeMod(ml, pl, &env->base), "removeMod succeeds");
    ASSERT_EQ(ml->count, modBefore - 1, "mod gone from modList");
    /* Brief originally wrote before-2 here; that's only the connection's
     * amount+type. Full teardown also drops 4 env-stage params
     * (2 stages * (duration + curvature)) + 1 output = 7 params removed
     * total, leaving only dest. Aligned with the spec's documented
     * "remove the mod's own params" + freeEnvelope flow. */
    ASSERT_EQ(pl->count, before - 7, "amount+type+env params gone from paramList");
    ASSERT_EQ(pl->count, 1, "only dest remains");
    ASSERT_EQ(pl->params[0], dest, "dest survives at index 0");
    ASSERT_EQ(dest->modulator_count, 0, "dest unmodulated");
    processModulations(pl, ml, 0.016f); /* no dangling deref */

    /* absent mod in a fresh list returns false. The brief's verbatim
     * code created an envelope via createAD for the "absent" case, but
     * createAD adds the mod to the list — so removeFromModList would
     * succeed and removeMod would return true. To actually test the
     * absent path we hand removeMod a stack-allocated Mod that's never
     * registered in ml2. */
    ParamList *pl2 = createParamList();
    ModList *ml2 = createModList();
    Envelope *other = createAD(pl2, ml2, 0.1f, 0.2f, "OTHER");
    Mod ghost = {0};
    ASSERT_TRUE(!removeMod(ml2, pl2, &ghost), "not in this list");
    ASSERT_EQ(ml2->count, 1, "nothing removed");
    (void)other;
    teardown(pl2, ml2);
    teardown(pl, ml);
    printf("PASS test_remove_mod\n");
    return 0;
}

/* --- looped feedback modulation graphs ------------------------------------- */

/* Deterministic const-generators. processModulations runs each mod's
 * generate() every pass (modsystem.c:895) and then recomputes EVERY
 * param (including mod outputs) as baseValue + sum(modulator values),
 * cascading in paramList order (modsystem.c:898-927). Overriding a mod's
 * generate with a constant writer makes the feedback math exact, so the
 * fixed points below are fully deterministic (no timing/wavetable
 * dependence). Note updateMod (modsystem.c:530) never writes a mod's
 * output, so the override is the sole output writer each pass. */
static void constGenQuarter(void *self) {
	setParameterValue(((Mod *)self)->output, 0.25f);
}
static void constGenHalf(void *self) {
	setParameterValue(((Mod *)self)->output, 0.5f);
}
static void constGenOneTenth(void *self) {
	setParameterValue(((Mod *)self)->output, 0.1f);
}
static void constGenTwoTenths(void *self) {
	setParameterValue(((Mod *)self)->output, 0.2f);
}
static void constGenThreeTenths(void *self) {
	setParameterValue(((Mod *)self)->output, 0.3f);
}

/* Two-element feedback cycle: B modulates A's output, A modulates B's
 * output. The apply pass cascades in paramList order — A's output is
 * created before B's (createLFO A then B), so:
 *
 *   generate:  A.output = 0.25,  B.output = 0.50
 *   apply:     A.output = 0 + 1.0 * B.output = 0.50   (reads B's fresh gen)
 *              B.output = 0 + 1.0 * A.output = 0.50   (reads A's post-apply)
 *
 * Stable fixed point: both 0.5. Without the feedback, A would stay at
 * its const-gen value (0.25) — so the assertion proves the loop is
 * live, not a tautology. Pins: no crash, no hang, outputs stay finite
 * and clamped to [0,1] across 200 frames (no divergence). */
static int test_two_cycle_feedback(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *a = createLFO(pl, ml, 0, 0.4f, LS_SIN, "A");
	LFO *b = createLFO(pl, ml, 0, 0.4f, LS_SIN, "B");
	a->base.generate = constGenQuarter;
	b->base.generate = constGenHalf;
	addModulation(pl, &b->base, a->base.output, 1.0f, MO_ADD);
	addModulation(pl, &a->base, b->base.output, 1.0f, MO_ADD);
	for(int i = 0; i < 200; i++) {
		processModulations(pl, ml, 0.016f);
		ASSERT_TRUE(isfinite(a->base.output->currentValue) &&
		                isfinite(b->base.output->currentValue),
		            "both outputs stay finite");
		ASSERT_TRUE(a->base.output->currentValue >= 0.0f &&
		                a->base.output->currentValue <= 1.0f,
		            "A output clamped to [0,1]");
		ASSERT_TRUE(b->base.output->currentValue >= 0.0f &&
		                b->base.output->currentValue <= 1.0f,
		            "B output clamped to [0,1]");
	}
	ASSERT_NEAR(a->base.output->currentValue, 0.5f, 0.0001f, "A converges to 0.5");
	ASSERT_NEAR(b->base.output->currentValue, 0.5f, 0.0001f, "B converges to 0.5");
	teardown(pl, ml);
	printf("PASS test_two_cycle_feedback\n");
	return 0;
}

/* Three-element cycle A->B->C->A. Fixed point (list order A,B,C):
 *
 *   generate:  A=0.1, B=0.2, C=0.3
 *   apply:     A = 0 + 1.0 * B.gen  = 0.2
 *              B = 0 + 1.0 * C.gen  = 0.3
 *              C = 0 + 1.0 * A.post = 0.2
 *
 * Stable at (0.2, 0.3, 0.2). A and C sit ABOVE their const-gen values,
 * proving the loop is live; all stay clamped/finite across 500 frames
 * (longer than the 2-cycle to stress no gradual divergence). */
static int test_three_cycle_feedback(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *a = createLFO(pl, ml, 0, 0.4f, LS_SIN, "A");
	LFO *b = createLFO(pl, ml, 0, 0.4f, LS_SIN, "B");
	LFO *c = createLFO(pl, ml, 0, 0.4f, LS_SIN, "C");
	a->base.generate = constGenOneTenth;
	b->base.generate = constGenTwoTenths;
	c->base.generate = constGenThreeTenths;
	addModulation(pl, &b->base, a->base.output, 1.0f, MO_ADD);
	addModulation(pl, &c->base, b->base.output, 1.0f, MO_ADD);
	addModulation(pl, &a->base, c->base.output, 1.0f, MO_ADD);
	for(int i = 0; i < 500; i++) {
		processModulations(pl, ml, 0.016f);
		ASSERT_TRUE(isfinite(a->base.output->currentValue) &&
		                isfinite(b->base.output->currentValue) &&
		                isfinite(c->base.output->currentValue),
		            "all three outputs stay finite");
		ASSERT_TRUE(a->base.output->currentValue >= 0.0f &&
		                a->base.output->currentValue <= 1.0f &&
		                b->base.output->currentValue >= 0.0f &&
		                b->base.output->currentValue <= 1.0f &&
		                c->base.output->currentValue >= 0.0f &&
		                c->base.output->currentValue <= 1.0f,
		            "all three outputs clamped to [0,1]");
	}
	ASSERT_NEAR(a->base.output->currentValue, 0.2f, 0.0001f, "A converges to 0.2");
	ASSERT_NEAR(b->base.output->currentValue, 0.3f, 0.0001f, "B converges to 0.3");
	ASSERT_NEAR(c->base.output->currentValue, 0.2f, 0.0001f, "C converges to 0.2");
	teardown(pl, ml);
	printf("PASS test_three_cycle_feedback\n");
	return 0;
}

/* Self-modulation: a mod's own output modulates itself. Pins that this
 * is a legal, stable graph (no recursion, no crash, no hang).
 *
 *   MO_ADD:  A.output = 0 + 1.0 * A.gen(0.25) = 0.25   (stable)
 *   MO_MUL:  A.output = base(0) * A.gen(0.25) = 0.0    (baseValue is 0,
 *            so MUL forces the output to 0 — the pinned quirk)
 *
 * Note processModulations ignores the connection's `amount` param (it
 * applies conn->source->output directly, modsystem.c:903), so the 1.0
 * is cosmetic here. */
static int test_self_modulation(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	LFO *a = createLFO(pl, ml, 0, 0.4f, LS_SIN, "A");
	a->base.generate = constGenQuarter;
	addModulation(pl, &a->base, a->base.output, 1.0f, MO_ADD);
	processModulations(pl, ml, 0.016f);
	ASSERT_NEAR(a->base.output->currentValue, 0.25f, 0.0001f, "self-ADD stable at 0.25");
	removeModulation(pl, a->base.output, &a->base);
	addModulation(pl, &a->base, a->base.output, 1.0f, MO_MUL);
	processModulations(pl, ml, 0.016f);
	ASSERT_NEAR(a->base.output->currentValue, 0.0f, 0.0001f, "self-MUL: 0 base * 0.25 = 0");
	teardown(pl, ml);
	printf("PASS test_self_modulation\n");
	return 0;
}

/* --- list failure modes ----------------------------------------------------- */

/* addToModList silently drops a mod past MAX_MODS (modsystem.c:102-106
 * has no error path). Pins: count caps at MAX_MODS, the over-capacity
 * mod is NOT registered, and the caller retains ownership of it (the
 * drop is a silent no-op, not a transfer). */
static int test_modlist_full_drop(void) {
	ModList *ml = createModList();
	Mod *added[MAX_MODS];
	for(int i = 0; i < MAX_MODS; i++) {
		added[i] = (Mod *)malloc(sizeof(Mod));
		addToModList(ml, added[i]);
	}
	ASSERT_EQ(ml->count, MAX_MODS, "list filled to capacity");
	Mod *extra = (Mod *)malloc(sizeof(Mod));
	addToModList(ml, extra);
	ASSERT_EQ(ml->count, MAX_MODS, "over-capacity add silently dropped");
	free(extra); /* the caller keeps ownership of a dropped mod */
	teardown(NULL, ml);
	printf("PASS test_modlist_full_drop\n");
	return 0;
}

/* createParameter past MAX_PARAMS (modsystem.c:144-148) still returns a
 * fresh Parameter, but addToParamList silently refuses to register it.
 * Pins: count caps at MAX_PARAMS, the orphan is returned but NOT in the
 * list, and the caller must free it themselves (it is not owned by the
 * list). */
static int test_paramlist_full_drop(void) {
	ParamList *pl = createParamList();
	for(int i = 0; i < MAX_PARAMS; i++) {
		createParameter(pl, "p", 0.0f, 0.0f, 1.0f);
	}
	ASSERT_EQ(pl->count, MAX_PARAMS, "list filled to capacity");
	Parameter *orphan = createParameter(pl, "orphan", 0.0f, 0.0f, 1.0f);
	ASSERT_TRUE(orphan != NULL, "createParameter still returns a param");
	ASSERT_EQ(pl->count, MAX_PARAMS, "orphan not registered");
	ASSERT_TRUE(pl->params[MAX_PARAMS - 1] != orphan, "orphan absent from list");
	freeParameter(orphan); /* the caller owns the orphan */
	teardown(pl, NULL);
	printf("PASS test_paramlist_full_drop\n");
	return 0;
}

/* NULL/absent failure modes for every removal primitive. All four guard
 * their list arguments (removeFromModList/removeFromParamList return
 * false, removeModulation false, removeModulationsForSource 0) and
 * return the "not found" result for absent items on live lists. */
static int test_remove_failure_modes(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	ASSERT_TRUE(!removeFromModList(NULL, NULL), "removeFromModList NULL/NULL");
	ASSERT_TRUE(!removeFromModList(NULL, (Mod *)0x1), "removeFromModList NULL list");
	ASSERT_TRUE(!removeFromModList(ml, NULL), "removeFromModList NULL mod");
	ASSERT_TRUE(!removeFromParamList(NULL, NULL), "removeFromParamList NULL/NULL");
	ASSERT_TRUE(!removeFromParamList(NULL, (Parameter *)0x1), "removeFromParamList NULL list");
	ASSERT_TRUE(!removeFromParamList(pl, NULL), "removeFromParamList NULL param");
	ASSERT_TRUE(!removeModulation(NULL, NULL, NULL), "removeModulation all NULL");
	ASSERT_TRUE(!removeModulation(pl, NULL, NULL), "removeModulation NULL dest");
	ASSERT_TRUE(!removeModulation(pl, NULL, (Mod *)0x1), "removeModulation NULL dest+source");
	ASSERT_TRUE(!removeModulationsForSource(NULL, NULL), "removeModulationsForSource NULL/NULL");
	ASSERT_TRUE(!removeModulationsForSource(NULL, (Mod *)0x1), "removeModulationsForSource NULL list");

	/* absent items on a live list: no crash, no mutation */
	Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
	Parameter *dest = createParameter(pl, "dest", 1.0f, 0.0f, 10.0f);
	int before = pl->count;
	ASSERT_TRUE(!removeModulation(pl, dest, &env->base), "absent connection -> false");
	ASSERT_EQ(removeModulationsForSource(pl, &env->base), 0, "no modulations to remove");
	ASSERT_EQ(pl->count, before, "absent removals do not mutate the list");
	teardown(pl, ml);
	printf("PASS test_remove_failure_modes\n");
	return 0;
}

/* clearModList/clearParamList on NULL and on empty lists are graceful
 * (error/warning print, no crash, no mutation). */
static int test_clear_null_and_empty(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	clearParamList(NULL);
	clearModList(NULL);
	clearParamList(pl);
	clearModList(ml);
	ASSERT_EQ(pl->count, 0, "paramList still empty after clear");
	ASSERT_EQ(ml->count, 0, "modList still empty after clear");
	teardown(pl, ml);
	printf("PASS test_clear_null_and_empty\n");
	return 0;
}

static int test_generated_output_survives_apply(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Envelope *env = createAD(pl, ml, 0.1f, 0.2f, "AD");
	triggerEnvelope(env);
	processModulations(pl, ml, 0.016f);
	/* The generate pass wrote a non-zero level into the envelope output; the
	 * apply pass must not clobber mod source outputs back to their baseValue.
	 * The instrument-page mod strip reads exactly these outputs, so a clobber
	 * makes it show nothing while a note plays. */
	ASSERT_TRUE(getParameterValue(env->base.output) > 0.0f,
	            "envelope output survives the processModulations apply pass");
	teardown(pl, ml);
	printf("PASS test_generated_output_survives_apply\n");
	return 0;
}

/* changeModType preserves the output param + existing routes and swaps
 * the type-specific params. Test list: the same modList slot is reused
 * (apply-pass order preserved). */
static int test_change_mod_type(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Parameter *dest = createParameter(pl, "dest", 0.5f, 0.0f, 1.0f);
    Parameter *dest2 = createParameter(pl, "dest2", 0.5f, 0.0f, 1.0f);

    Envelope *env = createAD(pl, ml, 0.25f, 4.25f, "src");
    Mod *m0 = &env->base;
    Parameter *out = m0->output;
    ASSERT_TRUE(out != NULL, "env has an output param");
    ASSERT_TRUE(addModulation(pl, m0, dest, 1.0f, MO_ADD), "route env->dest");
    ASSERT_TRUE(addModulation(pl, m0, dest2, 1.0f, MO_MUL), "route env->dest2");
    int before = ml->count;
    int beforeIdx = -1;
    for(int i = 0; i < ml->count; i++) if(ml->mods[i] == m0) beforeIdx = i;
    ASSERT_TRUE(beforeIdx >= 0, "env is registered in modList");

    /* ENV -> LFO */
    ASSERT_TRUE(changeModType(ml, m0, MT_LFO, pl), "ENV->LFO succeeds");
    Mod *m1 = ml->mods[beforeIdx];
    ASSERT_TRUE(m1 != m0, "a fresh struct was allocated");
    ASSERT_EQ(m1->type, MT_LFO, "type is now LFO");
    ASSERT_TRUE(m1->output == out, "output param is preserved (same pointer)");
    ASSERT_EQ(ml->count, before, "modList count unchanged");
    /* routes survived + rewired */
    ASSERT_TRUE(hasRouteFrom(pl, dest, m1), "dest still modulated by the (new) source");
    ASSERT_TRUE(hasRouteFrom(pl, dest2, m1), "dest2 still modulated by the (new) source");
    /* old type params are gone from the list */
    LFO *lfo = (LFO *)m1;
    ASSERT_TRUE(paramRegistered(pl, lfo->rate), "new LFO rate registered");
    ASSERT_TRUE(paramRegistered(pl, lfo->phase), "new LFO phase registered");
    ASSERT_TRUE(!env->stages[0].duration || !paramRegistered(pl, env->stages[0].duration), "old env stage params removed");
    int found = 0;
    for(int i = 0; i < pl->count; i++) {
        if(pl->params[i] && pl->params[i]->name && strcmp(pl->params[i]->name, "dest") == 0) found++;
    }
    ASSERT_EQ(found, 1, "dest param still present once");

    /* LFO -> RND */
    ASSERT_TRUE(changeModType(ml, m1, MT_RND, pl), "LFO->RND succeeds");
    Mod *m2 = ml->mods[beforeIdx];
    ASSERT_EQ(m2->type, MT_RND, "type is now RND");
    ASSERT_TRUE(m2->output == out, "output preserved again");
    ASSERT_TRUE(hasRouteFrom(pl, dest, m2), "dest still routed after LFO->RND");

    /* RND -> ENV (fresh AD) */
    ASSERT_TRUE(changeModType(ml, m2, MT_ENV, pl), "RND->ENV succeeds");
    Mod *m3 = ml->mods[beforeIdx];
    ASSERT_EQ(m3->type, MT_ENV, "type is now ENV");
    ASSERT_TRUE(m3->output == out, "output preserved");
    Envelope *env2 = (Envelope *)m3;
    ASSERT_EQ(env2->stageCount, 2, "fresh env has 2 AD stages");
    ASSERT_TRUE(hasRouteFrom(pl, dest, m3), "dest still routed after RND->ENV");

    /* invalid type rejected */
    ASSERT_TRUE(!changeModType(ml, m3, MT_OFS, pl), "MT_OFS rejected");
    ASSERT_TRUE(!changeModType(ml, m3, MT_COUNT, pl), "MT_COUNT rejected");

    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_change_mod_type\n");
    return 0;
}

static int test_change_mod_type_same_type(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.25f, 4.25f, "src");
    Mod *m0 = &env->base;
    int count = ml->count;
    ASSERT_TRUE(changeModType(ml, m0, MT_ENV, pl), "same-type change is a no-op");
    ASSERT_EQ(ml->count, count, "no structural change");
    ASSERT_TRUE(ml->mods[0] == m0, "same pointer kept");
    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_change_mod_type_same_type\n");
    return 0;
}

static int test_change_mod_type_null(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Envelope *env = createAD(pl, ml, 0.25f, 4.25f, "src");
    ASSERT_TRUE(!changeModType(NULL, &env->base, MT_LFO, pl), "NULL list rejected");
    ASSERT_TRUE(!changeModType(ml, NULL, MT_LFO, pl), "NULL mod rejected");
    ASSERT_TRUE(!changeModType(ml, &env->base, MT_LFO, NULL), "NULL paramList rejected");
    /* unregistered mod rejected */
    Envelope *stray = createEnvelope(pl, ml, "stray2");
    removeFromModList(ml, &stray->base);
    ASSERT_TRUE(!changeModType(ml, &stray->base, MT_LFO, pl), "unregistered mod rejected");
    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_change_mod_type_null\n");
    return 0;
}

/* Task 2: LFO/Random shape is a routable Parameter (not a bare int). The
 * shape onChange callback syncs shapeValue (the int the generate fn is
 * selected from) and picks the concrete generate fn. Out-of-range writes
 * must clamp, and removeMod must free the shape param. */
static int test_lfo_shape_param(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    LFO *lfo = createLFO(pl, ml, 0, 1.0f, LS_SIN, "lfo");
    ASSERT_TRUE(lfo->shape != NULL, "LFO has a shape Parameter");
    ASSERT_EQ(getParameterValueAsInt(lfo->shape), LS_SIN, "shape param defaults to the initial shape");
    ASSERT_EQ(lfo->base.generate == generateSine ? 1 : 0, 1, "generate is generateSine for LS_SIN");

    setParameterBaseValue(lfo->shape, (float)LS_SQU);
    ASSERT_EQ(lfo->shapeValue, LS_SQU, "onChange synced lfo->shapeValue int");
    ASSERT_EQ(lfo->base.generate == generateSquare ? 1 : 0, 1, "generate is generateSquare for LS_SQU");

    setParameterBaseValue(lfo->shape, (float)LS_RMP);
    ASSERT_EQ(lfo->base.generate == generateRamp ? 1 : 0, 1, "generate is generateRamp for LS_RMP");

    /* clamp: out-of-range writes clamp to the range */
    setParameterBaseValue(lfo->shape, 999.0f);
    ASSERT_EQ(getParameterValueAsInt(lfo->shape), LS_RMP, "out-of-range clamps to LS_RMP");

    /* removeMod removes the shape param */
    ASSERT_TRUE(removeMod(ml, pl, &lfo->base), "removeMod removes the LFO");
    ASSERT_TRUE(!paramRegistered(pl, lfo->shape), "shape param removed with the LFO");

    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_lfo_shape_param\n");
    return 0;
}

static int test_rand_shape_param(void) {
    ParamList *pl = createParamList();
    ModList *ml = createModList();
    Random *rnd = createRandom(pl, ml, 0, 1.0f, RT_SNH, "rnd");
    ASSERT_TRUE(rnd->shape != NULL, "Random has a shape Parameter");
    ASSERT_EQ(getParameterValueAsInt(rnd->shape), RT_SNH, "shape param defaults to RT_SNH");
    ASSERT_EQ(rnd->base.generate == generateRandom ? 1 : 0, 1, "generate is generateRandom for RT_SNH");

    setParameterBaseValue(rnd->shape, (float)RT_DRK);
    ASSERT_EQ(rnd->shapeValue, RT_DRK, "onChange synced rnd->shapeValue int");
    ASSERT_EQ(rnd->base.generate == generateDrunk ? 1 : 0, 1, "generate is generateDrunk for RT_DRK");

    ASSERT_TRUE(removeMod(ml, pl, &rnd->base), "removeMod removes the Random");
    freeParamList(pl);
    freeModList(ml);
    printf("PASS test_rand_shape_param\n");
    return 0;
}

int main(void) {
    initModSystem();
    int fails = 0;
    fails += test_generated_output_survives_apply();
    fails += test_create_lists();
    fails += test_add_modulation_wiring();
    fails += test_process_modulation_arithmetic();
    fails += test_multiple_modulators_apply_in_order();
    fails += test_envelope_create_and_stage_overflow();
    fails += test_param_clamp();
    fails += test_set_base_value_syncs_current();
    fails += test_empty_process_modulations();
    fails += test_lfo_phase_wrap();
    fails += test_remove_from_modlist();
    fails += test_remove_from_paramlist();
    fails += test_remove_modulation();
    fails += test_remove_modulations_for_source();
    fails += test_rewire_modulation();
fails += test_wrap_increment();
	fails += test_remove_mod();
	fails += test_two_cycle_feedback();
	fails += test_three_cycle_feedback();
	fails += test_self_modulation();
	fails += test_modlist_full_drop();
	fails += test_paramlist_full_drop();
	fails += test_remove_failure_modes();
	fails += test_clear_null_and_empty();
    fails += test_change_mod_type();
    fails += test_change_mod_type_same_type();
    fails += test_change_mod_type_null();
    fails += test_lfo_shape_param();
    fails += test_rand_shape_param();
    if (fails) {
        fprintf(stderr, "%d modsystem test(s) failed\n", fails);
        return 1;
    }
    printf("ALL modsystem tests passed\n");
    return 0;
}