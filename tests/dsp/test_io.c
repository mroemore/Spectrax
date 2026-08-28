/*
 * test_io.c — verify Spectrax preset / sequencer / settings file I/O and the
 * base DirectoryList helper, all built under fil-c.
 *
 * Build (fil-c toolchain):  make bin/test_io && ./bin/test_io
 *
 * Temp files are written under .tmp_files/ (repo convention; /tmp is off
 * limits on this machine).
 *
 * Known pre-existing behaviour pinned by these tests:
 *  - src/io/sequencer_io.c writes arranger->tempoSettings.loop (a 1-byte
 *    bool) with sizeof(int) via fwrite, and reads it back with sizeof(int)
 *    via fread (saveSequencerState / loadSequencerState). The struct padding
 *    keeps this in-bounds on the tested platform (verified by probe), so it
 *    round-trips correctly, but the 3 padding bytes are written to the file.
 *  - src/modsystem.c setParameterBaseValue() updates BOTH baseValue and
 *    currentValue (keeps unmodulated params' dials live). The sequencer
 *    save path serialises the param's currentValue and the load path
 *    restores it into both fields, so a saved BPM reads back identically
 *    in both slots — asserted directly on the Parameter field below.
 *  - src/io/preset_io.c savePresetFile() ignores the fwrite() return value
 *    (no PRESET_ERROR_WRITE on short writes). Not exercised here because
 *    fwrite to a healthy file succeeds; noted as pre-existing sloppiness.
 *
 * No Voice objects are created or freed in this file (BUG-VC-1 freeVoice
 * double-free — avoid entirely).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#include "io.h"
#include "io/preset_io.h"
#include "io/sequencer_io.h"
#include "io/settings_io.h"
#include "voice.h"
#include "modsystem.h"

/* ---- local test macros (same style as tests/dsp/test_notes.c) -------- */

#define ASSERT_EQ(actual, expected) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_MSG(actual, expected, msg) do { \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        fprintf(stderr, "FAIL %s:%d: %s (expected %lld, got %lld)\n", \
                __FILE__, __LINE__, (msg), _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define ASSERT_STREQ(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while (0)

#define ASSERT_NEAR(actual, expected, tol) do { \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: expected %.4f, got %.4f (tol %.4f)\n", \
                __FILE__, __LINE__, _e, _a, (float)(tol)); \
        return 1; \
    } \
} while (0)

/* ---- helpers ---------------------------------------------------------- */

#define TMP_DIR ".tmp_files/"
#define PRESET_DIR TMP_DIR "presetdir/"

/* Create .tmp_files/ and .tmp_files/presetdir/ if they don't exist. */
static void ensure_tmp_dirs(void) {
    mkdir(TMP_DIR, 0755);
    mkdir(PRESET_DIR, 0755);
}

static long long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

/* Read the first 4 bytes of a file into out[4]. Returns 0 on success. */
static int read_magic(const char *path, char out[4]) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    int rc = (fread(out, 1, 4, f) == 4) ? 0 : 1;
    fclose(f);
    return rc;
}

/* Write 4 bytes of garbage to path (for bad-magic tests). */
static void write_garbage(const char *path, const char *magic, size_t extra) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(magic, 1, 4, f);
    char pad[16];
    memset(pad, 0xAB, sizeof(pad));
    size_t n = extra > sizeof(pad) ? sizeof(pad) : extra;
    fwrite(pad, 1, n, f);
    fclose(f);
}

/* A self-contained preset: initDefaultFmPreset then tweak a couple fields. */
static void make_preset(Preset *p, int seed) {
    initDefaultFmPreset(p);
    p->voiceType = VOICE_TYPE_BLEP;
    p->pd.blep.shape = seed % 3;
    p->modSettingsCount = (seed % 2) + 1;
    p->modSettings[0].type = MT_ENV;
    p->modSettings[0].md.env.stageCount = 3;
    p->modSettings[0].md.env.stages[1].targetLevel = 0.5f + (float)seed * 0.1f;
}

static PresetBank *make_bank(void) {
    PresetBank *pb = (PresetBank *)calloc(1, sizeof(PresetBank));
    if (pb) initPresetBank(pb);
    return pb;
}

/* Minimal sequencer environment: Arranger + PatternList + bpm Parameter.
 * bpm must be a real Parameter* because save reads it via
 * getParameterValueAsInt() and load writes it via setParameterBaseValue(). */
typedef struct {
    ParamList *pl;
    Parameter *bpm;
    Arranger *arranger;
    PatternList *patterns;
} SeqEnv;

