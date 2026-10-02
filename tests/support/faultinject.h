#ifndef SPX_FAULTINJECT_H
#define SPX_FAULTINJECT_H

#include <stddef.h>

/*
 * Allocation-failure injection for tests.
 *
 * Build a test executable with
 *   -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
 * and link tests/support/faultinject.c into it. Calls to the allocator from
 * the linked objects (i.e. the code under test) then route through the shims
 * below. libc-internal allocations are unaffected.
 */

/* Zero the counters and disable injection. Does NOT touch fi_live(). */
void fi_reset(void);

/* Fail every allocation whose 0-based index is >= n; n < 0 disables. */
void fi_fail_after(long n);

long fi_count(void);    /* allocation attempts since the last fi_reset() */
long fi_failures(void); /* injected failures since the last fi_reset() */
long fi_live(void);     /* outstanding allocations (allocs - frees) */

#endif /* SPX_FAULTINJECT_H */
