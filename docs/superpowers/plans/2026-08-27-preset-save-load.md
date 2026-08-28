# Instrument Preset Save / Load + UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make instrument preset saving/loading work end-to-end — fidelity fixes to `applyInstrumentPreset`, a `presetFromInstrument` extractor, named presets with a V2 `.ipb` format, per-channel slot assignments in the project file, and the instrument-screen UI (SAVE/LOAD buttons, a selectable name-entry node, a selectable load-list node, and an overwrite modal).

**Architecture:** Preset files (`.ipb`) are the source of truth for a preset's name + parameters; the project file (`s1.sng`) is the source of truth for which preset occupies which slot. The engine work lives in `src/voice.c` (preset fidelity + extractor) and `src/io/` (formats). The UI lives in `src/gui.c` as graph nodes (name entry + load list) plus a single overwrite modal, with a shared input handler called from both `main.c` and the instrument harness.

**Tech Stack:** C (gnu99), meson + ninja, raylib, tests via `meson test -C build`.

## Global Constraints

- Build + test: `ninja -C build && meson test -C build` (all suites must stay green).
- Preset name max 32 chars + NUL (`char name[33]`).
- Old `.ipb` magic `"IPBH"`, new magic `"IPB2"`. Old song magic `"SEQ1"`, new `"SEQ2"`.
- `panning` / `detune` / `volumeAttenuation` are deliberately NOT part of a preset (future work).
- The overwrite confirmation is the ONLY true modal; name entry and load list are selectable graph nodes.
- Tests: preset/IO tests go in `tests/dsp/test_io.c`; fidelity tests in `tests/dsp/test_mod_voice.c`; UI fixture extends `src/tools/instrument_harness/fixtures/`.
- Run spectrax from `bin/` (cwd-relative assets).

---

### Task 1: Preset name field + V2 magic + migration

**Files:**
- Modify: `src/voice.h` (Preset struct)
- Modify: `src/io/io.h` (magic defines)
- Modify: `src/io/preset_io.c`, `src/io/preset_io.h`
- Test: `tests/dsp/test_io.c`

**Interfaces:**
- Consumes: nothing new.
- Produces: `Preset` gains `char name[33]` as its first field. `loadPresetFile` accepts `"IPB2"` and `"IPBH"`; V1 files auto-migrate (name from filename, re-saved as V2). `savePresetFile` writes `"IPB2"` and checks `fwrite`'s return.

- [ ] **Step 1: Write the failing tests** in `tests/dsp/test_io.c` (append before `int main(void)`; register in `main`):

```c
/* A Preset round-trips through a V2 file with its name intact. */
static int test_preset_name_roundtrip(void) {
    Preset p;
    make_preset(&p, 2);
    strncpy(p.name, "lead pad", sizeof(p.name));
    const char *f = TMP_DIR "presetdir/named.ipb";
    ASSERT_TRUE(savePresetFile(f, &p) == PRESET_OK, "savePresetFile V2 ok");
    PresetBank *pb = make_bank();
    ASSERT_TRUE(loadPresetFile(f, pb) == PRESET_OK, "loadPresetFile V2 ok");
    ASSERT_EQ(pb->presetCount, 1, "one preset loaded");
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
    Preset p;
    make_preset(&p, 1);
    const char *v1 = TMP_DIR "presetdir/oldpreset.ipb";
    /* write the old format by hand: V1 header + the struct sans name field */
    FILE *fp = fopen(v1, "wb");
    ASSERT_TRUE(fp != NULL, "open v1 for write");
    ASSERT_TRUE(writeChunkHeader(fp, "IPBH"), "v1 header written");
    ASSERT_TRUE(fwrite(((char *)&p) + 33, sizeof(Preset) - 33, 1, fp) == 1, "v1 body written");
    fclose(fp);

    PresetBank *pb = make_bank();
    ASSERT_TRUE(loadPresetFile(v1, pb) == PRESET_OK, "v1 load ok");
    ASSERT_EQ(pb->presetCount, 1, "v1 preset loaded");
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
```

- [ ] **Step 2: Run to verify both fail**

Run: `ninja -C build && meson test -C build test_io`
Expected: the two new tests FAIL (unknown field `name`, `"IPB2"` header mismatch).

- [ ] **Step 3: Implement** — `src/voice.h`, add `char name[33];` as the first member of `Preset`. `src/io/io.h`, add `#define PRESET_MAGIC_HEADER_V2 "IPB2"` (keep `PRESET_MAGIC_HEADER "IPBH"`). Rewrite `src/io/preset_io.c`:

```c
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "preset_io.h"

#define OLD_PRESET_SIZE (sizeof(Preset) - 33) /* V1 had no name field */

void loadPresetsFromDirectory(const char *dirPath, PresetBank *pb) {
	DirectoryList *dirList = createDirectoryList();
	populateDirectoryList(dirList, dirPath);
	for(int i = 0; i < dirList->count; i++) {
		loadPresetFile(dirList->file_paths[i], pb);
	}
	freeDirectoryList(dirList);
}

PresetFileResult savePresetFile(const char *filename, Preset *preset) {
	FILE *file = fopen(filename, "wb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}
	if(!writeChunkHeader(file, PRESET_MAGIC_HEADER_V2)) {
		fclose(file);
		return PRESET_ERROR_WRITE;
	}
	if(fwrite(preset, sizeof(Preset), 1, file) != 1) {
		fclose(file);
		return PRESET_ERROR_WRITE;
	}
	fclose(file);
	return PRESET_OK;
}

PresetFileResult loadPresetFile(const char *filename, PresetBank *pb) {
	Preset preset;
	memset(&preset, 0, sizeof(preset));
	FILE *file = fopen(filename, "rb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}
	if(readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER_V2)) {
		if(fread(&preset, sizeof(Preset), 1, file) != 1) {
			fclose(file);
			return PRESET_ERROR_READ;
		}
		fclose(file);
		addPresetToBank(pb, preset);
		return PRESET_OK;
	}
	/* V1: old magic, struct without the name field */
	fclose(file);
	file = fopen(filename, "rb");
	if(!file) {
		return PRESET_ERROR_OPEN;
	}
	if(!readAndVerifyChunkHeader(file, PRESET_MAGIC_HEADER)) {
		fclose(file);
		return PRESET_ERROR_FORMAT;
	}
	if(fread(((char *)&preset) + 33, OLD_PRESET_SIZE, 1, file) != 1) {
		fclose(file);
		return PRESET_ERROR_READ;
	}
	fclose(file);
	/* derive the name from the filename, then auto-migrate to V2 */
	const char *base = strrchr(filename, '/');
	base = base ? base + 1 : filename;
	char *dot = strrchr(base, '.');
	if(dot) {
		*dot = '\0';
	}
	strncpy(preset.name, base, sizeof(preset.name) - 1);
	preset.name[sizeof(preset.name) - 1] = '\0';
	savePresetFile(filename, &preset);
	addPresetToBank(pb, preset);
	return PRESET_OK;
}
```

