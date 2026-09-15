# Pattern-Sequencer Tracks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `MT_PATTERN` into four fixed per-channel pattern-sequence tracks that persist in the song file, are edited on the pattern screen (5 pages), and appear as one grouped row with four ROUTE buttons on the instrument screen.

**Architecture:** `Instrument.patternTracks[4]` is the runtime canonical store; `applyInstrumentPreset` always re-creates four core `MT_PATTERN` sources from it (so tracks survive preset load). `s1.sng` gains an optional `PTRK` chunk. The pattern screen gains pages `0=NOTES, 1..4=tracks`, each with an indicator + dial strip + 4×4 step grid navigated as one graph. The instrument mod-sources list collapses the four pattern sources into one grouped row.

**Tech Stack:** C (gnu99), meson/ninja, raylib, vendored kissfft, PortAudio. Tests: `tests/dsp/` (meson test), `src/tools/instrument_harness/` scripted fixtures.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-09-14-pattern-seq-tracks-design.md`.
- All `GuiNode` fields readable by a walk or generic dispatch must be zero-initialised in `initGuiNode` (rule #1154).
- Any mutation of `inst->modList`/`inst->paramList`/voice pools from the GUI thread must hold `g_audioLock` + set `inst->rebuilding` (rule #1058/#1065).
- No unconditional temp hooks in `main.c` (rule #1136).
- Test order: mod-system edge tests → voice/instrument integration → automated UI (rule #1063).
- Every `timeout N xvfb-run ... ./app` test invocation must be followed by the orphan sweep: `pkill -9 -x spectrax; pkill -9 -x instrument_harness; pkill -9 -f "Xvfb :"` (rule #1137).
- `docs/*` is gitignored; force-add plan/spec files (`git add -f`).
- Patterns are **song-level**: never serialised into a `.ipb` preset.

---

### Task A1: PatternTrackData + conversion helpers

**Files:**
- Modify: `src/modsystem.h`
- Modify: `src/modsystem.c`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: `PatternState`, `MAX_PATTERN_STEPS`, `PatternShape`, `PatternPolarity`, `getParameterValue`, `getParameterValueAsInt`, `setParameterBaseValue`.
- Produces:
  - `#define PATTERN_TRACKS 4`
  - `typedef struct { float steps[MAX_PATTERN_STEPS]; int length; int shape; float slew; int polarity; } PatternTrackData;`
  - `void initPatternTrackData(PatternTrackData *t);`
  - `void patternStateToTrackData(const PatternState *p, PatternTrackData *t);`
  - `void patternTrackDataToState(PatternState *p, const PatternTrackData *t);`
  - `PatternState` gains `int track;`

- [ ] **Step 1: Write the failing tests**

Add to `tests/dsp/test_modsystem.c` (near `test_pattern_shapes`):

```c
static int test_pattern_track_data_roundtrip(void) {
	PatternTrackData t;
	initPatternTrackData(&t);
	ASSERT_EQ(t.length, MAX_PATTERN_STEPS, "default length 16");
	ASSERT_EQ(t.shape, SH_HOLD, "default shape HOLD");
	ASSERT_EQ(t.polarity, PP_UNIPOLAR, "default polarity unipolar");
	ASSERT_NEAR(t.steps[0], 0.5f, 0.001f, "default step 0.5");

	ParamList *pl = createParamList();
	Mod *p = (Mod *)calloc(1, sizeof(Mod));
	initPatternDefaults(p, pl, 2);
	p->data.pattern.steps[0] = 0.9f;
	p->data.pattern.steps[3] = 0.1f;
	p->data.pattern.track = 2;
	setParameterBaseValue(p->data.pattern.length, 4.0f);
	setParameterBaseValue(p->data.pattern.shape, (float)SH_CURVE);
	setParameterBaseValue(p->data.pattern.slew, 0.7f);
	setParameterBaseValue(p->data.pattern.polarity, (float)PP_BIPOLAR);

	PatternTrackData out;
	patternStateToTrackData(&p->data.pattern, &out);
	ASSERT_EQ(out.length, 4, "captured length");
	ASSERT_EQ(out.shape, SH_CURVE, "captured shape");
	ASSERT_NEAR(out.slew, 0.7f, 0.001f, "captured slew");
	ASSERT_EQ(out.polarity, PP_BIPOLAR, "captured polarity");
	ASSERT_NEAR(out.steps[0], 0.9f, 0.001f, "captured step 0");

	/* wipe the source, then restore from the captured data */
	p->data.pattern.steps[0] = 0.0f;
	setParameterBaseValue(p->data.pattern.length, 16.0f);
	setParameterBaseValue(p->data.pattern.shape, (float)SH_HOLD);
	patternTrackDataToState(&p->data.pattern, &out);
	ASSERT_EQ(p->data.pattern.stepCount, 4, "restored stepCount");
	ASSERT_NEAR(p->data.pattern.steps[0], 0.9f, 0.001f, "restored step 0");
	ASSERT_EQ(getParameterValueAsInt(p->data.pattern.length), 4, "restored length param");
	ASSERT_EQ(getParameterValueAsInt(p->data.pattern.shape), SH_CURVE, "restored shape param");
	ASSERT_NEAR(getParameterValue(p->data.pattern.slew), 0.7f, 0.001f, "restored slew param");
	ASSERT_EQ(getParameterValueAsInt(p->data.pattern.polarity), PP_BIPOLAR, "restored polarity param");

	freeParamList(pl);
	free(p);
	printf("PASS test_pattern_track_data_roundtrip\n");
	return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build test_modsystem`
Expected: FAIL to compile — `PatternTrackData` / `initPatternTrackData` undefined.

- [ ] **Step 3: Implement**

In `src/modsystem.h`, before `typedef struct Mod` (after the `PatternState` definition), add:

```c
#define PATTERN_TRACKS 4

/* Persistent form of one pattern track (the Instrument.patternTracks[]
 * and song-file payload). PatternState holds the live Parameter pointers
 * + running state; this is the plain-data mirror. */
typedef struct {
	float steps[MAX_PATTERN_STEPS]; /* stored 0..1 */
	int length;                     /* 1..MAX_PATTERN_STEPS */
	int shape;                      /* PatternShape */
	float slew;                     /* 0..1 */
	int polarity;                   /* PatternPolarity */
} PatternTrackData;
```

Add the function declarations near `generatePattern`:

```c
void initPatternTrackData(PatternTrackData *t);
void patternStateToTrackData(const PatternState *p, PatternTrackData *t);
void patternTrackDataToState(PatternState *p, const PatternTrackData *t);
```

Add `int track;` to `PatternState` (after `int channel;`):

```c
	int channel;          /* which channel's playhead clocks this source */
	int track;            /* 0..PATTERN_TRACKS-1 (which UI track this is) */