static int make_seq_env(SeqEnv *e, float initialBpm, int patternCount) {
    memset(e, 0, sizeof(*e));
    e->pl = createParamList();
    if (!e->pl) return 1;
    e->bpm = createParameterPro(e->pl, "BPM", initialBpm, 1.0f, 1000.0f,
                                1.0f, 10.0f, NULL, NULL);
    if (!e->bpm) return 1;

    e->arranger = (Arranger *)calloc(1, sizeof(Arranger));
    e->patterns = (PatternList *)calloc(1, sizeof(PatternList));
    if (!e->arranger || !e->patterns) return 1;

    e->arranger->tempoSettings.bpm = e->bpm;
    e->arranger->enabledChannels = 2;
    e->arranger->selected_x = 1;
    e->arranger->selected_y = 3;
    e->arranger->tempoSettings.loop = true;
    e->arranger->playing = 1;
    e->arranger->playhead_indices[0] = 0;
    e->arranger->playhead_indices[1] = 2;
    for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
        for (int j = 0; j < MAX_SONG_LENGTH; j++) {
            e->arranger->song[i][j] = -1; /* empty, like createArranger */
        }
    }
    e->arranger->song[0][0] = 0;
    e->arranger->song[0][1] = 1;
    e->arranger->song[1][0] = 1;

    e->patterns->pattern_count = patternCount;
    for (int i = 0; i < patternCount; i++) {
        e->patterns->patterns[i].pattern_size = 8 + i;
        for (int r = 0; r < MAX_SEQUENCE_LENGTH; r++) {
            e->patterns->patterns[i].notes[r][0] = (r + i) % 12;
            e->patterns->patterns[i].notes[r][1] = 3 + (r / 8);
        }
    }
    return 0;
}

/* Free everything make_seq_env allocated. */
static void free_seq_env(SeqEnv *e) {
    if (!e) return;
    if (e->arranger) { free(e->arranger); e->arranger = NULL; }
    if (e->patterns) { free(e->patterns); e->patterns = NULL; }
    if (e->bpm)      { free(e->bpm);      e->bpm = NULL; }
    if (e->pl)       { free(e->pl);       e->pl = NULL; }
}

/* ---- preset tests ------------------------------------------------------ */

static int test_save_preset_ok(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_preset_ok.pst";
    remove(path);

    Preset p;
    make_preset(&p, 1);
    ASSERT_EQ(savePresetFile(path, &p), PRESET_OK);

    /* Magic header (V2 = IPB2; V1 = IPBH kept for migration only) */
    char magic[4];
    ASSERT_EQ(read_magic(path, magic), 0);
    ASSERT_TRUE(memcmp(magic, PRESET_MAGIC_HEADER_V2, 4) == 0,
                "preset file must start with IPB2 magic (V2 format)");

    /* Non-empty, exactly magic + raw struct */
    ASSERT_EQ(file_size(path), (long long)(4 + sizeof(Preset)));

    remove(path);
    printf("PASS test_save_preset_ok\n");
    return 0;
}

static int test_preset_roundtrip(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_preset_rt.pst";
    remove(path);

    Preset p;
    make_preset(&p, 2);
    ASSERT_EQ(savePresetFile(path, &p), PRESET_OK);

    PresetBank *pb = make_bank();
    ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");
    ASSERT_EQ(loadPresetFile(path, pb), PRESET_OK);
    ASSERT_EQ(pb->presetCount, 1);
    ASSERT_TRUE(memcmp(&pb->patches[0], &p, sizeof(Preset)) == 0,
                "loaded preset differs from saved preset (byte compare)");

    free(pb);
    remove(path);
    printf("PASS test_preset_roundtrip\n");
    return 0;
}

static int test_load_preset_missing(void) {
    PresetBank *pb = make_bank();
    ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");
    ASSERT_EQ(loadPresetFile(TMP_DIR "no_such_preset.pst", pb),
              PRESET_ERROR_OPEN);
    ASSERT_EQ(pb->presetCount, 0);
    free(pb);
    printf("PASS test_load_preset_missing\n");
    return 0;
}

static int test_load_preset_bad_magic(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_preset_badmagic.pst";
    remove(path);
    write_garbage(path, "XXXX", 16);

    PresetBank *pb = make_bank();
    ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");
    ASSERT_EQ(loadPresetFile(path, pb), PRESET_ERROR_FORMAT);
    ASSERT_EQ(pb->presetCount, 0);

    free(pb);
    remove(path);
    printf("PASS test_load_preset_bad_magic\n");
    return 0;
}

static int test_load_preset_truncated(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_preset_trunc.pst";
    remove(path);
    /* Magic only, no struct payload */
    {
        FILE *f = fopen(path, "wb");
        if (!f) return 1;
        fwrite("IPBH", 1, 4, f);
        fclose(f);
    }

    PresetBank *pb = make_bank();
    ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");
    ASSERT_EQ(loadPresetFile(path, pb), PRESET_ERROR_READ);
    ASSERT_EQ(pb->presetCount, 0);

    free(pb);
    remove(path);
    printf("PASS test_load_preset_truncated\n");
    return 0;
}

