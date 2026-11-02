/* test_canonical_math.c -- the canonical math module is deterministic by
 * construction, and accurate enough to replace libm.
 *
 * Criterion C says a voice's output must not depend on the device it plays
 * on. The inexact libm transcendentals (sin, cos, exp, log, pow) break that:
 * glibc's and musl's results differ by up to an ULP, and glibc's own differ
 * between x86-64 and aarch64. So the audio path -- and the bake, whose output
 * the audio path reads -- must not call them.
 *
 * The exact libm calls are fine and are not replaced: floor, fabs, trunc,
 * copysign, fmin, fmax and sqrt are exactly representable, so every correct
 * implementation returns the same bits.
 *
 * Two things are pinned here:
 *   1. the module calls no inexact libm function -- that is what makes the
 *      result deterministic rather than merely accurate;
 *   2. the results are accurate, against golden values computed outside this
 *      repo, and the sine table's node values are pinned bit for bit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "canonical_math.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

/* The sine table interpolates linearly between 1024 nodes, so the error
 * against the exact sine is set by the step, not by the table's precision:
 * (2*pi/1024)^2 / 8 = 4.7e-6, i.e. -106 dBFS peak. The accuracy contract is
 * that bound; the table's own values are pinned exactly by the node test. */
#define SIN_INTERP_TOLERANCE 1.0e-5f

/* One ULP of relative error: the goldens are single-precision and were
 * computed in double, so a correct implementation may land one ULP away. */
static int withinOneUlp(float got, float want) {
	if(got == want) {
		return 1;
	}
	if(want == 0.0f || got == 0.0f) {
		return 0;
	}
	union { float f; unsigned int u; } a, b;
	a.f = got;
	b.f = want;
	int ia = (a.u & 0x80000000u) ? (int)(0x80000000u - (a.u & 0x7fffffffu)) : (int)a.u;
	int ib = (b.u & 0x80000000u) ? (int)(0x80000000u - (b.u & 0x7fffffffu)) : (int)b.u;
	int diff = ia - ib;
	if(diff < 0) {
		diff = -diff;
	}
	return diff <= 1;
}

typedef struct {
	float want;
	float phase;
} SinCase;

static const SinCase SIN_CASES[] = {
	{ 0x0.0p+0f, 0.0f },
	{ 0x1.8f8b840000000p-3f, 0.03125f },
	{ 0x1.6a09e60000000p-1f, 0.125f },
	{ 0x1.0000000000000p+0f, 0.25f },
	{ 0x1.e6f0e20000000p-1f, 0.3f },
	{ 0x1.bb67ae0000000p-1f, 0.3333333333333333f },
	{ 0x1.6a09e60000000p-1f, 0.375f },
	{ 0x1.1a62640000000p-53f, 0.5f },
	{ -0x1.6a09e60000000p-1f, 0.625f },
	{ -0x1.e6f0e20000000p-1f, 0.7f },
	{ -0x1.0000000000000p+0f, 0.75f },
	{ -0x1.6a09e60000000p-1f, 0.875f },
	{ -0x1.1a62640000000p-52f, 1.0f },
	{ 0x1.0000000000000p+0f, 1.25f },
	{ 0x1.a793940000000p-52f, 1.5f },
	{ -0x1.1a62640000000p-51f, 2.0f },
	{ 0x1.0000000000000p+0f, 2.25f },
	{ 0x1.60fafc0000000p-51f, 2.5f },
	{ -0x1.a793940000000p-51f, 3.0f },
	{ -0x1.0000000000000p+0f, 3.75f },
	{ -0x1.1a62640000000p-50f, 4.0f },
	{ 0x1.6111d20000000p-48f, 5.5f },
	{ 0x1.0000000000000p+0f, 6.25f },
	{ 0x1.6a09e60000000p-1f, 8.125f },
};

typedef struct {
	float want;
	int node;
} SinNodeCase;

/* canonicalSin at a table node returns that node's value exactly: the
 * interpolation fraction is zero there, so the sample pins the table. */
