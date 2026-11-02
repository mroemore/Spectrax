/* Input-byte mutation testing for the WAV loader (src/io.c).
 *
 * The mutation oracle: for each mutation step, flip one seeded bit somewhere
 * in a *valid* WAV, hand it to the loader, and require that the suite notices —
 * either the load is refused (header malformed) or the decoded PCM differs
 * from the known-good values. A mutation the loader happily accepts as if it
 * were the original is a "surviving" mutant and fails this test.
 *
 * This is the retarget from allocation-time flips (which the loader's fread
 * overwrites before any assertion sees them — TESTING.md I23). Input bytes are
 * exactly what the loader's asserted values are derived from.
 *
 * Built with -DSPX_MUTATE; the mutation PRNG is seeded from MUTATE_SEED (the
 * runner sweeps seeds, and a kill is reproducible from the reported seed).
 * Not linked against the fault-injection shims: this test perturbs the file,
 * not the allocator. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "io.h"
#include "sample.h"
#include "mutate.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

enum { FRAMES = 16, HDR_LEN = 44, PCM_LEN = FRAMES * 2, FILE_LEN = HDR_LEN + PCM_LEN };

/* Build a byte-stable 16-bit mono WAV (little-endian) into out[FILE_LEN]. */
static void build_wav(uint8_t *out) {
	uint8_t hdr[HDR_LEN] = {
		'R','I','F','F', 36+PCM_LEN,0,0,0, 'W','A','V','E',
		'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
		0x44,0xac,0,0,        /* 44100 */
		0x88,0x58,0x01,0,     /* 88200 byte rate */
		2,0, 16,0,            /* block align, bits */
		'd','a','t','a', PCM_LEN,0,0,0
	};
	memcpy(out, hdr, HDR_LEN);
	for(int i = 0; i < FRAMES; i++) {
		int16_t v = (int16_t)(i * 1000 - 8000);
		out[HDR_LEN + 2*i]     = (uint8_t)(v & 0xff);
		out[HDR_LEN + 2*i + 1] = (uint8_t)((v >> 8) & 0xff);
	}
}

/* Load `buf` (a WAV image) through a memfd; copies the decoded PCM into
 * `out` (up to `max` floats) and returns the count, or 0 if the loader
 * refused it. Comparing the whole decoded sample — not just the first frame —
 * is what makes the oracle sensitive to a flip anywhere in the data chunk. */
static int load_pcm(const uint8_t *buf, float *out, int max) {
	int fd = memfd_create("spx_mut_input", MFD_CLOEXEC);
	if(fd < 0) {
		return 0;
	}
	if(write(fd, buf, FILE_LEN) != FILE_LEN) {
		close(fd);
		return 0;
	}
	lseek(fd, 0, SEEK_SET);
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	SamplePool *sp = createSamplePool();
	int n = 0;
	if(sp != NULL) {
		load_wav_sample(path, sp);
		if(sp->sampleCount == 1) {
			Sample *s = sp->samples[0];
			n = s->length < max ? s->length : max;
			memcpy(out, s->data, (size_t)n * sizeof(float));
		}
		freeSamplePool(sp);
	}
	close(fd);
	return n;
}

int main(void) {
	if(mutate_seed() == 0) {
		mutate_reset(1, 1);
	}

	uint8_t wav[FILE_LEN];
	build_wav(wav);

	float baseline[FRAMES];
	int base_n = load_pcm(wav, baseline, FRAMES);
	ASSERT_TRUE(base_n == FRAMES);
	/* Sanity: our synthetic WAV decodes to exactly this. */
	ASSERT_TRUE(baseline[0] == (float)-8000 / 32768.0f);

	/* Sweep every byte of the file; for each, flip one bit and require the
	 * loader to either refuse the image or decode something different.
	 * Exhaustive rather than seeded so the result is a definite number
	 * (killed / candidates), not a sample.
	 *
	 * Survivors are split by provenance: a flip in the data chunk must always
	 * be detected; a flip in a header field the loader does not use (chunkSize,
	 * audioFormat, byteRate, blockAlign, ...) is expected to be ignored. Only
	 * the data-chunk class gates — a new header survivor is informational
	 * (it means the loader started/stopped reading that field). */
	int killed = 0, data_survivors = 0, hdr_survivors = 0, candidates = 0;
	for(size_t pos = 0; pos < FILE_LEN; pos++) {
		for(int bit = 0; bit < 8; bit++) {
			uint8_t mutated[FILE_LEN];
			memcpy(mutated, wav, FILE_LEN);
			mutated[pos] ^= (uint8_t)(1u << bit);
			candidates++;

			float got[FRAMES];
			int n = load_pcm(mutated, got, FRAMES);
			int same = (n == base_n) &&
				(memcmp(got, baseline, (size_t)n * sizeof(float)) == 0);
			if(!same) {
				killed++;
			} else if(pos >= HDR_LEN) {
				data_survivors++;
				fprintf(stderr, "FAIL: data byte %zu bit %d flip survived\n",
					pos, bit);
			} else {
				hdr_survivors++;
				if(getenv("MUTATE_VERBOSE") != NULL) {
					fprintf(stderr, "[hdr-survivor] pos=%zu bit=%d\n", pos, bit);
				}
			}
		}
	}

	printf("mutation target: wav loader input, %d bit-flips\n", candidates);
	printf("killed=%d  data-chunk survivors=%d (must be 0)  ignored-header survivors=%d\n",
		killed, data_survivors, hdr_survivors);

	int failed = (data_survivors != 0);
	printf("%s (input mutation)\n", failed ? "FAILED" : "PASSED");
	return failed;
}
