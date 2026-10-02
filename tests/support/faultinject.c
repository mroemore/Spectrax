#include "faultinject.h"

#include <stdlib.h>

/* Provided by the linker's --wrap mechanism. */
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void  __real_free(void *);

static long g_count;
static long g_failures;
static long g_fail_after = -1;
static long g_live;

void fi_reset(void) {
	g_count = 0;
	g_failures = 0;
	g_fail_after = -1;
}

void fi_fail_after(long n) {
	g_fail_after = n;
}

long fi_count(void) {
	return g_count;
}

long fi_failures(void) {
	return g_failures;
}

long fi_live(void) {
	return g_live;
}

static int fi_should_fail(void) {
	long idx = g_count++;
	if(g_fail_after >= 0 && idx >= g_fail_after) {
		g_failures++;
		return 1;
	}
	return 0;
}

void *__wrap_malloc(size_t size) {
	if(fi_should_fail()) {
		return NULL;
	}
	void *p = __real_malloc(size);
	if(p) {
		g_live++;
	}
	return p;
}

void *__wrap_calloc(size_t n, size_t size) {
	if(fi_should_fail()) {
		return NULL;
	}
	void *p = __real_calloc(n, size);
	if(p) {
		g_live++;
	}
	return p;
}

void *__wrap_realloc(void *ptr, size_t size) {
	if(fi_should_fail()) {
		return NULL;
	}
	void *p = __real_realloc(ptr, size);
	/* Success with a null ptr is a fresh allocation; success with a non-null
	 * ptr frees the old block, so the live count is unchanged. */
	if(p && !ptr) {
		g_live++;
	}
	return p;
}

void __wrap_free(void *ptr) {
	if(ptr) {
		g_live--;
	}
	__real_free(ptr);
}