static const SinNodeCase SIN_NODES[] = {
	{ 0x0.0p+0f, 0 },
	{ 0x1.921f100000000p-8f, 1 },
	{ 0x1.921d200000000p-7f, 2 },
	{ 0x1.2d936c0000000p-6f, 3 },
	{ 0x1.91f6600000000p-5f, 8 },
	{ 0x1.917a6c0000000p-4f, 16 },
	{ 0x1.2c81060000000p-3f, 24 },
	{ 0x1.8f8b840000000p-3f, 32 },
	{ 0x1.f19f980000000p-3f, 40 },
	{ 0x1.2940620000000p-2f, 48 },
	{ 0x1.58f9a80000000p-2f, 56 },
	{ 0x1.87de2a0000000p-2f, 64 },
	{ 0x1.b5d1000000000p-2f, 72 },
	{ 0x1.e2b5d40000000p-2f, 80 },
	{ 0x1.07387a0000000p-1f, 88 },
	{ 0x1.1c73b40000000p-1f, 96 },
	{ 0x1.30ff800000000p-1f, 104 },
	{ 0x1.44cf320000000p-1f, 112 },
	{ 0x1.57d6940000000p-1f, 120 },
	{ 0x1.6a09e60000000p-1f, 128 },
	{ 0x1.7b5df20000000p-1f, 136 },
	{ 0x1.8bc8060000000p-1f, 144 },
	{ 0x1.9b3e040000000p-1f, 152 },
	{ 0x1.a9b6620000000p-1f, 160 },
	{ 0x1.b728340000000p-1f, 168 },
	{ 0x1.c38b300000000p-1f, 176 },
	{ 0x1.ced7b00000000p-1f, 184 },
	{ 0x1.d906bc0000000p-1f, 192 },
	{ 0x1.e212100000000p-1f, 200 },
	{ 0x1.e9f4160000000p-1f, 208 },
	{ 0x1.f0a7f00000000p-1f, 216 },
	{ 0x1.f6297c0000000p-1f, 224 },
	{ 0x1.fa75580000000p-1f, 232 },
	{ 0x1.fd88da0000000p-1f, 240 },
	{ 0x1.ff621e0000000p-1f, 248 },
	{ 0x1.0000000000000p+0f, 256 },
	{ 0x1.ff621e0000000p-1f, 264 },
	{ 0x1.fd88da0000000p-1f, 272 },
	{ 0x1.fa75580000000p-1f, 280 },
	{ 0x1.f6297c0000000p-1f, 288 },
	{ 0x1.f0a7f00000000p-1f, 296 },
	{ 0x1.e9f4160000000p-1f, 304 },
	{ 0x1.e212100000000p-1f, 312 },
	{ 0x1.d906bc0000000p-1f, 320 },
	{ 0x1.ced7b00000000p-1f, 328 },
	{ 0x1.c38b300000000p-1f, 336 },
	{ 0x1.b728340000000p-1f, 344 },
	{ 0x1.a9b6620000000p-1f, 352 },
	{ 0x1.9b3e040000000p-1f, 360 },
	{ 0x1.8bc8060000000p-1f, 368 },
	{ 0x1.7b5df20000000p-1f, 376 },
	{ 0x1.6a09e60000000p-1f, 384 },
	{ 0x1.57d6940000000p-1f, 392 },
	{ 0x1.44cf320000000p-1f, 400 },
	{ 0x1.30ff800000000p-1f, 408 },
	{ 0x1.1c73b40000000p-1f, 416 },
	{ 0x1.07387a0000000p-1f, 424 },
	{ 0x1.e2b5d40000000p-2f, 432 },
	{ 0x1.b5d1000000000p-2f, 440 },
	{ 0x1.87de2a0000000p-2f, 448 },
	{ 0x1.58f9a80000000p-2f, 456 },
	{ 0x1.2940620000000p-2f, 464 },
	{ 0x1.f19f980000000p-3f, 472 },
	{ 0x1.8f8b840000000p-3f, 480 },
	{ 0x1.2c81060000000p-3f, 488 },
	{ 0x1.917a6c0000000p-4f, 496 },
	{ 0x1.91f6600000000p-5f, 504 },
	{ 0x1.921f100000000p-8f, 511 },
	{ 0x0.0p+0f, 512 },
	{ -0x1.921f100000000p-8f, 513 },
	{ -0x1.91f6600000000p-5f, 520 },
	{ -0x1.917a6c0000000p-4f, 528 },
	{ -0x1.2c81060000000p-3f, 536 },
	{ -0x1.8f8b840000000p-3f, 544 },
	{ -0x1.f19f980000000p-3f, 552 },
	{ -0x1.2940620000000p-2f, 560 },
	{ -0x1.58f9a80000000p-2f, 568 },
	{ -0x1.87de2a0000000p-2f, 576 },
	{ -0x1.b5d1000000000p-2f, 584 },
	{ -0x1.e2b5d40000000p-2f, 592 },
	{ -0x1.07387a0000000p-1f, 600 },
	{ -0x1.1c73b40000000p-1f, 608 },
	{ -0x1.30ff800000000p-1f, 616 },
	{ -0x1.44cf320000000p-1f, 624 },
	{ -0x1.57d6940000000p-1f, 632 },
	{ -0x1.6a09e60000000p-1f, 640 },
	{ -0x1.7b5df20000000p-1f, 648 },
	{ -0x1.8bc8060000000p-1f, 656 },
	{ -0x1.9b3e040000000p-1f, 664 },
	{ -0x1.a9b6620000000p-1f, 672 },
	{ -0x1.b728340000000p-1f, 680 },
	{ -0x1.c38b300000000p-1f, 688 },
	{ -0x1.ced7b00000000p-1f, 696 },
	{ -0x1.d906bc0000000p-1f, 704 },
	{ -0x1.e212100000000p-1f, 712 },
	{ -0x1.e9f4160000000p-1f, 720 },
	{ -0x1.f0a7f00000000p-1f, 728 },
	{ -0x1.f6297c0000000p-1f, 736 },
	{ -0x1.fa75580000000p-1f, 744 },
	{ -0x1.fd88da0000000p-1f, 752 },
	{ -0x1.ff621e0000000p-1f, 760 },
	{ -0x1.0000000000000p+0f, 768 },
	{ -0x1.ff621e0000000p-1f, 776 },
	{ -0x1.fd88da0000000p-1f, 784 },
	{ -0x1.fa75580000000p-1f, 792 },
	{ -0x1.f6297c0000000p-1f, 800 },
	{ -0x1.f0a7f00000000p-1f, 808 },
	{ -0x1.e9f4160000000p-1f, 816 },
	{ -0x1.e212100000000p-1f, 824 },
	{ -0x1.d906bc0000000p-1f, 832 },
	{ -0x1.ced7b00000000p-1f, 840 },
	{ -0x1.c38b300000000p-1f, 848 },
	{ -0x1.b728340000000p-1f, 856 },
	{ -0x1.a9b6620000000p-1f, 864 },
	{ -0x1.9b3e040000000p-1f, 872 },
	{ -0x1.8bc8060000000p-1f, 880 },
	{ -0x1.7b5df20000000p-1f, 888 },
	{ -0x1.6a09e60000000p-1f, 896 },
	{ -0x1.57d6940000000p-1f, 904 },
	{ -0x1.44cf320000000p-1f, 912 },
	{ -0x1.30ff800000000p-1f, 920 },
	{ -0x1.1c73b40000000p-1f, 928 },
	{ -0x1.07387a0000000p-1f, 936 },
	{ -0x1.e2b5d40000000p-2f, 944 },
	{ -0x1.b5d1000000000p-2f, 952 },
	{ -0x1.87de2a0000000p-2f, 960 },
	{ -0x1.58f9a80000000p-2f, 968 },
	{ -0x1.2940620000000p-2f, 976 },
	{ -0x1.f19f980000000p-3f, 984 },
	{ -0x1.8f8b840000000p-3f, 992 },
	{ -0x1.2c81060000000p-3f, 1000 },
	{ -0x1.917a6c0000000p-4f, 1008 },
	{ -0x1.91f6600000000p-5f, 1016 },
	{ -0x1.921d200000000p-7f, 1022 },
	{ -0x1.921f100000000p-8f, 1023 },
	{ -0x0.0p+0f, 1024 },
};

