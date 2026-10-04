#include "mutate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Provided by the linker's --wrap mechanism (see tests/support/faultinject.c
 * for the matching __wrap_* shims; this file only implements the mutation
 * bookkeeping they call). */
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void  __real_free(void *);

static uint32_t g_state = 0x9e3779b9u; /* xorshift32 state */
static unsigned int g_seed = 0x9e3779b9u;
static unsigned int g_rate = 1;
static unsigned int g_alloc_seen = 0;
static unsigned int g_mutations = 0;
static int g_last_index = 0;

/* xorshift32: fast, deterministic, adequate for spreading the flip site. */
static uint32_t next_rand(void) {
	uint32_t x = g_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g_state = x;
	return x;
}

static void configure_from_env(void) {
	static int done = 0;
	if(done) {
		return;
	}
	done = 1;
	const char *s = getenv("MUTATE_SEED");
	const char *r = getenv("MUTATE_RATE");
	if(s != NULL) {
		g_seed = (unsigned int)strtoul(s, NULL, 0);
	}
	if(r != NULL) {
		unsigned int rate = (unsigned int)strtoul(r, NULL, 0);
		g_rate = rate > 0 ? rate : 1;
	}
	g_state = g_seed ? g_seed : 0x9e3779b9u;
}

void mutate_reset(unsigned int seed, unsigned int rate) {
	g_seed = seed ? seed : 0x9e3779b9u;
	g_rate = rate > 0 ? rate : 1;
	g_state = g_seed;
	g_alloc_seen = 0;
	g_mutations = 0;
	g_last_index = 0;
}

unsigned int mutate_seed(void) {
	configure_from_env();
	return g_seed;
}

unsigned int mutate_rate(void) {
	configure_from_env();
	return g_rate;
}

int mutate_last_index(void) {
	return g_last_index;
}

/* One byte of `base`+`len` is XORed with a random non-zero bit mask. Returns
 * the 1-based mutation index, or 0 if this allocation was not mutated. */
int mutate_alloc(void *base, size_t len) {
	configure_from_env();
	if(base == NULL || len == 0) {
		return 0;
	}
	g_alloc_seen++;
	if(g_rate > 1 && (g_alloc_seen % g_rate) != 0) {
		return 0;
	}
	uint32_t r = next_rand();
	size_t off = r % len;
	uint8_t bit = (uint8_t)(1u << ((r >> 16) & 7));
	((uint8_t *)base)[off] ^= bit;
	g_mutations++;
	g_last_index = (int)g_mutations;
	return g_last_index;
}

/* Same, applied just before a block is released. */
int mutate_free(void *base, size_t len) {
	configure_from_env();
	if(base == NULL || len == 0) {
		return 0;
	}
	if(g_rate > 1 && ((g_alloc_seen++) % g_rate) != 0) {
		return 0;
	}
	uint32_t r = next_rand();
	size_t off = r % len;
	uint8_t bit = (uint8_t)(1u << ((r >> 16) & 7));
	((uint8_t *)base)[off] ^= bit;
	g_mutations++;
	g_last_index = (int)g_mutations;
	return g_last_index;
}