static int test_load_presets_from_directory(void) {
    ensure_tmp_dirs();
    /* Clean the preset dir, then drop two preset files into it. */
    remove(PRESET_DIR "a.pst");
    remove(PRESET_DIR "b.pst");

    Preset pa, pb;
    make_preset(&pa, 1);
    make_preset(&pb, 2);
    ASSERT_EQ(savePresetFile(PRESET_DIR "a.pst", &pa), PRESET_OK);
    ASSERT_EQ(savePresetFile(PRESET_DIR "b.pst", &pb), PRESET_OK);

    PresetBank *bank = make_bank();
    ASSERT_TRUE(bank != NULL, "calloc PresetBank failed");
    loadPresetsFromDirectory(PRESET_DIR, bank);

    ASSERT_EQ(bank->presetCount, 2);
    ASSERT_TRUE(memcmp(&bank->patches[0], &pa, sizeof(Preset)) == 0,
                "dir load: first preset mismatch");
    ASSERT_TRUE(memcmp(&bank->patches[1], &pb, sizeof(Preset)) == 0,
                "dir load: second preset mismatch");

    free(bank);
    remove(PRESET_DIR "a.pst");
    remove(PRESET_DIR "b.pst");
    printf("PASS test_load_presets_from_directory\n");
    return 0;
}

static int test_directory_list(void) {
    ensure_tmp_dirs();
    /* Recreate the two-file preset dir (independent of other tests). */
    Preset pa, pb;
    make_preset(&pa, 1);
    make_preset(&pb, 2);
    savePresetFile(PRESET_DIR "a.pst", &pa);
    savePresetFile(PRESET_DIR "b.pst", &pb);

    DirectoryList *list = createDirectoryList();
    ASSERT_TRUE(list != NULL, "createDirectoryList returned NULL");
    ASSERT_EQ((long long)list->count, 0LL);

    populateDirectoryList(list, PRESET_DIR);
    ASSERT_EQ((long long)list->count, 2LL);
    ASSERT_TRUE(list->file_paths != NULL, "file_paths is NULL after populate");
    ASSERT_TRUE(list->file_paths[0] != NULL && list->file_paths[1] != NULL,
                "file path entries are NULL");
    /* Paths must be absolute-ish (dirPath prefix + filename). */
    ASSERT_TRUE(strstr(list->file_paths[0], PRESET_DIR) != NULL,
                "file path missing directory prefix");

    freeDirectoryList(list);
    remove(PRESET_DIR "a.pst");
    remove(PRESET_DIR "b.pst");
    printf("PASS test_directory_list\n");
    return 0;
}

/* ---- sequencer tests --------------------------------------------------- */

static int test_save_sequencer_ok(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_seq_ok.sng";
    remove(path);

    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 1), 0);

    ASSERT_EQ(saveSequencerState(path, e.arranger, e.patterns), SEQ_OK);

    /* Magic headers: SEQ1 at 0, PATT at 4, ARRG after the pattern section. */
    char magic[4];
    ASSERT_EQ(read_magic(path, magic), 0);
    ASSERT_TRUE(memcmp(magic, SEQ_MAGIC_HEADER_V2, 4) == 0,
                "sequence file must start with SEQ2 magic (V2 = channelSlots present)");
    {
        FILE *f = fopen(path, "rb");
        if (!f) return 1;
        char buf[8];
        fread(buf, 1, 4, f);
        fread(buf, 1, 4, f); /* PATT */
        ASSERT_TRUE(memcmp(buf, PATTERN_SECTION, 4) == 0,
                    "PATT section header missing after SEQ1");
        /* skip: pattern_count (4) + pattern_size (4) + notes (48*4) */
        fseek(f, 4 + 4 + MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE * (int)sizeof(int), SEEK_CUR);
        fread(buf, 1, 4, f); /* ARRG */
        ASSERT_TRUE(memcmp(buf, ARRANGER_SECTION, 4) == 0,
                    "ARRG section header missing");
        fclose(f);
    }

    /* Exact serialised size, computed from the save layout (SEQ2 / V2). */
    long long expected =
        4 + 4 + (long long)sizeof(int)                       /* SEQ2, PATT, pattern_count */
        + 4 + (long long)(MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE * sizeof(int)) /* one pattern */
        + 4                                                 /* ARRG */
        + (long long)(MAX_SEQUENCER_CHANNELS * sizeof(int))  /* playhead_indices */
        + 6LL * (long long)sizeof(int)                       /* enabled, selx, sely, loop, bpm, playing */
        + (long long)(MAX_SEQUENCER_CHANNELS * sizeof(int))  /* channelSlots (V2) */
        + (long long)(MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH * sizeof(int)); /* song */
    ASSERT_EQ(file_size(path), expected);

    remove(path);
    printf("PASS test_save_sequencer_ok\n");
    return 0;
}

