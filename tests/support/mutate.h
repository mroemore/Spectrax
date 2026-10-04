#ifndef SPX_MUTATE_H
#define SPX_MUTATE_H

#include <stddef.h>

/*
 * Deterministic, heuristic-free bit-flip "mutation testing" for C.
 *
 * Classical mutation testing enables one mutation, runs the test suite, and
 * checks that it fails (a mutant "killed" by the suite). That needs fidelity
 * and runtime control we don't have here, so this does the cheaper inverse:
 * it mutates memory *while the suite runs*, and the suite's own assertions
 * become the oracle. A mutant that survives is a value the tests never
 * constrain.
 *
 * Two mutations, chosen because they are deterministic and reproducible:
 *   - allocation flips: malloc/calloc/realloc results have one byte XOred
 *     (rotating position) every `rate`-th allocation.
 *   - free flips: every `rate`-th freed block has one byte XOred *before*
 *     being released, to disturb teardown state.
 *
 * Everything is seeded, so a failure is reproducible from the reported seed
 * and mutation index. Enable from a test with MUTATE_SEED / MUTATE_RATE, or
 * from the environment. See meson/mutate.sh.
 */

/* Configure + reset the PRNG. rate <= 1 mutates every allocation. */
void mutate_reset(unsigned int seed, unsigned int rate);

/* 1-based index of the most recent mutation, 0 if none. */
int mutate_last_index(void);

/* The seed and rate in effect. */
unsigned int mutate_seed(void);
unsigned int mutate_rate(void);

#endif /* SPX_MUTATE_H */
