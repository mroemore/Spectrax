/* test_golden_render.c -- the golden render: the same scene, the same bits.
 *
 * Criterion C says a voice's output must be a pure function of its
 * settings. The golden render is how that claim is checked across
 * machines: a canonical scene is rendered headlessly, the samples are
 * hashed, and the hash is compared against a recorded value. The test
 * runs on the host, and the qemu smoke test cross-compiles it and runs it
 * under qemu-aarch64 and qemu-arm, so an x86-64 build and an ARM build
 * must produce the same hash.
 *
 * A difference is not a tolerance question: if the two machines disagree
 * in the last bit, the engines are not the same engine, and the whole
 * cross-architecture contract is void. So the comparison is exact.
 *
 * The scene is deliberately plain: one FM instrument, one voice, one note,
 * no port audio and no wall clock. Everything it touches is a fixed
 * sequence of IEEE-754 operations, so the hash is a function of the code
 * and nothing else.
 *
 * The test also renders twice and requires the two hashes to be equal,
 * which catches a render that depends on uninitialised memory: meson runs
 * the suite with a random MALLOC_PERTURB_, so such a render would differ
 * between the two passes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "voice.h"
#include "modsystem.h"
#include "notes.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)

/* The scene: an FM instrument with the default patch, one voice, one note,
 * this many frames. */
#define GOLDEN_FRAMES 8192
#define GOLDEN_VOICES 1

/* FNV-1a over the sample bytes. Not a cryptographic hash -- it is a
 * regression gate, and any change to the samples must change it. */
static uint64_t hashBytes(uint64_t h, const void *data, size_t len) {
	const unsigned char *p = (const unsigned char *)data;
	for(size_t i = 0; i < len; i++) {
		h ^= (uint64_t)p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

#define FNV_OFFSET 14695981039346656037ULL

typedef struct {
	VoiceManager *vm;
	SamplePool *sp;
	WavetablePool *wtp;
	PresetBank pb;
} GoldenEnv;

static int goldenSetup(GoldenEnv *e) {
	memset(e, 0, sizeof(*e));
	e->sp = createSamplePool();
	if(!e->sp) {
		return 1;
	}
	e->wtp = createWavetablePool();
	if(!e->wtp) {
		return 1;
	}
	initPresetBank(&e->pb);
	Settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.enabledChannels = 1;
	settings.defaultVoiceCount = GOLDEN_VOICES;
	settings.defaultBPM = 120;
	e->vm = createVoiceManager(&settings, e->sp, e->wtp, &e->pb);
	if(!e->vm) {
		return 1;
	}
	return 0;
}

static void goldenTeardown(GoldenEnv *e) {
	if(e->vm) {
		freeVoiceManager(e->vm);
		e->vm = NULL;
	}
	if(e->sp) {
		freeSamplePool(e->sp);
		e->sp = NULL;
	}
	if(e->wtp) {
		freeWavetablePool(e->wtp);
		e->wtp = NULL;
	}
}

/* Render the scene and return the hash of the samples. */
static uint64_t goldenRender(void) {
	GoldenEnv e;
	if(goldenSetup(&e) != 0) {
		return 0;
	}
	/* An FM instrument: the sine table, the modulation graph and the
	 * envelope are all on the path, which is what makes it worth hashing. */
	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_FM, e.sp, &e.pb);
	if(!inst) {
		goldenTeardown(&e);
		return 0;
	}
	e.vm->instruments[0] = inst;
	inst->vm = e.vm;
	initVoicePool(e.vm, 0, GOLDEN_VOICES, inst);

	int note[NOTE_INFO_SIZE] = { A, 4 };
	Voice *v = getFreeVoice(e.vm, 0);
	if(!v) {
		goldenTeardown(&e);
		return 0;
	}
	triggerVoice(v, note);

	float *out = (float *)malloc(sizeof(float) * GOLDEN_FRAMES * 2);
	if(!out) {
		goldenTeardown(&e);
		return 0;
	}
	uint64_t h = FNV_OFFSET;
	for(int i = 0; i < GOLDEN_FRAMES; i++) {
		/* The same calls the audio callback makes, in the same order. */
		syncVoiceGraph(v);
		processModulations(v->paramList, v->modList, 1.0f / (float)SPECTRAX_ENGINE_RATE);
		OutVal o = generateVoice(e.vm, v, 440.0f / (float)SPECTRAX_ENGINE_RATE, 440.0f);
		out[i * 2] = o.L;
		out[i * 2 + 1] = o.R;
	}
	h = hashBytes(h, out, sizeof(float) * GOLDEN_FRAMES * 2);
	free(out);
	goldenTeardown(&e);
	return h;
}

static int test_the_render_is_reproducible(void) {
	uint64_t a = goldenRender();
	uint64_t b = goldenRender();
	if(a == 0 || b == 0) {
		printf("FAIL: the golden render produced nothing\n");
		return 1;
	}
	if(a != b) {
		printf("FAIL: the render is not reproducible: %016llx vs %016llx\n",
		       (unsigned long long)a, (unsigned long long)b);
		return 1;
	}
	printf("PASS test_the_render_is_reproducible\n");
	return 0;
}

static int test_the_render_matches_the_recorded_hash(void) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/tests/golden/voice.render.hash", SPECTRAX_REPO_ROOT);
	uint64_t got = goldenRender();
	/* Recording mode: SPECTRAX_GOLDEN_RECORD=1 writes the hash instead of
	 * comparing it. That is how the recorded value is refreshed after an
	 * intentional change -- and it has to be an intentional change, because
	 * the hash is the cross-architecture contract. */
	if(getenv("SPECTRAX_GOLDEN_RECORD")) {
		FILE *w = fopen(path, "w");
		if(!w) {
			printf("FAIL: cannot write %s\n", path);
			return 1;
		}
		fprintf(w, "%016llx\n", (unsigned long long)got);
		fclose(w);
		printf("RECORDED %016llx into %s\n", (unsigned long long)got, path);
		return 0;
	}
	FILE *f = fopen(path, "r");
	if(!f) {
		printf("FAIL: cannot read %s\n", path);
		return 1;
	}
	unsigned long long want = 0;
	if(fscanf(f, "%llx", &want) != 1) {
		fclose(f);
		printf("FAIL: %s does not hold a hash\n", path);
		return 1;
	}
	fclose(f);
	if(got != (uint64_t)want) {
		printf("FAIL: the render hashes %016llx, the recorded value is %016llx\n",
		       (unsigned long long)got, want);
		printf("      (if this is an intentional change, update %s)\n", path);
		return 1;
	}
	printf("PASS test_the_render_matches_the_recorded_hash\n");
	return 0;
}

int main(void) {
	initModSystem();
	int fails = 0;
	fails += test_the_render_is_reproducible();
	fails += test_the_render_matches_the_recorded_hash();
	if(fails) {
		fprintf(stderr, "%d golden-render test(s) failed\n", fails);
		return 1;
	}
	printf("ALL golden-render tests passed\n");
	return 0;
}
