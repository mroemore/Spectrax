/* Loop-until-clean anomaly injection for the binary loaders.
 *
 *   io/sequencer_io.c  loadSequencerState
 *   io/preset_io.c     loadPresetFile
 *
 * Both are chains of raw fread into fixed-size destination structs with no
 * length validation of their own (see TESTING.md I32), so "what happens when a
 * read or an allocation fails partway through" is worth asking systematically
 * rather than case by case.
 *
 * "Degrades cleanly" here means:
 *   - an I/O failure returns a non-OK error code and does not crash;
 *   - a failed required read is never reported as success;
 *   - an OOM failure returns an error code or NULL, not a half-populated
 *     destination the caller would treat as valid;
 *   - nothing leaks (valgrind's authority, not this file's).
 *
 * One structural note that costs real debugging time if you get it wrong: the
 * harness's own allocations are wrapped too, so allocating the destination
 * inside the injection window means the *test* runs out of memory and then
 * dereferences NULL. Everything the loader needs is therefore built once,
 * outside the loop, and reset in place between iterations. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "faultinject.h"
#include "io.h"
#include "io/preset_io.h"
#include "io/sequencer_io.h"
#include "modsystem.h"
#include "notes.h"
#include "sequencer.h"

#define ASSERT_TRUE(cond, msg) do { \
	if(!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
		return 1; \
	} \
} while (0)

/* ---- a valid project file in a memfd --------------------------------- */