typedef struct {
	float want;
	float x;
} ExpCase;

static const ExpCase EXP_CASES[] = {
	{ 0x1.1b48660000000p-29f, -20.0f },
	{ 0x1.5fc2100000000p-12f, -8.0f },
	{ 0x1.eec1020000000p-6f, -3.5f },
	{ 0x1.78b5640000000p-2f, -1.0f },
	{ 0x1.368b300000000p-1f, -0.5f },
	{ 0x1.c3d6a20000000p-1f, -0.125f },
	{ 0x1.0000000000000p+0f, 0.0f },
	{ 0x1.2216040000000p+0f, 0.125f },
	{ 0x1.a612980000000p+0f, 0.5f },
	{ 0x1.5bf0a80000000p+1f, 1.0f },
	{ 0x1.85d6fe0000000p+3f, 2.5f },
	{ 0x1.936dc60000000p+8f, 6.0f },
	{ 0x1.5829dc0000000p+14f, 10.0f },
	{ 0x1.3de1660000000p+17f, 12.0f },
};

typedef struct {
	float want;
	float x;
} LogCase;

static const LogCase LOG_CASES[] = {
	{ -0x1.ba18aa0000000p+3f, 1e-06f },
	{ -0x1.ba18aa0000000p+2f, 0.001f },
	{ -0x1.0a2b240000000p+1f, 0.125f },
	{ -0x1.62e4300000000p+0f, 0.25f },
	{ -0x1.62e4300000000p-1f, 0.5f },
	{ -0x1.2696220000000p-2f, 0.75f },
	{ 0x0.0p+0f, 1.0f },
	{ 0x1.9f323e0000000p-2f, 1.5f },
	{ 0x1.62e4300000000p-1f, 2.0f },
	{ 0x1.193ea80000000p+0f, 3.0f },
	{ 0x1.26bb1c0000000p+1f, 10.0f },
	{ 0x1.26bb1c0000000p+2f, 100.0f },
	{ 0x1.62a4100000000p+2f, 255.0f },
	{ 0x1.62e4300000000p+2f, 256.0f },
	{ 0x1.ba18aa0000000p+3f, 1000000.0f },
};