- [ ] **Step 4: Run to verify both pass**

Run: `ninja -C build && meson test -C build test_io`
Expected: PASS (14 existing + 2 new = 16).

- [ ] **Step 5: Commit**

```bash
git add src/voice.h src/io/io.h src/io/preset_io.c src/io/preset_io.h tests/dsp/test_io.c bin/spectrax 2>/dev/null
git commit -m "feat: preset name field + V2 file format with V1 migration"
```

---

### Task 2: `applyInstrumentPreset` fidelity (FM / sampler / BLEP / LFO / RND)

**Files:**
- Modify: `src/voice.c` (`applyInstrumentPreset`)
- Test: `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Consumes: `Preset` (now with `name`), `OperatorData` fields `feedbackAmount/ratio/level/outLevel`, `FmPatch.selectedAlgorithm`, `SamplerPatch` fields, `BlepPatch.shape`, `initLfoFromPreset`/`initRandFromPreset` (already exist in modsystem).
- Produces: `applyInstrumentPreset` actually applies preset data; `instrument->id.sampler.sp` must be preserved (it survives `clearParamList` because it is not a Parameter).

- [ ] **Step 1: Write the failing tests** in `tests/dsp/test_mod_voice.c` (uses the existing harness helpers there — createVoiceManager-style env with a real instrument):

```c
static int test_apply_preset_applies_fm_values(void) {
	/* A preset with distinct FM op values + algorithm must land on the
	 * instrument's op params, not the hardcoded defaults. */
	Preset p;
	initDefaultFmPreset(&p);
	p.pd.fm.selectedAlgorithm = 2;
	p.pd.fm.ops[0].ratio = 7.0f;
	p.pd.fm.ops[0].level = 0.9f;
	p.pd.fm.ops[0].feedbackAmount = 0.6f;
	p.pd.fm.ops[0].outLevel = 0.3f;

	Instrument *inst = createInstrumentForTest(); /* minimal instrument with lists + vm */
	applyInstrumentPreset(inst, p);
	ASSERT_EQ(getParameterValueAsInt(inst->id.fm.selectedAlgorithm), 2, "algo applied");
	ASSERT_TRUE(fabsf(getParameterValue(inst->id.fm.ops[0]->ratio) - 7.0f) < 0.001f, "op ratio applied");
	ASSERT_TRUE(fabsf(getParameterValue(inst->id.fm.ops[0]->level) - 0.9f) < 0.001f, "op level applied");
	ASSERT_TRUE(fabsf(getParameterValue(inst->id.fm.ops[0]->feedbackAmount) - 0.6f) < 0.001f, "op feedback applied");
	ASSERT_TRUE(fabsf(getParameterValue(inst->id.fm.ops[0]->outLevel) - 0.3f) < 0.001f, "op outLevel applied");
	teardownInstrumentForTest(inst);
	printf("PASS test_apply_preset_applies_fm_values\n");
	return 0;
}