static int test_sequencer_roundtrip(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_seq_rt.sng";
    remove(path);

    SeqEnv src;
    ASSERT_EQ(make_seq_env(&src, 137.0f, 2), 0);
    ASSERT_EQ(saveSequencerState(path, src.arranger, src.patterns), SEQ_OK);

    /* Load into a fresh environment (bpm param starts at 0). */
    SeqEnv dst;
    ASSERT_EQ(make_seq_env(&dst, 0.0f, 0), 0);
    ASSERT_EQ(loadSequencerState(path, dst.arranger, dst.patterns), SEQ_OK);

    /* Patterns */
    ASSERT_EQ(dst.patterns->pattern_count, 2);
    ASSERT_EQ(dst.patterns->patterns[0].pattern_size, 8);
    ASSERT_EQ(dst.patterns->patterns[1].pattern_size, 9);
    for (int i = 0; i < 2; i++) {
        for (int r = 0; r < MAX_SEQUENCE_LENGTH; r++) {
            ASSERT_EQ(dst.patterns->patterns[i].notes[r][0],
                      src.patterns->patterns[i].notes[r][0]);
            ASSERT_EQ(dst.patterns->patterns[i].notes[r][1],
                      src.patterns->patterns[i].notes[r][1]);
        }
    }

    /* Arranger fields */
    ASSERT_EQ(dst.arranger->enabledChannels, src.arranger->enabledChannels);
    ASSERT_EQ(dst.arranger->selected_x, src.arranger->selected_x);
    ASSERT_EQ(dst.arranger->selected_y, src.arranger->selected_y);
    ASSERT_EQ((int)dst.arranger->tempoSettings.loop,
              (int)src.arranger->tempoSettings.loop);
    ASSERT_EQ(dst.arranger->playing, src.arranger->playing);
    for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
        ASSERT_EQ(dst.arranger->playhead_indices[i],
                  src.arranger->playhead_indices[i]);
    }
    ASSERT_TRUE(memcmp(dst.arranger->song, src.arranger->song,
                       sizeof(dst.arranger->song)) == 0,
                "song array differs after roundtrip");

    /* BPM: save serialises currentValue; load restores into baseValue
     * AND currentValue (setParameterBaseValue keeps them in sync, so
     * unmodulated dials stay live after a load). Assert the restored
     * baseValue — currentValue is asserted by the dedicated
     * test_set_base_value_syncs_current in tests/dsp/test_modsystem.c. */
    ASSERT_NEAR(dst.bpm->baseValue, 137.0f, 0.001f);

    remove(path);
    printf("PASS test_sequencer_roundtrip\n");
    return 0;
}

static int test_load_sequencer_missing(void) {
    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 0), 0);
    ASSERT_EQ(loadSequencerState(TMP_DIR "no_such_seq.sng",
                                 e.arranger, e.patterns), SEQ_ERROR_OPEN);
    printf("PASS test_load_sequencer_missing\n");
    return 0;
}

static int test_load_sequencer_bad_magic(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_seq_badmagic.sng";
    remove(path);
    write_garbage(path, "XXXX", 64);

    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 0), 0);
    ASSERT_EQ(loadSequencerState(path, e.arranger, e.patterns),
              SEQ_ERROR_FORMAT);
    remove(path);
    printf("PASS test_load_sequencer_bad_magic\n");
    return 0;
}

static int test_load_sequencer_truncated(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_seq_trunc.sng";
    remove(path);
    /* SEQ1 + PATT + pattern_count=1 but no pattern payload. */
    {
        FILE *f = fopen(path, "wb");
        if (!f) return 1;
        fwrite("SEQ1", 1, 4, f);
        fwrite("PATT", 1, 4, f);
        int one = 1;
        fwrite(&one, sizeof(int), 1, f);
        fclose(f);
    }

    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 0), 0);
    ASSERT_EQ(loadSequencerState(path, e.arranger, e.patterns),
              SEQ_ERROR_READ);
    remove(path);
    printf("PASS test_load_sequencer_truncated\n");
    return 0;
}

/* ---- per-channel slot assignment tests (Task 4) ------------------------ */

/* Per-channel slot indices round-trip through a SEQ2 project file.
 * make_seq_env() builds a SeqEnv; we set a few slots, save, load into a
 * freshly calloc'd Arranger, and confirm the saved values come back. */