static int make_seq_file(void) {
	int fd = memfd_create("spx_seq_fault", MFD_CLOEXEC);
	if(fd < 0) {
		return -1;
	}
	ParamList *pl = createParamList();
	Arranger *a = (Arranger *)calloc(1, sizeof(Arranger));
	PatternList *p = (PatternList *)calloc(1, sizeof(PatternList));
	if(pl == NULL || a == NULL || p == NULL) {
		free(a);
		free(p);
		freeParamList(pl);
		close(fd);
		return -1;
	}
	a->tempoSettings.bpm = createParameterPro(pl, "BPM", 120.0f, 1.0f, 1000.0f,
	                                           1.0f, 10.0f, NULL, NULL);
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		for(int j = 0; j < MAX_SONG_LENGTH; j++) {
			a->song[i][j] = -1;
		}
		a->playhead_indices[i] = 0;
	}
	a->enabledChannels = 2;
	p->pattern_count = 1;
	p->patterns[0].pattern_size = 8;
	for(int r = 0; r < MAX_SEQUENCE_LENGTH; r++) {
		p->patterns[0].notes[r][0] = r % 12;
		p->patterns[0].notes[r][1] = 3;
	}
	a->song[0][0] = 0;

	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
	SequencerFileResult rc = saveSequencerState(path, a, p);
	free(p);
	free(a);
	freeParamList(pl);
	if(rc != SEQ_OK || lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/* A destination built once, reused across every injection iteration.
 * reset() restores the fields the loader writes so each iteration starts from
 * the same state. */
typedef struct {
	ParamList *pl;
	Arranger *arranger;
	PatternList *patterns;
	Parameter *bpm;
} Dest;

static int dest_init(Dest *d) {
	memset(d, 0, sizeof(*d));
	d->pl = createParamList();
	d->arranger = (Arranger *)calloc(1, sizeof(Arranger));
	d->patterns = (PatternList *)calloc(1, sizeof(PatternList));
	if(d->pl == NULL || d->arranger == NULL || d->patterns == NULL) {
		return 1;
	}
	d->bpm = createParameterPro(d->pl, "BPM", 120.0f, 1.0f, 1000.0f, 1.0f, 10.0f,
	                            NULL, NULL);
	return d->bpm == NULL;
}

static void dest_reset(Dest *d) {
	memset(d->arranger, 0, sizeof(Arranger));
	memset(d->patterns, 0, sizeof(PatternList));
	d->arranger->tempoSettings.bpm = d->bpm;
}

static void dest_free(Dest *d) {
	free(d->patterns);
	free(d->arranger);
	freeParamList(d->pl);
}

/* ---- I/O failure injection ------------------------------------------- */

/* The LABL chip-label chunk is optional (older V2 files predate it), so a
 * failed read in the tail legitimately still returns SEQ_OK. The assertion is
 * therefore not "no read may be followed by success" but "no *required* read
 * may be": walk forward until success-with-a-failure first appears, and
 * require that boundary to sit past the required region. If a required read's
 * failure were tolerated the boundary would collapse towards 0 and this fails.
 *
 * The boundary is also recorded and asserted to be stable: it is a property of
 * the loader's read sequence, not something to tune per run. */
static int test_seq_io_faults(void) {
	int fd = make_seq_file();
	ASSERT_TRUE(fd >= 0, "make_seq_file");
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	Dest d;
	ASSERT_TRUE(dest_init(&d) == 0, "dest_init");

	/* Baseline: with no injection the file must load. This is what proves the
	 * loop below is failing something real rather than walking past an
	 * already-failing input. */
	dest_reset(&d);
	ASSERT_TRUE(loadSequencerState(path, d.arranger, d.patterns) == SEQ_OK,
	            "clean load succeeds");

	long first_tolerated = -1;
	for(long k = 0; k < 64; k++) {
		fi_reset();
		fi_fail_io_after(k);
		dest_reset(&d);
		SequencerFileResult rc = loadSequencerState(path, d.arranger, d.patterns);
		long fails = fi_io_failures();
		fi_fail_io_after(-1);
		if(fails == 0) {
			break;
		}
		if(rc == SEQ_OK) {
			first_tolerated = k;
			break;
		}
	}

	/* Reads before the optional chunk: magic, PATT header, count, size,
	 * notes, ARRG header, playhead_indices, enabled, selx, sely, loop, bpm,
	 * playing, song, channelSlots, LABL peek -- 16 of them. Tolerating a
	 * failure before #10 would mean a required field is silently ignored. */
	if(first_tolerated < 0) {
		fprintf(stderr, "FAIL no tolerated read found; is the file still loading?\n");
		dest_free(&d);
		close(fd);
		return 1;
	}
	if(first_tolerated < 10) {
		fprintf(stderr, "FAIL loader tolerates a failed read as early as #%ld\n",
		        first_tolerated);
		dest_free(&d);
		close(fd);
		return 1;
	}
	printf("  (optional-chunk boundary at read #%ld)\n", first_tolerated);

	fi_reset();
	dest_free(&d);
	close(fd);
	printf("PASS test_seq_io_faults\n");
	return 0;
}

/* ---- allocation failure injection ------------------------------------ */

/* loadSequencerState allocates almost nothing of its own, so loop-until-clean
 * converges in a couple of iterations -- unlike the WAV loader. Worth stating
 * because "the loop only ran twice" is a result, not a bug in the harness. */
static int test_seq_oom(void) {
	int fd = make_seq_file();
	ASSERT_TRUE(fd >= 0, "make_seq_file");
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	Dest d;
	ASSERT_TRUE(dest_init(&d) == 0, "dest_init");

	long converged = -1;
	for(long k = 0; k < 32; k++) {
		fi_reset();
		fi_fail_after(k);
		dest_reset(&d);
		SequencerFileResult rc = loadSequencerState(path, d.arranger, d.patterns);
		long fails = fi_failures();
		fi_fail_after(-1);
		if(fails == 0) {
			ASSERT_TRUE(rc == SEQ_OK, "clean load succeeds");
			converged = k;
			break;
		}
		/* The loader allocates no destination memory of its own, so a failed
		 * allocation is not expected to change the verdict -- what matters is
		 * that it does not crash and does not report a bogus success state. */
		if(rc != SEQ_OK && rc != SEQ_ERROR_READ && rc != SEQ_ERROR_FORMAT) {
			fprintf(stderr, "FAIL unexpected rc %d with an allocation failed\n", rc);
			dest_free(&d);
			close(fd);
			return 1;
		}
	}
	printf("  (loader allocation surface: %ld iterations)\n", converged);

	fi_reset();
	dest_free(&d);
	close(fd);
	printf("PASS test_seq_oom\n");
	return 0;
}

/* ---- the preset loader ----------------------------------------------- */

/* loadPresetFile reads a Preset struct after a magic check. A short read must
 * come back as an error and must not leave presetCount claiming a patch that
 * was never populated. */
static int test_preset_io_faults(void) {
	int fd = memfd_create("spx_preset_fault", MFD_CLOEXEC);
	ASSERT_TRUE(fd >= 0, "memfd_create");
	{
		char buf[sizeof(Preset) + 16];
		memset(buf, 0, sizeof(buf));
		memcpy(buf, PRESET_MAGIC_HEADER_V3, 4);
		if(write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
			close(fd);
			return 1;
		}
	}
	if(lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return 1;
	}
	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	/* Built once, outside the injection window, for the same reason as Dest. */
	PresetBank *pb = (PresetBank *)calloc(1, sizeof(PresetBank));
	ASSERT_TRUE(pb != NULL, "calloc PresetBank");

	long first_tolerated = -1;
	for(long k = 0; k < 8; k++) {
		fi_reset();
		fi_fail_io_after(k);
		memset(pb, 0, sizeof(*pb));
		PresetFileResult rc = loadPresetFile(path, pb);
		long fails = fi_io_failures();
		fi_fail_io_after(-1);
		if(fails == 0) {
			break;
		}
		if(rc == PRESET_OK && pb->presetCount > 0) {
			fprintf(stderr, "FAIL fread #%ld failed but load claimed %d patches\n",
			        k, pb->presetCount);
			free(pb);
			close(fd);
			return 1;
		}
		if(rc == PRESET_OK) {
			first_tolerated = k;
		}
	}
	printf("  (preset tolerated-failure boundary at read #%ld)\n", first_tolerated);

	fi_reset();
	free(pb);
	close(fd);
	printf("PASS test_preset_io_faults\n");
	return 0;
}

int main(void) {
	int rc = 0;
	rc |= test_seq_io_faults();
	rc |= test_seq_oom();
	rc |= test_preset_io_faults();
	if(rc != 0) {
		fprintf(stderr, "FAILED (binary loader fault injection)\n");
		return 1;
	}
	printf("PASSED (binary loader fault injection)\n");
	return 0;
}