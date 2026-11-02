#ifndef SPX_FAULTINJECT_H
#define SPX_FAULTINJECT_H

#include <stddef.h>
#include <stdio.h>

/*
 * Allocation- and I/O-failure injection for tests.
 *
 * Build a test executable with
 *   -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
 *   -Wl,--wrap=fopen -Wl,--wrap=fread -Wl,--wrap=fclose -Wl,--wrap=fopen64
 * and link tests/support/faultinject.c into it. Calls from the linked objects
 * (i.e. the code under test) then route through the shims below. libc-internal
 * calls are unaffected.
 *
 * This file is injection only. Mutation testing is input-byte mutation and
 * lives in tests/support/mutate.{h,c} (see meson/mutate.sh), deliberately
 * separate: allocation-time flips are overwritten by the fread that fills the
 * buffer, so they never reach an assertion (TESTING.md I23).
 */

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