```

In `src/modsystem.c`, add the three functions near `initPatternDefaults`:

```c
void initPatternTrackData(PatternTrackData *t) {
	if(!t) {
		return;
	}
	for(int i = 0; i < MAX_PATTERN_STEPS; i++) {
		t->steps[i] = 0.5f;
	}
	t->length = MAX_PATTERN_STEPS;
	t->shape = SH_HOLD;
	t->slew = 0.2f;
	t->polarity = PP_UNIPOLAR;
}

void patternStateToTrackData(const PatternState *p, PatternTrackData *t) {
	if(!p || !t) {
		return;
	}
	memcpy(t->steps, p->steps, sizeof(t->steps));
	t->length = (p->length) ? getParameterValueAsInt(p->length) : p->stepCount;
	t->shape = (p->shape) ? getParameterValueAsInt(p->shape) : SH_HOLD;
	t->slew = (p->slew) ? getParameterValue(p->slew) : 0.2f;
	t->polarity = (p->polarity) ? getParameterValueAsInt(p->polarity) : PP_UNIPOLAR;
}

void patternTrackDataToState(PatternState *p, const PatternTrackData *t) {
	if(!p || !t) {
		return;
	}
	memcpy(p->steps, t->steps, sizeof(p->steps));
	int len = t->length;
	if(len < 1) {
		len = 1;
	}
	if(len > MAX_PATTERN_STEPS) {
		len = MAX_PATTERN_STEPS;
	}
	p->stepCount = len;
	if(p->length) setParameterBaseValue(p->length, (float)len);
	if(p->shape) setParameterBaseValue(p->shape, (float)t->shape);
	if(p->slew) setParameterBaseValue(p->slew, t->slew);
	if(p->polarity) setParameterBaseValue(p->polarity, (float)t->polarity);
}
```

In `cloneMod`'s `MT_PATTERN` case (`src/modsystem.c`), add after `p->channel = sp->channel;`:

```c
			p->track = sp->track;
```

- [ ] **Step 4: Register the test in `main()` and run**

In `tests/dsp/test_modsystem.c` `main()`, after `fails += test_pattern_shapes();` add:

```c
    fails += test_pattern_track_data_roundtrip();
```

Run: `meson test -C build test_modsystem`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.h src/modsystem.c tests/dsp/test_modsystem.c
git commit -m "feat(mod): PatternTrackData + state<->track conversion helpers"
```

---

### Task A2: MAX_MOD_SOURCES + Instrument.patternTracks + core reseed

**Files:**
- Modify: `src/voice.h`
- Modify: `src/voice.c`
- Modify: `src/gui_inst_mod.c`
- Modify: `src/gui.h`
- Test: `tests/dsp/test_mod_voice.c`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: `PatternTrackData`, `createPattern`, `modSourceCount`, `modIndexAt`, `patternTrackDataToState`, `patternStateToTrackData`.
- Produces:
  - `#define MAX_MOD_SOURCES 16` (voice.h); `Instrument.envelopes[MAX_MOD_SOURCES]`; `Instrument.patternTracks[PATTERN_TRACKS]`.
  - `void applyPatternTracksToInstrument(Instrument *inst);` (voice.c)
  - `void capturePatternTracksToInstrument(Instrument *inst);` (voice.c)
  - `void syncPatternTracksToState(Instrument *inst);` (voice.c)
  - `void capturePatternTrackSet(VoiceManager *vm, PatternTrackSet *out);` (voice.c)
  - `void applyPatternTrackSetToInstruments(VoiceManager *vm, const PatternTrackSet *in);` (voice.c)

- [ ] **Step 1: Write the failing tests**

Add to `tests/dsp/test_mod_voice.c` (near `test_add_runtime_pattern`), and **delete** `test_add_runtime_pattern` and its `main()` registration (the runtime-pattern path is removed by this task):

```c
static int test_pattern_tracks_core_and_reseed(void) {
	SamplePool *sp = createSamplePool();
	PresetBank pb;
	initPresetBank(&pb);
	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);
	ASSERT_TRUE(inst != NULL, "init_instrument FM");

	/* four pattern sources exist and are core */
	ASSERT_EQ(modSourceCount(inst->modList), 8, "4 AD + 4 pattern sources");
	ASSERT_TRUE(inst->coreEnvelopeCount == 8, "patterns are core");
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		ASSERT_TRUE(inst->patternTracks[t].length == MAX_PATTERN_STEPS, "default track length");
	}
	/* the last four sources are MT_PATTERN, ordered by track */
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		int mi = modIndexAt(inst->modList, 4 + t);
		ASSERT_TRUE(mi >= 0, "pattern source present");
		ASSERT_TRUE(inst->modList->mods[mi]->type == MT_PATTERN, "source is MT_PATTERN");
		ASSERT_EQ(inst->modList->mods[mi]->data.pattern.track, t, "track index set");
	}

	/* edit a track, capture, wipe, reseed */
	inst->modList->mods[modIndexAt(inst->modList, 4)]->data.pattern.steps[0] = 0.9f;
	capturePatternTracksToInstrument(inst);
	ASSERT_NEAR(inst->patternTracks[0].steps[0], 0.9f, 0.001f, "captured into patternTracks");

	/* re-apply wipes the source step back from patternTracks */
	inst->modList->mods[modIndexAt(inst->modList, 4)]->data.pattern.steps[0] = 0.0f;
	syncPatternTracksToState(inst);
	ASSERT_NEAR(inst->modList->mods[modIndexAt(inst->modList, 4)]->data.pattern.steps[0], 0.9f, 0.001f,
	            "synced from patternTracks");

	free(inst);
	freeSamplePool(sp);
	printf("PASS test_pattern_tracks_core_and_reseed\n");
	return 0;
}

static int test_pattern_tracks_survive_preset(void) {
	SamplePool *sp = createSamplePool();
	PresetBank pb;
	initPresetBank(&pb);
	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);

	/* user edits track 2 */
	int mi = modIndexAt(inst->modList, 6);
	ASSERT_TRUE(mi >= 0, "track 2 source present");
	inst->modList->mods[mi]->data.pattern.steps[5] = 0.77f;
	capturePatternTracksToInstrument(inst);

	/* preset load clears + rebuilds the list */
	Preset p = presetFromInstrument(inst);
	applyInstrumentPreset(inst, p);

	ASSERT_EQ(modSourceCount(inst->modList), 8, "patterns re-created after load");
	mi = modIndexAt(inst->modList, 6);
	ASSERT_TRUE(mi >= 0, "track 2 source re-created");
	ASSERT_NEAR(inst->modList->mods[mi]->data.pattern.steps[5], 0.77f, 0.001f,
	            "track 2 step survived preset load");

	/* preset never carries patterns */
	ASSERT_TRUE(p.modSettingsCount <= MAX_ENVELOPES + MAX_LFOS, "modSettings bounded");
	for(int i = 0; i < p.modSettingsCount; i++) {
		ASSERT_TRUE(p.modSettings[i].type != MT_PATTERN, "preset excludes MT_PATTERN");
	}

	free(inst);
	freeSamplePool(sp);
	printf("PASS test_pattern_tracks_survive_preset\n");
	return 0;
}
```

