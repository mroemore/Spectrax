# Sampler Character Engine (vintage-sampler emulation) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the sampler instrument into a profile-driven "character chain" — record-time bake (band-limited decimation + coding-law quantization) → playback-time interpolator (drop-sample / linear) → per-voice OTA character filter with slewed, envelope-routable cutoff — with machine profiles (Clean, SP-1200, MPC60, S950, Mirage, SK-1) persisted through preset format V4.

**Architecture:** The chain follows the hardware/TAL ordering: degrade **once** at bake (rate → bits → law), then pitch-shift at playback by reading the baked buffer with a chosen interpolator, then filter per voice. Config is per-instrument: new `Parameter`s on the instrument `paramList` (so they become mod-routing destinations automatically); a `BakedSample` cache per instrument is rebuilt (under the audio lock) whenever a character param changes; machine profiles are a static defaults table applied by one callback that writes the other params with their callbacks suppressed.

**Tech Stack:** C (meson, Unity-style plain-assert test files in `tests/dsp/`), raylib GUI, existing core_lib.

**Spec:** the research corpus at `~/proj/envoy/my_name_is_proxxy/.research/corpus/samplers/` (chiefly `03-emulation-dsp.md`, sections cited per task below) and its condensed article: https://appk.lynsand.tricky.click/articles/samplers-emulation-and-mechanisms/ . The corpus is outside this repo, so the load-bearing facts are inlined in "Grounded facts" below; the plan argues from them.

## Grounded facts (inlined spec)

All from the corpus with its evidence tags: `[V]` verified against primary source, `[P]` published/peer-reviewed, `[C]` contested/approximation.

- **A sampler's character is a chain, not a bit depth.** Order is everything: degrade at *record* time, then pitch at *playback* time (downsample → quantize → [filter] → upsample-to-pitch). Degrading after the pitch shift, or crushing on the output, is the standard way to get it wrong (`03-emulation-dsp.md` §1.4, §8.3).
- **Drop-sample is the SP-1200's actual tuning algorithm** — integer-address accumulator, fractional increment truncated when indexing, samples may repeat on pitch-down, never stalls (`§2.2`, Yeh/Nolting/Smith ICMC 2007 `[P]`; Lytrix PAL reverse-engineering `[P]`).
- **Truncation, not dither**, is the vintage quantizer error profile (signal-correlated harmonics, −Δ/2 DC bias); TPDF is the "clean 8-bit" alternative; noise-shaping is the wrong tool for lo-fi (§1.2).
- **Emu II / Linn LM-1 companding = 8-bit μ-law (G.711)**; MPC60 storage is "12-bit proprietary NON-LINEAR" with no public curve → approximate, flag `[C]` (§0, TAL modes; Roger Linn MPC60 FAQ).
- **Per-voice analog filters:** SSM2044 (SP-1200 ch1–2) = transistor **ladder** (Huovilainen model, needs oversampling); **CEM3320/SSM2040-style OTA cascade** = 4 one-poles in a global feedback loop, **no tanh, no oversampling needed** (§4.5). Mirage = CEM3328 4-pole → OTA cascade is Mirage-accurate. Ladder is deferred to Wave 3.
- **Cutoff slew 200–1000 Hz** (one-pole glide on the cutoff CV) is "the most under-implemented analog-feel parameter" — a step change in cutoff is a broadband click (§4.3).
- Machine ground truth (§0):

| Machine | Bits | Rate | Pitch | Filter |
|---|---|---|---|---|
| SP-1200 | 12 linear | 26.04 kHz fixed `[V]` | drop-sample, no interp | SSM2044 ch1–2, 3–6 fixed LP, 7–8 none |
| MPC60 | 12 non-linear (companded) | 40 kHz | drop-sample (era-typical) | none on voices |
| Akai S950 | 12 linear | 7.5–48k variable | varispeed (linear-interp class) | 6-pole switched-cap |
| Ensoniq Mirage | 8 | 10–33k variable | varispeed | CEM3328 4-pole |
| Casio SK-1 | 8 | 9.38 kHz | varispeed | none |

- **Headroom discipline:** pre-band-limit to rate/2 *before* quantizing; the bake resampler must be band-limited (polyphase/windowed-sinc class), the crudeness comes from truncation + no reconstruction filter + drop-sample reads, not from a sloppy bake (master cheat-sheet step 1, §2.6).

**This plan supersedes the earlier bounded Phase 2/3 sketch** (plain bitcrush + sample-hold on the output path + standalone waveshaper): the bake path implements rate/bits the *correct* way (before pitch), and the waveshaper folds into Wave 1/3's saturation stage.

## Global Constraints

