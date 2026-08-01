# DSP Test Coverage Audit

**Date:** 2026-08-01
**Auditor:** subagent (read-only)
**Scope:** DSP-related functions in src/ (branch `fil-c-dsp-port`, HEAD add67e2)

**Method:** Read all in-scope headers (notes, fft, oscillator, wavetable,
blit_synth, distortion, filters, voice, modsystem, sample, sequencer, dstruct,
appstate, io, io/preset_io, io/sequencer_io, io/settings_io) and all test
files in `tests/dsp/` + `dsp/test_wav_writer.c` + `dsp/render.c`. Cross-checked
each declared function against `grep` of test sources and of the implementation
files for missing definitions.

**Headline findings**

1. **`tests/dsp/test_sample.c` and `tests/dsp/test_fm_synth.c` DO NOT EXIST.**
   They are listed in the dispatch as "newly added" but are absent from the
   working tree, `git log --all`, and the Makefile. The FM oscillator cluster
   and the sample-player playback functions therefore have **no direct unit
   tests** — only indirect coverage through `test_voice.c`.
2. **12 public functions are declared in headers but have NO definition**
   anywhere in the branch (verified by grep of the .c files): see
   `Unimplemented public functions` below. Tests document 5 of them; the other
   7 (initVoiceManager, initInstDefaults, initInstrumentFromPreset,
   initApplicationState, …) are undocumented gaps.
3. **dstruct.c has zero test coverage** — all 15 public functions are untested
   and no test file even includes dstruct.h.
4. The render harness `dsp/render.c` deliberately bypasses voice.c and
   sequencer.c (both have known bugs), so the one byte-identical integration
   test exercises only `square_wave` + `wav_writer` + a hand-rolled envelope.

---

## Coverage matrix

