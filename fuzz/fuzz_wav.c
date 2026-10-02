/* libFuzzer harness for the WAV loader (src/io.c:load_wav_sample).
 *
 * load_wav_sample() opens a path, so the fuzz input is exposed through an
 * anonymous memfd and handed over as /proc/self/fd/N — nothing touches disk.
 * Build/run with meson/fuzz.sh.
 *
 * Targets untrusted audio files: header parsing, the per-bit-depth PCM
 * conversion loops, and the sample-pool accounting. Run under ASan/UBSan. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "io.h"
#include "sample.h"

/* The loader is chatty (prints bytes/samples); silence it so libFuzzer's
 * output stays readable. */
static void silence_stdout(void) {
	static int done = 0;
	if(done) {
		return;
	}
	done = 1;
	int devnull = open("/dev/null", O_WRONLY);
	if(devnull >= 0) {
		dup2(devnull, STDOUT_FILENO);
		close(devnull);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	silence_stdout();

	int fd = memfd_create("spx_fuzz_wav", MFD_CLOEXEC);
	if(fd < 0) {
		return 0;
	}
	if(size > 0 && write(fd, data, size) != (ssize_t)size) {
		close(fd);
		return 0;
	}
	if(lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return 0;
	}

	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	SamplePool *sp = createSamplePool();
	if(sp != NULL) {
		load_wav_sample(path, sp);
		freeSamplePool(sp);
	}

	close(fd);
	return 0;
}