- CTOL code conventions throughout: braces on every body, `/* */` comments only, named constants not magic numbers, null-checks on pointer params, clang-format-clean-ish (match surrounding hand style; the repo is not format-enforced).
- Every new source file: `<PREFIX>_<MODULE>_H` include guard style matching repo (`SAMPLER_CHARACTER_H`), added to `core_sources` in `src/meson.build`; every new test file listed in `test_names` in `tests/meson.build`.
- TDD: each test written first, run RED, then implemented, run GREEN.
- All tests offline-first (`meson test -C build`); interactive verification only via `src/tools/instrument_harness/run_scripted.sh fixtures/<name>.txt` from the repo root. After any fixture run: `git show HEAD:bin/s1.sng > bin/s1.sng` (fixtures mutate it).
- **`meson install -C build` before any fixture run** — fixtures run `bin/` binaries; a stale binary silently voids new asserts (known trap).
- Any temp files under `.tmp_files/`, never `/tmp`.
- **Never push** (#1078: local commits only; user handles the remote after review).
- Never add unconditional test hooks to `main.c` — probes live in gui seams + harness verbs, opt-in by construction (they only exist under the harness run).
- The legacy `getSampleValueFwd`/`getSampleValueRev` stay **byte-identical**: the spectral boot path (`voice.c:1154`) and granular paths still call them; the sampler *voice* path simply stops using them. Do not "fix" the odd `PA_SR/(sr/bit)*2` ratio in place.

## Review Focus

Input classes the tasks' tests must pin:

1. **Empty sample pool at boot** — every channel boots as a SAMPLE instrument with 0 samples (`createVoiceManager`); bake of a NULL/empty source must fail cleanly, `samplerVoiceTick` must return silence, no deref. → Task 2 (`test_bake_null_source`), Task 5 (`test_tick_null_baked_silent`).
2. **Character params changed while a voice sustains** — the bake swap must happen under `g_audioLock` (the callback holds it for the whole buffer), so no torn reads. Not unit-testable; pinned by following the `applyInstrumentPreset` lock pattern exactly + the preset-load fixtures that exercise locked rebuilds. → Task 6 steps, Task 8 regression fixtures.
3. **Very short samples** (baked length < 4) — drop-sample wrap and linear tail must stay in bounds. → Task 4 (`test_baked_drop_wrap_short`).
4. **rate ≥ source rate** — bake must be an exact copy (no sinc rounding at unity) and upsample must lengthen, not truncate. → Task 2 (`test_bake_identity_exact`, `test_bake_upsample_length`).
5. **Old preset files after the format bump** — V3 files must migrate (new fields defaulted, old fields preserved byte-for-byte semantics), V2/V1 chain untouched. → Task 6 (`test_preset_v3_migrates`, `test_preset_v4_roundtrip`; existing `test_io` V2/V1 tests must stay green).

---

### Task 1: Quantizer laws (`src/sampler_character.{c,h}`)

**Files:**
- Create: `src/sampler_character.h`, `src/sampler_character.c`
- Modify: `src/meson.build` (add `sampler_character.c` to `core_sources` beside `sample.c`), `tests/meson.build` (add `'test_sampler_character'` to `test_names`)
- Test: `tests/dsp/test_sampler_character.c`

**Interfaces:**
- Produces:

```c
typedef enum { SQL_OFF, SQL_TRUNC, SQL_ROUND, SQL_TPDF, SQL_ULAW, SQL_COMPAND12, SQL_COUNT } SamplerQuantLaw;
/* Quantize x (roughly [-1,1] float) one sample. law=SQL_OFF passes through.
 * Linear laws use `bits` (clamped [1,24]); SQL_ULAW is G.711 mu=255 8-bit
 * (bits ignored); SQL_COMPAND12 is a mu-law-shaped 12-bit compand (MPC60
 * approximation, corpus [C]). Deterministic: no hidden RNG state (TPDF uses a
 * local xorshift seeded from a fixed constant so tests are reproducible). */
float samplerQuantize(float x, int bits, SamplerQuantLaw law);
```

- [ ] **Step 1: Write failing tests** in `tests/dsp/test_sampler_character.c` (plain-assert style of `tests/dsp/test_waveform.c`):
  - `test_quant_off_passthrough`: SQL_OFF returns x exactly for x in {−1, 0, 0.503, 1}.
  - `test_quant_trunc_floor`: SQL_TRUNC, bits=8: x=0.503 → 0.5 exactly; x=−0.503 → −65/128 exactly (floor toward −∞); error always in [−Δ, 0) over 1000 ramp points.
  - `test_quant_round_bounds`: SQL_ROUND, bits=8: error in [−Δ/2, +Δ/2) over 1000 points.
  - `test_quant_tpdf_bounded`: SQL_TPDF, bits=8, over 10000 samples of a 997 Hz sine: |y−x| ≤ Δ, and the *variance* of (y−x) is ≥ trunc's variance ×2 (error decorrelated, corpus §1.5).
  - `test_quant_ulaw_roundtrip`: SQL_ULAW: x=0 → 0 exactly; roundtrip is monotonic; |y−x| ≤ 1/128; relative error at x=0.001 < relative error at x=0.9 (companding resolution).
  - `test_quant_compand12`: SQL_COMPAND12: relative error at 0.01 < relative error at 0.9; |y−x| ≤ 1/2048.
  - `test_quant_bits_clamp`: bits ∈ {−3, 0, 25, 999} → no crash, output finite (clamped internally).
- [ ] **Step 2: Run** `meson compile -C build test_sampler_character && meson test -C build test_sampler_character` — expect compile FAIL (types/functions missing).
- [ ] **Step 3: Implement** in `src/sampler_character.c`: the four linear-law one-liners straight from corpus §1.2 (trunc `floorf(x*L)/L` with L=2^(bits−1); round `floorf(x*L+0.5f)/L`; TPDF = local xorshift `spx_rand()` returning [−0.5,0.5), dither `(spx_rand()+spx_rand())*step` then round). μ-law encode: `f(x)=sign(x)*logf(1+255*|x|)/logf(256)` quantized to 127 levels + sign, decode its inverse; SQL_COMPAND12 = same shape with μ=90 (`#define SAMPLER_COMPAND12_MU 90.0f`, corpus [C], tunable in Wave 3) at 12-bit. Null-free pure module; `#define`s for every constant.
- [ ] **Step 4: Run tests → PASS.**
- [ ] **Step 5: Commit** — `git log --oneline -5` for message style; e.g. `sampler: character engine quantizer laws (trunc/round/tpdf/ulaw/compand12)`.

### Task 2: Bake — BakedSample + band-limited decimation

**Files:**
- Modify: `src/sampler_character.{c,h}`, `tests/dsp/test_sampler_character.c`

**Interfaces:**
- Consumes: `Sample` (`src/sample.h`: `float *data; int length; int sampleRate; int bit;`), `samplerQuantize`.
- Produces:

```c
typedef struct { float *data; int length; int rate; int srcBit; } BakedSample;
/* Build the degraded copy: windowed-sinc resample src -> cfg->rate
 * (cutoff 0.47*min(srcRate, rate) so the copy is band-limited below the
 * target Nyquist), then apply cfg->law/cfg->bits. rate >= srcRate means
 * copy/upsample. src==NULL or length<=0 -> returns false, out zeroed. */
bool bakeSamplerCharacter(const Sample *src, int rate, int bits, SamplerQuantLaw law, BakedSample *out);
void freeBakedSample(BakedSample *b);
/* Cache-validity check: true when b holds the degradation of src under
 * (rate, bits, law). */
bool bakedMatches(const BakedSample *b, const Sample *src, int rate, int bits, SamplerQuantLaw law);
```

- [ ] **Step 1: Failing tests:**
  - `test_bake_null_source`: NULL src → false, `out` zeroed, no crash.
  - `test_bake_identity_exact`: 44100 src, rate=44100, law=SQL_OFF → `baked.length == src.length`, `memcmp`-equal data (exact copy path, no sinc rounding at unity).
  - `test_bake_upsample_length`: 22050 src, rate=44100 → length == src.length*2.
  - `test_bake_bandlimit`: 44100 src holding a 1 kHz + 18 kHz two-tone sine (exact float synthesis), bake rate=26040, SQL_OFF → 1 kHz component within 0.5 dB of its input amplitude; 18 kHz component at ≤ −40 dB of its input amplitude (measure with direct cos/sin correlation, no FFT dependency).
  - `test_bake_trunc_floor`: 997 Hz full-scale sine, rate=26040, bits=12, SQL_TRUNC → residual RMS (y minus the ideal sine) within [0.4, 2.0]× Δ/√12 with Δ=2^-11 (corpus §1.1/§1.5).
  - `test_baked_matches`: true only when rate/bits/law and the src pointer/length all match.
- [ ] **Step 2: Run → FAIL (link errors).**
- [ ] **Step 3: Implement:** `rate == src->sampleRate` → direct copy then quantize (identity exactness); else windowed-sinc per output sample: `srcPos = m * srcRate / rate`, 16 taps (`#define BAKE_SINC_TAPS 16`), kernel `sinc(2*fc*t)*hann(t)`, `fc = 0.47f * min(srcRate, rate)`, taps normalized to unity DC gain. `out->srcBit = src->bit`. Free-then-alloc `out->data` (caller may reuse). All constants `#define`d.
- [ ] **Step 4: Run → PASS. Commit** e.g. `sampler: bake — band-limited decimation + law quantization cache`.

### Task 3: Machine profiles table

**Files:**
- Modify: `src/sampler_character.{c,h}`, `tests/dsp/test_sampler_character.c`

**Interfaces:**
- Produces:

```c
typedef enum { SPI_DROPSAMPLE, SPI_LINEAR, SPI_INTERP_COUNT } SamplerInterp;
typedef enum { SMP_CLEAN, SMP_SP1200, SMP_MPC60, SMP_S950, SMP_MIRAGE, SMP_SK1, SMP_PROFILE_COUNT } SamplerProfile;
void samplerProfileConfig(SamplerProfile profile, int *rate, int *bits, SamplerQuantLaw *law,
                          SamplerInterp *interp, int *filterOn, float *cutoff, float *res);
```

- [ ] **Step 1: Failing tests:**
  - `test_profile_table_complete`: for every SMP_* in [0, SMP_PROFILE_COUNT): rate ∈ [2000,44100], bits ∈ [1,24], law/interp valid enums, filterOn ∈ {0,1}, cutoff ∈ [20,20000], res ∈ [0,0.9].
  - `test_profile_values` — the grounded table, asserted exactly:

| profile | rate | bits | law | interp | filterOn | cutoff | res |
|---|---|---|---|---|---|---|---|
| CLEAN | 44100 | 24 | SQL_OFF | SPI_LINEAR | 0 | 20000 | 0.0 |
| SP1200 | 26040 | 12 | SQL_TRUNC | SPI_DROPSAMPLE | 1 | 12000 | 0.05 |
| MPC60 | 40000 | 12 | SQL_COMPAND12 | SPI_DROPSAMPLE | 0 | 20000 | 0.0 |
| S950 | 30000 | 12 | SQL_TRUNC | SPI_LINEAR | 1 | 16000 | 0.1 |
| MIRAGE | 27000 | 8 | SQL_TRUNC | SPI_LINEAR | 1 | 8000 | 0.2 |
| SK1 | 9380 | 8 | SQL_TRUNC | SPI_LINEAR | 0 | 20000 | 0.0 |

  - `test_profile_clean_is_passthrough`: CLEAN config baked through Task 2 yields the identity bake.
- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement** the table as a `static const` struct array + the accessor (out-params, no allocation). SP1200 values are measured ground truth `[V]`; MPC60 compand is the corpus `[C]` approximation; S950 cutoff/resonance model the 6-pole switched-cap character coarsely (topology refinement is Wave 3).
- [ ] **Step 4: Run → PASS. Commit** e.g. `sampler: machine profile defaults table (clean/sp1200/mpc60/s950/mirage/sk1)`.

### Task 4: Playback read path — drop-sample + linear + parity advance

**Files:**
- Modify: `src/sampler_character.{c,h}`, `tests/dsp/test_sampler_character.c`

**Interfaces:**
- Produces:

```c
/* Per-output-sample advance in BAKED samples. Parity-preserving: at
 * bakedRate == source rate and tune == 1.0 this equals the legacy
 * getSampleValueFwd internal ratio PA_SR*2*srcBit/srcRate, so default
 * settings keep today's tuning bit-for-bit. */
float samplerAdvance(float phaseIncrement, int srcBit, int bakedRate, float tune);
/* SP-1200 drop-sample read (corpus §2.2): integer addr accumulator + double
 * fractional phase, index truncated, NEVER stalls (advance >= 1 per tick),
 * wrap on loop, silence at the tail when !loop. addr/phase owned by the voice. */
float getBakedSampleDrop(BakedSample *b, uint32_t *addr, double *phase, float advance, int loop);
/* Linear-interpolated read of the baked buffer; loop wrap + the legacy
 * length-2 tail-to-zero semantics. */
float getBakedSampleLinear(BakedSample *b, float *pos, float advance, int loop);
```

- [ ] **Step 1: Failing tests:**
  - `test_advance_parity`: `samplerAdvance(phaseIncrement, 16, 44100, 1.0f)` == `phaseIncrement * (PA_SR / ((float)44100 / 16) * 2)` within 1e-6 (the legacy expression, `src/sample.c:94`) for phaseIncrement ∈ {0.001, 0.00917, 0.05}.
  - `test_advance_rate_drop`: rate 26040 → advance == parity value × (44100/26040) (reading the same wall-clock position in a slower-rate buffer).
  - `test_drop_never_stalls`: advance=0.3, 1000 ticks → addr increases by ≥ 1 every tick (corpus: `addr += (i != 0) ? i : 1`).
  - `test_drop_pitch_down_repeats`: advance=0.5, ≥ 1 repeated output value across 100 ticks (sample-and-hold behavior).
  - `test_baked_drop_wrap_short`: loop=1 on a 3-sample bake, 1000 ticks at advance 2.7 → every return is one of the 3 values, no OOB (assert addr < length after each tick via a debug echo fn or assert via return sanity: finite and in [min,max] of data).
  - `test_linear_matches_legacy`: identity bake of a 44100/16 source → `getBakedSampleLinear(baked, &p, samplerAdvance(0.01f, 16, 44100, 1.0f), loop)` output == `getSampleValueFwd(sample, &p2, 0.01f, loop)` output for 500 successive reads, loop ∈ {0, 1}.
- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement.** Drop tick body (semantics are exact and non-obvious — corpus §2.2):

```c
double ip = *phase + advance;
uint32_t i = (uint32_t)ip;
*phase = ip - i;
*addr += (i != 0) ? i : 1;            /* never stall */
if(*addr >= (uint32_t)b->length) { if(loop) *addr %= (uint32_t)b->length; else return 0.0f; }
return b->data[*addr];
```

  Linear read = the legacy body (`src/sample.c:97-114`) transplanted onto `b->data/b->length` with `*pos += advance` instead of the internal ratio.
- [ ] **Step 4: Run → PASS. Commit** e.g. `sampler: drop-sample + linear baked read paths with tuning parity`.

### Task 5: Instrument params, profiles wiring, preset format V4

**Files:**
- Modify: `src/voice.h` (`SamplerPatch` += `int profile; int law; int interp; int filterOn; int cutoff; float res; float tune;` appended after `loopEndIndex`), `src/voice.c`, `src/io.h` (`#define PRESET_MAGIC_HEADER_V4 "IPB4"` beside the V1–V3 defines, line 16-18), `src/io/preset_io.c`, `tests/dsp/test_sampler_character.c`, `tests/dsp/test_io.c`

**Interfaces:**
- Consumes: Tasks 1–3 (quantize / bake / profile table); `createParameterEx`/`createParameterPro` (signatures per `voice.c:1119-1125`); the param-recreate rule (#1135: every param `applyInstrumentPreset` clears must be recreated before return).
- Produces: `static void rebuildSamplerBakeUnsafe(Instrument *inst)` (no locking — callers hold `g_audioLock` or take it); `static void cb_applySamplerProfile(void *inst)` + `static void cb_rebuildSamplerBake(void *inst)` (both lock `g_audioLock` around the rebuild; profile cb writes the six other params with their `onChange` suppressed per the `cb_setVoiceCount` pattern `voice.c:665-668` to avoid recursive rebuilds/deadlock, then rebuilds once); V4 save/load in `preset_io.c`.

- [ ] **Step 1: Failing tests:**
  - `test_preset_v4_roundtrip` (in `test_io.c` pattern): build a `Preset` with SAMPLE voiceType + all 7 new fields set distinctly → `savePresetFile` → `loadPresetFile` → all fields equal. Use `.tmp_files/` paths; delete artifacts in-test (rule #1297 stray `.ipb`).
  - `test_preset_v3_migrates`: hand-write a V3 file (`PresetV3` mirror struct in the test: old `SamplerPatchV3` fields only) with `IPB3` magic via direct `fwrite`, bitDepth=12, sampleRate=26040 → load → old fields preserved; new fields defaulted: `profile==SMP_CLEAN`, `law==SQL_TRUNC` (bitDepth<24), `interp==SPI_LINEAR`, `filterOn==0`, `cutoff==20000`, `res==0`, `tune==1.0`.
  - `test_preset_v2_still_loads`: existing V2 fixture test stays green (regression, part of `test_io`).
  - `test_params_recreated_on_apply` (test_sampler_character.c): construct instrument per `test_voice.c`'s pattern, `applyInstrumentPreset` with a patch carrying profile=SP1200 → all 7 params exist with profile-applied values, `id.sampler.baked` rebuilt (`baked->rate == 26040`, `bakedMatches` true), and per #1135 no param pointer dangling.
- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement:**
  - `createInstrument` SAMPLE branch (`voice.c:1108-1126`) + `applyInstrumentPreset` SAMPLE branch (`voice.c:715-728`): create the 7 params (both places, same order/names): `profile` (createParameterPro, cb_applySamplerProfile, initial `p.pd.sampler.profile`), `law` (Pro, cb_rebuildSamplerBake), `interp` (Ex), `filter` (Ex, 0..1), `cutoff` (Ex, 20..20000, initial `p.pd.sampler.cutoff`), `res` (Ex, 0..0.9), `tune` (Ex, 0.25..8.0, initial `p.pd.sampler.tune`). Call `rebuildSamplerBakeUnsafe` at the end of both branches (apply path is already under the lock).
  - `updateSampleReferences` (sampleIndex cb): append the locked rebuild (sample changed → new bake).
  - `cb_applySamplerProfile`: `samplerProfileConfig` → set rate/bits/law/interp/filterOn/cutoff/res params (suppress their cbs) → rebuild → restore cbs.
  - `presetFromInstrument` SAMPLE branch (`voice.c:873-881`): pull the 7 fields.
  - `preset_io.c`: `savePresetFile` writes `PRESET_MAGIC_HEADER_V4`; `loadPresetFile` tries V4 first, then a `PresetV3` mirror struct (full old union: `SamplerPatchV3` + current FmPatch/BlepPatch) migrated field-by-field exactly like the V2 branch does (`preset_io.c:95-144` pattern), then V2, then V1 — all untouched below.
- [ ] **Step 4: Run → PASS** (`meson test -C build test_io test_sampler_character test_voice` — the mod-voice and voice suites exercise applyInstrumentPreset heavily). **Commit** e.g. `sampler: character params + profiles + preset format V4 with V3 migration`.

### Task 6: OTA cascade filter + voice integration (`samplerVoiceTick`)

**Files:**
- Modify: `src/sampler_character.{c,h}` (filter), `src/voice.h`, `src/voice.c`, `src/main.c`, `tests/dsp/test_sampler_character.c`

**Interfaces:**
- Consumes: Tasks 1–5 (bake + reads from Task 4, params from Task 5); existing dest-param pattern: `inst->gain`→`voice->volume` pairing in `initVoiceDrivers` (`voice.c:424-425`), per-buffer base sync in `syncVoiceGraph` (`voice.c:362`), destMap lookup `voice.c:266-267`.
- Produces:

```c
/* CEM3320/SSM2040-class OTA cascade: four one-poles in a global feedback
 * loop, no tanh (corpus §4.5) — needs no oversampling. Cutoff one-pole
 * slew (corpus §4.3, SAMPLER_CUTOFF_SLEW_HZ 400) toward cutoffTarget. */
typedef struct { float y[4]; float fcCur; } OtaCascade;
void otaCascadeInit(OtaCascade *f);
float otaCascadeProcess(OtaCascade *f, float x, float cutoffTarget, float res, float sampleRate);
```

  In `src/voice.h`: `SamplerVoiceData` += `OtaCascade filter; Parameter *cutoffV; uint32_t dsAddr; double dsPhase;`; `SamplerInstrumentData` += `BakedSample *baked;` + new `Parameter *profile, *law, *interp, *filterOn, *cutoff, *res, *tune;` (params created in Task 5). In `src/voice.c`:

```c
/* The whole per-sample sampler voice path, unit-testable without a Voice:
 * NULL/empty guards -> advance -> interp read -> optional OTA filter.
 * cutoffV is the voice-owned mirror of inst cutoff (env-routable). */
float samplerVoiceTick(const SamplerInstrumentData *id, SamplerVoiceData *vd, const Parameter *cutoffV, float phaseIncrement);
```

- [ ] **Step 1: Failing tests:**
  - `test_ota_passband_stopband`: cutoff=500: 1 kHz sine at amplitude 1 → output amplitude ≥ 0.9 after settling; 5 kHz sine → ≤ 0.15 (4-pole, corpus §4.5 topology).
  - `test_ota_slew_no_click`: input constant 0.5, cutoffTarget steps 20000→500 at t=100 → |y[n]−y[n−1]| ≤ 0.01 for all n around the step (the §4.3 impulse is gone).
  - `test_ota_resonance_rings`: impulse, cutoff=1000, res=0.8 → output sign flips ≥ 3 times (feedback pole pair); res=0 → ≤ 1 flip.
  - `test_ota_init_zero`: after `otaCascadeInit`, one process call at any cutoff returns finite.
  - `test_tick_null_baked_silent`: `samplerVoiceTick` with `id->baked == NULL` (or `baked->data == NULL`) → 0.0f, no crash (boot-with-empty-pool path).
  - `test_tick_filter_off_bypasses`: identity bake + `filterOn` param 0 → output == the unfiltered read.
- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement.** OTA body (topology per corpus §4.5): `x' = x - res*y4;` then four `y[i] += g*(in - y[i])` cascaded with `g = 1 - expf(-2π*fcCur/sr)`, `fcCur += k*(target-fcCur)`, `k = 1 - expf(-2π*SAMPLER_CUTOFF_SLEW_HZ/sr)`. `samplerVoiceTick` in `voice.c`: read `rate/bits/law/interp/filterOn/tune/res` via `getParameterValueAsInt(id->…)`; `advance = samplerAdvance(phaseIncrement, baked->srcBit, baked->rate, getParameterValue(id->tune))`; DROP vs LINEAR dispatch to Task 4 fns; filter stage unless `filterOn==0`. `generateSample` (`voice.c:106`) becomes the delegate (keep `out.L *= 0.5f` and L=R for parity).
  - `initialize_voice` VOICE_TYPE_SAMPLE branch (`voice.c:366-469`): create `vd->sampler.cutoffV = createParameter(voice->paramList, "cutoff", <inst cutoff base>)` beside `voice->volume`; zero `dsAddr/dsPhase`; `otaCascadeInit(&vd->sampler.filter)`.
  - `initVoiceDrivers` (`voice.c:420-445`): pair `inst->id.sampler.cutoff` → `voice->vd.sampler.cutoffV` in destMap for sampler instruments (mirror the gain→volume pair) so `syncVoiceGraph`/`replicateConnections`/`processModulations` treat cutoff as a per-voice destination (envelope→cutoff = SP-1200 dynamic filter, corpus §4.5).
  - Note-on reset: everywhere `vd.sampler.samplePosition` is zeroed on trigger (`main.c:414-415` and any twin in `voice.c` — grep `samplePosition = 0`), also reset `dsAddr=0, dsPhase=0` and `otaCascadeInit(&filter)`.
- [ ] **Step 4: Run → PASS. Commit** e.g. `sampler: OTA cascade character filter + env-routable per-voice cutoff`.

### Task 7: UI character panel + harness probe/fixture

**Files:**
- Modify: `src/gui_inst_sample.c` (panel rows + probe), `src/gui_instrument.c:1388` (sctrl weight), `src/tools/instrument_harness/instrument_harness.c` (+`SOP_ASSERT_SPROFILE`, `SOP_ASSERT_SRATE` verbs: `ASSERT sprofile==N`, `ASSERT srate==N`), `src/tools/instrument_harness/fixtures/sampler_character.txt` (new)
- Test: the fixture + existing fixture regression (Task 8).

**Interfaces:**
- Consumes: Tasks 5–6 (character params + tick); Phase-1 panel structure (`gui_inst_sample.c:194-241`: rows `R_1..R_3` of 5 dials + waveform row; `guiWaveformViewMode` probe pattern at `gui_inst_sample.c:262+`).
- Produces: `int guiSamplerProfile(void)` and `int guiSamplerRate(void)` probes (selected instrument's profile idx / bitrate param, −1 when the sampler panel isn't built); panel rows: `R_1` unchanged (SAMPLE/LOOP/START/END/PLAYBACK), `R_2` (PAN/GAIN + 2 blanks), `R_3` new (PROFILE/LAW/INTERP/BITS/RATE — bits/rate dials bind the existing `bitDepth`/`sampleRate` params), `R_4` new (FILTER/CUTOFF/RES/TUNE/VIEW moved here), `R_5` waveform. Discrete dials (`drawDiscreteDialGuiNode`) for PROFILE/LAW/INTERP/FILTER; continuous for BITS/RATE/CUTOFF/RES/TUNE.

- [ ] **Step 1: Write the fixture** `fixtures/sampler_character.txt` (pattern of `fixtures/sampler_waveform.txt`): boot → `JUMP TPREV`, `EDIT`, `ASSERT voiceType==SAMPLE`, `JUMP SAMPLE` (so the panel's selected node is a dial), `REPORT SAMPLE_CONTROLS` (assert new row height), `JUMP PROFILE`, `EDIT` (CLEAN→SP1200), `ASSERT sprofile==1`, `ASSERT srate==26040`, `EDIT` (→MPC60) `ASSERT srate==40000`, `SHOT .tmp_files/sampler_character.png`, `QUIT`.
- [ ] **Step 2: Run it → FAIL** (`ASSERT sprofile` unknown verb; panel rows missing).
- [ ] **Step 3: Implement** the probes + verbs (parse + evaluate like `SOP_ASSERT_WAVEVIEW`), the two new rows (dial creation calls mirror `gui_inst_sample.c:203-211`; drop the two blanks from R_2), and bump `appendSampleInstControlNode(..., 24 → 35, ...)` at `gui_instrument.c:1388` (4 dial rows + waveform ≈ 1.47× the old content). Verify the panel fits via the fixture's `REPORT SAMPLE_CONTROLS` height (≈ 4×33+73+pad ≈ 210px; adjust the weight ±5 if clipped).
- [ ] **Step 4: Run fixture → PASS** (all asserts + SHOT written). Visual check of `.tmp_files/sampler_character.png`: 5 rows, dials labeled, values fit cells (Phase-1 dial geometry guarantees it).
- [ ] **Step 5: Commit** e.g. `sampler: character panel rows + profile/rate harness asserts`.

### Task 8: Coverage ratchet + full regression + checkpoint

**Files:**
- Modify: `meson/coverage-min.txt` (+ `src/sampler_character.c` floor)
- Test: whole suite + regression fixtures.

- [ ] **Step 1: Full unit suite:** `meson test -C build` (18/18 expected) and `meson test -C build-cov` — all green.
- [ ] **Step 2: Coverage:** clear `build-cov` gcda, rerun, capture with lcov, refresh `build-cov/coverage-src.info` (the Phase-1 commands: `find build-cov -name '*.gcda' -delete; meson test -C build-cov; lcov --directory build-cov --capture --output-file build-cov/coverage-fresh.info --rc lcov_branch_coverage=1; lcov --extract build-cov/coverage-fresh.info '*/Spectrax/src/*' --output-file build-cov/coverage-src.info --rc lcov_branch_coverage=1`). Read `sampler_character.c`'s branch % and add `src/sampler_character.c  <achieved-2>` to `meson/coverage-min.txt`. Verify the gate via the `ctol_status` Code Mode tool: gate PASS, 3 modules gated.
- [ ] **Step 3: Build + install + regression fixtures:** `meson compile -C build && meson install -C build` (rule #1312), then from repo root: `run_scripted.sh` on `sampler_character`, `sampler_waveform`, `preset_save_load`, `add_route_delete`, `task4_size_verify`, `mod_sources`, `pagenav`, `route_lines_follow_selection` — all PASS; after each, `git show HEAD:bin/s1.sng > bin/s1.sng` (rule #1313). Watch for orphan sweeps per rule #1137 if anything is killed manually.
- [ ] **Step 4: Commit** e.g. `sampler: coverage floor + regression green for character engine`.
- [ ] **Step 5: Checkpoint handoff** — user tests the app hands-on (PROFILE dial cycles SP1200/MPC60/etc., BITDEPTH/BITRATE now audible before pitch, envelope→CUTOFF routing via the picker, preset save/load keeps the profile). NOT a wave boundary to self-declare done: wait for user verification.

---

## Follow-up waves (not tasks in this plan — each gets its own plan after the slice is verified)

**Wave 1 — Output stage & machine feel** (corpus §5, §7.3, §3.2):
- Per-instrument bus saturation: sum that channel's voices at ~−12 dBFS each, asymmetric soft-clip the SUM (`saturate(x, drive, bias)` corpus §5.1 — even-order term + even-order compression), post-gain to ~−3 dBFS. Restructure the `main.c:428-443` mix loop to per-channel sum → saturate → master add. This is where `src/distortion.c`'s unused `fold()` becomes the first curve option (the deferred "shared waveshaper" — curve enum OFF/SOFT/HARD/FOLD).
- Noise floor stack added *before* the bus saturation: converter hiss at the law's floor (12-bit ≈ −72 dBFS, 8-bit ≈ −48), 50 Hz mains family at −60..−45, optional 0.5–4 kHz clock buzz (§7.3).
- Semitone-quantized tuning + separate finetune (SP TUNE faders are semitone steps `[V]`; RX1200's finetune pattern), DAC INL warp + major-carry glitch impulses (§3.1 — the transient "crunch"), coarse fixed-point fader multiply (§3.2), TAL's level-as-quality control.

**Wave 2 — Voice architecture & looping** (corpus §6, §8.4):
- Honor `loopStartIndex`/`loopEndIndex` in the read path (both interps) — the params already exist and the waveform already *shows* the region, but DSP ignores it today.
- Loop crossfade ≥ 1 loop period, equal-gain vs equal-power law choice (§6.1-6.2), zero-crossing-displaced auto-loop (S612-style §6.3), loop micro-variation (±0.2 dB/pitch/cutoff random walk per pass, NEVER length, §6.5).
- Real reverse + ping-pong (`SPT_*` currently collapses to fwd/rev) with the documented artifacts (reverse-envelope, §6.4/§8.4).
- MPC60 velocity→start-address + velocity→level (prerequisite: sequencer note velocity plumbing — currently absent, only `grainVelocity` exists), no-choke/dynamic-allocation policy option (maps onto the existing per-channel `AllocationBehaviour`), per-voice cutoff drift (Ornstein-Uhlenbeck ±5¢) + key tracking (§4.4).

**Wave 3 — Machine accuracy & verification** (corpus §4.1-4.2, §9, §10):
- SSM2044 **ladder** topology option for the SP1200 profile (Huovilainen difference equations, 5 tanh, 2× oversampling + half-delay feedback compensation) alongside the OTA cascade; 6-pole switched-cap character for S950; per-machine bake filter shapes (SP-1200's 5th-order leaky output stage vs MPC60's proper 18 kHz brickwall — the filter is part of the emulation, not a quality knob, §2.6).
- Full profile set (Fairlight variable-rate 8–24k, LM-1, MPC60 compand curve fit against measurement), cubic + sinc interpolators (§2.3 table).
- Verification harness per corpus §9: offline render (via the existing `inspectro_wavget` render path) → sweep tests (reflected downward streaks), white-noise-through-filter-envelope cutoff trajectory, impulse 4-pole decay count, THD+N notch method for the quantizer — as scripted, assertable checks, not listening.

**Wave 4 — Capture path:** sampling INTO the app (mic/line-in capture with the 11th-order-style AA pre-filter emulation baked at record time) — the other half of the emulation story; large, separate plan.

## Decision points & assumptions (flagged for review)

1. **Slice = profile-driven chain with 6 profiles**, SP-1200 deepest (best primary sources: Yeh et al. `[P]`, Rossum `[V]`). Every stage stays individually dialable after a profile sets defaults — "maximal flexibility" per the brief.
2. **Bake is band-limited decimation + truncation** (cheat-sheet step 1), not hard decimation — matches the measured SP (aliasing recorded *in* was minor; the grit comes from no reconstruction filter + drop-sample reads).
3. **Tuning parity is an invariant**: default params (CLEAN profile, rate 44100, tune 1.0) reproduce today's pitch and clean sound bit-for-bit (Task 4 tests). Existing songs change only if a profile is dialed.
4. **Preset format V4** with V3 auto-migration (raw-`fwrite` format, so new `SamplerPatch` fields force the bump; V1/V2 chains untouched). Without this the profiles wouldn't survive save/load.
5. **OTA cascade now, ladder in Wave 3** — the cascade is Mirage-accurate (CEM3328), needs no oversampling machinery; the ladder needs 2× oversampling + half-delay, its own task.
6. **MPC60 compand curve is an approximation** (μ=90, corpus `[C]` — no public curve exists).
7. **Filter envelope routability comes free** via the existing dest-param machinery (envelope→CUTOFF = SP-1200 dynamic filter); default mod routing unchanged in the slice.
8. **UI**: values for PROFILE/LAW/INTERP show raw indices for now (consistent with existing discrete dials); named labels + baked-data waveform view are Wave 1/3 polish.
9. The old bounded Phase 2/3 sketch (output-path bitcrush + standalone waveshaper) is **superseded** by this design; the waveshaper resurfaces as Wave 1's saturation curve set.
