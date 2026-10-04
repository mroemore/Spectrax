#include "faultinject.h"

#include <stdio.h>
#include <stdlib.h>

/* Provided by the linker's --wrap mechanism. */
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void  __real_free(void *);
FILE *__real_fopen(const char *, const char *);
size_t __real_fread(void *, size_t, size_t, FILE *);
int __real_fclose(FILE *);

static long g_count;
static long g_failures;
static long g_fail_after = -1;
static long g_live;

static long g_io_count;
static long g_io_failures;
static long g_fail_io_after = -1;

void fi_reset(void) {
	g_count = 0;
	g_failures = 0;
	g_fail_after = -1;
	g_io_count = 0;
	g_io_failures = 0;
	g_fail_io_after = -1;
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

void fi_fail_io_after(long n) {
	g_fail_io_after = n;
}

long fi_io_count(void) {
	return g_io_count;
}

long fi_io_failures(void) {
	return g_io_failures;
}

static int fi_io_should_fail(void) {
	long idx = g_io_count++;
	if(g_fail_io_after >= 0 && idx >= g_fail_io_after) {
		g_io_failures++;
		return 1;
	}
	return 0;
}

FILE *__wrap_fopen(const char *path, const char *mode) {
	if(fi_io_should_fail()) {
		return NULL;
	}
	return __real_fopen(path, mode);
}

/* With _FILE_OFFSET_BITS=64 (which meson sets) glibc's headers redirect fopen
 * to fopen64, so wrapping only the unsuffixed name silently never fires; wrap
 * fopen64 as well. fread/fclose are not remapped (only fopen is), so their
 * unsuffixed wrappers above already catch the loader's calls. */
FILE *__wrap_fopen64(const char *path, const char *mode) {
	if(fi_io_should_fail()) {
		return NULL;
	}
	return __real_fopen(path, mode);
}

/* From here on, refer to the unsuffixed symbol so the macro cannot rewrite the
 * definition of __real_fread into __real_fread64 (which does not exist). */
#undef fread
#undef fclose

size_t __wrap_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	if(fi_io_should_fail()) {
		return 0;
	}
	return __real_fread(ptr, size, nmemb, stream);
}

int __wrap_fclose(FILE *stream) {
	/* Closing is not injected: it must still release the descriptor. */
	return __real_fclose(stream);
}