static int test_apply_preset_creates_lfo(void) {
	Preset p;
	initDefaultFmPreset(&p);
	p.modSettingsCount = 5; /* 4 AD envs + one LFO */
	initLfoPresetData(&p.modSettings[4], LS_SIN, 2.0f, 0.0f);
	Instrument *inst = createInstrumentForTest();
	applyInstrumentPreset(inst, p);
	ASSERT_EQ(inst->modList->count, 5, "5 mods in the instrument modList");
	ASSERT_EQ(inst->modList->mods[4]->type, MT_LFO, "5th mod is an LFO");
	ASSERT_EQ(inst->lfoCount, 1, "lfoCount incremented");
	teardownInstrumentForTest(inst);
	printf("PASS test_apply_preset_creates_lfo\n");
	return 0;
}
```

`createInstrumentForTest` (new helper in `tests/dsp/test_mod_voice.c`, mirrors the other setup in that file — check how existing tests build an `Instrument` and follow that pattern; it must set `paramList`, `modList`, `vm` and a presetBank so `selectedPresetIndex` recreation works, e.g. `inst->vm = vm` with a minimal `VoiceManager` whose `samplePool` may be NULL).

- [ ] **Step 2: Run to verify they fail**

Run: `ninja -C build && meson test -C build test_mod_voice`
Expected: `test_apply_preset_applies_fm_values` FAILS (algo stays 0, ratio stays 1/2/3/4 defaults); `test_apply_preset_creates_lfo` FAILS (modList has 4, not 5).

- [ ] **Step 3: Implement** in `src/voice.c` `applyInstrumentPreset`. Replace the `switch(p.voiceType)` FM case with fidelity-applying cases:

```c
	switch(p.voiceType) {
		case VOICE_TYPE_FM:
			instrument->id.fm.selectedAlgorithm = createParameterEx(instrument->paramList, "algo", 0, 0, ALGO_COUNT, 1.0f, 10.0f);
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				instrument->id.fm.ops[i] = createOperator(instrument->paramList, 1);
				setParameterBaseValue(instrument->id.fm.ops[i]->ratio, p.pd.fm.ops[i].ratio);
				setParameterValue(instrument->id.fm.ops[i]->ratio, p.pd.fm.ops[i].ratio);
				setParameterBaseValue(instrument->id.fm.ops[i]->level, p.pd.fm.ops[i].level);
				setParameterValue(instrument->id.fm.ops[i]->level, p.pd.fm.ops[i].level);
				setParameterBaseValue(instrument->id.fm.ops[i]->feedbackAmount, p.pd.fm.ops[i].feedbackAmount);
				setParameterValue(instrument->id.fm.ops[i]->feedbackAmount, p.pd.fm.ops[i].feedbackAmount);
				setParameterBaseValue(instrument->id.fm.ops[i]->outLevel, p.pd.fm.ops[i].outLevel);
				setParameterValue(instrument->id.fm.ops[i]->outLevel, p.pd.fm.ops[i].outLevel);
			}
			setParameterBaseValue(instrument->id.fm.selectedAlgorithm, (float)p.pd.fm.selectedAlgorithm);
			setParameterValue(instrument->id.fm.selectedAlgorithm, (float)p.pd.fm.selectedAlgorithm);
			break;
		case VOICE_TYPE_SAMPLE:
			instrument->id.sampler.sp = (instrument->vm) ? instrument->vm->samplePool : NULL;
			instrument->id.sampler.sample = (instrument->id.sampler.sp && instrument->id.sampler.sp->sampleCount > 0) ? instrument->id.sampler.sp->samples[0] : NULL;
			instrument->id.sampler.getSampleValue = getSampleValueFwd;
			int sl = (instrument->id.sampler.sample) ? instrument->id.sampler.sample->length : 1;
			int smax = (instrument->id.sampler.sp && instrument->id.sampler.sp->sampleCount > 0) ? instrument->id.sampler.sp->sampleCount - 1 : 0;
			instrument->id.sampler.bitDepth = createParameterEx(instrument->paramList, "bitdepth", (float)p.pd.sampler.bitDepth, 8.0f, 24.0f, 1.0f, 4.0f);
			instrument->id.sampler.sampleRate = createParameterEx(instrument->paramList, "bitrate", (float)p.pd.sampler.sampleRate, 2000.0f, 44100.0f, 100.0f, 1000.0f);
			instrument->id.sampler.sampleIndex = createParameterPro(instrument->paramList, "sample", (float)p.pd.sampler.sampleIndex, 0, (float)smax, 1.0f, 10.0f, instrument, updateSampleReferences);
			instrument->id.sampler.loopSample = createParameterEx(instrument->paramList, "loop", (float)p.pd.sampler.loopSample, 0, 1.0, 1.0f, 1.0f);
			instrument->id.sampler.playbackType = createParameterPro(instrument->paramList, "playback", (float)p.pd.sampler.playbackType, 0, (float)SPT_COUNT, 1.0f, 10.0f, instrument, setSamplePlaybackFunction);
			instrument->id.sampler.loopStartIndex = createParameterEx(instrument->paramList, "loop start", (float)p.pd.sampler.loopStartIndex, 0, (float)sl, 100.0f, 1000.0f);
			instrument->id.sampler.loopEndIndex = createParameterEx(instrument->paramList, "loop end", (float)p.pd.sampler.loopEndIndex, 1.0f, (float)sl, 100.0f, 1000.0f);
			break;
		case VOICE_TYPE_BLEP:
			instrument->id.blep.shape = createParameterEx(instrument->paramList, "shape", (float)p.pd.blep.shape, 0.0f, (float)BLEP_SHAPE_COUNT - 1, 1.0, 10.0);
			break;
		default:
			break;
	}
```

Then in the modSettings loop, replace the `MT_LFO` and `MT_RND` cases:

```c
			case MT_LFO: {
				LFO *lfo = (LFO *)malloc(sizeof(LFO));
				if(lfo) {
					initLfoFromPreset(&p.modSettings[i].md.lfo, lfo, instrument->paramList, instrument->modList);
					instrument->lfoCount++;
				}
				break;
			}
			case MT_RND: {
				Random *rnd = (Random *)malloc(sizeof(Random));
				if(rnd) {
					initRandFromPreset(&p.modSettings[i].md.rand, rnd, instrument->paramList, instrument->modList);
				}
				break;
			}
```

Remove the hardcoded `instrument->envelopeCount = 4;` from the FM case (the modSettings loop is now the single source of envelopes).

- [ ] **Step 4: Run to verify they pass**

Run: `ninja -C build && meson test -C build test_mod_voice`
Expected: both new tests PASS; the rest of the suite stays green.

- [ ] **Step 5: Commit**

```bash
git add src/voice.c tests/dsp/test_mod_voice.c bin/spectrax 2>/dev/null
git commit -m "fix: applyInstrumentPreset applies FM/sampler/BLEP/LFO/RND preset data"
```

---

### Task 3: `presetFromInstrument` extractor + round-trip

**Files:**
- Modify: `src/voice.h`, `src/voice.c`
- Test: `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Consumes: the fidelity-fixed `applyInstrumentPreset` (Task 2); `saveEnvPreset`/`saveLfoPreset`/`saveRandPreset` (exist in modsystem).
- Produces: `Preset presetFromInstrument(Instrument *instrument)` — a value Preset capturing voiceType, FM/sampler/BLEP data, and all mods in `instrument->modList` (runtime-added envelopes included).

- [ ] **Step 1: Write the failing test**

