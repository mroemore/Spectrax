#ifndef SPX_MUTATE_H
#define SPX_MUTATE_H

#include <stddef.h>

/*
 * Deterministic, dependency-free mutation testing for C.
 *
 * Classical mutation testing enables one mutation, runs the test suite, and
 * checks that it fails (a mutant "killed" by the suite). That needs an
 * operator-level toolchain we don't have here, so this does the cheaper
 * inverse: it mutates the *input* the code under test consumes, and the
 * suite's own assertions are the oracle. A mutant that survives is an input
 * malformation the tests do not react to.
 *
 * Why input bytes and not allocator bytes: an allocation-time flip is
 * overwritten by the very fread/copy that fills the buffer, so it never
 * reaches an asserted value. The loader's asserted values come from the file
 * contents, so that is what must be perturbed. (See TESTING.md I23/I25.)
 *
 * Everything is seeded, so a kill is reproducible from the reported seed and
 * mutation index. Configure from the environment (MUTATE_SEED / MUTATE_RATE),
 * or programmatically. See meson/mutate.sh.
 */

/* Configure + reset the PRNG. rate <= 1 mutates every input; rate N mutates
 * roughly every N-th candidate byte. */
void mutate_reset(unsigned int seed, unsigned int rate);

/* 1-based index of the most recent mutation, 0 if none. Useful for logging a
 * reproducible "seed=N mutation=M" pair. */
int mutate_last_index(void);

/* The seed and rate in effect (also read from MUTATE_SEED / MUTATE_RATE). */
unsigned int mutate_seed(void);
unsigned int mutate_rate(void);

/* Flip one bit somewhere in buf[0..len). Returns the 1-based mutation index,
 * or 0 if the buffer was too small / this call was skipped by the rate. The
 * caller applies this to its input exactly once per mutation attempt. */
int mutate_bytes(unsigned char *buf, size_t len);

/* Apply the next mutation step to an abstract "input of length len", returning
 * the byte offset to flip (or (size_t)-1 when no flip this step). Kept for
 * callers that hold the input as several buffers. */
size_t mutate_pick_offset(size_t len);

#endif /* SPX_MUTATE_H */