Register both in `main()` (replace the removed `fails += test_add_runtime_pattern();`):

```c
    /* Pattern tracks — fixed core sources, song-level persistence */
    fails += test_pattern_tracks_core_and_reseed();
    fails += test_pattern_tracks_survive_preset();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C build test_mod_voice`
Expected: FAIL to compile — `patternTracks`, `applyPatternTracksToInstrument`, etc. undefined.

- [ ] **Step 3: Implement**

In `src/voice.h`:

```c
#define MAX_LFOS 8
#define MAX_ENVELOPES 6
#define MAX_MOD_SOURCES 16
```

Change the Instrument array:

```c
	Mod *envelopes[MAX_MOD_SOURCES];
```

Add to `Instrument` (near `modList`/`paramList`):

```c
	/* Four fixed pattern-sequence tracks (song-level, not preset-level).
	 * Never cleared by applyInstrumentPreset: the four core MT_PATTERN
	 * sources are re-created from this store on every rebuild. */
	PatternTrackData patternTracks[PATTERN_TRACKS];
```

In `src/voice.c`, add `#include "io/sequencer_io.h"` is NOT needed here — `PatternTrackSet` is defined in `modsystem.h`. Add the helpers after `initVoiceDrivers` (which is forward-declared above `applyInstrumentPreset`; place the pattern helpers BEFORE `applyInstrumentPreset` so it can call them, or forward-declare them next to `initVoiceDrivers`):

```c
/* Create the four core MT_PATTERN sources from inst->patternTracks.
 * Called from init_instrument + applyInstrumentPreset, so tracks survive
 * every mod-list rebuild. */
static void seedPatternTracks(Instrument *inst) {
	if(!inst || !inst->modList || !inst->paramList) {
		return;
	}
	int channel = (inst->metaChannel >= 0) ? inst->metaChannel : 0;
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		char name[MAX_NAME_LEN];
		snprintf(name, sizeof(name), "PTN%d", t + 1);
		Mod *p = createPattern(inst->paramList, inst->modList, channel, name);
		if(!p) {
			continue;
		}
		p->data.pattern.track = t;
		patternTrackDataToState(&p->data.pattern, &inst->patternTracks[t]);
	}
}

void applyPatternTracksToInstrument(Instrument *inst) {
	seedPatternTracks(inst);
}

void capturePatternTracksToInstrument(Instrument *inst) {
	if(!inst || !inst->modList) {
		return;
	}
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		int mi = modIndexAt(inst->modList, inst->coreEnvelopeCount - PATTERN_TRACKS + t);
		if(mi < 0) {
			continue;
		}
		Mod *m = inst->modList->mods[mi];
		if(m && m->type == MT_PATTERN && m->data.pattern.track == t) {
			patternStateToTrackData(&m->data.pattern, &inst->patternTracks[t]);
		}
	}
}

void syncPatternTracksToState(Instrument *inst) {
	if(!inst || !inst->modList) {
		return;
	}
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		int mi = modIndexAt(inst->modList, inst->coreEnvelopeCount - PATTERN_TRACKS + t);
		if(mi < 0) {
			continue;
		}
		Mod *m = inst->modList->mods[mi];
		if(m && m->type == MT_PATTERN) {
			patternTrackDataToState(&m->data.pattern, &inst->patternTracks[t]);
		}
	}
}

void capturePatternTrackSet(VoiceManager *vm, PatternTrackSet *out) {
	if(!vm || !out) {
		return;
	}
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		for(int t = 0; t < PATTERN_TRACKS; t++) {
			if(vm->instruments[ch]) {
				out->track[ch][t] = vm->instruments[ch]->patternTracks[t];
			}
		}
	}
}

void applyPatternTrackSetToInstruments(VoiceManager *vm, const PatternTrackSet *in) {
	if(!vm || !in) {
		return;
	}
	for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
		Instrument *inst = vm->instruments[ch];
		if(!inst) {
			continue;
		}
		for(int t = 0; t < PATTERN_TRACKS; t++) {
			inst->patternTracks[t] = in->track[ch][t];
		}
		syncPatternTracksToState(inst);
	}
}
```

In `init_instrument` (`src/voice.c`), replace the core-count block:

```c
	(*instrument)->voiceType = vt;
	(*instrument)->coreEnvelopeCount = (*instrument)->envelopeCount;
	initVoiceDrivers(*instrument);
```

with:

```c
	(*instrument)->voiceType = vt;
	for(int t = 0; t < PATTERN_TRACKS; t++) {
		initPatternTrackData(&(*instrument)->patternTracks[t]);
	}
	seedPatternTracks(*instrument);
	(*instrument)->coreEnvelopeCount = modSourceCount((*instrument)->modList);
	initVoiceDrivers(*instrument);
```

In `applyInstrumentPreset` (`src/voice.c`), before the existing `instrument->coreEnvelopeCount = modSourceCount(instrument->modList);` line, insert:

```c
	/* Patterns are song-level: re-create the four core tracks from the
	 * persistent store (never from the preset). */
	seedPatternTracks(instrument);
```

In `presetFromInstrument` (`src/voice.c`), replace the `n`/loop block:

```c
	int n = instrument->modList ? modSourceCount(instrument->modList) : 0;
	if(n > MAX_ENVELOPES + MAX_LFOS) {
		n = MAX_ENVELOPES + MAX_LFOS;
	}
	p.modSettingsCount = n;
	for(int i = 0; i < n; i++) {
		int mi = modIndexAt(instrument->modList, i);
		if(mi < 0) {
			break;
		}
		Mod *mod = instrument->modList->mods[mi];
		p.modSettings[i].type = mod->type;
		switch(mod->type) {
			case MT_ENV:
				saveEnvPreset(&p.modSettings[i].md.env, mod);
				break;
			case MT_LFO:
				saveLfoPreset(&p.modSettings[i].md.lfo, mod);
				break;
			case MT_RND:
				saveRandPreset(&p.modSettings[i].md.rand, mod);
				break;
			default:
				break;
		}
	}
```

with:

```c
	/* Patterns are song-level, so they are excluded from the patch:
	 * compact every non-MT_PATTERN source into modSettings. */
	int n = instrument->modList ? modSourceCount(instrument->modList) : 0;
	p.modSettingsCount = 0;
	for(int i = 0; i < n; i++) {
		if(p.modSettingsCount >= MAX_ENVELOPES + MAX_LFOS) {
			break;
		}
		int mi = modIndexAt(instrument->modList, i);
		if(mi < 0) {
			break;
		}
		Mod *mod = instrument->modList->mods[mi];
		if(mod->type == MT_PATTERN) {
			continue;
		}
		ModPreset *mp = &p.modSettings[p.modSettingsCount];
		mp->type = mod->type;
		switch(mod->type) {
			case MT_ENV:
				saveEnvPreset(&mp->md.env, mod);
				break;
			case MT_LFO:
				saveLfoPreset(&mp->md.lfo, mod);
				break;
			case MT_RND:
				saveRandPreset(&mp->md.rand, mod);
				break;
			default:
				break;
		}
		p.modSettingsCount++;
	}
```