static int test_seq_channel_slots_roundtrip(void) {
    ensure_tmp_dirs();
    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 1), 0);
    e.arranger->channelSlots[0] = 2;
    e.arranger->channelSlots[1] = 5;
    e.arranger->channelSlots[2] = 0;
    e.arranger->channelSlots[3] = 1;
    const char *f = TMP_DIR "seqslot.sng";
    remove(f);
    ASSERT_EQ(saveSequencerState(f, e.arranger, e.patterns), SEQ_OK);

    Arranger arr2;
    PatternList pl2;
    memset(&arr2, 0, sizeof(arr2));
    memset(&pl2, 0, sizeof(pl2));
    /* Load needs a valid bpm Parameter to land in; use make_seq_env to set
     * one up, then drop the arranger+patternList slots and reuse the bpm. */
    SeqEnv dst;
    ASSERT_EQ(make_seq_env(&dst, 0.0f, 0), 0);
    arr2.tempoSettings.bpm = dst.arranger->tempoSettings.bpm;
    pl2 = *dst.patterns; /* keep dst.patterns zeroed for the loader */
    memset(dst.arranger->channelSlots, 0x7F, sizeof(dst.arranger->channelSlots)); /* sentinel */
    ASSERT_EQ(loadSequencerState(f, &arr2, &pl2), SEQ_OK);

    ASSERT_EQ(arr2.channelSlots[0], 2);
    ASSERT_EQ(arr2.channelSlots[1], 5);
    ASSERT_EQ(arr2.channelSlots[2], 0);
    ASSERT_EQ(arr2.channelSlots[3], 1);

    /* Make sure the rest of the array is also sane (zero where unset). */
    ASSERT_EQ(arr2.channelSlots[7], 0);

    remove(f);
    free(dst.arranger); free(dst.patterns); free(dst.bpm);
    free_seq_env(&e);
    printf("PASS test_seq_channel_slots_roundtrip\n");
    return 0;
}

/* A hand-written SEQ1 (V1) file must still load with SEQ_OK, but every
 * channelSlots entry is zero (V1 has no slot field). This guards the
 * migration path: existing projects still open, with no preset assigned. */
static int test_seq_v1_loads_with_zero_slots(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_seq_v1_zero_slots.sng";
    remove(path);

    /* Build a SEQ1 file by hand: the same layout saveSequencerState used
     * before Task 4 (no channelSlots payload). We need a real bpm param
     * for the loader to restore into, so allocate one. */
    Parameter *bpm = createParameterPro(
        createParamList(), "BPM", 0.0f, 1.0f, 1000.0f, 1.0f, 10.0f, NULL, NULL);
    ASSERT_TRUE(bpm != NULL, "bpm param allocation");
    Arranger arr;
    PatternList pat;
    memset(&arr, 0, sizeof(arr));
    memset(&pat, 0, sizeof(pat));
    arr.tempoSettings.bpm = bpm;

    FILE *fp = fopen(path, "wb");
    ASSERT_TRUE(fp != NULL, "open v1 seq for write");
    ASSERT_TRUE(writeChunkHeader(fp, SEQ_MAGIC_HEADER), "v1 SEQ1 magic");
    ASSERT_TRUE(writeChunkHeader(fp, PATTERN_SECTION), "PATT section");
    int zero = 0;
    fwrite(&zero, sizeof(int), 1, fp);
    ASSERT_TRUE(writeChunkHeader(fp, ARRANGER_SECTION), "ARRG section");
    int playheads[MAX_SEQUENCER_CHANNELS] = {0};
    fwrite(playheads, sizeof(int), MAX_SEQUENCER_CHANNELS, fp);
    int enabled = 1;
    fwrite(&enabled, sizeof(int), 1, fp);
    int selx = 0, sely = 0, loop = 0, bpmv = 120, playing = 0;
    fwrite(&selx, sizeof(int), 1, fp);
    fwrite(&sely, sizeof(int), 1, fp);
    fwrite(&loop, sizeof(int), 1, fp);
    fwrite(&bpmv, sizeof(int), 1, fp);
    fwrite(&playing, sizeof(int), 1, fp);
    int song[MAX_SEQUENCER_CHANNELS][MAX_SONG_LENGTH];
    memset(song, -1, sizeof(song));
    fwrite(song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, fp);
    fclose(fp);

    /* Load — should succeed with SEQ_OK. */
    ASSERT_EQ(loadSequencerState(path, &arr, &pat), SEQ_OK);

    /* All channelSlots default to 0 under V1. */
    int any_nonzero = 0;
    for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
        if (arr.channelSlots[i] != 0) any_nonzero = 1;
    }
    ASSERT_TRUE(!any_nonzero, "V1 load must zero every channelSlots entry");

    /* And the rest still loads: bpm is 120. */
    ASSERT_NEAR(bpm->baseValue, 120.0f, 0.001f);

    remove(path);
    printf("PASS test_seq_v1_loads_with_zero_slots\n");
    return 0;
}

/* Save file must start with the SEQ2 magic (V2 format marker). The brief
 * is explicit: V2 files carry channelSlots, V1 files do not. */
static int test_save_sequencer_seq2_magic(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_seq_seq2_magic.sng";
    remove(path);

    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 1), 0);
    ASSERT_EQ(saveSequencerState(path, e.arranger, e.patterns), SEQ_OK);

    char magic[4];
    ASSERT_EQ(read_magic(path, magic), 0);
    ASSERT_TRUE(memcmp(magic, SEQ_MAGIC_HEADER_V2, 4) == 0,
                "SEQ2 magic must prefix every saved .sng");

    remove(path);
    free_seq_env(&e);
    printf("PASS test_save_sequencer_seq2_magic\n");
    return 0;
}