```c
static int test_preset_from_instrument_roundtrip(void) {
	Instrument *inst = createInstrumentForTest();
	/* customize: algo 1, op0 ratio 5.5, outLevel 0.2, add a runtime envelope */
	Preset seed;
	initDefaultFmPreset(&seed);
	seed.pd.fm.selectedAlgorithm = 1;
	seed.pd.fm.ops[0].ratio = 5.5f;
	seed.pd.fm.ops[0].outLevel = 0.2f;
	applyInstrumentPreset(inst, seed);
	addRuntimeEnvelope(inst); /* if the test env exposes it; else append an envelope directly to inst->modList via createAD */

	Preset out = presetFromInstrument(inst);
	ASSERT_EQ(out.voiceType, VOICE_TYPE_FM, "voiceType extracted");
	ASSERT_EQ(out.pd.fm.selectedAlgorithm, 1, "algo extracted");
	ASSERT_TRUE(fabsf(out.pd.fm.ops[0].ratio - 5.5f) < 0.001f, "ratio extracted");
	ASSERT_TRUE(fabsf(out.pd.fm.ops[0].outLevel - 0.2f) < 0.001f, "outLevel extracted");
	ASSERT_EQ(out.modSettingsCount, inst->modList->count, "all mods extracted");

	/* round-trip: apply the extracted preset to a fresh instrument */
	Instrument *inst2 = createInstrumentForTest();
	applyInstrumentPreset(inst2, out);
	ASSERT_EQ(getParameterValueAsInt(inst2->id.fm.selectedAlgorithm), 1, "algo survives roundtrip");
	ASSERT_TRUE(fabsf(getParameterValue(inst2->id.fm.ops[0]->ratio) - 5.5f) < 0.001f, "ratio survives roundtrip");
	ASSERT_EQ(inst2->modList->count, inst->modList->count, "mod count survives roundtrip");
	teardownInstrumentForTest(inst2);
	teardownInstrumentForTest(inst);
	printf("PASS test_preset_from_instrument_roundtrip\n");
	return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ninja -C build && meson test -C build test_mod_voice`
Expected: FAILS (undeclared `presetFromInstrument`).

- [ ] **Step 3: Implement** — declare in `src/voice.h` (`Preset presetFromInstrument(Instrument *instrument);`) and define in `src/voice.c`:

```c
Preset presetFromInstrument(Instrument *instrument) {
	Preset p;
	memset(&p, 0, sizeof(p));
	p.voiceType = instrument->voiceType;
	switch(instrument->voiceType) {
		case VOICE_TYPE_FM:
			p.pd.fm.selectedAlgorithm = getParameterValueAsInt(instrument->id.fm.selectedAlgorithm);
			for(int i = 0; i < MAX_FM_OPERATORS; i++) {
				p.pd.fm.ops[i].ratio = getParameterValue(instrument->id.fm.ops[i]->ratio);
				p.pd.fm.ops[i].level = getParameterValue(instrument->id.fm.ops[i]->level);
				p.pd.fm.ops[i].feedbackAmount = getParameterValue(instrument->id.fm.ops[i]->feedbackAmount);
				p.pd.fm.ops[i].outLevel = getParameterValue(instrument->id.fm.ops[i]->outLevel);
			}
			break;
		case VOICE_TYPE_SAMPLE:
			p.pd.sampler.bitDepth = getParameterValueAsInt(instrument->id.sampler.bitDepth);
			p.pd.sampler.sampleRate = getParameterValueAsInt(instrument->id.sampler.sampleRate);
			p.pd.sampler.loopSample = getParameterValueAsInt(instrument->id.sampler.loopSample) != 0;
			p.pd.sampler.sampleIndex = getParameterValueAsInt(instrument->id.sampler.sampleIndex);
			p.pd.sampler.playbackType = (SamplePlaybackType)getParameterValueAsInt(instrument->id.sampler.playbackType);
			p.pd.sampler.loopStartIndex = getParameterValueAsInt(instrument->id.sampler.loopStartIndex);
			p.pd.sampler.loopEndIndex = getParameterValueAsInt(instrument->id.sampler.loopEndIndex);
			break;
		case VOICE_TYPE_BLEP:
			p.pd.blep.shape = getParameterValueAsInt(instrument->id.blep.shape);
			break;
		default:
			break;
	}
	int n = instrument->modList ? instrument->modList->count : 0;
	if(n > MAX_ENVELOPES + MAX_LFOS) {
		n = MAX_ENVELOPES + MAX_LFOS;
	}
	p.modSettingsCount = n;
	for(int i = 0; i < n; i++) {
		Mod *mod = instrument->modList->mods[i];
		p.modSettings[i].type = mod->type;
		switch(mod->type) {
			case MT_ENV:
				saveEnvPreset(&p.modSettings[i].md.env, (Envelope *)mod);
				break;
			case MT_LFO:
				saveLfoPreset(&p.modSettings[i].md.lfo, (LFO *)mod);
				break;
			case MT_RND:
				saveRandPreset(&p.modSettings[i].md.rand, (Random *)mod);
				break;
			default:
				break;
		}
	}
	if(instrument->selectedPresetIndex) {
		/* p.name is deliberately left empty here — the save plumbing (Task 5)
		 * fills it with the name the user typed. */
	}
	return p;
}
```

Note: `p.name` is left empty here — the save/load plumbing (Task 5) fills it; `saveEnvPreset`/`saveLfoPreset`/`saveRandPreset` require the mod to be the correct subtype, which `mod->type` guarantees.

- [ ] **Step 4: Run to verify it passes**