In `src/gui_inst_mod.c`, change `g_sourceCtx[MAX_ENVELOPES]` to `g_sourceCtx[MAX_MOD_SOURCES]`, and the `modSourceCount(...) >= MAX_ENVELOPES` guards in `addRuntimeSource` and `addRuntimePattern` to `MAX_MOD_SOURCES`. Delete `addRuntimePattern` entirely and remove its declaration from `src/gui.h:321`.

- [ ] **Step 4: Run tests + build**

Run: `meson test -C build test_mod_voice test_modsystem`
Expected: PASS. Some pre-existing tests may assert `envelopeCount` counts that now include patterns; update those assertions to the new totals (FM core = 8 sources: 4 preset sources + 4 patterns).

- [ ] **Step 5: Commit**

```bash
git add src/voice.h src/voice.c src/gui_inst_mod.c src/gui.h tests/dsp/test_mod_voice.c tests/dsp/test_modsystem.c
git commit -m "feat(voice): fixed core pattern tracks from Instrument.patternTracks; exclude patterns from presets"
```

---

### Task B1: `PTRK` song-file chunk

**Files:**
- Modify: `src/io.h`
- Modify: `src/io/sequencer_io.h`
- Modify: `src/io/sequencer_io.c`
- Modify: `src/main.c`
- Modify: `src/tools/instrument_harness/instrument_harness.c`
- Test: `tests/dsp/test_io.c`
- Test: `tests/dsp/test_voice.c`

**Interfaces:**
- Consumes: `PatternTrackSet`, `PatternTrackData`, `initPatternTrackData`.
- Produces:
  - `#define PATTERN_TRACKS_SECTION "PTRK"` (io.h)
  - `SequencerFileResult saveSequencerState(const char *filename, Arranger *arranger, PatternList *patterns, const PatternTrackSet *tracks);`
  - `SequencerFileResult loadSequencerState(const char *filename, Arranger *arranger, PatternList *patterns, PatternTrackSet *tracks);`

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_io.c`:

```c
static int test_sequencer_pattern_tracks_roundtrip(void) {
    ensure_tmp_dirs();
    const char *path = TMP_DIR "test_io_ptrk.sng";
    remove(path);

    SeqEnv e;
    ASSERT_EQ(make_seq_env(&e, 120.0f, 1), 0);

    PatternTrackSet src;
    memset(&src, 0, sizeof(src));
    for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
        for(int t = 0; t < PATTERN_TRACKS; t++) {
            initPatternTrackData(&src.track[ch][t]);
            src.track[ch][t].steps[t] = 0.25f * (float)(t + 1);
            src.track[ch][t].length = 8 + t;
            src.track[ch][t].shape = (t % 2) ? SH_LINEAR : SH_HOLD;
        }
    }
    ASSERT_EQ(saveSequencerState(path, e.arranger, e.patterns, &src), SEQ_OK);

    PatternTrackSet dst;
    memset(&dst, 0, sizeof(dst));
    for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
        for(int t = 0; t < PATTERN_TRACKS; t++) {
            initPatternTrackData(&dst.track[ch][t]);
        }
    }
    ASSERT_EQ(loadSequencerState(path, e.arranger, e.patterns, &dst), SEQ_OK);
    for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch += 5) {
        for(int t = 0; t < PATTERN_TRACKS; t++) {
            ASSERT_EQ(dst.track[ch][t].length, src.track[ch][t].length);
            ASSERT_EQ(dst.track[ch][t].shape, src.track[ch][t].shape);
            ASSERT_EQ_MSG((int)(dst.track[ch][t].steps[t] * 100.0f),
                          (int)(src.track[ch][t].steps[t] * 100.0f), "step value");
        }
    }

    /* NULL tracks: no PTRK is written; a later load must leave the
     * caller's pre-filled store untouched (defaults survive). */
    ASSERT_EQ(saveSequencerState(path, e.arranger, e.patterns, NULL), SEQ_OK);
    PatternTrackSet keep;
    memset(&keep, 0, sizeof(keep));
    for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
        for(int t = 0; t < PATTERN_TRACKS; t++) {
            initPatternTrackData(&keep.track[ch][t]);
        }
    }
    ASSERT_EQ(loadSequencerState(path, e.arranger, e.patterns, &keep), SEQ_OK);
    ASSERT_EQ(keep.track[3][2].length, MAX_PATTERN_STEPS, "absent PTRK leaves store untouched");

    remove(path);
    printf("PASS test_sequencer_pattern_tracks_roundtrip\n");
    return 0;
}
```

Register it in `test_io.c`'s `main()` next to the other sequencer tests.

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build test_io`
Expected: FAIL to compile — 4-arg signatures undefined.

- [ ] **Step 3: Implement**

In `src/io.h`:
- Add `#define PATTERN_TRACKS_SECTION "PTRK"` after `CHIP_LABELS_SECTION`.
- Change the two `SequencerFileResult` declarations to the 4-arg forms.

In `src/io/sequencer_io.h` and `src/io.h`, mirror the change.

In `src/io/sequencer_io.c` `saveSequencerState`, after the LABL chunk write, add:

```c
	/* PTRK: per-channel pattern-sequence tracks. Optional — skipped when
	 * the caller passes NULL (arranger-only saves + legacy tests). */
	if(tracks != NULL) {
		if(!writeChunkHeader(file, PATTERN_TRACKS_SECTION)) {
			fclose(file);
			return SEQ_ERROR_WRITE;
		}
		if(fwrite(tracks->track, sizeof(tracks->track), 1, file) != 1) {
			fclose(file);
			return SEQ_ERROR_WRITE;
		}
	}
```

In `loadSequencerState`, after the LABL handling block (still inside `if(is_v2)`), add a peek for PTRK:

```c
		/* PTRK (pattern tracks) is OPTIONAL — older V2 files predate it.
		 * Peek the magic; on match read the body, else rewind so any
		 * future trailing chunk still reads from the right offset.
		 * A NULL destination skips the body (stream stays in sync). */
		long ptrkPos = ftell(file);
		char ptrk_magic[4];
		if(fread(ptrk_magic, 1, 4, file) == 4 && memcmp(ptrk_magic, PATTERN_TRACKS_SECTION, 4) == 0) {
			long ptrkBytes = (long)(sizeof(PatternTrackData) * MAX_SEQUENCER_CHANNELS * PATTERN_TRACKS);
			if(tracks != NULL) {
				if(fread(tracks->track, sizeof(tracks->track), 1, file) != 1) {
					fclose(file);
					printf("error reading pattern tracks (PTRK)\n");
					return SEQ_ERROR_READ;
				}
			} else {
				fseek(file, ptrkBytes, SEEK_CUR);
			}
		} else {
			fseek(file, ptrkPos, SEEK_SET);
		}
```