Legend: **DT** = direct test calls the function; **IT** = indirect only
(exercised through another module's test); **—** = no coverage.
**Edge** = edge cases exercised (NULL inputs, empty/full pools, wrap, clamp,
bounds). "unimpl" = declared in header, no definition in branch.

### notes.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| notes.h | `noteNames` (global) | yes | IT | partial | read indirectly by `getNoteString` assertions |
| notes.h | `noteFrequencies` (global) | yes | yes | yes | known freqs, all octave doublings, zero-skip |
| notes.h | `getNoteString(int,int)` | yes | yes | yes | valid notes + NULL on out-of-range note/octave |

### fft.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| fft.h | `triangularWindow` | yes | yes | partial | baseline of known buggy impl (center=1, edge=2) |
| fft.h | `hannWindow` | yes | yes | yes | edges=0, peak=1 |
| fft.h | `hammingWindow` | yes | yes | partial | baseline of known reversed-coeff bug (edge -0.29) |
| fft.h | `blackmanWindowEstimated` | yes | yes | partial | range-only sanity, no exact values |
| fft.h | `blackmanWindowExact` | yes | yes | partial | range-only sanity, no exact values |
| fft.h | `initFFT` | yes | yes | partial | fftSize 256/64 only; no negative/odd fftSize, no removeDC/cpxOut variants |
| fft.h | `incWindowFunc` | yes | yes | yes | full forward wrap + decrement path |
| fft.h | `pushFrameToFFT` | yes | yes | partial | only via DC roundtrip trigger-zone pattern |
| fft.h | `processFFTData` | yes | yes | partial | only DC-input roundtrip; no sine peak, no averaging (navg>1), no overlap/cpxOut/removeDC paths |
| fft.h | `toggleFFTProcessing` | yes | no | no | **zero coverage** |

### oscillator.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| oscillator.h | `sawtooth_wave` | yes | yes | yes | 0/0.5/1.0 + monotonicity |
| oscillator.h | `sine_wave` | yes | yes | yes | quadrants + mod-shift/cumulative mod |
| oscillator.h | `square_wave` | yes | yes | yes | quadrants + exact 0.5 discontinuity |
| oscillator.h | `band_limited_sawtooth` | yes | **unimpl** | no | declared, no definition anywhere |
| oscillator.h | `band_limited_square` | yes | **unimpl** | no | declared, no definition anywhere |
| oscillator.h | `sine_fm` | yes | no | no | **zero direct coverage**; only reached via voice FM render |
| oscillator.h | `sineFmAlgo` | yes | no | no | **zero direct coverage**; algorithm table untested |
| oscillator.h | `sine_op` | yes | no | no | **zero direct coverage**; feedback/mod semantics untested |
| oscillator.h | `createOperator` | yes | no | no | **zero direct coverage** (only via voice init) |
| oscillator.h | `createParamPointerOperator` | yes | no | no | **zero direct coverage** |
| oscillator.h | `freeOperator` | yes | no | no | **zero direct coverage** |

### wavetable.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| wavetable.h | `createWavetablePool` | yes | yes | partial | init state checked; no malloc-failure path |
| wavetable.h | `freeWavetablePool` | yes | yes | partial | NULL handled; double-free deliberately not called (UB) |
| wavetable.h | `loadWavetable` | yes | yes | partial | single/multi/integrity/NULL-name/128-entry cap; **129th entry OOB (`>` vs `>=` bug) deliberately untested**; MAX_WTPOOL_BYTES overflow untested |

### blit_synth.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| blit_synth.h | `init_blit` | yes | **unimpl** | no | declared, no definition anywhere |
| blit_synth.h | `blit_synth` | yes | **unimpl** | no | declared, no definition anywhere |
| blit_synth.h | `poly_blep` | yes | yes | partial | discontinuity/one/small-t/near-one/mid; no dt=0 or dt>1 |
| blit_synth.h | `blep_square` | yes | yes | partial | **only phase 0 tested**; no sweep across phases |
| blit_synth.h | `blep_saw` | yes | yes | partial | **only phase 0 tested** |
| blit_synth.h | `blep_tri` | yes | yes | partial | only phase 0; pins naive triangle (BLEP disabled) |
| blit_synth.h | `noblep_sine` | yes | yes | partial | 0 and π/2 only |

### distortion.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| distortion.h | `fold` | yes | yes | yes | best-covered primitive: linear/threshold/fold±/ampFactor/attenuation/16×5×4 finite sweep; pins negative-side sign bug |

### filters.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| filters.h | `createBiquadFilter` | yes | yes | partial | all 4 BiquadTypes created; uninitialised-coefficient behavior accepted |
| filters.h | `resetState` | yes | **unimpl** | no | declared, no definition anywhere |
| filters.h | `checkFLoatUnderflow` | yes | yes | yes | pos/neg underflow, normal, exact zero |
| filters.h | `processKDirect` | yes | yes | partial | zero-input + impulse no-crash only; **no frequency-response / coefficient correctness check** |
| filters.h | `processKCanonical` | yes | yes | partial | same, zero-input + no-crash only |
| filters.h | `processKTransposeDirect` | yes | yes | partial | same |
| filters.h | `processKTransposeCanonical` | yes | yes | partial | same |
| filters.h | `createFilter` | yes | yes | partial | LPF/HPF + invalid type (leak on error path pinned); **q=0 / extreme-q / coefficient formula vs RBJ not verified** |

### voice.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| voice.h | `createGranularProcessor` | yes | yes | partial | exercised with planted grainReadPos (seeded overflow bug workaround) |
| voice.h | `granularProcess` | yes | yes | partial | render smoke only; window/grain-count/velocity behaviors untested |
| voice.h | `createVoiceManager` | yes | yes | partial | 0 and 4 voices; single channel; requires ≥1 sample+preset |
| voice.h | `initVoicePool` | yes | yes | partial | channels 1 and 2 |
| voice.h | `initVoiceManager` | yes | **unimpl** | no | declared, no definition anywhere |
| voice.h | `freeVoice` | yes | IT | no | not called directly (double-free bug); crash pinned via `freeVoiceManager` in forked child |
| voice.h | `freeVoiceManager` | yes | yes | partial | empty pool path clean; populated pool crash pinned |
| voice.h | `getFreeVoice` | yes | yes | partial | FM default; documents last-free-voice (VA_FREE_OR_ZERO) quirk; other AllocationBehaviour values untested |
| voice.h | `triggerVoice` | yes | yes | partial | note only; no detune/pan/velocity paths |
| voice.h | `generateVoice` | yes | yes | partial | FM, sample, BLEP-square rendered; **VOICE_TYPE_SPECTRAL and VOICE_TYPE_GRAIN-as-voice never exercised** |
| voice.h | `initDefaultFmPreset` | yes | yes | partial | via test_io make_preset |
| voice.h | `applyInstrumentPreset` | yes | IT | no | called by createVoiceManager/init_instrument; not directly asserted |
| voice.h | `cb_setInstrumentPreset` | yes | no | no | **zero direct coverage** (UI callback) |
| voice.h | `initPresetBank` | yes | yes | partial | empty bank + 1 preset |
| voice.h | `addPresetToBank` | yes | yes | partial | single add; no MAX_PATCHES overflow test |
| voice.h | `initialize_voice` | yes | IT | no | reached via initVoicePool |
| voice.h | `initInstDefaults` | yes | **unimpl** | no | declared, no definition anywhere |
| voice.h | `init_instrument` | yes | yes | partial | SAMPLE and BLEP; FM default via manager; no spectral/granular instrument |
| voice.h | `initInstrumentFromPreset` | yes | **unimpl** | no | declared, no definition anywhere |
| voice.h | `setSamplePlaybackFunction` | yes | no | no | **zero direct coverage** (callback) |
| voice.h | `updateSampleReferences` | yes | no | no | **zero direct coverage** (callback) |

### modsystem.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| modsystem.h | `initModSystem` | yes | yes | partial | envTables created (16×1024); pool freed at end |
| modsystem.h | `createModList` | yes | yes | partial | count=0 only |
| modsystem.h | `createParamList` | yes | yes | partial | count=0 only |
| modsystem.h | `clearParamList` | yes | yes | partial | pins count-reset-no-free leak |
| modsystem.h | `clearModList` | yes | yes | partial | pins count-reset-no-free leak |
| modsystem.h | `addToModList` | yes | IT | no | via createLFO/Random/Envelope; no MAX_MODS overflow test |
| modsystem.h | `addToParamList` | yes | IT | no | via createParameter; no MAX_PARAMS overflow test |
| modsystem.h | `addModulation` | yes | yes | partial | 1 and 2 connections, head-insert order; no duplicate-source/MAX_CONNECTIONS test |
| modsystem.h | `updateMod` | yes | yes | partial | phase advance + wrap ≥1.0 |
| modsystem.h | `processModulations` | yes | yes | yes | no-connections, NULL lists, MUL/ADD/SUB/DIV, application order, env end-to-end |
| modsystem.h | `initMod` | yes | IT | no | bug pinned indirectly (always installs generateEnvelope) |
| modsystem.h | `initLfoDefaults` | yes | IT | no | via createLFO |
| modsystem.h | `createLFO` | yes | yes | partial | SIN/SQU; shape fields; generator bug pinned |
| modsystem.h | `initRandDefaults` | yes | IT | no | via createRandom |
| modsystem.h | `createRandom` | yes | yes | partial | SNH/DRK range checks |
| modsystem.h | `initEnvelopeDefaults` | yes | IT | no | via createEnvelope |
| modsystem.h | `createEnvelope` | yes | yes | partial | empty env + addEnvelopeStage |
| modsystem.h | `addEnvelopeStage` | yes | yes | partial | sustain flag + 0.001 clamp pinned |
| modsystem.h | `addParamPointerEnvelopeStage` | yes | IT | no | via createParamPointerAD |
| modsystem.h | `createADSR` | yes | yes | yes | stage setup + timing counts per stage |
| modsystem.h | `createParamPointerADSR` | yes | no | no | **zero direct coverage** |
| modsystem.h | `createAD` | yes | yes | partial | used in loop-ignored test |
| modsystem.h | `createParamPointerAD` | yes | yes | partial | pointer wiring asserted |
| modsystem.h | `initADPresetData` | yes | yes | partial | AD only |
| modsystem.h | `initLfoPresetData` | yes | yes | partial | SQU shape only |
| modsystem.h | `initRandPresetData` | yes | yes | partial | SNH |
| modsystem.h | `initEnvelopeFromPreset` | yes | yes | yes | env preset roundtrip |
| modsystem.h | `saveEnvPreset` | yes | yes | yes | env preset roundtrip |
| modsystem.h | `initLfoFromPreset` | yes | yes | partial | LFO preset roundtrip; generator bug pinned |
| modsystem.h | `saveLfoPreset` | yes | yes | partial | LFO preset roundtrip |
| modsystem.h | `initRandFromPreset` | yes | yes | partial | **pins empty no-op stub** |
| modsystem.h | `saveRandPreset` | yes | yes | partial | **pins empty no-op stub** |
| modsystem.h | `generateCurve` | yes | yes | partial | 0.25/0.5/0.75 curves, len 1024 only |
| modsystem.h | `generateCurveWavetables` | yes | IT | no | via initModSystem only |
| modsystem.h | `generateSine` | yes | yes | partial | 0.25/0.5/0.75; negative-clamp bug pinned |
| modsystem.h | `generateSquare` | yes | yes | partial | 0.25/0.75; negative-clamp bug pinned |
| modsystem.h | `generateRamp` | yes | yes | partial | pins always-0 ramp bug |
| modsystem.h | `generateRandom` | yes | yes | partial | range checks only |
| modsystem.h | `generateDrunk` | yes | yes | partial | range checks only |
| modsystem.h | `applyCurve` | yes | yes | yes | linear + curvature extremes + clamp |
| modsystem.h | `generateEnvelope` | yes | yes | yes | attack/decay/sustain/release timing, loop-ignored bug |
| modsystem.h | `triggerEnvelope` | yes | yes | partial | before/after trigger |
| modsystem.h | `createParameter` | yes | yes | yes | clamp at creation, increments, fields |
| modsystem.h | `createParameterEx` | yes | yes | partial | increments only |
| modsystem.h | `createParameterPro` | yes | yes | partial | callback fire/no-fire; sign-crossing bug pinned |
| modsystem.h | `setParameterValue` | yes | yes | yes | clamp both ends, base untouched |
| modsystem.h | `setParameterBaseValue` | yes | yes | partial | base clamp; currentValue untouched |
| modsystem.h | `setParameterMinValue` | yes | yes | partial | min clamp + min≥max reject |
| modsystem.h | `setParameterMaxValue` | yes | yes | partial | inverted-check bug pinned |
| modsystem.h | `getParameterValue` | yes | yes | partial | clamp behavior |
| modsystem.h | `getParameterValueAsInt` | yes | yes | yes | ±rounding |
| modsystem.h | `modifyParameterBaseValue` | yes | yes | partial | ±1.0 |
| modsystem.h | `modifyParameterValue` | yes | yes | partial | ±1.0 |
| modsystem.h | `incParameterBaseValue` | yes | yes | partial | fine/coarse + abs() truncation bug pinned |
| modsystem.h | `createConnection` | yes | yes | partial | amount-ignored bug pinned |
| modsystem.h | `incrementConnectionType` | yes | **unimpl** | no | declared, no definition anywhere |
| modsystem.h | `decrementConnectionType` | yes | **unimpl** | no | declared, no definition anywhere |
| modsystem.h | `freeParamList` | yes | yes | partial | via free_test_lists |
| modsystem.h | `freeModList` | yes | no | no | **zero direct coverage** (cleanupModSystem used instead) |
| modsystem.h | `freeParameter` | yes | no | no | **zero direct coverage** |
| modsystem.h | `freeMod` | yes | no | no | **zero direct coverage** |
| modsystem.h | `freeLFO` | yes | no | no | **zero direct coverage** |
| modsystem.h | `freeRandom` | yes | no | no | **zero direct coverage** |
| modsystem.h | `freeEnvelope` | yes | no | no | **zero direct coverage** (part of voice double-free chain) |
| modsystem.h | `cleanupModSystem` | yes | yes | partial | full-graph free smoke |

### sample.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| sample.h | `loadSample` | yes | IT | no | via test_voice make_env only |
| sample.h | `createSamplePool` | yes | IT | no | via test_voice make_env only |
| sample.h | `freeSamplePool` | yes | no | no | **zero coverage** (voice test leaks pool on purpose) |
| sample.h | `freeSample` | yes | no | no | **zero coverage** (invalid-free-on-arena bug documented in voice.c, not pinned) |
| sample.h | `getSampleValueFwd` | yes | IT | no | via sample-voice render only; no loop/bit-depth/endpoint checks |
| sample.h | `getSampleValueRev` | yes | no | no | **zero coverage**; **no test file `test_sample.c` exists** |

### sequencer.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| sequencer.h | `createPatternList` | yes | yes | partial | selectedPattern=-1 quirk pinned |
| sequencer.h | `createArranger` | yes | yes | partial | 1–2 channels; NULL vm stored only |
| sequencer.h | `addChannel` | yes | yes | partial | append-style + cap only; **interior insert panics (memmove overflow) — untestable as-is** |
| sequencer.h | `updateBpm` | yes | **unimpl** | no | declared, body commented out |
| sequencer.h | `removeChannel` | yes | yes | partial | valid + invalid indices |
| sequencer.h | `addPattern` | yes | yes | partial | sizes 8/16; no MAX_PATTERNS overflow |
| sequencer.h | `addPatternToArranger` | yes | yes | partial | single cell write |
| sequencer.h | `addBlankIfEmpty` | yes | yes | partial | always-allocates quirk pinned |
| sequencer.h | `createSequencer` | yes | yes | partial | running/empty-channel init |
| sequencer.h | `getStep` | yes | yes | partial | pointer identity |
| sequencer.h | `getCurrentStep` | yes | yes | partial | aliases getStep |
| sequencer.h | `selectArrangerCell` | yes | yes | yes | all 4 clamp directions + checkBlankPattern |
| sequencer.h | `getPatternIDfromArranger` | yes | yes | partial | after selection |
| sequencer.h | `findArrangerLoopIndex` | yes | yes | partial | contiguous/gap/row0 |
| sequencer.h | `selectStep` | yes | yes | yes | clamp both ends + callback |
| sequencer.h | `currentNoteIsBlank` | yes | yes | partial | before/after setCurrentNote |
| sequencer.h | `setCurrentNote` | yes | yes | partial | `&note` callback bug pinned |
| sequencer.h | `editCurrentNote` | yes | yes | partial | blank-step default overwrite pinned |
| sequencer.h | `editCurrentNoteRelative` | yes | yes | yes | wraps + octave clamp |
| sequencer.h | `incrementSequencer` | yes | yes | partial | loop/end/pattern-advance + BUG-SEQ-6 modulo pinned; **only single-channel** |
| sequencer.h | `editStep` | yes | yes | partial | notes[patternIndex] bug pinned |
| sequencer.h | `stopPlaying` | yes | yes | partial | sets playing=0 |
| sequencer.h | `startPlaying` | yes | yes | partial | from selected row |

### dstruct.h

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| dstruct.h | `assignGetter` | yes | no | no | **zero coverage — no test includes dstruct.h** |
| dstruct.h | `getFloatValue` | yes | no | no | **zero coverage** |
| dstruct.h | `getIntValue` | yes | no | no | **zero coverage** |
| dstruct.h | `createIntList` | yes | no | no | **zero coverage** |
| dstruct.h | `createList` | yes | no | no | **zero coverage** |
| dstruct.h | `allocateElement` | yes | no | no | **zero coverage** |
| dstruct.h | `freeElement` | yes | no | no | **zero coverage** |
| dstruct.h | `appendToList` | yes | no | no | **zero coverage** |
| dstruct.h | `removeFromList` | yes | no | no | **zero coverage** |
| dstruct.h | `replaceElement` | yes | no | no | **zero coverage** |
| dstruct.h | `swapAdjacent` | yes | no | no | **zero coverage** |
| dstruct.h | `swapListElements` | yes | no | no | **zero coverage** |
| dstruct.h | `freeList` | yes | no | no | **zero coverage** |
| dstruct.h | `createNode` | yes | no | no | **zero coverage** |
| dstruct.h | `buildGraph` | yes | no | no | **zero coverage** |

### appstate.h (UI-coupled — appstate.c pulls in input.c → raylib)

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| appstate.h | `initApplicationState` | yes | **unimpl** | no | declared, no definition anywhere |
| appstate.h | `createApplicationState` | yes | no | no | needs raylib input layer |
| appstate.h | `incrementScene` | yes | no | no | zero direct |
| appstate.h | `decrementScene` | yes | no | no | zero direct |
| appstate.h | `setCurrentPattern` | yes | no | no | zero direct |
| appstate.h | `setSelectedPattern` | yes | no | no | zero direct |
| appstate.h | `setSelectedStep` | yes | no | no | zero direct |
| appstate.h | `setSelectedArrangerCell` | yes | no | no | zero direct |
| appstate.h | `setLastUsedNote` | yes | no | no | zero direct (test_sequencer defines *local copies* of the four setters rather than linking appstate.c) |

### io.h (+ io/preset_io.h, io/sequencer_io.h, io/settings_io.h)

| File | Function | Header-declared? | Direct tests? | Edge cases covered? | Notes |
|------|----------|-----------------|---------------|---------------------|-------|
| io.h | `writeChunkHeader` | yes | IT | no | via saveSequencerState only |
| io.h | `readAndVerifyChunkHeader` | yes | IT | no | via loadSequencerState only; no partial-read magic test |
| io.h | `createDirectoryList` | yes | yes | partial | count=0 |
| io.h | `freeDirectoryList` | yes | yes | partial | after populate |
| io.h | `populateDirectoryList` | yes | yes | partial | 2-file dir; no empty dir, no unreadable entries |
| io.h | `loadSamplesfromDirectory` | yes | no | no | **zero coverage** |
| io.h | `load_wav_sample` | yes | no | no | **zero coverage — real WAV parsing never tested** |
| io.h | `saveColourScheme` | yes | no | no | implemented in io/gui_io.c (raylib-coupled); zero |
| io.h | `loadColourScheme` | yes | no | no | same, zero |
| io.h | `loadColourSchemeTxt` | yes | no | no | same, zero |
| io.h | `saveSequencerState` | yes | yes | partial | exact file size, section layout; **uses fake calloc'd Arranger/PatternList, not real sequencer objects** |
| io.h | `loadSequencerState` | yes | yes | partial | roundtrip, missing/bad-magic/truncated; same fake-struct caveat |
| io.h | `saveSettings` | yes | yes | partial | roundtrip |
| io.h | `loadSettings` | yes | yes | partial | roundtrip + missing |
| io/preset_io.h | `loadPresetsFromDirectory` | yes | yes | partial | 2-file dir |
| io/preset_io.h | `savePresetFile` | yes | yes | partial | magic+exact size; **fwrite return ignored (documented, not exercised)** |
| io/preset_io.h | `loadPresetFile` | yes | yes | yes | ok/missing/bad-magic/truncated |
| io/sequencer_io.h | `saveSequencerState` / `loadSequencerState` | yes (dup of io.h) | see io.h rows | — | duplicate declarations |
| io/settings_io.h | `saveSettings` / `loadSettings` | yes (dup of io.h) | see io.h rows | — | duplicate declarations |

---

## High-priority gaps

Ranked by impact (what a future bug would most likely hide):

1. **`init_blit()` / `blit_synth()` (blit_synth.h)** — public API, zero
   implementation and zero tests. Any voice using the BLIT path is dead code
   today; the header's `BlitParams` struct is also untested.
2. **`band_limited_sawtooth()` / `band_limited_square()` (oscillator.h)** —
   same class; the two "band-limited" oscillator entry points the UI
   presumably calls are missing entirely.
3. **FM oscillator cluster (oscillator.h): `sine_fm`, `sineFmAlgo`,
   `sine_op`, `createOperator`, `createParamPointerOperator`, `freeOperator`**
   — zero direct unit tests. Only reachable through the single FM render test
   in test_voice.c, which never checks actual FM math (algorithm table, ratio,
   feedback, operator level). The dispatch expected `test_fm_synth.c`; it was
   never written.
4. **Sample player (sample.h): `getSampleValueFwd`, `getSampleValueRev`,
   `freeSample`, `freeSamplePool`** — zero direct tests; the dispatch expected
   `test_sample.c`; it was never written. Playback types
   (SPT_REVERSE/PINGPONG), loop mode, bit-depth reduction, and sample-position
   wraparound are all unverified. `freeSample`'s invalid-free-on-arena bug is
   documented but not pinned.
5. **dstruct.c — whole module (15 functions)** — used by the GUI/container
   layer; no test includes `dstruct.h`. List append/remove/replace/swap and
   graph build have no coverage at all.
6. **`toggleFFTProcessing` (fft.h)** — the only FFT API function with zero
   coverage; also the removeDC=true, cpxOut=true, navg>1 averaging and
   rowCount-overflow paths are untested.
7. **Filters lack behavioral verification**: `processKDirect/Canonical/
   TransposeDirect/TransposeCanonical` are only smoke-tested (zero-input and
   impulse-no-crash). No test checks a *configured* filter's frequency
   response, cutoff accuracy, q behavior, or that the four topologies agree.
   `createFilter` coefficients are only spot-checked (c0/d0).
8. **`createParamPointerADSR` (modsystem.h)** — only AD-pointer variant is
   tested; ADSR-pointer path is a likely duplicate-bug site.
9. **Modsystem free functions (`freeLFO`, `freeRandom`, `freeEnvelope`,
   `freeMod`, `freeParameter`, `freeModList`)** — never called directly;
   teardown is only exercised through `cleanupModSystem`/`freeParamList`.
   These are exactly the functions implicated in the voice double-free.
10. **`freeVoice` (voice.h)** — the most dangerous teardown path is only
    verified by asserting it crashes in a child process. If someone fixes the
    double-free, the test still passes (by design) — it cannot validate a
    correct teardown.
11. **Unimplemented voice APIs**: `initVoiceManager`, `initInstDefaults`,
    `initInstrumentFromPreset` — declared public, no definition; the
    preset→instrument pipeline is a hole in the product surface.

## Cross-module interactions not tested

1. **Sequencer → voice triggering.** No test connects a real
   `Sequencer`/`Arranger` to a `VoiceManager`. test_sequencer passes `NULL`
   vm deliberately; `render.c` avoids sequencer.c entirely. "Does
   incrementSequencer actually fire notes?" is unverified.
2. **render.c integration harness is minimal.** The one byte-identical render
   test covers `square_wave` + `wav_writer` + a hand-rolled linear envelope.
   No oscillator→filter→mix, no voice, no modsystem, no sequencer in the
   byte-identical loop.
3. **Preset I/O → instrument.** test_io round-trips raw `Preset` bytes but
   never applies a loaded preset to an `Instrument` (and
   `initInstrumentFromPreset` is unimplemented).
4. **Sequencer I/O → real sequencer objects.** `saveSequencerState`/
   `loadSequencerState` are tested against calloc'd fake Arranger/PatternList
   structs, not `createArranger`/`createPatternList` output.
5. **voice → wavetable pool / spectral voice.** `VoiceManager.wavetablePool`
   is never populated or read by a test; `VOICE_TYPE_SPECTRAL` (needs FFT
   setup) is explicitly avoided.
6. **Positive note:** voice → modsystem IS exercised — render_voice drives
   `processModulations(voice->paramList, voice->modList)` per sample, and the
   FM envelope attack/decay-to-silence test verifies the voice correctly
   drives envelope mods. oscillator FM also rides along that path (indirect
   only).

## Low-priority / acceptable gaps

- **appstate.c** — UI state setters; test_sequencer already proves the
  callback *shape* with faithful local copies. Linking the real file drags in
  raylib. Acceptable to skip until a headless appstate shim exists.
- **`saveColourScheme` / `loadColourScheme` / `loadColourSchemeTxt`** —
  raylib ColourScheme/Color types; GUI I/O, out of DSP scope.
- **`initApplicationState`** — unimplemented AND UI-coupled; would need the
  raylib input layer. Note: it's declared public but unused.
- **`initRandFromPreset` / `saveRandPreset`** — verified as no-op stubs (bug
  pinned by a smoke test). Testing the empty stub further has zero ROI until
  someone implements it.
- **`DEBUG_LOG` / `DEBUG_FREE` macros (modsystem.h)** — test-only noise,
  fprintf to stderr; not worth covering.
- **`wav_writer.c/h`** — test-only helper for the render harness, already
  covered by test_wav_writer.c.
- **`addChannel` interior-insert panic** — genuinely untestable without
  fixing the memmove overflow; documented in test.
- **`wavetable loadWavetable` 129th-entry OOB** — deliberately avoided in
  tests (would trigger a fil-c bounds panic); documented as pre-existing.

## Recommendation

Ordered by ROI:

1. **Write `test_fm_synth.c`** (the dispatch already planned it) — direct
   tests of `sine_fm`, `sineFmAlgo` (all 7 algorithms / 6 columns of
   `fm_algorithm`), `sine_op` (feedback + mod), `createOperator`,
   `freeOperator`. Highest ROI: this is the synthesis core of the default
   VOICE_TYPE_FM and currently has zero direct assertions.
2. **Write `test_sample.c`** (also already planned) — `getSampleValueFwd`
   forward/loop/endpoint, `getSampleValueRev`, all four
   `SamplePlaybackType`s, bit-depth reduction, position wraparound. Also
   free paths (`freeSample`, `freeSamplePool`) with the arena caveat pinned.
3. **dstruct coverage** — one test file covering list lifecycle
   (create/append/remove/replace/swap/free) and `buildGraph`; cheap to write,
   currently 100% uncovered.
4. **Filter behavioral tests** — feed a configured LPF/HPF a sine sweep and
   assert cutoff attenuation; compare the four biquad topologies on identical
   input; test q extremes (including q that should destabilize).
5. **FFT edge paths** — `toggleFFTProcessing`, removeDC, cpxOut, navg>1
   averaging, and a sine-input bin-peak test (currently only DC is tested).
6. **Sequencer→voice integration** — build a real Arranger/Sequencer with a
   real (single-voice) VoiceManager and assert that incrementSequencer causes
   a voice trigger on note-on steps. This is the largest integration hole.
7. **Teardown regression test** — after fixing the freeVoice double-free,
   replace the "crash must happen" assertion with a true "teardown must
   succeed" test.

---

## Summary stats

- Total public functions audited: **199** (unique declarations across the 17
  in-scope headers)
- Directly tested (incl. partial): **~127** (well-covered: notes, wavetable,
  distortion, sequencer, modsystem core, preset/settings I/O)
- Indirect-only coverage: **~12** (initMod, addToModList, generateCurve-
  Wavetables, getSampleValueFwd, sample voice, etc.)
- **Zero direct tests: 60** — of which 12 are declared-but-unimplemented,
  ~37 are implemented-but-untested (non-UI), and 11 are UI-coupled
  (appstate + colour scheme)
- Unimplemented public functions: **12** (band_limited_sawtooth,
  band_limited_square, init_blit, blit_synth, resetState,
  incrementConnectionType, decrementConnectionType, updateBpm,
  initVoiceManager, initInstDefaults, initInstrumentFromPreset,
  initApplicationState)
- Cross-module gaps: **5** (sequencer→voice, render-harness scope, preset→
  instrument, io→real sequencer objects, voice→wavetable/spectral)
- Test files claimed but missing: **test_sample.c, test_fm_synth.c**