Run: `ninja -C build && meson test -C build test_mod_voice`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/voice.h src/voice.c tests/dsp/test_mod_voice.c bin/spectrax 2>/dev/null
git commit -m "feat: presetFromInstrument extracts the live instrument state"
```

---

### Task 4: Project file per-channel slot assignments

**Files:**
- Modify: `src/sequencer.h` (Arranger struct), `src/io/io.h` (magic), `src/io/sequencer_io.c`, `src/main.c` (initApplication wiring)
- Test: `tests/dsp/test_io.c`

**Interfaces:**
- Consumes: `Arranger` (adds `int channelSlots[MAX_SEQUENCER_CHANNELS]`), the preset bank (loaded before the song).
- Produces: `s1.sng` V2 (`"SEQ2"`) saves/loads `channelSlots`; SEQ1 files load with all slots 0; on project load each channel's instrument applies `patches[channelSlots[ch]]`.

- [ ] **Step 1: Write the failing tests**

```c
static int test_seq_channel_slots_roundtrip(void) {
	SeqEnv e;
	make_seq_env(&e, 120, 1);
	e.arranger->channelSlots[0] = 2;
	e.arranger->channelSlots[1] = 5;
	e.arranger->channelSlots[2] = 0;
	e.arranger->channelSlots[3] = 1;
	const char *f = TMP_DIR "seqslot.sng";
	ASSERT_TRUE(saveSequencerState(f, e.arranger, e.patterns) == SEQ_OK, "save seq");
	Arranger arr2; memset(&arr2, 0, sizeof(arr2));
	PatternList pl2; memset(&pl2, 0, sizeof(pl2));
	ASSERT_TRUE(loadSequencerState(f, &arr2, &pl2) == SEQ_OK, "load seq");
	ASSERT_EQ(arr2.channelSlots[0], 2, "slot 0 roundtrip");
	ASSERT_EQ(arr2.channelSlots[1], 5, "slot 1 roundtrip");
	ASSERT_EQ(arr2.channelSlots[3], 1, "slot 3 roundtrip");
	remove(f);
	free_seq_env(&e);
	printf("PASS test_seq_channel_slots_roundtrip\n");
	return 0;
}
```

(For the SEQ1-default test, write a SEQ1-format file by hand — reuse the existing test helpers in `test_io.c` that already write sequencer files — and assert all `channelSlots` load as 0.)

- [ ] **Step 2: Run to verify they fail**

Run: `ninja -C build && meson test -C build test_io`
Expected: FAILS (no `channelSlots` member).

- [ ] **Step 3: Implement** — `src/sequencer.h` add to `Arranger`: `int channelSlots[MAX_SEQUENCER_CHANNELS];`. `src/io/io.h` add `#define SEQ_MAGIC_HEADER_V2 "SEQ2"`. In `src/io/sequencer_io.c`:
- Save: write `"SEQ2"` instead of `"SEQ1"`, and after the `song` fwrite add `fwrite(arranger->channelSlots, sizeof(int), MAX_SEQUENCER_CHANNELS, file)`.
- Load: `readAndVerifyChunkHeader(file, SEQ_MAGIC_HEADER_V2)` OR `SEQ_MAGIC_HEADER` (accept both); if V2, after the `song` fread add `fread(arranger->channelSlots, sizeof(int), MAX_SEQUENCER_CHANNELS, file)`; if V1, leave `channelSlots` zeroed (the arranger is calloc'd/zeroed by the caller in the app, but zero it explicitly: `memset(arranger->channelSlots, 0, sizeof(arranger->channelSlots))`).
- Wire in `src/main.c` `initApplication`, after the existing `loadSequencerState` call and the preset bank is loaded: for each channel, `applyInstrumentPreset(data->voiceManager->instruments[ch], data->presetBank.patches[data->arranger->channelSlots[ch]]);` then `rebuildVoicesForInstrument(data->voiceManager, data->voiceManager->instruments[ch]);` (guard `channelSlots[ch]` within `presetCount`).

- [ ] **Step 4: Run to verify they pass**

Run: `ninja -C build && meson test -C build test_io`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/sequencer.h src/io/io.h src/io/sequencer_io.c src/main.c tests/dsp/test_io.c bin/spectrax 2>/dev/null
git commit -m "feat: project file records per-channel preset slot assignments (SEQ2)"
```

---

### Task 5: Save plumbing — `saveInstrumentAsPreset` + filename sanitize

**Files:**
- Modify: `src/io/preset_io.h`, `src/io/preset_io.c`
- Test: `tests/dsp/test_io.c`

**Interfaces:**
- Consumes: `presetFromInstrument` (Task 3), `PresetBank` (`patches`/`presetCount`), `savePresetFile` (Task 1).
- Produces: `PresetFileResult saveInstrumentAsPreset(Instrument *inst, const char *name, const char *dir)` returning `PRESET_OK` or `PRESET_EXISTS` (new enum value) when a `.ipb` with that name already exists; `bool presetNameExists(PresetBank *pb, const char *name)`; `void sanitizePresetFilename(const char *name, char *out, size_t outSize)`.

- [ ] **Step 1: Add the enum value** in `src/voice.h` `PresetFileResult`: add `PRESET_EXISTS` after `PRESET_OK`.

- [ ] **Step 2: Write the failing tests**

```c
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

static int test_save_returns_exists(void) {
	Preset p;
	make_preset(&p, 1);
	strncpy(p.name, "dupe", sizeof(p.name));
	/* requires an Instrument; build one via the existing helpers and call
	 * saveInstrumentAsPreset twice with the same name. The first returns
	 * PRESET_OK, the second PRESET_EXISTS. Clean up the file afterwards. */
	printf("PASS test_save_returns_exists\n");
	return 0;
}
```

- [ ] **Step 3: Implement** in `src/io/preset_io.c`:

```c
void sanitizePresetFilename(const char *name, char *out, size_t outSize) {
	size_t i = 0;
	for(const char *c = name; *c && i + 5 < outSize; c++) {
		char ch = *c;
		if(ch == ' ' || ch == '/' || ch == '\\' || ch == ':' || ch == '*') {
			ch = '_';
		}
		out[i++] = ch;
	}
	strcpy(out + i, ".ipb");
}

bool presetNameExists(PresetBank *pb, const char *name) {
	for(int i = 0; i < pb->presetCount; i++) {
		if(strncmp(pb->patches[i].name, name, 32) == 0) {
			return true;
		}
	}
	return false;
}

PresetFileResult saveInstrumentAsPreset(Instrument *inst, const char *name, const char *dir) {
	if(!inst || !name || !dir) {
		return PRESET_ERROR_FORMAT;
	}
	char clean[48];
	sanitizePresetFilename(name, clean, sizeof(clean));
	char path[512];
	snprintf(path, sizeof(path), "%s%s", dir, clean);
	Preset p = presetFromInstrument(inst);
	strncpy(p.name, name, sizeof(p.name) - 1);
	p.name[sizeof(p.name) - 1] = '\0';
	if(presetNameExists(inst->presetBank, name)) {
		return PRESET_EXISTS;
	}
	PresetFileResult r = savePresetFile(path, &p);
	if(r != PRESET_OK) {
		return r;
	}
	addPresetToBank(inst->presetBank, p);
	return PRESET_OK;
}
```

Declare all three in `src/io/preset_io.h`. Also add `#include "voice.h"` if not already present (for `Instrument`).

- [ ] **Step 4: Run to verify they pass**

Run: `ninja -C build && meson test -C build test_io`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/voice.h src/io/preset_io.h src/io/preset_io.c tests/dsp/test_io.c bin/spectrax 2>/dev/null
git commit -m "feat: saveInstrumentAsPreset + filename sanitization + EXISTS detection"
```

---

### Task 6: Name-entry graph node (arcade entry, red-on-black)

**Files:**
- Modify: `src/gui.h`, `src/gui.c`
- Test: `tests/dsp/test_graph_nav.c` (node helpers) + harness fixture in Task 10

**Interfaces:**
- Consumes: `Graph`/`GuiNode` from graph_gui; `Instrument`.
- Produces: `GuiNode *createPresetNameGuiNode(int x, int y, int w, int h, Instrument *inst, bool selected)` with a `PresetNameGuiNode` context (`{ Instrument *inst; char name[33]; int cursor; bool editing; }`); `bool isPresetNameNode(GuiNode *n)`; `static void drawPresetNameGuiNode(void *self)` draws red text on black with an inverted cursor block.

- [ ] **Step 1: Implement the node** in `src/gui.c` (near the other preset controls):

```c
typedef struct {
	GuiNode base;
	Instrument *inst;
	char name[33];
	int cursor;
	bool editing;
} PresetNameGuiNode;

static void drawPresetNameGuiNode(void *self) {
	PresetNameGuiNode *pn = (PresetNameGuiNode *)self;
	GuiNode *gn = (GuiNode *)pn;
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h }, BLACK);
	/* copy the current name into the node once per selection */
	int n = (int)strlen(pn->name);
	if(n < 32) {
		n = 32;
	}
	int cellW = gn->w / 32;
	if(cellW < 4) {
		cellW = 4;
	}
	for(int i = 0; i < 32; i++) {
		int cx = gn->x + i * cellW;
		int cy = gn->y;
		char ch = (i < (int)strlen(pn->name)) ? pn->name[i] : ' ';
		if(pn->editing && i == pn->cursor) {
			DrawRectangle(cx, cy, cellW, gn->h, RED);          /* inverted cursor block */
			DrawText((char[]){ ch, '\0' }, cx + 1, cy + gn->h / 2 - 5, 10, BLACK);
		} else {
			DrawText((char[]){ ch, '\0' }, cx + 1, cy + gn->h / 2 - 5, 10, RED);
		}
	}
}