Then update **all** call sites to pass a 4th argument:
- `src/main.c:1037` (`save`): `NULL` for now (Task B2 wires the real set).
- `src/main.c:1147` (`load`): `NULL` for now.
- `src/tools/instrument_harness/instrument_harness.c`: any calls (search `SequencerState`).
- `tests/dsp/test_io.c`, `tests/dsp/test_voice.c`: pass `NULL`.

- [ ] **Step 4: Run tests**

Run: `meson test -C build test_io test_voice`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/io.h src/io/sequencer_io.h src/io/sequencer_io.c src/main.c src/tools/instrument_harness/instrument_harness.c tests/dsp/test_io.c tests/dsp/test_voice.c
git commit -m "feat(io): optional PTRK song chunk for per-channel pattern tracks"
```

---

### Task B2: Wire pattern tracks through save/load

**Files:**
- Modify: `src/main.c`
- Modify: `src/tools/instrument_harness/instrument_harness.c`

**Interfaces:**
- Consumes: `capturePatternTrackSet`, `applyPatternTrackSetToInstruments`, `capturePatternTracksToInstrument`, the 4-arg I/O functions.
- Produces: end-to-end track persistence.

- [ ] **Step 1: Implement save wiring in `src/main.c`**

At the save site (`saveSequencerState("s1.sng", ...)` around line 1037), before the call:

```c
			for(int ch = 0; ch < data.voiceManager->enabledChannels; ch++) {
				capturePatternTracksToInstrument(data.voiceManager->instruments[ch]);
			}
			PatternTrackSet trackSet;
			capturePatternTrackSet(data.voiceManager, &trackSet);
			int saveResult = saveSequencerState("s1.sng", data.arranger, data.patternList, &trackSet);
```

(Replace the existing `int saveResult = saveSequencerState("s1.sng", data.arranger, data.patternList);` line.)

- [ ] **Step 2: Implement load wiring**

At the load site (around line 1147), after `loadSequencerState` succeeds:

```c
	PatternTrackSet trackSet;
	memset(&trackSet, 0xFF, sizeof(trackSet));
	int loadstate = loadSequencerState("s1.sng", data->arranger, data->patternList, &trackSet);
	if(loadstate == SEQ_OK) {
		applyPatternTrackSetToInstruments(data->voiceManager, &trackSet);
	}
```

> The `0xFF` fill makes "PTRK absent" detectable if needed; the loader leaves the store untouched when the chunk is missing, and `applyPatternTrackSetToInstruments` then writes garbage. To avoid that, pre-fill the set from the live instruments before loading (so an absent chunk keeps the current values):
> ```c
> PatternTrackSet trackSet;
> capturePatternTrackSet(data->voiceManager, &trackSet);
> int loadstate = loadSequencerState("s1.sng", data->arranger, data->patternList, &trackSet);
> if(loadstate == SEQ_OK) { applyPatternTrackSetToInstruments(data->voiceManager, &trackSet); }
> ```

- [ ] **Step 3: Mirror in the harness**

Find the harness's save/load calls (`grep -n "SequencerState" src/tools/instrument_harness/instrument_harness.c`) and apply the same pattern. The harness boots via `initApplication`, so `data.voiceManager` is available.

- [ ] **Step 4: Build + run**

Run: `ninja -C build && meson test -C build`
Expected: clean build, all tests green.

- [ ] **Step 5: Commit**

```bash
git add src/main.c src/tools/instrument_harness/instrument_harness.c
git commit -m "feat(app): persist pattern tracks through the song save/load path"
```

---

### Task C1: `patternPage` application state

**Files:**
- Modify: `src/appstate.h`
- Modify: `src/appstate.c`
- Modify: `src/gui.h`
- Modify: `src/gui_pattern.c`

**Interfaces:**
- Produces:
  - `ApplicationState.patternPage` (int, 0..PATTERN_TRACKS).
  - `void clampPatternPage(ApplicationState *as);` (appstate.c)
  - `void setPatternPage(int page);` + `int getPatternPage(void);` (gui_pattern.c / gui.h)

- [ ] **Step 1: Implement state**

In `src/appstate.h`, add `int patternPage;` to `ApplicationState` and declare `void clampPatternPage(ApplicationState *as);`.

In `src/appstate.c` `createApplicationState`, set `as->patternPage = 0;`. Implement:

```c
void clampPatternPage(ApplicationState *as) {
	if(!as) {
		return;
	}
	if(as->patternPage < 0) {
		as->patternPage = 0;
	}
	if(as->patternPage > PATTERN_TRACKS) {
		as->patternPage = PATTERN_TRACKS;
	}
	setPatternPage(as->patternPage);
}
```

`setSelectedPattern` calls `clampPatternPage(as);` before `rebuildPatternGraph();`.

- [ ] **Step 2: Implement the gui_pattern static**

In `src/gui_pattern.c` add:

```c
static int patternPage = 0;
void setPatternPage(int page) { patternPage = page; }
int getPatternPage(void) { return patternPage; }
```

Declare both in `src/gui.h` near `rebuildPatternGraph`.

- [ ] **Step 3: Build**

Run: `ninja -C build`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/appstate.h src/appstate.c src/gui.h src/gui_pattern.c
git commit -m "feat(pattern): patternPage state + clamping"
```

---

### Task C2: Pattern-screen track pages (indicator + dial strip + grid)

**Files:**
- Modify: `src/gui_pattern.c`
- Modify: `src/gui.h`

**Interfaces:**
- Consumes: `patternPage`, `getSelectedInstInstrument()`, `PatternState`, `createDialGuiNode`, `incParameterBaseValue`, existing `StepNodeData`/`stepNodes` machinery.
- Produces:
  - `createPatternGraph` builds a track page when `patternPage > 0`.
  - `navigatePatternGraph` extended for track pages.
  - `bool handlePatternTrackEdit(int keymapping);` (gui_pattern.c) — called by main.c while EDIT is held on a track page.
  - `int patternPageIndicatorsDrawn(void);` optional test hook.

- [ ] **Step 1: Implement track-page graph**

In `src/gui_pattern.c`, extend `createPatternGraph`: keep the note branch exactly as-is when `patternPage == 0`; otherwise build a track page:

