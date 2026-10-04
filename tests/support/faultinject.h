#ifndef SPX_FAULTINJECT_H
#define SPX_FAULTINJECT_H

#include <stddef.h>
#include <stdio.h>

/*
 * Allocation- and I/O-failure injection, plus allocation-size lookup for the
 * mutation hooks, for tests.
 *
 * Build a test executable with
 *   -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
 *   -Wl,--wrap=fopen -Wl,--wrap=fread -Wl,--wrap=fclose
 * and link tests/support/faultinject.c into it. Calls from the linked objects
 * (i.e. the code under test) then route through the shims below. libc-internal
 * calls are unaffected.
 *
 * The shims also call mutate_alloc()/mutate_free() when -DSPX_MUTATE is set,
 * so the same support file powers P7's mutation runs (meson/mutate.sh).
 */

#ifdef SPX_MUTATE
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void  __real_free(void *);
int mutate_alloc(void *base, size_t len);
int mutate_free(void *base, size_t len);
#endif

/* Zero the counters and disable injection. Does NOT touch fi_live(). */
void fi_reset(void);

/* Fail every allocation whose 0-based index is >= n; n < 0 disables. */
void fi_fail_after(long n);

long fi_count(void);    /* allocation attempts since the last fi_reset() */
long fi_failures(void); /* injected allocation failures since the last fi_reset() */
long fi_live(void);     /* outstanding allocations (allocs - frees) */

/* I/O injection: fail every fopen/fread whose 0-based index is >= n. */
void fi_fail_io_after(long n);
long fi_io_count(void);    /* I/O attempts since the last fi_reset() */
long fi_io_failures(void); /* injected I/O failures since the last fi_reset() */

#endif /* SPX_FAULTINJECT_H */