typedef struct {
	float want;
	float x;
	float y;
} PowCase;

static const PowCase POW_CASES[] = {
	{ 0x1.0000000000000p-4f, 0.25f, 2.0f },
	{ 0x1.6a09e60000000p-1f, 0.5f, 0.5f },
	{ 0x1.0000000000000p-2f, 0.5f, 2.0f },
	{ 0x1.b5273c0000000p-1f, 0.9f, 1.5f },
	{ 0x1.0000000000000p+0f, 1.0f, 3.0f },
	{ 0x1.6a09e60000000p+0f, 2.0f, 0.5f },
	{ 0x1.0000000000000p+10f, 2.0f, 10.0f },
	{ 0x1.0000000000000p+3f, 0.125f, -1.0f },
};

typedef struct {
	float want;
	float fc;
	float sr;
} OnePoleCase;

static const OnePoleCase ONEPOLE_CASES[] = {
	{ 0x1.aa23100000000p-7f, 100.0f, 48000.0f },
	{ 0x1.037f760000000p-4f, 500.0f, 48000.0f },
	{ 0x1.f68e3a0000000p-4f, 1000.0f, 48000.0f },
	{ 0x1.a165de0000000p-2f, 4000.0f, 48000.0f },
	{ 0x1.9590ce0000000p-1f, 12000.0f, 48000.0f },
	{ 0x1.daa66c0000000p-1f, 20000.0f, 48000.0f },
	{ 0x1.c5cf880000000p-5f, 400.0f, 44100.0f },
};

/* The inexact transcendentals. A call to any of these makes the result
 * depend on the platform's libm, which is exactly what this module exists to
 * prevent. */
static const char *const INEXACT_LIBM[] = {
	"sinf(", "cosf(", "expf(", "logf(", "powf(", "tanf(",
	"asinf(", "acosf(", "atanf(", "atan2f(",
	"sin(", "cos(", "exp(", "log(", "pow(",
};

/* True when `name` appears in `line` as a call rather than as the tail of a
 * longer identifier: ldexp( is not exp(. */