```c
	if(patternPage > 0) {
		Instrument *inst = getSelectedInstInstrument();
		Mod *pat = NULL;
		if(inst && inst->modList && patternPage - 1 < PATTERN_TRACKS) {
			int mi = modIndexAt(inst->modList, inst->coreEnvelopeCount - PATTERN_TRACKS + (patternPage - 1));
			if(mi >= 0) {
				pat = inst->modList->mods[mi];
			}
		}
		/* indicator strip */
		GuiNode *indicator = createGuiNode(10, 10, 620, 40, 4, na_horizontal, "PTN_IND", 0, 0);
		indicator->drawable = true;
		indicator->draw = drawPatternIndicatorNode; /* new: renders page label + readout */
		appendItem(patternGraph->root, indicator, 2);
		if(!pat || pat->type != MT_PATTERN) {
			return;
		}
		PatternState *ps = &pat->data.pattern;
		GuiNode *strip = createGuiNode(10, 60, 620, 60, 6, na_horizontal, "PTN_DIALS", 0, 0);
		appendItem(strip, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEN", 1, incParameterBaseValue, ps->length), 1);
		appendItem(strip, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SHAPE", 0, incParameterBaseValue, ps->shape), 1);
		appendItem(strip, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SLEW", 0, incParameterBaseValue, ps->slew), 1);
		appendItem(strip, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "POL", 0, incParameterBaseValue, ps->polarity), 1);
		appendItem(patternGraph->root, strip, 6);
		/* step grid: 4 rows x 4 cells, same cell helper family as the note page */
		GuiNode *gridWrap = createGuiNode(10, 130, 230, 300, 6, na_vertical, "PTN_GRID", 0, 0);
		for(int r = 0; r < 4; r++) {
			GuiNode *row = createGuiNode(0, 0, 100, 50, 4, na_horizontal, "row", 0, 0);
			for(int c = 0; c < 4; c++) {
				int i = r * 4 + c;
				appendItem(row, createTrackStepNode(ps, inst, i, selectedStep), 1);
			}
			appendItem(gridWrap, row, 1);
		}
		appendItem(patternGraph->root, gridWrap, 16);
		/* initial selection: dial strip's LEN */
		patternGraph->selected = strip->items->head ? *(GuiNode **)strip->items->head->data : NULL;
		if(patternGraph->selected) {
			patternGraph->selected->selected = 1;
		}
		return;
	}
```

Add a `TrackStepNode` (a selectable cell drawing value as a bar and handling `EDIT+UP/DOWN`). Model it on the existing `StepNodeData`/`drawStepGuiNode` in `gui_pattern.c`. Add these module-private pieces (concrete bodies; tune the cell geometry to the note page's look):

```c
static GuiNode *trackDialStrip = NULL;
static GuiNode *trackDialNodes[4] = {0};
static GuiNode *trackStepNodes[16] = {0};

static bool isDialStripSelected(const GuiNode *n) {
	if(!n || !trackDialStrip) {
		return false;
	}
	for(int i = 0; i < 4; i++) {
		if(n == trackDialNodes[i]) {
			return true;
		}
	}
	return false;
}

static GuiNode *firstDialNode(void) {
	return trackDialNodes[0];
}

static int selectedStepIndex(void) {
	if(patternSelectedStepPtr) {
		int s = *patternSelectedStepPtr;
		if(s < 0) s = 0;
		if(s > 15) s = 15;
		return s;
	}
	return 0;
}

static void drawTrackStepNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	TrackStepData *d = (TrackStepData *)gn->p;
	if(!d || !d->pattern) {
		return;
	}
	Rectangle cell = (Rectangle){ gn->x, gn->y, gn->w, gn->h };
	if(*d->selectedStepPtr == d->stepIndex) {
		DrawRectangle(gn->x - 3, gn->y - 3, gn->w + 6, gn->h + 6, cs.outlineColour);
	}
	bool inRange = (d->stepIndex < d->pattern->stepCount);
	float frac = inRange ? d->pattern->steps[d->stepIndex] : 0.0f;
	DrawRectangleRec(cell, cs.defaultCell);
	DrawRectangle((int)gn->x, (int)(gn->y + gn->h - frac * gn->h), gn->w, (int)(frac * gn->h), cs.highlightedCell);
	char buf[8];
	snprintf(buf, sizeof(buf), "%d", (int)(frac * 100.0f));
	DrawText(buf, gn->x + 2, gn->y + 2, 10, cs.fontColour);
}

static TrackStepData trackStepData[16];

static GuiNode *createTrackStepNode(PatternState *pattern, Instrument *inst, int stepIndex, int *selectedStepPtr) {
	GuiNode *n = createGuiNode(0, 0, 50, 50, 4, na_vertical, "STEP", 1, 0);
	trackStepData[stepIndex].stepIndex = stepIndex;
	trackStepData[stepIndex].pattern = pattern;
	trackStepData[stepIndex].inst = inst;
	trackStepData[stepIndex].selectedStepPtr = selectedStepPtr;
	n->p = (Parameter *)&trackStepData[stepIndex];
	n->drawable = true;
	n->draw = drawTrackStepNode;
	trackStepNodes[stepIndex] = n;
	return n;
}
```

`TrackStepData` is:

```c
typedef struct {
	int stepIndex;
	PatternState *pattern;
	Instrument *inst;
	int *selectedStepPtr;
} TrackStepData;
```

Set `trackDialStrip = strip;` and `trackDialNodes[i] = <the i-th dial>` after the strip is built (capture the `createDialGuiNode` returns into locals first).

- [ ] **Step 2: Implement navigation**

Extend `navigatePatternGraph`: when `patternPage > 0`, use a separate branch:

```c
	if(patternPage > 0) {
		if(!patternGraph || !patternGraph->selected) return;
		/* if the cursor is in the dial strip, LEFT/RIGHT move dials; DOWN returns to the grid */
		if(isDialStripSelected(patternGraph->selected)) {
			switch(keymapping) {
				case KM_LEFT:  selectAdjacent(patternGraph, patternGraph->selected, true);  return;
				case KM_RIGHT: selectAdjacent(patternGraph, patternGraph->selected, false); return;
				case KM_DOWN:  changeGraphSelection(patternGraph, trackStepNodes[selectedStepIndex()]); return;
				default: return;
			}
		}
		/* on the grid: 4-per-row movement; UP from row 0 goes to the dial strip */
		TrackStepData *d = (TrackStepData *)patternGraph->selected->p;
		int cur = *d->selectedStepPtr;
		int next = cur;
		switch(keymapping) {
			case KM_LEFT:  if(cur % 4 > 0) next = cur - 1; break;
			case KM_RIGHT: if(cur % 4 < 3) next = cur + 1; break;
			case KM_UP:    if(cur < 4) { changeGraphSelection(patternGraph, firstDialNode()); return; } next = cur - 4; break;
			case KM_DOWN:  if(cur < 12) next = cur + 4; break;
			default: return;
		}
		*d->selectedStepPtr = next;
		changeGraphSelection(patternGraph, trackStepNodes[next]);
		return;
	}
```

Implement small helpers `isDialStripSelected`/`firstDialNode` by storing the dial strip node in a module static (`static GuiNode *trackDialStrip;` and an array `trackDialNodes[4]`).

- [ ] **Step 3: Implement EDIT handling**

```c
bool handlePatternTrackEdit(int keymapping) {
	if(patternPage == 0 || !patternGraph || !patternGraph->selected) {
		return false;
	}
	GuiNode *sel = patternGraph->selected;
	if(isDialStripSelected(sel)) {
		if(isSelectedDialNode(patternGraph) && sel->callback) {
			float delta = (keymapping == KM_UP) ? 0.05f
			            : (keymapping == KM_DOWN) ? -0.05f
			            : 0.0f;
			if(delta == 0.0f) {
				return false;
			}
			sel->callback(sel->p, delta);
			return true;
		}
		return false;
	}
	TrackStepData *d = (TrackStepData *)sel->p;
	if(!d || !d->pattern) {
		return false;
	}
	float delta = (keymapping == KM_UP) ? (1.0f / 16.0f)
	            : (keymapping == KM_DOWN) ? -(1.0f / 16.0f)
	            : 0.0f;
	if(delta == 0.0f) {
		return false;
	}
	d->pattern->steps[d->stepIndex] += delta;
	if(d->pattern->steps[d->stepIndex] > 1.0f) d->pattern->steps[d->stepIndex] = 1.0f;
	if(d->pattern->steps[d->stepIndex] < 0.0f) d->pattern->steps[d->stepIndex] = 0.0f;
	if(d->inst) {
		d->inst->loaded.dirty = true;
		capturePatternTracksToInstrument(d->inst);
	}
	return true;
}
```

Declare `handlePatternTrackEdit` in `src/gui.h`.

- [ ] **Step 4: Indicator draw**

Implement `drawPatternIndicatorNode`: draw a panel rect + text `"NOTES"` or `"PTN %d/%d"` + `"LEN %d · %s · %s"` using `getParameterValueAsInt`/shape name table.

- [ ] **Step 5: Build**

Run: `ninja -C build`
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add src/gui_pattern.c src/gui.h
git commit -m "feat(pattern): track pages — indicator, dial strip, step grid, nav"
```

---

### Task C3: `SCENE_PATTERN` input for page switching + track editing

**Files:**
- Modify: `src/main.c`
- Modify: `src/tools/instrument_harness/instrument_harness.c`

**Interfaces:**
- Consumes: `patternPage`, `clampPatternPage`, `setPatternPage`, `handlePatternTrackEdit`, `rebuildPatternGraph`, `KM_SELECT`.
- Produces: page switching and per-page editing in the real app + harness.

- [ ] **Step 1: Add page switching + track branch in `src/main.c`**

At the top of `case SCENE_PATTERN:`, before the `KM_FUNCTION` branch:

```c
				if(isKeyHeld(appState->inputState, KM_SELECT)) {
					if(isKeyJustPressed(appState->inputState, KM_UP)) {
						appState->patternPage = (appState->patternPage + 1) % (PATTERN_TRACKS + 1);
						clampPatternPage(appState);
						rebuildPatternGraph();
					} else if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
						appState->patternPage = (appState->patternPage + PATTERN_TRACKS) % (PATTERN_TRACKS + 1);
						clampPatternPage(appState);
						rebuildPatternGraph();
					}
					break;
				}
				if(appState->patternPage > 0) {
					if(isKeyHeld(appState->inputState, KM_EDIT)) {
						if(isKeyJustPressed(appState->inputState, KM_UP)) {
							handlePatternTrackEdit(KM_UP);
						} else if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
							handlePatternTrackEdit(KM_DOWN);
						}
					} else {
						if(isKeyJustPressed(appState->inputState, KM_LEFT))  navigatePatternGraph(KM_LEFT);
						if(isKeyJustPressed(appState->inputState, KM_RIGHT)) navigatePatternGraph(KM_RIGHT);
						if(isKeyJustPressed(appState->inputState, KM_UP))    navigatePatternGraph(KM_UP);
						if(isKeyJustPressed(appState->inputState, KM_DOWN))  navigatePatternGraph(KM_DOWN);
					}
					break;
				}