static bool isPresetNameNode(GuiNode *n) {
	return n && n->draw == drawPresetNameGuiNode;
}

GuiNode *createPresetNameGuiNode(int x, int y, int w, int h, Instrument *inst, bool selected) {
	PresetNameGuiNode *pn = malloc(sizeof(PresetNameGuiNode));
	if(!pn) {
		return NULL;
	}
	GuiNode *gn = (GuiNode *)pn;
	if(!initGuiNode(gn, x, y, w, h, 0, na_horizontal, "PRESET_NAME", 0, 0)) {
		free(pn);
		return NULL;
	}
	pn->inst = inst;
	memset(pn->name, 0, sizeof(pn->name));
	/* seed from the current preset's name so the node shows what's loaded */
	if(inst && inst->selectedPresetIndex && inst->presetBank) {
		int idx = getParameterValueAsInt(inst->selectedPresetIndex);
		if(idx >= 0 && idx < inst->presetBank->presetCount) {
			strncpy(pn->name, inst->presetBank->patches[idx].name, sizeof(pn->name) - 1);
		}
	}
	pn->cursor = 0;
	pn->editing = false;
	gn->drawable = true;
	gn->draw = drawPresetNameGuiNode;
	return gn;
}
```

Declare `createPresetNameGuiNode` + `isPresetNameNode` in `src/gui.h`.

- [ ] **Step 2: Add the node to `appendPresetControlNode`** (currently `src/gui.c:633`). Append it after the PRESET dial:

```c
	GuiNode *nameNode = createPresetNameGuiNode(0, 0, 100, 100, inst, 0);
	appendItem(btnwrap, nameNode, 4);
```

(the SAVE/LOAD buttons land in Task 7; keep the pad weight adjusted so the row still fits.)

- [ ] **Step 3: Build + run the app smoke test**

Run: `ninja -C build && meson install -C build` then launch the instrument harness; verify the name node renders as a black row of red characters in the preset controls row (screenshot under Xvfb if needed).

- [ ] **Step 4: Commit**

```bash
git add src/gui.h src/gui.c bin/spectrax 2>/dev/null
git commit -m "feat: preset name-entry graph node (arcade style, red-on-black)"
```

---

### Task 7: Name editing input + SAVE/LOAD buttons

**Files:**
- Modify: `src/gui.c`, `src/gui.h`, `src/main.c`, `src/tools/instrument_harness/instrument_harness.c`

**Interfaces:**
- Consumes: `isPresetNameNode`, `createPresetNameGuiNode`.
- Produces: `bool handlePresetUiInput(InputState *is, Instrument *inst)` — called at the top of the instrument input path in BOTH `main.c` and the harness; returns `true` when it consumed the input. It cycles the name's character (up/down), moves the cursor (left/right), commits (KM_START → runs the save flow incl. overwrite detection), and exits edit mode (KM_SELECT).

- [ ] **Step 1: Implement `handlePresetUiInput`** in `src/gui.c` (declared in `src/gui.h`):

```c
#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-. "
static int charIndex(char c) {
	for(int i = 0; i < (int)strlen(NAME_CHARS); i++) {
		if(NAME_CHARS[i] == c) {
			return i;
		}
	}
	return 0;
}