/* ---- settings tests (same I/O layer, cheap to cover) ------------------- */

static int test_settings_roundtrip(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_settings.dat";
    remove(path);

    Settings s, loaded;
    memset(&s, 0, sizeof(s));
    s.enabledChannels = 4;
    s.defaultSequenceLength = 16;
    s.defaultVoiceCount = 8;
    s.defaultBPM = 140;
    s.voiceTypes[0] = VOICE_TYPE_FM;
    s.voiceTypes[1] = VOICE_TYPE_SAMPLE;
    memset(&loaded, 0, sizeof(loaded));

    ASSERT_EQ(saveSettings(path, &s), FILE_OK);

    char magic[4];
    ASSERT_EQ(read_magic(path, magic), 0);
    ASSERT_TRUE(memcmp(magic, "SET1", 4) == 0, "settings magic must be SET1");

    ASSERT_EQ(loadSettings(path, &loaded), FILE_OK);
    ASSERT_TRUE(memcmp(&s, &loaded, sizeof(Settings)) == 0,
                "settings roundtrip mismatch");

    remove(path);
    printf("PASS test_settings_roundtrip\n");
    return 0;
}

static int test_settings_missing(void) {
    Settings s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(loadSettings(TMP_DIR "no_such_settings.dat", &s),
              FILE_ERROR_OPEN);
    printf("PASS test_settings_missing\n");
    return 0;
}

/* ---- main -------------------------------------------------------------- */

/* A Preset round-trips through a V2 file with its name intact. */
static int test_preset_name_roundtrip(void) {
    ensure_tmp_dirs();
    const char *f = TMP_DIR "presetdir/named.ipb";
    remove(f); /* scrub in case a prior interrupted run left it behind */
    Preset p;
    make_preset(&p, 2);
    strncpy(p.name, "lead pad", sizeof(p.name));
    ASSERT_TRUE(savePresetFile(f, &p) == PRESET_OK, "savePresetFile V2 ok");
    PresetBank *pb = make_bank();
    ASSERT_TRUE(loadPresetFile(f, pb) == PRESET_OK, "loadPresetFile V2 ok");
    ASSERT_EQ_MSG(pb->presetCount, 1, "one preset loaded");
    ASSERT_TRUE(strcmp(pb->patches[0].name, "lead pad") == 0, "name preserved");
    ASSERT_TRUE(pb->patches[0].pd.fm.ops[1].ratio == p.pd.fm.ops[1].ratio, "op ratio preserved");
    free(pb);
    remove(f);
    printf("PASS test_preset_name_roundtrip\n");
    return 0;
}

/* A V1 file (old magic, struct without the name field) loads with the name
 * derived from the filename and is re-saved as V2. */
static int test_preset_v1_migration(void) {
    ensure_tmp_dirs();
    const char *v1 = TMP_DIR "presetdir/oldpreset.ipb";
    remove(v1); /* scrub in case a prior interrupted run left it behind */
    Preset p;
    make_preset(&p, 1);
    /* write the old format by hand: V1 header + the struct sans name field */
    FILE *fp = fopen(v1, "wb");
    ASSERT_TRUE(fp != NULL, "open v1 for write");
    ASSERT_TRUE(writeChunkHeader(fp, "IPBH"), "v1 header written");
    /* match the real V1 layout: body starts at voiceType (offsetof, not 33 — the
     * 33-byte name field is followed by 3 bytes of padding before voiceType). */
    size_t v1_body_off = offsetof(Preset, voiceType);
    ASSERT_TRUE(fwrite(((char *)&p) + v1_body_off, sizeof(Preset) - v1_body_off, 1, fp) == 1, "v1 body written");
    fclose(fp);

    PresetBank *pb = make_bank();
    ASSERT_TRUE(loadPresetFile(v1, pb) == PRESET_OK, "v1 load ok");
    ASSERT_EQ_MSG(pb->presetCount, 1, "v1 preset loaded");
    ASSERT_TRUE(strcmp(pb->patches[0].name, "oldpreset") == 0, "name from filename");

    /* migration re-saved the file as V2: check the header magic */
    fp = fopen(v1, "rb");
    ASSERT_TRUE(fp != NULL, "open v1 for read");
    ASSERT_TRUE(readAndVerifyChunkHeader(fp, "IPB2"), "file now V2 after migration");
    fclose(fp);

    free(pb);
    remove(v1);
    printf("PASS test_preset_v1_migration\n");
    return 0;
}

/* Loading the real shipped V1 preset directory must migrate every file:
 * the 5 default-FM .ipb files load with names derived from their filenames.
 * Meson runs this test with cwd == builddir, so the shipped dir is reached
 * via ../bin/data/instrument_presets/. readdir() ordering is not guaranteed,
 * so we locate "fm1" by name rather than assuming patches[0] is fm1. */
