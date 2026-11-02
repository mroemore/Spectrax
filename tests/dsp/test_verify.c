/* Self-test for the contract macros in src/verify.h. Pure C: no raylib or
 * device deps. Checks both the release semantics (compile away) and the debug
 * semantics (assert on an impossible path), plus the side-effect contract of
 * testcase(). */

#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "verify.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static int test_always_true(void) {
	/* ALWAYS returns the expression's truth, not a normalised 1: debug builds
	 * yield 1, release/coverage builds yield X unchanged. */
	ASSERT_TRUE(ALWAYS(1));
	ASSERT_TRUE(ALWAYS(2));
	ASSERT_TRUE(ALWAYS(1 == 1));
	printf("PASS test_always_true\n");
	return 0;
}

static int test_never_false(void) {
	ASSERT_TRUE(!NEVER(0));
	ASSERT_TRUE(!NEVER(1 == 2));
	printf("PASS test_never_false\n");
	return 0;
}

/* Debug: ALWAYS(false) / NEVER(true) must abort. Release: they must fall
 * through to the bare expression and be harmless. */
static int test_impossible_path(void) {
#if SPX_DEBUG && !defined(SPX_COVERAGE_TEST)
	pid_t pid = fork();
	if(pid < 0) {
		perror("fork");
		return 1;
	}
	if(pid == 0) {
		freopen("/dev/null", "w", stderr); /* hide the assert message */
		(void)ALWAYS(0); /* must abort here */
		_exit(0);        /* reached only if the assert failed to fire */
	}
	int status = 0;
	if(waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return 1;
	}
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_TRUE(WTERMSIG(status) == SIGABRT);
#else
	ASSERT_TRUE(ALWAYS(0) == 0);
	ASSERT_TRUE(NEVER(1) == 1);
#endif
	printf("PASS test_impossible_path\n");
	return 0;
}

/* testcase(X) must be side-effect free. The argument runs only when
 * SPX_COVERAGE_TEST is defined. */
static int test_testcase_side_effect_contract(void) {
	int calls = 0;
	int cond = 0;
	testcase(cond);
	testcase(++calls == 1);
#if defined(SPX_COVERAGE_TEST)
	ASSERT_TRUE(calls == 1);
#else
	ASSERT_TRUE(calls == 0);
#endif
	printf("PASS test_testcase_side_effect_contract\n");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_always_true();
	failed |= test_never_false();
	failed |= test_impossible_path();
	failed |= test_testcase_side_effect_contract();

	printf("\n%s (verify macros)\n", failed ? "FAILED" : "PASSED");
	return failed;
}
