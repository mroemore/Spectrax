#include "verify.h"

/*
 * Called by testcase() in coverage builds. Because the macro expands to
 * `if (X) spxCoverage(__LINE__);`, gcov records both the branch on X and this
 * call, so an intentionally-reachable edge shows as exercised rather than as a
 * suspicious never-taken branch. Compiled to nothing in non-coverage builds;
 * ALWAYS/NEVER need no runtime support.
 */
#if defined(SPX_COVERAGE_TEST)
void spxCoverage(int line) {
	(void)line;
}
#endif