static int test_preset_ship_dir_migration(void) {
    const char *ship_dir = "../bin/data/instrument_presets/";
    PresetBank *pb = make_bank();
    ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");
    loadPresetsFromDirectory(ship_dir, pb);
    ASSERT_EQ(pb->presetCount, 5);
    int fm1_idx = -1;
    for (int i = 0; i < pb->presetCount; i++) {
        if (strcmp(pb->patches[i].name, "fm1") == 0) { fm1_idx = i; break; }
    }
    ASSERT_TRUE(fm1_idx >= 0, "fm1 present after migration (name from filename)");
    /* a default-FM preset: voiceType FM, 4 AD env mods */
    ASSERT_EQ_MSG(pb->patches[fm1_idx].voiceType, VOICE_TYPE_FM, "fm1 is FM");
    ASSERT_EQ_MSG(pb->patches[fm1_idx].modSettingsCount, 4, "fm1 has 4 mods");
    free(pb);
    printf("PASS test_preset_ship_dir_migration\n");
    return 0;
}

/* Task 5: sanitizePresetFilename must replace path/space/colon/star chars
 * with underscores and append ".ipb". The brief fixes the exact form so
 * callers can build <dir>/<sanitized>.ipb paths without further work. */
static int test_sanitize_filename(void) {
	char out[64];
	sanitizePresetFilename("lead pad dark", out, sizeof(out));
	ASSERT_TRUE(strcmp(out, "lead_pad_dark.ipb") == 0, "spaces -> underscores");
	sanitizePresetFilename("UNNAMED", out, sizeof(out));
	ASSERT_TRUE(strcmp(out, "UNNAMED.ipb") == 0, "plain name");
	sanitizePresetFilename("a/b\\c", out, sizeof(out));
	ASSERT_TRUE(strcmp(out, "a_b_c.ipb") == 0, "path separators stripped");
	printf("PASS test_sanitize_filename\n");
	return 0;
}

/* Task 5: saveInstrumentAsPreset must return PRESET_EXISTS when the
 * sanitized .ipb file is already present in the destination directory. We
 * build a real Instrument via init_instrument (FM, with a real PresetBank
 * allocated by make_bank) so saveInstrumentAsPreset can call
 * presetFromInstrument + presetNameExists + addPresetToBank end-to-end.
 * SamplePool is NULL because FM has no sample data; init_instrument only
 * dereferences it for VOICE_TYPE_SAMPLE. */
static int test_save_returns_exists(void) {
	ensure_tmp_dirs();
	const char *dir = PRESET_DIR;

	PresetBank *pb = make_bank();
	ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");

	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_FM, NULL, pb);
	ASSERT_TRUE(inst != NULL, "init_instrument FM");
	ASSERT_TRUE(inst->presetBank == pb, "instrument bank == pb");

	const char *name = "dupe";

	/* Clean any stale file from a prior aborted run so the first save
	 * really creates the file. */
	char clean[64];
	sanitizePresetFilename(name, clean, sizeof(clean));
	char path[512];
	snprintf(path, sizeof(path), "%s%s", dir, clean);
	remove(path);

	PresetFileResult r1 = saveInstrumentAsPreset(inst, name, dir);
	ASSERT_EQ_MSG(r1, PRESET_OK, "first save ok");
	ASSERT_EQ_MSG(pb->presetCount, 1, "bank grew by one after first save");
	ASSERT_TRUE(strcmp(pb->patches[0].name, "dupe") == 0,
	            "preset name stored verbatim");
	ASSERT_TRUE(access(path, F_OK) == 0, "preset file written");

	/* Same instrument + same name into the same dir → PRESET_EXISTS.
	 * (presetFromInstrument + addPresetToBank must NOT run when EXIST.) */
	PresetFileResult r2 = saveInstrumentAsPreset(inst, name, dir);
	ASSERT_EQ_MSG(r2, PRESET_EXISTS, "second save returns PRESET_EXISTS");
	ASSERT_EQ_MSG(pb->presetCount, 1,
	              "bank unchanged after PRESET_EXISTS (no double-add)");

	/* Different name in the same dir → PRESET_OK, bank grows. */
	const char *name2 = "second";
	char clean2[64];
	sanitizePresetFilename(name2, clean2, sizeof(clean2));
	char path2[512];
	snprintf(path2, sizeof(path2), "%s%s", dir, clean2);
	remove(path2);
	PresetFileResult r3 = saveInstrumentAsPreset(inst, name2, dir);
	ASSERT_EQ_MSG(r3, PRESET_OK, "different name saves ok");
	ASSERT_EQ_MSG(pb->presetCount, 2, "bank grew by one");
	ASSERT_TRUE(access(path2, F_OK) == 0, "second preset file written");

	/* Cleanup: scrub the temp .ipb files we wrote. The PresetBank entries
	 * reference the Preset structs inlined in patches[], which we leak
	 * alongside pb — same shortcut other tests in this file use. */
	remove(path);
	remove(path2);
	free(pb);
	printf("PASS test_save_returns_exists\n");
	return 0;
}

