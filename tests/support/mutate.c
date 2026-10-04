#include "mutate.h"

#include <stdint.h>
#include <stdlib.h>

static uint32_t g_state = 0x9e3779b9u;
static unsigned int g_seed = 0x9e3779b9u;
static unsigned int g_rate = 1;
static unsigned int g_steps = 0;
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
	g_steps = 0;
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

/* Decides whether this mutation step fires, and where. Returns a byte offset
 * into [0,len) plus the bit to flip, or 0 if skipped. */
static int mutate_step(size_t len, size_t *off, uint8_t *bit) {
	configure_from_env();
	if(len == 0) {
		return 0;
	}
	g_steps++;
	if(g_rate > 1 && (g_steps % g_rate) != 0) {
		return 0;
	}
	uint32_t r = next_rand();
	*off = r % len;
	*bit = (uint8_t)(1u << ((r >> 16) & 7));
	g_last_index = (int)(next_rand() & 0x7fffffff);
	return 1;
}

int mutate_bytes(unsigned char *buf, size_t len) {
	size_t off;
	uint8_t bit;
	if(buf == NULL || !mutate_step(len, &off, &bit)) {
		return 0;
	}
	buf[off] ^= bit;
	return g_last_index;
}

size_t mutate_pick_offset(size_t len) {
	size_t off;
	uint8_t bit;
	(void)bit;
	if(!mutate_step(len, &off, &bit)) {
		return (size_t)-1;
	}
	return off;
}