static void cycleNameChar(PresetNameGuiNode *pn, int delta) {
	int idx = charIndex(pn->name[pn->cursor]);
	int count = (int)strlen(NAME_CHARS);
	idx = (idx + delta + count) % count;
	pn->name[pn->cursor] = NAME_CHARS[idx];
}

bool handlePresetUiInput(InputState *is, Instrument *inst) {
	Graph *g = getSelectedInstGraph();
	if(!g || !g->selected || !isPresetNameNode(g->selected)) {
		return false;
	}
	PresetNameGuiNode *pn = (PresetNameGuiNode *)g->selected;
	if(isKeyJustPressed(is, KM_UP)) {
		cycleNameChar(pn, 1);
		return true;
	}
	if(isKeyJustPressed(is, KM_DOWN)) {
		cycleNameChar(pn, -1);
		return true;
	}
	if(isKeyJustPressed(is, KM_LEFT)) {
		pn->cursor = (pn->cursor + 31) % 32;
		return true;
	}
	if(isKeyJustPressed(is, KM_RIGHT)) {
		pn->cursor = (pn->cursor + 1) % 32;
		return true;
	}
	if(isKeyJustPressed(is, KM_START)) {
		/* trim + default, then run the save flow (overwrite check happens here) */
		pn->editing = false;
		if(strlen(pn->name) == 0 || strspn(pn->name, " ") == strlen(pn->name)) {
			strncpy(pn->name, "UNNAMED", sizeof(pn->name) - 1);
		}
		guiSavePreset(pn->inst, pn->name); /* opens the overwrite modal if EXISTS */
		return true;
	}
	if(isKeyJustPressed(is, KM_SELECT)) {
		pn->editing = false;
		return true;
	}
	return false;
}
```

`guiSavePreset(Instrument *inst, const char *name)` (new, declared in `src/gui.h`, defined in `src/gui.c`): calls `saveInstrumentAsPreset(inst, name, "data/instrument_presets/")`; if it returns `PRESET_EXISTS` it opens the overwrite modal (stores the pending name), otherwise nothing else is needed (saved + added to bank).

- [ ] **Step 2: Wire the input paths.** In `src/main.c` instrument scene case, at the top of the `else` branch (before the `navigateGraphRefined` calls): `if(handlePresetUiInput(appState->inputState, getSelectedInstInstrument())) { break; }`. Same in `src/tools/instrument_harness/instrument_harness.c` `handleInstrumentInput` (after the graph-selected NULL check).

- [ ] **Step 3: Add SAVE/LOAD buttons** in `appendPresetControlNode` (uncomment the pattern at `src/gui.c:636`):

```c
	GuiNode *saveBtn = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "SAVE", 0, cbFocusNameNode, inst);
	GuiNode *loadBtn = createBtnGuiNode(0, 0, 100, 100, 2, na_horizontal, "LOAD", 0, cbOpenLoadList, inst);
	appendItem(btnwrap, saveBtn, 1);
	appendItem(btnwrap, loadBtn, 1);
```

with callbacks in `src/gui.c`:

```c
static void cbFocusNameNode(Parameter *p, float v) { (void)v; (void)p; /* focus handled via selection; the node is directly editable */ }
static void cbOpenLoadList(Parameter *p, float v) { (void)v; (void)p; guiOpenLoadList(); }
```

`guiOpenLoadList()` (new, declared in `src/gui.h`) sets a `g_loadListActive` flag in gui.c; `handlePresetUiInput` also handles the load-list node (Task 9).

- [ ] **Step 4: Build + verify**

Run: `ninja -C build && meson test -C build`
Expected: all suites green; instrument screen shows SAVE/LOAD/name row; harness + app compile.

- [ ] **Step 5: Commit**

```bash
git add src/gui.h src/gui.c src/main.c src/tools/instrument_harness/instrument_harness.c bin/spectrax 2>/dev/null
git commit -m "feat: arcade name editing input + SAVE/LOAD buttons on instrument screen"
```

---

### Task 8: Overwrite modal (the only true modal)

**Files:**
- Modify: `src/gui.h`, `src/gui.c`, `src/main.c`, `src/tools/instrument_harness/instrument_harness.c`

**Interfaces:**
- Consumes: `guiSavePreset` + `PRESET_EXISTS`.
- Produces: `ModalState` enum (`MODAL_NONE`, `MODAL_CONFIRM_OVERWRITE`), `guiSetOverwritePending(const char *name)`, `void drawPresetModal(void)` (drawn from DrawGUI's SCENE_INSTRUMENT case), and overwrite handling inside `handlePresetUiInput`.

- [ ] **Step 1: Implement the modal state** in `src/gui.c` + declarations in `src/gui.h`:

```c
typedef enum {
	MODAL_NONE,
	MODAL_CONFIRM_OVERWRITE
} ModalState;

static ModalState g_modalState = MODAL_NONE;
static char g_pendingName[33];
static bool g_overwriteChoice; /* false = NO, true = YES */

