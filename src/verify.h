#ifndef SPECTRAX_VERIFY_H
#define SPECTRAX_VERIFY_H

/*
 * Contract and coverage macros, after the SQLite pattern (sqliteInt.h /
 * <https://sqlite.org/testing.html>).
 *
 * ALWAYS(X) / NEVER(X) mark a condition the author believes is invariantly
 * true / false. In a debug build they assert, so a supposedly impossible path
 * that is actually taken aborts loudly during testing instead of silently
 * corrupting state. In a release build they compile away to the bare
 * expression, so they cost nothing on the hot path.
 *
 *   if (NEVER(head == NULL)) return;   // "this must not happen"
 *   if (ALWAYS(idx >= 0)) use(idx);
 *
 * testcase(X) marks a branch that is reachable but not otherwise exercised by
 * the suite, so the coverage tool counts it as intentional. It is empty
 * unless SPX_COVERAGE_TEST is defined; X must be side-effect free, because it
 * is not evaluated in non-coverage builds.
 */

#include <assert.h>

/* Debug unless NDEBUG says otherwise (meson sets NDEBUG for release). */
#ifndef SPX_DEBUG
#  ifdef NDEBUG
#    define SPX_DEBUG 0
#  else
#    define SPX_DEBUG 1
#  endif
#endif

/* Contract assertions run in debug builds, but NOT in coverage builds: an
 * asserting ternary compiles to a decision (the assert edge) that no test can
 * ever take, which would put an uncoverable branch in every ALWAYS/NEVER site.
 * Coverage builds therefore compile the macros away and measure only real
 * branches; run the suite under both configs to get assertions *and* coverage. */
#if SPX_DEBUG && !defined(SPX_COVERAGE_TEST)
#  define ALWAYS(X) ((X) ? 1 : (assert(!"ALWAYS() failed"), 0))
#  define NEVER(X)  ((X) ? (assert(!"NEVER() failed"), 1) : 0)
#else
#  define ALWAYS(X) (X)
#  define NEVER(X)  (X)
#endif

#if defined(SPX_COVERAGE_TEST)
void spxCoverage(int line);
#  define testcase(X) if (X) spxCoverage(__LINE__)
#else
#  define testcase(X) ((void)0)
#endif

#endif /* SPECTRAX_VERIFY_H */