```

> `KM_SELECT` is `KEY_LEFT_SHIFT` (input.h), so this is the "shift+up/down" gesture. `FUNCTION+arrows` (channel switch) is handled by the existing branch below the track branch only when `patternPage == 0`; if channel-switching on track pages is desired, move the `KM_FUNCTION` block above the `patternPage > 0` branch.

- [ ] **Step 2: Mirror in the harness**

Add the same `SCENE_PATTERN` branch to the harness input dispatch (search for the `switch(... currentScene)` in `runScriptEventInjection` / the input handler the script drives). The harness already supports `SOP_HOLD_SELECT`; use it to drive the page switch in a fixture.

- [ ] **Step 3: Build + boot smoke test**

Run:
```bash
ninja -C build
timeout -k 2 20 xvfb-run -a ./bin/spectrax --data-dir ./bin/data ; pkill -9 -x spectrax; pkill -9 -x instrument_harness; pkill -9 -f "Xvfb :"
```
Expected: clean boot, no crash/FPE.

- [ ] **Step 4: Commit**

```bash
git add src/main.c src/tools/instrument_harness/instrument_harness.c
git commit -m "feat(input): pattern-screen page switching + track editing"
```

---

### Task C4: Track-page graph-nav unit test

**Files:**
- Test: `tests/dsp/test_graph_nav.c`

**Interfaces:**
- Consumes: the note-page `createPatternGraph` path is unchanged; use the existing test helpers for building/querying graphs.

- [ ] **Step 1: Inspect the existing test structure**

Run: `grep -n "pattern\|Pattern\|navigatePatternGraph\|createPatternGraph" tests/dsp/test_graph_nav.c`
Determine whether the test can build the pattern graph without a GL context. If it cannot (GL is required), skip the graph construction and instead assert the pure helpers (page clamp) in `test_appstate`-style tests; if it can, add node-count assertions (4 dials + 16 cells + 1 indicator).

- [ ] **Step 2: Write the test**

Add a test that calls `setPatternPage(2)` and, if graph construction is testable, asserts:
- the graph has an indicator node named `PTN_IND`,
- a dial strip holding 4 selectable dials named `LEN`/`SHAPE`/`SLEW`/`POL`,
- 16 step cells.

Otherwise assert `clampPatternPage` bounds `patternPage` to `0..PATTERN_TRACKS`. Register it in `main()`.

- [ ] **Step 3: Run**

Run: `meson test -C build test_graph_nav`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/dsp/test_graph_nav.c
git commit -m "test(graph): pattern track-page node layout"
```

---

### Task D1: Grouped `PTN` instrument row

**Files:**
- Modify: `src/gui_instrument.c`
- Modify: `src/gui_inst_mod.c`
- Modify: `src/gui_inst_internal.h`

**Interfaces:**
- Consumes: `PATTERN_TRACKS`, `modSourceCount`, `modIndexAt`, `g_sourceCtx`, `cbOpenRouteLayer`, `modTypeTag`.
- Produces: one row for all four pattern sources; inline grid node removed; `MT_PATTERN` removed from the type cycle.

- [ ] **Step 1: Group the rows**

In `src/gui_instrument.c::createInstGraph`, the scroll container height uses `modSourceCount + 1` rows and the loop calls `appendModSourceEntry` per source. Introduce a row count that collapses the four patterns:

```c
	int srcCount = modSourceCount(inst->modList);
	int patternRows = (srcCount >= PATTERN_TRACKS) ? (PATTERN_TRACKS - 1) : 0;
	int nRows = srcCount - patternRows + 1;
```

and pass `patternRows` awareness to `appendModSourceEntry` via the existing `idx`: emit the grouped row only at the first pattern source index and return early for the others (Step 2).