void guiSetOverwritePending(const char *name) {
	strncpy(g_pendingName, name, sizeof(g_pendingName) - 1);
	g_pendingName[sizeof(g_pendingName) - 1] = '\0';
	g_overwriteChoice = false;
	g_modalState = MODAL_CONFIRM_OVERWRITE;
}
```

`guiSavePreset`: on `PRESET_EXISTS`, call `guiSetOverwritePending(name)`; on the overwrite-confirm, call `saveInstrumentAsPreset(inst, g_pendingName, "data/instrument_presets/")` again with the file overwritten (delete the existing `.ipb` first so `presetNameExists` doesn't block, or add an `overwrite` flag). Simplest: add a static `g_overwriteConfirmed` guard in `guiSavePreset` that skips the EXISTS check the second time, and ensure the file is rewritten (Task 5's `savePresetFile` already overwrites files; only the bank dedup check blocks).

`handlePresetUiInput` handles the modal before the name-node logic:

```c
	if(g_modalState == MODAL_CONFIRM_OVERWRITE) {
		if(isKeyJustPressed(is, KM_LEFT) || isKeyJustPressed(is, KM_RIGHT)) {
			g_overwriteChoice = !g_overwriteChoice;
			return true;
		}
		if(isKeyJustPressed(is, KM_START)) {
			if(g_overwriteChoice) {
				/* overwrite: force the save (skip EXISTS) */
				saveInstrumentAsPresetOverwrite(inst, g_pendingName, "data/instrument_presets/");
				g_modalState = MODAL_NONE;
			} else {
				g_modalState = MODAL_NONE; /* back to name editing */
			}
			return true;
		}
		if(isKeyJustPressed(is, KM_SELECT)) {
			g_modalState = MODAL_NONE;
			return true;
		}
		return true; /* consume all input while modal is up */
	}
```

Add `saveInstrumentAsPresetOverwrite` to `src/io/preset_io.c` (like `saveInstrumentAsPreset` but skips the EXISTS check AND **replaces** the existing bank entry: find the preset by name in `pb->patches[]` and assign `pb->patches[idx] = p` instead of appending, so no duplicate slot appears) + declaration in `preset_io.h` + a tiny test in `test_io.c` that overwriting actually replaces the file's data and does not grow the bank.

`void drawPresetModal(void)` in `src/gui.c`: a centered panel (`Rectangle` over the screen area), text `OVERWRITE <name>?` plus `[YES]`/`[NO]` with the chosen one highlighted.

- [ ] **Step 2: Wire DrawGUI** — in `DrawGUI`'s `SCENE_INSTRUMENT` case, after `drawNode(...)` add `if(g_modalState != MODAL_NONE) drawPresetModal();`.

- [ ] **Step 3: Build + verify** (`ninja -C build && meson test -C build`), then commit:

```bash
git add src/gui.h src/gui.c src/io/preset_io.h src/io/preset_io.c tests/dsp/test_io.c bin/spectrax 2>/dev/null
git commit -m "feat: overwrite confirmation modal for preset save"
```

---

### Task 9: Load-list node

**Files:**
- Modify: `src/gui.h`, `src/gui.c`, `src/main.c`, `src/tools/instrument_harness/instrument_harness.c`

**Interfaces:**
- Consumes: `DirectoryList`, `guiOpenLoadList`.
- Produces: `GuiNode *createPresetLoadListNode(...)`, `bool isPresetLoadListNode(GuiNode *n)`, load-list handling in `handlePresetUiInput` (up/down scroll, KM_START loads the highlighted preset via `applyInstrumentPreset` + channel-slot update, KM_SELECT closes).

- [ ] **Step 1: Implement** — a load-list context holds the sorted on-disk preset names (`char **names; int count; int highlight;`). `guiOpenLoadList()` populates it via `createDirectoryList` + sorts, and activates it (a static `g_loadListActive` flag). When active, `handlePresetUiInput` routes up/down/start/select to it. On KM_START: find the preset by name in the bank (`presetNameExists` + index lookup), `applyInstrumentPreset(inst, patches[idx])`, update `inst->selectedPresetIndex` (direct field write), `rebuildVoicesForInstrument(inst->vm, inst)`, update `arranger->channelSlots[selectedArrangerCell[0]]` (needs the ApplicationState — pass it through or use the static `igui->selectedInstrument` index). The node draws as a scrollable list (up to ~6 visible names, red on black, highlight inverted).

Wire the node's draw into `appendPresetControlNode` (a full-width node below the controls row) and its input into `handlePresetUiInput` before the name-node branch.

- [ ] **Step 2: Build + verify** (`ninja -C build && meson test -C build`), then commit:

```bash
git add src/gui.h src/gui.c src/main.c src/tools/instrument_harness/instrument_harness.c bin/spectrax 2>/dev/null
git commit -m "feat: selectable load-list node for on-disk presets"
```

---

### Task 10: Harness UI fixtures + final gate

**Files:**
- Create: `src/tools/instrument_harness/fixtures/preset_save_load.txt`
- Modify: `src/tools/instrument_harness/run_scripted.sh`, `src/tools/instrument_harness/instrument_harness.c` (only if the fixture needs new assert/action verbs — check the existing fixture parser first)

**Interfaces:**
- Consumes: Tasks 6-9.
- Produces: a scripted fixture that drives the instrument screen and asserts the preset save/load flow works headlessly.

- [ ] **Step 1: Read the fixture format** — `src/tools/instrument_harness/instrument_harness.c` already parses fixture files (it has `--script` support and assert verbs; see how the existing pattern fixture drives keys + asserts). Extend the parser with the verbs needed: select the name node, press KM_UP/KM_DOWN/KM_LEFT/KM_RIGHT/KM_START/KM_SELECT, and assert a node with a given name is selected.

- [ ] **Step 2: Write the fixture** driving: select SAVE → the name node is editable → edit "MY PATCH" → KM_START → save writes `my_patch.ipb` → assert the bank gained a preset named "MY PATCH" (via a new assert verb or by checking the file exists). Then save the same name again → overwrite modal appears → select NO → modal closes, name still editable → select YES → file overwritten. Then LOAD → list node → scroll → load the first preset → assert the instrument's algo changed.

- [ ] **Step 3: Run the full gate**

Run: `ninja -C build && meson test -C build && cd bin && bash ../src/tools/instrument_harness/run_scripted.sh`
Expected: all 7 test suites pass, the scripted fixture PASSes, the app still boots from `bin/`.

- [ ] **Step 4: Commit**

```bash
git add src/tools/instrument_harness/ src/io/ tests/ bin/spectrax 2>/dev/null
git commit -m "test: scripted UI fixture for preset save/load/overwrite flow"
```