/* Task 5: presetNameExists must report membership in the in-memory
 * PresetBank by name. Independent of the filesystem check inside
 * saveInstrumentAsPreset — this helper only inspects the bank. */
static int test_preset_name_exists(void) {
	PresetBank *pb = make_bank();
	ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");

	Preset p;
	make_preset(&p, 1);
	strncpy(p.name, "alpha", sizeof(p.name) - 1);
	p.name[sizeof(p.name) - 1] = '\0';
	addPresetToBank(pb, p);

	make_preset(&p, 2);
	strncpy(p.name, "beta", sizeof(p.name) - 1);
	p.name[sizeof(p.name) - 1] = '\0';
	addPresetToBank(pb, p);

	ASSERT_TRUE(presetNameExists(pb, "alpha") == true, "alpha present");
	ASSERT_TRUE(presetNameExists(pb, "beta") == true, "beta present");
	ASSERT_TRUE(presetNameExists(pb, "gamma") == false, "gamma absent");
	/* Prefix collision must NOT count — strncmp fixed-width compares the
	 * full 32-byte name slot, not just up to the first NUL. */
	ASSERT_TRUE(presetNameExists(pb, "alph") == false, "prefix is not a match");
	ASSERT_TRUE(presetNameExists(pb, "") == false, "empty name not present");

	free(pb);
	printf("PASS test_preset_name_exists\n");
	return 0;
}

/* Task 5: end-to-end — saveInstrumentAsPreset writes a file that
 * loadPresetFile can read back. Confirms the .name field round-trips
 * through saveInstrumentAsPreset → savePresetFile → loadPresetFile. */
static int test_save_instrument_roundtrip(void) {
	ensure_tmp_dirs();
	const char *dir = PRESET_DIR;
	const char *name = "fm_roundtrip";
	char clean[64];
	sanitizePresetFilename(name, clean, sizeof(clean));
	char path[512];
	snprintf(path, sizeof(path), "%s%s", dir, clean);
	remove(path);

	PresetBank *pb = make_bank();
	ASSERT_TRUE(pb != NULL, "calloc PresetBank failed");
	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_FM, NULL, pb);
	ASSERT_TRUE(inst != NULL, "init_instrument FM");

	/* tweak an FM operator so we can prove the data path copied the live
	 * instrument's param values (not just a default-FM preset). */
	setParameterBaseValue(inst->id.fm.ops[1]->ratio, 7.5f);

	PresetFileResult r = saveInstrumentAsPreset(inst, name, dir);
	ASSERT_EQ_MSG(r, PRESET_OK, "save ok");
	ASSERT_EQ_MSG(pb->presetCount, 1, "bank has one preset");

	PresetBank *pb2 = make_bank();
	ASSERT_TRUE(pb2 != NULL, "calloc PresetBank 2");
	ASSERT_EQ_MSG(loadPresetFile(path, pb2), PRESET_OK, "load ok");
	ASSERT_EQ_MSG(pb2->presetCount, 1, "loaded bank has one preset");
	ASSERT_TRUE(strcmp(pb2->patches[0].name, name) == 0, "name round-trips");
	ASSERT_EQ_MSG(pb2->patches[0].voiceType, VOICE_TYPE_FM, "voiceType FM");
	ASSERT_TRUE(pb2->patches[0].pd.fm.ops[1].ratio == 7.5f,
	            "tweaked op ratio round-trips");

	remove(path);
	free(pb);
	free(pb2);
	printf("PASS test_save_instrument_roundtrip\n");
	return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_save_preset_ok();
    failed |= test_preset_roundtrip();
    failed |= test_load_preset_missing();
    failed |= test_load_preset_bad_magic();
    failed |= test_load_preset_truncated();
    failed |= test_load_presets_from_directory();
    failed |= test_directory_list();
    failed |= test_preset_name_roundtrip();
    failed |= test_preset_v1_migration();
    failed |= test_preset_ship_dir_migration();
    failed |= test_save_sequencer_ok();
    failed |= test_sequencer_roundtrip();
    failed |= test_load_sequencer_missing();
    failed |= test_load_sequencer_bad_magic();
    failed |= test_load_sequencer_truncated();
    failed |= test_seq_channel_slots_roundtrip();
    failed |= test_seq_v1_loads_with_zero_slots();
    failed |= test_save_sequencer_seq2_magic();
    failed |= test_settings_roundtrip();
    failed |= test_settings_missing();
    failed |= test_sanitize_filename();
    failed |= test_save_returns_exists();
    failed |= test_preset_name_exists();
    failed |= test_save_instrument_roundtrip();

    printf("\n%s (%d tests, %s)\n",
           failed ? "FAILED" : "PASSED", 24, failed ? ">=1 failed" : "all passed");
    return failed;
}