- [ ] **Step 2: Emit the grouped row**

In `src/gui_inst_mod.c::appendModSourceEntry`, at the top:

```c
	if(mod->type == MT_PATTERN) {
		/* Grouped PTN row: only the first pattern source builds it. */
		int firstPat = inst->coreEnvelopeCount - PATTERN_TRACKS;
		if(idx != firstPat) {
			return;
		}
		GuiNode *wrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODSRC", 0, 0);
		wrap->drawable = true;
		wrap->draw = drawWrapperNode;
		GuiNode *label = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "PTN", 0, 0);
		appendItem(wrap, label, 2);
		for(int t = 0; t < PATTERN_TRACKS; t++) {
			char tag[8];
			snprintf(tag, sizeof(tag), "%d", t + 1);
			GuiNode *route = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal,
			                                        tag, 0, cbOpenRouteLayer, &g_sourceCtx[firstPat + t]);
			appendItem(wrap, route, 2);
		}
		applyLayout(wrap, "mod-source-row");
		appendItem(container, wrap, weight);
		return;
	}
```

The readout is added in Task D2; leave a `readout` node slot here (weight 3) if it is ready.

- [ ] **Step 3: Remove the inline grid + PTN from the type cycle**

- Delete `PatternGridNode`, `drawPatternGridNode`, `isPatternGridNode`, `handlePatternGridInput`, `createPatternGridNode`, and the `case MT_PATTERN:` body in `appendModSourceEntry`'s switch.
- Delete the declarations in `src/gui_inst_internal.h` and the `isPatternGridNode`/`handlePatternGridInput` call in `src/gui_instrument.c:~1001`.
- In `cbCycleSourceType`, change the RND case: `case MT_RND: next = MT_ENV; break;` (no PTN).
- `modTypeTag` keeps the `MT_PATTERN` case (used by the grouped row's label if needed).

- [ ] **Step 4: Build + run existing tests**

Run: `ninja -C build && meson test -C build`
Expected: clean build. `mod_sources.txt` and other fixtures will fail until Task E1 — that is expected; do **not** commit a red fixture state. Either commit with the known fixture breakage documented, or proceed immediately to E1 before committing. Prefer: implement D1 and E1, then run the full gate, then commit both.

- [ ] **Step 5: Commit (after E1 gate is green)**

```bash
git add src/gui_instrument.c src/gui_inst_mod.c src/gui_inst_internal.h
git commit -m "feat(gui): single grouped PTN row with four route buttons; drop inline grid"
```

---

### Task D2: Grouped-row readout

**Files:**
- Modify: `src/gui_inst_mod.c`

**Interfaces:**
- Produces: a non-interactive readout on the grouped row showing each track's shape + length.

- [ ] **Step 1: Implement**

Add a `drawPatternReadoutNode` that walks the four pattern sources from `inst->coreEnvelopeCount - PATTERN_TRACKS` and renders e.g. `1:L16 HLD  2:L16 LIN  3:L8 SLEW  4:L16 CUR`. Add it to the grouped row (weight 3). A shape abbreviation table:

```c
static const char *patternShapeAbbrev(int shape) {
	switch(shape) {
		case SH_HOLD:   return "HLD";
		case SH_LINEAR: return "LIN";
		case SH_SLEW:   return "SLEW";
		case SH_CURVE:  return "CUR";
		default:        return "?";
	}
}
```

- [ ] **Step 2: Build + gate**

Run: `ninja -C build && meson test -C build`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add src/gui_inst_mod.c
git commit -m "feat(gui): PTN grouped-row readout"
```

---

### Task E1: Fixture + full-suite migration and gate

**Files:**
- Modify: `src/tools/instrument_harness/fixtures/*.txt` (count/layout-sensitive fixtures)
- Create: `src/tools/instrument_harness/fixtures/pattern_screen.txt`
- Modify: `src/tools/instrument_harness/instrument_harness.c` (new assert verbs if needed)
- Modify: any `tests/dsp/*.c` still asserting old counts.

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Add harness assert verbs**

Add:
- `ASSERT patpage==<N>` — `getPatternPage() == N`.
- `ASSERT tracklen==(<trk>,<N>)` — pattern track's length param.
- `ASSERT trackstep==(<trk>,<step>,<pct>)` — `steps[step] * 100`.
- `ASSERT patshape==(<trk>,<N>)` — pattern track shape.
- `ASSERT ptnrows==<N>` — number of `MODSRC` pattern rows (expect 1).

Follow the existing `SOP_ASSERT_*` / parser / executor pattern.

- [ ] **Step 2: New `pattern_screen.txt` fixture**

Script (adjust nav geometry after running it once):
```
# boot into the pattern screen via SCENE 2 (or the app's scene order)
SCENE 2
FRAMES 2
ASSERT scene==2
ASSERT patpage==0
HOLD_SELECT
UP
RELEASE
ASSERT patpage==1
# navigate to a step cell and edit it
<nav to the grid cell>
EDIT UP
ASSERT trackstep==(0,0,56)
# cycle SHAPE via the dial
<nav to SHAPE dial>
EDIT UP
ASSERT patshape==(0,1)
QUIT
```

Run:
```bash
cd bin && ../src/tools/instrument_harness/instrument_harness --script ../src/tools/instrument_harness/fixtures/pattern_screen.txt
pkill -9 -x spectrax; pkill -9 -x instrument_harness; pkill -9 -f "Xvfb :"
```
Expected: PASS.

- [ ] **Step 3: Migrate existing fixtures**

For each failing fixture, update the expected source counts (`envcount` now includes the four core patterns) and remove the deleted PTN-row steps from `mod_sources.txt`. Use the harness output to rediscover nav distances.

- [ ] **Step 4: Full gate**

Run:
- `meson test -C build` — all suites green.
- every fixture under `src/tools/instrument_harness/fixtures/` PASSes.
- `ninja -C build` clean; app boots under Xvfb.
- orphan sweep after each run.

- [ ] **Step 5: Commit**

```bash
git add src/tools/instrument_harness tests/dsp
git commit -m "test: pattern-screen fixture + migrate count/layout-sensitive fixtures"
```

---

## Self-Review

- **Spec coverage:** A1→§A data model; A2→§B caps/creation/§C preset skip; B1/B2→§D song persistence; C1-C4→§F pages/nav/indicator; D1/D2→§E grouped row; E1→§H testing. All spec sections have a task.
- **Type consistency:** `PatternTrackData`, `PatternTrackSet`, `MAX_MOD_SOURCES`, `PATTERN_TRACKS`, `patternStateToTrackData`/`patternTrackDataToState`, `applyPatternTracksToInstrument`, `capturePatternTrackSet`/`applyPatternTrackSetToInstruments`, `setPatternPage`/`getPatternPage`, `clampPatternPage`, `handlePatternTrackEdit` are used consistently across tasks.
- **Known risk:** Task D1 leaves fixtures red until E1; the plan commits D1 only after E1's gate is green.
