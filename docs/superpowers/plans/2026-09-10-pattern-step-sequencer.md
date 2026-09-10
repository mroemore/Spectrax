# Pattern Step-Sequencer Mod Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `MT_PATTERN` — a tempo-synced step-sequencer modulation source (16 steps, HOLD/LINEAR/SLEW/CURVE shapes, uni/bipolar polarity) as a first-class source in the unified mod-sources framework, clocked by the song's pattern-step playhead via a global clock.

**Architecture:** `PatternState` joins the union'd `Mod.data`; a global `g_patternClock` (written by main.c's audio callback each buffer, read-only by `updateMod`) supplies the per-channel pattern-step playhead + swing-aware step duration. Per-voice clones own independent running state but share the clock. UI: a step grid + dials in the mod-source row, reusing the existing graph/layer machinery.

**Tech Stack:** C (gnu99), meson/ninja, raylib, vendored kissfft/raylib, PortAudio. Tests: `tests/dsp/` (meson test), `src/tools/instrument_harness/` scripted fixtures.

## Global Constraints

- All `GuiNode` fields readable by a walk or generic dispatch must be zero-initialized in `initGuiNode` (rule #1154).
- Any mutation of `inst->modList`/`inst->paramList`/voice pools from the GUI thread must hold `g_audioLock` + set `inst->rebuilding` (rules #1058/#1065/#1078 pattern).
- No unconditional temp hooks in `main.c` (rule #1136) — the clock write is a permanent feature line, not a probe.
- New mod/UI work follows the ground-up test order: mod-system edge tests → voice/instrument integration → automated UI (rule #1063).
- Pattern sources are **runtime-only** — NOT persisted in presets or the song file (spec section E).
- Do NOT modify `src/voice.c`'s seeded driver connections or `initVoiceDrivers` (the default FM/BLEP/SAMPLE seeds stay as-is; patterns are added at runtime only).

---

### Task 1: MT_PATTERN enum + PatternState union + g_patternClock

**Files:**
- Modify: `src/modsystem.h`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: existing `ModType` enum, `Mod` union, `MAX_ENVELOPE_STAGES` pattern.
- Produces: `MT_PATTERN` enum value, `PatternState` struct, `PatternClock` type + `extern PatternClock g_patternClock;`.

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_modsystem.c` (near the other MT_ type tests):

```c
static int test_pattern_clock_global(void) {
	extern PatternClock g_patternClock;
	/* Global clock is plain writable state (not const — the audio
	 * callback writes it each buffer, tests drive it directly). */
	g_patternClock.playhead[0] = 3;
	g_patternClock.stepDuration = 7603;
	ASSERT_TRUE(g_patternClock.playhead[0] == 3, "clock playhead writable");
	ASSERT_TRUE(g_patternClock.stepDuration == 7603, "clock step duration writable");
	printf("PASS test_pattern_clock_global\n");
	return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build test_modsystem`
Expected: compile fails — `PatternClock` undefined.

- [ ] **Step 3: Implement**

In `src/modsystem.h`:

```c
/* Shape modes for a PatternState. */
typedef enum {
	SH_HOLD = 0,
	SH_LINEAR,
	SH_SLEW,
	SH_CURVE,
	SH_COUNT
} PatternShape;

/* Polarity modes for a PatternState. */
typedef enum {
	PP_UNIPOLAR = 0,
	PP_BIPOLAR,
	PP_COUNT
} PatternPolarity;

#define MAX_PATTERN_STEPS 16
#define MAX_SEQUENCER_CHANNELS 16

/* The tempo-synced pattern clock. Written by main.c's audio callback each
 * buffer; read-only by the mod system's updateMod MT_PATTERN case. */
typedef struct {
	int playhead[MAX_SEQUENCER_CHANNELS]; /* sequencer->playhead_index[ch] */
	int stepDuration;                     /* current step duration in samples */
} PatternClock;
extern PatternClock g_patternClock;
```

Add `MT_PATTERN` to the `ModType` enum:

```c
typedef enum {
	MT_LFO, // Low-frequency oscillator
	MT_ENV, // Envelope
	MT_RND, // Random
	MT_OFS, // Constant Offset
	MT_ATTEN, // Attenuator (per-connection, inserted by addModulation)
	MT_PATTERN, // Tempo-synced step-sequencer
	MT_COUNT
} ModType;
```

Add `PatternState` before the `Mod` struct (alongside `AttenState`/`EnvState`/`LfoState`/`RndState`):

```c
typedef struct {
	Parameter *length;    /* 1..16 (int param) */
	Parameter *shape;     /* PatternShape (SH_HOLD/LINEAR/SLEW/CURVE) */
	Parameter *slew;      /* glide time, 0..1 (SLEW mode) */
	Parameter *polarity;  /* PatternPolarity (PP_UNIPOLAR/BIPOLAR) */
	float steps[MAX_PATTERN_STEPS]; /* stored 0..1 */
	int stepCount;        /* 1..length */
	int channel;          /* which channel's playhead clocks this source */
	/* per-voice running state (cloned independently per voice) */
	int currentStep;
	float currentValue;   /* the output value */
	float stepProgress;   /* 0..1 within the current song step */
	float startValue;     /* the value at the start of the current step */
	int lastPlayhead;     /* boundary detection */
} PatternState;
```

Add `PatternState pattern;` to the `Mod` union.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C build test_modsystem`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.h tests/dsp/test_modsystem.c
git commit -m "feat(mod): MT_PATTERN enum, PatternState union member, global PatternClock"
```

---

### Task 2: initPatternDefaults + generatePattern + updateMod advance

**Files:**
- Modify: `src/modsystem.c`, `src/modsystem.h`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: `PatternState` (Task 1), `g_patternClock` (Task 1), existing `createParameter`/`setParameterBaseValue`/`setParameterValue`/`getParameterValueAsInt`.
- Produces: `void initPatternDefaults(Mod *mod, ParamList *paramList, int channel);` and `void generatePattern(void *self);` (used as `mod->generate`). `updateMod` gains an `MT_PATTERN` case.

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_modsystem.c`:

```c
static int test_pattern_boundary_advances_step(void) {
	extern PatternClock g_patternClock;
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Mod *p = (Mod *)calloc(1, sizeof(Mod));
	initPatternDefaults(p, pl, 2);
	addToModList(ml, p);
	/* length 4, all steps 0.5 except step1 = 0.8 */
	setParameterValue(p->data.pattern.length, 4.0f);
	setParameterBaseValue(p->data.pattern.length, 4.0f);
	p->data.pattern.steps[0] = 0.5f;
	p->data.pattern.steps[1] = 0.8f;
	p->data.pattern.steps[2] = 0.5f;
	p->data.pattern.steps[3] = 0.5f;
	p->data.pattern.stepCount = 4;

	g_patternClock.playhead[2] = 0;
	g_patternClock.stepDuration = 100;
	p->data.pattern.lastPlayhead = 0;

	/* playhead moves to step 1 -> boundary -> currentStep advances */
	g_patternClock.playhead[2] = 1;
	updateMod(p, 0.005f);
	ASSERT_TRUE(p->data.pattern.currentStep == 1, "currentStep follows playhead");
	ASSERT_TRUE(p->data.pattern.stepProgress > 0.0f, "stepProgress advances");

	freeModList(ml);
	freeParamList(pl);
	printf("PASS test_pattern_boundary_advances_step\n");
	return 0;
}

static int test_pattern_shapes(void) {
	extern PatternClock g_patternClock;
	ParamList *pl = createParamList();
	Mod *p = (Mod *)calloc(1, sizeof(Mod));
	initPatternDefaults(p, pl, 0);
	p->data.pattern.steps[0] = 1.0f;
	p->data.pattern.stepCount = 1;
	g_patternClock.playhead[0] = 0;
	g_patternClock.stepDuration = 100;
	p->data.pattern.lastPlayhead = 0;

	/* HOLD: output = target */
	p->data.pattern.currentStep = 0;
	p->data.pattern.stepProgress = 0.5f;
	p->data.pattern.startValue = 0.0f;
	p->data.pattern.shape->baseValue = (float)SH_HOLD;
	p->data.pattern.shape->currentValue = (float)SH_HOLD;
	generatePattern(p);
	ASSERT_TRUE(getParameterValue(p->output) == 1.0f, "HOLD outputs target");

	/* LINEAR: output = mix(start, target, progress) */
	p->data.pattern.shape->baseValue = (float)SH_LINEAR;
	p->data.pattern.shape->currentValue = (float)SH_LINEAR;
	p->data.pattern.stepProgress = 0.5f;
	generatePattern(p);
	ASSERT_TRUE(fabsf(getParameterValue(p->output) - 0.5f) < 0.001f, "LINEAR mixes by progress");

	/* BIPOLAR: output = value*2 - 1 */
	p->data.pattern.polarity->baseValue = (float)PP_BIPOLAR;
	p->data.pattern.polarity->currentValue = (float)PP_BIPOLAR;
	p->data.pattern.shape->baseValue = (float)SH_HOLD;
	p->data.pattern.shape->currentValue = (float)SH_HOLD;
	p->data.pattern.currentValue = 1.0f;
	generatePattern(p);
	ASSERT_TRUE(fabsf(getParameterValue(p->output) - 1.0f) < 0.001f, "BIPOLAR maps 1.0 to 1.0");
	p->data.pattern.currentValue = 0.0f;
	generatePattern(p);
	ASSERT_TRUE(fabsf(getParameterValue(p->output) - (-1.0f)) < 0.001f, "BIPOLAR maps 0.0 to -1.0");

	/* length wraps: step % stepCount */
	p->data.pattern.stepCount = 4;
	p->data.pattern.lastPlayhead = 3;
	g_patternClock.playhead[0] = 4; /* step 4 of a 4-step seq wraps to 0 */
	updateMod(p, 0.001f);
	ASSERT_TRUE(p->data.pattern.currentStep == 0, "length wraps (step % stepCount)");

	freeParamList(pl);
	free(p);
	printf("PASS test_pattern_shapes\n");
	return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build test_modsystem`
Expected: compile fails — `initPatternDefaults`/`generatePattern` undefined.

- [ ] **Step 3: Implement**

In `src/modsystem.h`, declare:

```c
void initPatternDefaults(Mod *mod, ParamList *paramList, int channel);
void generatePattern(void *self);
```

In `src/modsystem.c` (near `initRandDefaults`):

```c
void initPatternDefaults(Mod *mod, ParamList *paramList, int channel) {
	PatternState *p = &mod->data.pattern;
	p->length = createParameter(paramList, "PTN len", 16.0f, 1.0f, (float)MAX_PATTERN_STEPS);
	p->shape = createParameter(paramList, "PTN shape", 0.0f, 0.0f, (float)(SH_COUNT - 1));
	p->slew = createParameter(paramList, "PTN slew", 0.2f, 0.0f, 1.0f);
	p->polarity = createParameter(paramList, "PTN pol", 0.0f, 0.0f, (float)(PP_COUNT - 1));
	for(int i = 0; i < MAX_PATTERN_STEPS; i++) {
		p->steps[i] = 0.5f;
	}
	p->stepCount = MAX_PATTERN_STEPS;
	p->channel = channel;
	p->currentStep = 0;
	p->currentValue = 0.5f;
	p->stepProgress = 0.0f;
	p->startValue = 0.5f;
	p->lastPlayhead = g_patternClock.playhead[channel];
	mod->generate = generatePattern;
}

void generatePattern(void *self) {
	Mod *mod = (Mod *)self;
	PatternState *p = &mod->data.pattern;
	if(!p || !p->shape || !p->slew || !p->polarity) {
		return;
	}
	float target = p->steps[p->currentStep];
	int shape = getParameterValueAsInt(p->shape);
	switch(shape) {
		case SH_HOLD:
			p->currentValue = target;
			break;
		case SH_LINEAR:
			p->currentValue = p->startValue + (target - p->startValue) * p->stepProgress;
			break;
		case SH_CURVE: {
			/* smoothstep */
			float t = p->stepProgress;
			float e = t * t * (3.0f - 2.0f * t);
			p->currentValue = p->startValue + (target - p->startValue) * e;
			break;
		}
		case SH_SLEW: {
			/* one-pole glide: time constant from the slew param (0..1).
			 * slews toward target independently of stepProgress. */
			float tc = 1.0f - getParameterValue(p->slew);
			tc = (tc < 0.01f) ? 0.01f : tc;
			p->currentValue += (target - p->currentValue) * (1.0f - expf(-1.0f / tc));
			break;
		}
		default:
			break;
	}
	float out = (getParameterValueAsInt(p->polarity) == PP_BIPOLAR)
		? p->currentValue * 2.0f - 1.0f
		: p->currentValue;
	setParameterBaseValue(mod->output, out);
	setParameterValue(mod->output, out);
}
```

In `src/modsystem.c`, add an `MT_PATTERN` case to `updateMod`:

```c
		case MT_PATTERN: {
			PatternState *p = &mod->data.pattern;
			int playhead = (p->channel >= 0 && p->channel < MAX_SEQUENCER_CHANNELS)
				? g_patternClock.playhead[p->channel] : 0;
			if(playhead != p->lastPlayhead) {
				/* step boundary: capture new target + reset progress */
				p->currentStep = playhead % p->stepCount;
				p->lastPlayhead = playhead;
				p->startValue = p->currentValue;
				p->stepProgress = 0.0f;
			}
			if(g_patternClock.stepDuration > 0) {
				p->stepProgress += deltaTime / (float)g_patternClock.stepDuration;
				if(p->stepProgress > 1.0f) {
					p->stepProgress = 1.0f;
				}
			}
			break;
		}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C build test_modsystem`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.c src/modsystem.h tests/dsp/test_modsystem.c
git commit -m "feat(mod): PatternState defaults, generatePattern shapes, updateMod step clock"
```

---

### Task 3: clock write in main.c's audio callback

**Files:**
- Modify: `src/main.c`
- Test: `tests/dsp/test_modsystem.c` (clock struct already covered by Task 1)

**Interfaces:**
- Consumes: `g_patternClock` (Task 1), `data->sequencer->playhead_index[]` + `data->arranger->tempoSettings.*` (existing).
- Produces: the clock populated every buffer on the audio thread.

- [ ] **Step 1: Write the failing test**

The clock write is inside the PortAudio callback (not unit-testable — GUI tests segfault without GL, rule #1121). Covered at the fixture level (Task 7). This task's verification is the app boot + fixtures. No new unit test here.

- [ ] **Step 2: Implement**

In `src/main.c`, inside `patestCallback`, immediately after the `advanceSequencerStep(...)` call (inside the `if(data->arranger->playing)` block):

```c
			/* Pattern clock: tempo-synced mod sources read the per-channel
			 * pattern-step playhead + swing-aware step duration. */
			for(int ch = 0; ch < data->arranger->enabledChannels; ch++) {
				g_patternClock.playhead[ch] = data->sequencer->playhead_index[ch];
			}
			g_patternClock.stepDuration = stepSamples;
```

Ensure `g_patternClock` is included via `modsystem.h` (already in main.c's include chain). Verify zero-init: add `g_patternClock` to the BSS (it is a global struct — zero-initialized automatically).

- [ ] **Step 3: Verify it builds + boots**

Run: `ninja -C build && meson test -C build` — all suites green, app boots (the clock write is inside the `playing` guard; no behavioral change when stopped).

- [ ] **Step 4: Commit**

```bash
git add src/main.c
git commit -m "feat(audio): write per-channel pattern clock in the audio callback"
```

---

### Task 4: cloneMod + syncModValues + changeModType + removeMod MT_PATTERN cases

**Files:**
- Modify: `src/modsystem.c`
- Test: `tests/dsp/test_modsystem.c`

**Interfaces:**
- Consumes: `PatternState` + `initPatternDefaults` (Tasks 1-2).
- Produces: `MT_PATTERN` handled in `cloneMod`, `syncModValues`, `changeModType`, `removeMod`, and the free-by-type switch.

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_modsystem.c`:

```c
static int test_pattern_clone_and_retype(void) {
	ParamList *pl = createParamList();
	ModList *ml = createModList();
	Mod *p = (Mod *)calloc(1, sizeof(Mod));
	initPatternDefaults(p, pl, 1);
	addToModList(ml, p);
	p->data.pattern.steps[0] = 0.9f;
	p->data.pattern.stepCount = 2;

	/* clone into a voice's lists */
	ParamList *vpl = createParamList();
	ModList *vml = createModList();
	Mod *c = cloneMod(vpl, vml, p);
	ASSERT_TRUE(c != NULL && c->type == MT_PATTERN, "clone is MT_PATTERN");
	ASSERT_TRUE(c->data.pattern.channel == 1, "clone keeps channel");
	ASSERT_TRUE(c->data.pattern.stepCount == 2, "clone keeps stepCount");
	ASSERT_TRUE(fabsf(c->data.pattern.steps[0] - 0.9f) < 0.001f, "clone keeps steps");

	/* changeModType PTN -> ENV keeps routes/output */
	ModConnection *conn = (ModConnection *)calloc(1, sizeof(ModConnection));
	conn->source = p;
	conn->type = createParameter(vpl, "type", 0, 0, 3);
	ASSERT_TRUE(changeModType(ml, p, MT_ENV, pl), "retype PTN->ENV ok");
	ASSERT_TRUE(p->type == MT_ENV, "type changed");
	ASSERT_TRUE(conn->source == p, "route survives (pointer stable)");
	free(conn->type);
	free(conn);

	/* removeMod on a pattern source frees its params cleanly */
	Mod *p2 = (Mod *)calloc(1, sizeof(Mod));
	initPatternDefaults(p2, vpl, 0);
	addToModList(vml, p2);
	ASSERT_TRUE(removeMod(vml, vpl, p2), "remove pattern source ok");

	freeModList(ml);
	freeModList(vml);
	freeParamList(pl);
	freeParamList(vpl);
	printf("PASS test_pattern_clone_and_retype\n");
	return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build test_modsystem`
Expected: compile fails or `changeModType` returns false for MT_PATTERN (guard excludes it).

- [ ] **Step 3: Implement**

In `src/modsystem.c`:

**`changeModType`** — allow MT_PATTERN:

```c
if(newType != MT_ENV && newType != MT_LFO && newType != MT_RND && newType != MT_PATTERN) {
		return false;
	}
```

Add a `case MT_PATTERN:` to the old-type param-free switch:

```c
		case MT_PATTERN: {
			PatternState *p = &mod->data.pattern;
			if(p->length) removeFromParamList(paramList, p->length);
			if(p->shape) removeFromParamList(paramList, p->shape);
			if(p->slew) removeFromParamList(paramList, p->slew);
			if(p->polarity) removeFromParamList(paramList, p->polarity);
			break;
		}
```

Add a `case MT_PATTERN:` to the new-type init switch:

```c
		case MT_PATTERN:
			mod->type = MT_PATTERN;
			initPatternDefaults(mod, paramList, 0);
			break;
```

**`cloneMod`** — add an MT_PATTERN case (copy steps, params, channel, running state):

```c
		case MT_PATTERN: {
			PatternState *p = &c->data.pattern;
			const PatternState *sp = &src->data.pattern;
			p->stepCount = sp->stepCount;
			p->channel = sp->channel;
			p->currentStep = sp->currentStep;
			p->currentValue = sp->currentValue;
			p->stepProgress = sp->stepProgress;
			p->startValue = sp->startValue;
			p->lastPlayhead = sp->lastPlayhead;
			memcpy(p->steps, sp->steps, sizeof(p->steps));
			if(sp->length) {
				p->length = createParameter(voicePl, sp->length->name,
					sp->length->baseValue, sp->length->minValue, sp->length->maxValue);
			}
			if(sp->shape) {
				p->shape = createParameter(voicePl, sp->shape->name,
					sp->shape->baseValue, sp->shape->minValue, sp->shape->maxValue);
			}
			if(sp->slew) {
				p->slew = createParameter(voicePl, sp->slew->name,
					sp->slew->baseValue, sp->slew->minValue, sp->slew->maxValue);
			}
			if(sp->polarity) {
				p->polarity = createParameter(voicePl, sp->polarity->name,
					sp->polarity->baseValue, sp->polarity->minValue, sp->polarity->maxValue);
			}
			break;
		}
```

**`syncModValues`** — add an MT_PATTERN case (copy base values only; the steps + running state are NOT synced — they live on the instrument source as the editor's truth, but step data changes need to reach clones... the spec says step editing edits the instrument source; clones get step updates via this sync):

```c
		case MT_PATTERN: {
			PatternState *p = &c->data.pattern;
			const PatternState *sp = &src->data.pattern;
			if(p->length && sp->length) setParameterBaseValue(p->length, sp->length->baseValue);
			if(p->shape && sp->shape) setParameterBaseValue(p->shape, sp->shape->baseValue);
			if(p->slew && sp->slew) setParameterBaseValue(p->slew, sp->slew->baseValue);
			if(p->polarity && sp->polarity) setParameterBaseValue(p->polarity, sp->polarity->baseValue);
			p->stepCount = sp->stepCount;
			memcpy(p->steps, sp->steps, sizeof(p->steps));
			break;
		}
```

**`removeMod`** — add MT_PATTERN to the param-removal switch:

```c
		case MT_PATTERN: {
			PatternState *p = &mod->data.pattern;
			if(p->length) removeFromParamList(paramList, p->length);
			if(p->shape) removeFromParamList(paramList, p->shape);
			if(p->slew) removeFromParamList(paramList, p->slew);
			if(p->polarity) removeFromParamList(paramList, p->polarity);
			break;
		}
```

(MT_PATTERN needs no custom free — falls through the `default: freeMod(mod);` branch.)

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C build test_modsystem`
Expected: PASS. Also `meson test -C build test_mod_voice` (the clone path is exercised).

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.c tests/dsp/test_modsystem.c
git commit -m "feat(mod): MT_PATTERN in clone/sync/retype/remove lifecycle"
```

---

### Task 5: addRuntimePattern source creation

**Files:**
- Modify: `src/gui_inst_mod.c`, `src/modsystem.c`, `src/modsystem.h`
- Test: `tests/dsp/test_mod_voice.c`

**Interfaces:**
- Consumes: `initPatternDefaults` (Task 2), `modSourceCount`, `rebuildVoicesForInstrument`, `g_audioLock`/`rebuilding`.
- Produces: `void addRuntimePattern(Instrument *inst, int channel);` (mirrors `addRuntimeSource` but creates an MT_PATTERN source).

- [ ] **Step 1: Write the failing test**

Add to `tests/dsp/test_mod_voice.c` (after `test_runtime_source_lifecycle`, using its exact setup shape):

```c
static int test_add_runtime_pattern(void) {
	extern PatternClock g_patternClock;
	(void)g_patternClock;
	SamplePool *sp = createSamplePool();
	PresetBank pb;
	initPresetBank(&pb);
	Instrument *inst = NULL;
	init_instrument(&inst, VOICE_TYPE_FM, sp, &pb);
	ASSERT_TRUE(inst != NULL, "init_instrument FM");
	int before = modSourceCount(inst->modList);

	addRuntimePattern(inst, 2);
	ASSERT_TRUE(modSourceCount(inst->modList) == before + 1, "pattern source added");
	int mi = modIndexAt(inst->modList, before);
	ASSERT_TRUE(mi >= 0 && inst->modList->mods[mi]->type == MT_PATTERN, "source is MT_PATTERN");
	ASSERT_TRUE(inst->modList->mods[mi]->data.pattern.channel == 2, "channel resolved");
	inst->modList->mods[mi]->data.pattern.steps[0] = 0.75f;

	ASSERT_TRUE(removeSource(inst, before), "remove pattern source ok");
	ASSERT_TRUE(modSourceCount(inst->modList) == before, "source count restored");

	free(inst);
	freeSamplePool(sp);
	printf("PASS test_add_runtime_pattern\n");
	return 0;
}
```

Register `test_add_runtime_pattern` in `main()` (register it in `tests/dsp/test_mod_voice.c`'s main like the other tests).

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build test_mod_voice`
Expected: compile fails — `addRuntimePattern` undefined.

- [ ] **Step 3: Implement**

In `src/modsystem.h`, declare:

```c
Mod *createPattern(ParamList *paramList, ModList *modList, int channel, const char *name);
```

In `src/modsystem.c`, add (near `createRandom`):

```c
Mod *createPattern(ParamList *paramList, ModList *modList, int channel, const char *name) {
	Mod *p = (Mod *)calloc(1, sizeof(Mod));
	if(!p) {
		return NULL;
	}
	initMod(p, paramList, name, MT_PATTERN, generatePattern);
	initPatternDefaults(p, paramList, channel);
	if(modList) {
		addToModList(modList, p);
	}
	return p;
}
```

In `src/gui_inst_mod.c`, add `addRuntimePattern` (mirror `addRuntimeSource` at line ~248):

```c
void addRuntimePattern(Instrument *inst, int channel) {
	if(!inst || !inst->modList || modSourceCount(inst->modList) >= MAX_ENVELOPES) {
		return;
	}
	pthread_mutex_lock(&g_audioLock);
	inst->rebuilding = true;
	Mod *p = createPattern(inst->paramList, inst->modList, channel, "PTN");
	if(p && inst->envelopeCount < MAX_ENVELOPES) {
		inst->envelopes[inst->envelopeCount] = p;
	}
	inst->envelopeCount = modSourceCount(inst->modList);
	if(inst->vm) {
		rebuildVoicesForInstrument(inst->vm, inst);
	}
	rebuildInstrumentGraph();
	inst->rebuilding = false;
	pthread_mutex_unlock(&g_audioLock);
}
```

Declare `addRuntimePattern` in `src/gui.h` (next to `addRuntimeSource` at `src/gui.h:320`).

Note: mirror the existing `test_runtime_source_lifecycle` setup exactly (`createSamplePool` + `initPresetBank` + `init_instrument`); `modIndexAt`/`removeSource` take the source position (see the existing test's use).

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C build test_mod_voice`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/modsystem.c src/modsystem.h src/gui_inst_mod.c tests/dsp/test_mod_voice.c
git commit -m "feat(mod): addRuntimePattern — runtime MT_PATTERN source creation"
```

---

### Task 6: type cycle + source-row UI (step grid + dials)

**Files:**
- Modify: `src/gui_inst_mod.c`
- Test: `tests/dsp/test_graph_nav.c` (node factory geometry)

**Interfaces:**
- Consumes: `MT_PATTERN`, `PatternState`, `appendModSourceEntry`'s per-type switch, `modTypeTag`, `cbCycleSourceType` (all in `src/gui_inst_mod.c`).
- Produces: MT_PATTERN included in the type cycle + a rendered pattern source row (step grid + LENGTH/SHAPE/SLEW/POLARITY dials + ROUTE/DEL). Verified by the Task 7 harness fixture.

- [ ] **Step 1: Write the failing test**

`modTypeTag` and `cbCycleSourceType` are `static` in `gui_inst_mod.c` (not reachable from `tests/dsp/test_graph_nav.c`), and GUI construction requires a GL context (rule #1121). The type-cycle + step-grid behavior is verified end-to-end by the Task 7 harness fixture (the `PTN` tag appears after cycling RND→PTN, and the grid navigates). No new unit test in this task — the fixture is the gate.

- [ ] **Step 2: Build + verify app compiles**

Run: `ninja -C build`
Expected: 0 errors.

- [ ] **Step 3: Implement**

In `src/gui_inst_mod.c`:

**`modTypeTag`** — add the pattern case:

```c
		case MT_PATTERN: return "PTN";
```

**`cbCycleSourceType`** — extend the cycle:

```c
	switch(mod->type) {
		case MT_ENV: next = MT_LFO; break;
		case MT_LFO: next = MT_RND; break;
		case MT_RND: next = MT_PATTERN; break;
		default:     next = MT_ENV; break;
	}
```

**`appendModSourceEntry`** — add an MT_PATTERN case to the per-type switch. The pattern row is taller than the others (the step grid needs vertical space). The step grid is a single selectable node whose draw renders the 16 cells from `PatternState`:

```c
		case MT_PATTERN: {
			PatternState *p = &mod->data.pattern;
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "LEN", selected, incParameterBaseValue, p->length), 3);
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SHAPE", 0, incParameterBaseValue, p->shape), 3);
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "SLEW", 0, incParameterBaseValue, p->slew), 3);
			appendItem(wrap, createDialGuiNode(0, 0, 100, 100, 2, na_horizontal, "POL", 0, incParameterBaseValue, p->polarity), 3);
			break;
		}
```

Add a **step-grid node** factory + draw. The grid is a `GuiNode` whose `p` is a `PatternGridData` { `PatternState *pattern; int selectedStep; bool editing; }`, selectable (navigable across 16 cells), with KM_EDIT toggling edit mode and arrows adjusting `steps[selectedStep]`:

```c
/* PatternGridData — p payload for the step-grid node in a pattern source row. */
typedef struct {
	PatternState *pattern;
	int selectedStep;
	bool editing;
} PatternGridData;

static void drawPatternGridNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	PatternGridData *d = (PatternGridData *)gn->p;
	if(!d || !d->pattern) {
		return;
	}
	PatternState *p = d->pattern;
	int n = p->stepCount;
	float cellW = (float)gn->w / (float)MAX_PATTERN_STEPS;
	for(int i = 0; i < MAX_PATTERN_STEPS; i++) {
		float cx = gn->x + i * cellW;
		float h = (i < n) ? p->steps[i] * (float)gn->h : 2.0f;
		Color col = (i == p->currentStep) ? cs.highlightedCell
			: (i == d->selectedStep) ? cs.outlineColour : cs.defaultCell;
		DrawRectangle((int)cx, (int)(gn->y + gn->h - h), (int)cellW - 1, (int)h, col);
	}
}

static GuiNode *createPatternGridNode(PatternState *pattern) {
	GuiNode *n = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "STEPS", 0, 0);
	n->drawable = true;
	n->draw = drawPatternGridNode;
	PatternGridData *d = (PatternGridData *)calloc(1, sizeof(PatternGridData));
	d->pattern = pattern;
	n->p = d;
	return n;
}
```

Wire grid editing into the input handler (the same place `layerStackInput`/`handleInstrumentInput` handles the other per-node key logic — follow the existing pattern where KM_EDIT on a selected node fires its actionCb; for the grid, KM_EDIT toggles `d->editing`, arrows adjust the step when `d->editing`).

- [ ] **Step 4: Run the build + fixtures sanity**

Run: `ninja -C build` + `src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/mod_sources.txt`
Expected: 0 errors; fixture still PASSes (the PTN row is added, existing nav unaffected).

- [ ] **Step 5: Commit**

```bash
git add src/gui_inst_mod.c
git commit -m "feat(gui): pattern source row — step grid + LEN/SHAPE/SLEW/POL dials, type cycle includes PTN"
```

---

### Task 7: fixture + full gate

**Files:**
- Modify: `src/tools/instrument_harness/instrument_harness.c` (if a new verb is needed), `src/tools/instrument_harness/fixtures/mod_sources.txt` (extend), or a new fixture.
- Test: fixture + full suite.

**Interfaces:**
- Consumes: everything from Tasks 1-6.
- Produces: end-to-end proof the pattern source works in the real app graph.

- [ ] **Step 1: Write the fixture**

Extend `mod_sources.txt` (or add a `pattern_source.txt` fixture) with a pattern block:

```
# --- PTN type-cycle + step grid -------------------------------------------
# After the existing RND cycle block lands back on ENV, cycle RND->PTN.
# (Mirror the existing nav to the TYPE button; the cycle now wraps RND->PTN.)
<...nav to TYPE button...>
EDIT
ASSERT selected==PTN
# LEN dial present on the row; step grid present
<...nav to the LEN dial...>
ASSERT selected==LEN
<...nav to the step grid...>
ASSERT selected==STEPS
```

Follow the existing fixture grammar (JUMPTL/SHOW/SHOWTL verbs from the harness). The exact nav steps depend on the row layout after Task 6 — the implementer must run the harness to discover the geometry, exactly as the existing `mod_sources` fixture does.

- [ ] **Step 2: Verify the fixture PASSes**

Run: `src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/pattern_source.txt`
Expected: PASS.

- [ ] **Step 3: Run the full gate**

Run:
- `meson test -C build` — all suites green (incl. the new modsystem/mod_voice/graph_nav tests)
- all existing fixtures PASS (mod_sources, add_route_delete, clear_routes, route_lines_follow_selection, chip_meta, pagenav, preset_save_load, save_overwrite, task4_size_verify, visual_*)
- app boots clean (Xvfb boot, no FPE/segfault)

- [ ] **Step 4: Commit**

```bash
git add src/tools/instrument_harness/instrument_harness.c src/tools/instrument_harness/fixtures/pattern_source.txt
git commit -m "test(harness): pattern-source fixture — PTN type, dials, step grid, gate green"
```