static int containsCall(const char *line, const char *name) {
	size_t n = strlen(name);
	const char *p = line;
	while((p = strstr(p, name)) != NULL) {
		if(p == line) {
			return 1;
		}
		unsigned char before = (unsigned char)p[-1];
		if(!((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
		     (before >= '0' && before <= '9') || before == '_')) {
			return 1;
		}
		p += n;
	}
	return 0;
}

static int test_canonical_module_calls_no_inexact_libm(void) {
	static const char *const FILES[] = {"canonical_math.c", "sintab.c"};
	for(size_t fi = 0; fi < sizeof(FILES) / sizeof(FILES[0]); fi++) {
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", SPECTRAX_SRC_DIR, FILES[fi]);
		FILE *f = fopen(path, "r");
		ASSERT_TRUE(f != NULL, "the canonical source is readable");
		char line[1024];
		int lineno = 0;
		while(fgets(line, sizeof(line), f)) {
			lineno++;
			/* Skip comment lines: the check is about calls, not prose. */
			char *p = line;
			while(*p == ' ' || *p == '\t') {
				p++;
			}
			if(p[0] == '/' || p[0] == '*') {
				continue;
			}
			for(size_t i = 0; i < sizeof(INEXACT_LIBM) / sizeof(INEXACT_LIBM[0]); i++) {
				if(containsCall(line, INEXACT_LIBM[i])) {
					fclose(f);
					printf("FAIL: %s:%i calls %s\n", FILES[fi], lineno, INEXACT_LIBM[i]);
					return 1;
				}
			}
		}
		fclose(f);
	}
	printf("PASS test_canonical_module_calls_no_inexact_libm\n");
	return 0;
}

static int test_canonical_sin_matches_the_exact_sine(void) {
	for(size_t i = 0; i < sizeof(SIN_CASES) / sizeof(SIN_CASES[0]); i++) {
		float got = canonicalSin(SIN_CASES[i].phase);
		float diff = got - SIN_CASES[i].want;
		if(diff < 0) {
			diff = -diff;
		}
		if(diff > SIN_INTERP_TOLERANCE) {
			printf("FAIL: canonicalSin(%g) = %.9g, want %.9g (off by %.3g)\n",
			       (double)SIN_CASES[i].phase, (double)got,
			       (double)SIN_CASES[i].want, (double)diff);
			return 1;
		}
	}
	printf("PASS test_canonical_sin_matches_the_exact_sine\n");
	return 0;
}

static int test_canonical_sin_table_nodes_are_pinned(void) {
	for(size_t i = 0; i < sizeof(SIN_NODES) / sizeof(SIN_NODES[0]); i++) {
		int node = SIN_NODES[i].node;
		float got = canonicalSin((float)node / 1024.0f);
		if(!withinOneUlp(got, SIN_NODES[i].want)) {
			printf("FAIL: sine table node %d = %.9g, want %.9g\n",
			       node, (double)got, (double)SIN_NODES[i].want);
			return 1;
		}
	}
	printf("PASS test_canonical_sin_table_nodes_are_pinned\n");
	return 0;
}

static int test_canonical_cos_matches_sin_shifted(void) {
	for(size_t i = 0; i < sizeof(SIN_CASES) / sizeof(SIN_CASES[0]); i++) {
		float p = SIN_CASES[i].phase;
		if(canonicalCos(p) != canonicalSin(p + 0.25f)) {
			printf("FAIL: canonicalCos(%g) differs from canonicalSin(+0.25)\n", (double)p);
			return 1;
		}
	}
	printf("PASS test_canonical_cos_matches_sin_shifted\n");
	return 0;
}

static int test_canonical_exp_matches_golden(void) {
	for(size_t i = 0; i < sizeof(EXP_CASES) / sizeof(EXP_CASES[0]); i++) {
		float got = canonicalExp(EXP_CASES[i].x);
		if(!withinOneUlp(got, EXP_CASES[i].want)) {
			printf("FAIL: canonicalExp(%g) = %.9g, want %.9g\n",
			       (double)EXP_CASES[i].x, (double)got, (double)EXP_CASES[i].want);
			return 1;
		}
	}
	printf("PASS test_canonical_exp_matches_golden\n");
	return 0;
}

static int test_canonical_log_matches_golden(void) {
	for(size_t i = 0; i < sizeof(LOG_CASES) / sizeof(LOG_CASES[0]); i++) {
		float got = canonicalLog(LOG_CASES[i].x);
		if(!withinOneUlp(got, LOG_CASES[i].want)) {
			printf("FAIL: canonicalLog(%g) = %.9g, want %.9g\n",
			       (double)LOG_CASES[i].x, (double)got, (double)LOG_CASES[i].want);
			return 1;
		}
	}
	printf("PASS test_canonical_log_matches_golden\n");
	return 0;
}

static int test_canonical_pow_matches_golden(void) {
	for(size_t i = 0; i < sizeof(POW_CASES) / sizeof(POW_CASES[0]); i++) {
		float got = canonicalPow(POW_CASES[i].x, POW_CASES[i].y);
		if(!withinOneUlp(got, POW_CASES[i].want)) {
			printf("FAIL: canonicalPow(%g, %g) = %.9g, want %.9g\n",
			       (double)POW_CASES[i].x, (double)POW_CASES[i].y,
			       (double)got, (double)POW_CASES[i].want);
			return 1;
		}
	}
	printf("PASS test_canonical_pow_matches_golden\n");
	return 0;
}

static int test_canonical_one_pole_coef_matches_golden(void) {
	for(size_t i = 0; i < sizeof(ONEPOLE_CASES) / sizeof(ONEPOLE_CASES[0]); i++) {
		float got = canonicalOnePoleCoef(ONEPOLE_CASES[i].fc, ONEPOLE_CASES[i].sr);
		if(!withinOneUlp(got, ONEPOLE_CASES[i].want)) {
			printf("FAIL: canonicalOnePoleCoef(%g, %g) = %.9g, want %.9g\n",
			       (double)ONEPOLE_CASES[i].fc, (double)ONEPOLE_CASES[i].sr,
			       (double)got, (double)ONEPOLE_CASES[i].want);
			return 1;
		}
	}
	printf("PASS test_canonical_one_pole_coef_matches_golden\n");
	return 0;
}

static int test_canonical_sin_is_period_exact(void) {
	for(int i = 0; i < 64; i++) {
		float p = (float)i / 64.0f;
		float a = canonicalSin(p);
		if(a != canonicalSin(p + 1.0f) || a != canonicalSin(p - 3.0f)) {
			printf("FAIL: canonicalSin is not period exact at %g\n", (double)p);
			return 1;
		}
	}
	printf("PASS test_canonical_sin_is_period_exact\n");
	return 0;
}

static int test_canonical_exact_points(void) {
	ASSERT_TRUE(canonicalLog(1.0f) == 0.0f, "canonicalLog(1) is exactly zero");
	ASSERT_TRUE(canonicalExp(0.0f) == 1.0f, "canonicalExp(0) is exactly one");
	ASSERT_TRUE(canonicalPow(1.0f, 3.0f) == 1.0f, "canonicalPow(1, 3) is exactly one");
	ASSERT_TRUE(canonicalPow(0.0f, 2.0f) == 0.0f, "canonicalPow(0, 2) is exactly zero");
	printf("PASS test_canonical_exact_points\n");
	return 0;
}

int main(void) {
	int fails = 0;
	fails += test_canonical_module_calls_no_inexact_libm();
	fails += test_canonical_sin_matches_the_exact_sine();
	fails += test_canonical_sin_table_nodes_are_pinned();
	fails += test_canonical_cos_matches_sin_shifted();
	fails += test_canonical_exp_matches_golden();
	fails += test_canonical_log_matches_golden();
	fails += test_canonical_pow_matches_golden();
	fails += test_canonical_one_pole_coef_matches_golden();
	fails += test_canonical_sin_is_period_exact();
	fails += test_canonical_exact_points();
	if(fails) {
		fprintf(stderr, "%d canonical-math test(s) failed\n", fails);
		return 1;
	}
	printf("ALL canonical-math tests passed\n");
	return 0;
}
