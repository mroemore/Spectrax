/* test_sample_pool.c — the sample pool's ceiling, and its laziness.
 *
 * The pool used to malloc a fixed 32 MB arena the moment it was created,
 * whether the device had one sample in it or none. On a 128 MB handheld
 * that is a quarter of the RAM, held for the app's whole life, and it is
 * the largest single allocation the engine makes.
 *
 * The pool now grows as samples arrive, up to a ceiling the device's
 * budget sets. Two things have to hold:
 *
 * - the arena is not allocated until a sample needs it, and
 * - the samples already in the pool stay valid when the arena grows,
 *   because growing it moves the memory they point into.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sample.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define ASSERT_EQ(a, b, m) do { if((a) != (b)) { \
	printf("FAIL: %s (%ld, expected %ld)\n", m, (long)(a), (long)(b)); return 1; } } while(0)

/* A fresh pool holds no memory. */
static int test_a_fresh_pool_holds_no_memory(void) {
	SamplePool *sp = createSamplePool();
	if(!sp) {
		printf("FAIL: createSamplePool\n");
		return 1;
	}
	if(sp->sampleData != NULL) {
		printf("FAIL: the pool allocated its arena before anything needed it\n");
		freeSamplePool(sp);
		return 1;
	}
	if(sp->memoryUsed != 0) {
		printf("FAIL: the fresh pool reports %zu bytes used\n", sp->memoryUsed);
		freeSamplePool(sp);
		return 1;
	}
	freeSamplePool(sp);
	printf("PASS test_a_fresh_pool_holds_no_memory\n");
	return 0;
}

/* A sample's data is readable after it is loaded. */
static int test_a_loaded_sample_reads_back(void) {
	SamplePool *sp = createSamplePool();
	if(!sp) {
		printf("FAIL: createSamplePool\n");
		return 1;
	}
	const int len = 256;
	float *data = (float *)malloc(sizeof(float) * len);
	for(int i = 0; i < len; i++) {
		data[i] = (float)i;
	}
	loadSample(sp, "ramp", data, 24, 48000, len);
	free(data);
	ASSERT_EQ((long)sp->sampleCount, 1, "the sample is in the pool");
	if(sp->sampleData == NULL) {
		printf("FAIL: the pool did not allocate its arena\n");
		freeSamplePool(sp);
		return 1;
	}
	Sample *s = sp->samples[0];
	if(!s) {
		printf("FAIL: the sample is NULL\n");
		freeSamplePool(sp);
		return 1;
	}
	for(int i = 0; i < len; i++) {
		if(s->data[i] != (float)i) {
			printf("FAIL: the sample's data at %d is %g, expected %d\n",
			       i, (double)s->data[i], i);
			freeSamplePool(sp);
			return 1;
		}
	}
	freeSamplePool(sp);
	printf("PASS test_a_loaded_sample_reads_back\n");
	return 0;
}

/* The samples already in the pool survive the arena growing under them.
 * This is the trap: the arena is one block, so growing it moves every
 * sample's data, and the pointers have to be fixed up. */
static int test_the_samples_survive_the_arena_growing(void) {
	SamplePool *sp = createSamplePool();
	if(!sp) {
		printf("FAIL: createSamplePool\n");
		return 1;
	}
	const int len = 64;
	const int count = 40;
	for(int n = 0; n < count; n++) {
		float *data = (float *)malloc(sizeof(float) * len);
		for(int i = 0; i < len; i++) {
			data[i] = (float)(n * 1000 + i);
		}
		loadSample(sp, "s", data, 24, 48000, len);
		free(data);
	}
	ASSERT_EQ((long)sp->sampleCount, count, "every sample is in the pool");
	/* Every sample's data must still be its own. */
	for(int n = 0; n < count; n++) {
		Sample *s = sp->samples[n];
		if(!s) {
			printf("FAIL: sample %d is NULL\n", n);
			freeSamplePool(sp);
			return 1;
		}
		for(int i = 0; i < len; i++) {
			if(s->data[i] != (float)(n * 1000 + i)) {
				printf("FAIL: sample %d's data at %d is %g, expected %d\n",
				       n, i, (double)s->data[i], n * 1000 + i);
				freeSamplePool(sp);
				return 1;
			}
		}
	}
	freeSamplePool(sp);
	printf("PASS test_the_samples_survive_the_arena_growing\n");
	return 0;
}

/* The pool's ceiling is the device's budget, and a sample that would
 * pass it is refused rather than overflowing the pool. */
static int test_the_ceiling_is_enforced(void) {
	/* A pool with a 4096-byte ceiling. */
	SamplePool *sp = createSamplePoolWithCapacity(4096);
	if(!sp) {
		printf("FAIL: createSamplePoolWithCapacity\n");
		return 1;
	}
	const int len = 1024; /* 1024 floats is 4096 bytes: exactly the ceiling */
	float *data = (float *)malloc(sizeof(float) * len);
	for(int i = 0; i < len; i++) {
		data[i] = 1.0f;
	}
	/* This one exactly fills the ceiling. */
	loadSample(sp, "fits", data, 24, 48000, len);
	free(data);
	ASSERT_EQ((long)sp->sampleCount, 1, "the first sample fits");
	/* This one does not, and is refused. */
	float *more = (float *)malloc(sizeof(float) * len);
	for(int i = 0; i < len; i++) {
		more[i] = 2.0f;
	}
	loadSample(sp, "too big", more, 24, 48000, len);
	free(more);
	ASSERT_EQ((long)sp->sampleCount, 1, "the sample past the ceiling is refused");
	ASSERT_TRUE(sp->memoryUsed <= 4096, "the pool does not hold more than its ceiling");
	freeSamplePool(sp);
	printf("PASS test_the_ceiling_is_enforced\n");
	return 0;
}

/* The default pool's ceiling is the tree's constant, so nothing that
 * worked before stops working. */
static int test_the_default_ceiling_is_unchanged(void) {
	SamplePool *sp = createSamplePool();
	if(!sp) {
		printf("FAIL: createSamplePool\n");
		return 1;
	}
	ASSERT_EQ((long)sp->capacityBytes, (long)MAX_SAMPLE_POOL_BYTES,
	          "the default ceiling is the tree's constant");
	freeSamplePool(sp);
	printf("PASS test_the_default_ceiling_is_unchanged\n");
	return 0;
}

int main(void) {
	int fails = 0;
	fails += test_a_fresh_pool_holds_no_memory();
	fails += test_a_loaded_sample_reads_back();
	fails += test_the_samples_survive_the_arena_growing();
	fails += test_the_ceiling_is_enforced();
	fails += test_the_default_ceiling_is_unchanged();
	if(fails) {
		fprintf(stderr, "%d sample-pool test(s) failed\n", fails);
		return 1;
	}
	printf("ALL sample-pool tests passed\n");
	return 0;
}
