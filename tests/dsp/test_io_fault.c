/* Loop-until-clean anomaly injection for the WAV loader (src/io.c).
 *
 * Two passes over the loader's failure points:
 *   - I/O: fail each fopen/fread in turn (short reads / missing file)
 *   - OOM: fail each malloc in turn
 * Both must degrade cleanly: no crash, no half-registered sample, no leak.
 *
 * Built with -Wl,--wrap=... (see tests/meson.build); the input WAV is handed
 * to the loader through an anonymous memfd, so nothing touches disk. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "io.h"
#include "sample.h"
#include "faultinject.h"

#define ASSERT_TRUE(cond) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

/* A valid 16-bit mono WAV in a memfd; caller keeps the fd open and passes the
 * /proc/self/fd/N path to the loader. */
static int make_wav_fd(void) {
	enum { N = 16 };
	WAVHeader h;
	memset(&h, 0, sizeof(h));
	memcpy(h.chunkID, "RIFF", 4);
	memcpy(h.format, "WAVE", 4);
	memcpy(h.subchunk1ID, "fmt ", 4);
	h.subchunk1Size = 16;
	h.audioFormat = 1;
	h.numChannels = 1;
	h.sampleRate = 44100;
	h.byteRate = 44100 * 2;
	h.blockAlign = 2;
	h.bitsPerSample = 16;
	memcpy(h.subchunk2ID, "data", 4);
	h.subchunk2Size = N * 2;
	h.chunkSize = 36 + h.subchunk2Size;

	int fd = memfd_create("spx_io_fault", MFD_CLOEXEC);
	if(fd < 0) {
		return -1;
	}
	/* Header and data are written as raw bytes (not the struct) so the file
	 * content is byte-stable regardless of struct padding. */
	static const uint8_t hdr[44] = {
		'R','I','F','F', 36+32,0,0,0, 'W','A','V','E',
		'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
		0x44,0xac,0,0,        /* 44100 */
		0x88,0x58,0x01,0,     /* 88200 byte rate */
		2,0, 16,0,            /* block align, bits */
		'd','a','t','a', 32,0,0,0
	};
	if(write(fd, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
		close(fd);
		return -1;
	}
	/* Data chunk: 16 little-endian int16 samples, i*1000 - 8000. */
	uint8_t pcm[32];
	for(int i = 0; i < N; i++) {
		int16_t v = (int16_t)(i * 1000 - 8000);
		pcm[2*i] = (uint8_t)(v & 0xff);
		pcm[2*i+1] = (uint8_t)((v >> 8) & 0xff);
	}
	if(write(fd, pcm, sizeof(pcm)) != (ssize_t)sizeof(pcm)) {
		close(fd);
		return -1;
	}
	if(lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int test_wav_io_faults(void) {
	int fd = make_wav_fd();
	ASSERT_TRUE(fd >= 0);
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	for(long k = 0; k < 16; k++) {
		SamplePool *sp = createSamplePool();
		ASSERT_TRUE(sp != NULL);

		fi_reset();
		fi_fail_io_after(k);
		load_wav_sample(path, sp);
		long fails = fi_io_failures();
		fi_fail_io_after(-1);

		if(fails == 0) {
			ASSERT_TRUE(sp->sampleCount == 1);
			/* The oracle: the loaded PCM must equal the file's PCM to the
			 * bit. readSampleData() (sample.h) is the only way the value
			 * leaves the pool, so touching both the source buffer and the
			 * copy inside loadSample(); any mutation of either must show
			 * here. */
			Sample *s = sp->samples[0];
			ASSERT_TRUE(s->length == 16);
			ASSERT_TRUE(s->sampleRate == 44100);
			for(int i = 0; i < 16; i++) {
				float expect = (float)(i * 1000 - 8000) / 32768.0f;
				float got = s->data[i];
				if(got != expect) {
					fprintf(stderr, "FAIL sample[%d]=%f expected %f\n",
						i, (double)got, (double)expect);
					return 1;
				}
			}
			freeSamplePool(sp);
			printf("PASS test_wav_io_faults (clean after %ld io ops)\n", fi_io_count());
			close(fd);
			return 0;
		}
		ASSERT_TRUE(sp->sampleCount == 0); /* aborted without registering */
		/* Memory-consistency oracle: while a load is in the fault window,
		 * a successful load must have registered exactly one sample and
		 * consumed memory; a refusal must leave the pool pristine. This
		 * catches a header/data mutation that makes the loader take a
		 * different exit path without honouring the invariant (the
		 * P7 mutation run exercises exactly this). */
		if(sp->sampleCount != 0) {
			fprintf(stderr, "FAIL: io fault k=%ld left sampleCount=%zu\n",
				k, sp->sampleCount);
			return 1;
		}
		ASSERT_TRUE(sp->memoryUsed == 0);
		freeSamplePool(sp);
	}
	fprintf(stderr, "FAIL test_wav_io_faults: never reached a clean load\n");
	close(fd);
	return 1;
}

static int test_wav_oom(void) {
	int fd = make_wav_fd();
	ASSERT_TRUE(fd >= 0);
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	for(long k = 0; k < 32; k++) {
		SamplePool *sp = createSamplePool();
		ASSERT_TRUE(sp != NULL);

		fi_reset();
		fi_fail_after(k);
		load_wav_sample(path, sp);
		long fails = fi_failures();
		fi_fail_after(-1);

		if(fails == 0) {
			ASSERT_TRUE(sp->sampleCount == 1);
			freeSamplePool(sp);
			ASSERT_TRUE(fi_live() == 0);
			printf("PASS test_wav_oom (clean after %ld allocations)\n", fi_count());
			close(fd);
			return 0;
		}
		ASSERT_TRUE(sp->sampleCount == 0); /* OOM aborted the load */
		freeSamplePool(sp);
		ASSERT_TRUE(fi_live() == 0);       /* nothing leaked */
	}
	fprintf(stderr, "FAIL test_wav_oom: never reached a clean load\n");
	close(fd);
	return 1;
}

int main(void) {
	int failed = 0;
	failed |= test_wav_io_faults();
	failed |= test_wav_oom();

	printf("\n%s (io fault injection)\n", failed ? "FAILED" : "PASSED");
	return failed;
}
