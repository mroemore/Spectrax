/* libFuzzer harness for the project/sequencer file loader
 * (src/io/sequencer_io.c:loadSequencerState).
 *
 * loadSequencerState() opens a path, so the fuzz input is exposed through an
 * anonymous memfd and handed over as /proc/self/fd/N — nothing touches disk.
 * Build/run with meson/fuzz.sh.
 *
 * Why this target earns its keep: this is the project's only remaining
 * untrusted binary format. loadSequencerState is a chain of raw freads into
 * fixed-size destination arrays, with no length validation beyond the return
 * value of each fread — the I29 defect (an unvalidated pattern_size read off
 * disk) lived here, and the WAV fuzzer cannot reach this code at all.
 *
 * The destination is built fresh per input (a zeroed PatternList and Arranger
 * with a real BPM Parameter, which the loader writes into via
 * setParameterBaseValue), so a crash is the loader's, not the harness's.
 *
 * A rejected input is a pass: every SEQ_ERROR_* return is a legitimate
 * outcome. We only care about out-of-bounds access, leaks, and UB, which
 * ASan/UBSan/LeakSanitizer decide. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "io.h"
#include "modsystem.h"
#include "notes.h"
#include "sequencer.h"
#include "sequencer_io.h"

/* The loader prints a line per failed field ("error selx", "error sxy", ...).
 * Silence stdout so libFuzzer's output stays readable; stderr is left alone
 * because that is where sanitizers report. */
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

	int fd = memfd_create("spx_fuzz_seq", MFD_CLOEXEC);
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

	ParamList *pl = createParamList();
	Arranger *arranger = (Arranger *)calloc(1, sizeof(Arranger));
	PatternList *patterns = (PatternList *)calloc(1, sizeof(PatternList));
	if(pl && arranger && patterns) {
		/* The loader calls setParameterBaseValue on this, so it must be a
		 * real Parameter rather than NULL. */
		arranger->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f,
		                                                   1000.0f, 1.0f, 10.0f, NULL, NULL);
		if(arranger->tempoSettings.bpm) {
			if(loadSequencerState(path, arranger, patterns) == SEQ_OK) {
				/* A loader-only harness has no teeth for I29: the loader is a
				 * pure deserializer that stores pattern_size verbatim, and the
				 * out-of-bounds access only happens when a CONSUMER treats
				 * that value as a step count. Proving that took reverting the
				 * loader check and fuzzing for two minutes to no effect, so
				 * the harness now drives the two consumers a loaded project
				 * reaches immediately: build a Sequencer from the arranger,
				 * then run the playback path that does
				 * `playhead_index % pattern_size` and reads steps. */
				Sequencer *seq = createSequencer(arranger);
				if(seq) {
					startPlaying(seq, patterns, arranger, 0);
					for(int step = 0; step < 8; step++) {
						/* NULL trigger: advance without allocating voices. */
						advanceSequencerStep(seq, patterns, arranger, NULL, NULL);
					}
					stopPlaying(arranger);
					free(seq);
				}
			}
		}
	}

	free(patterns);
	free(arranger);
	if(pl) {
		freeParamList(pl);
	}

	close(fd);
	return 0;
}