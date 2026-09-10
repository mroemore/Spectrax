# Spectrax Coverage Report — 2026-09-10

Method: GCC `-fprofile-arcs -ftest-coverage` (meson `-Db_coverage=true`) build at
`build-cov/`, run the full gate against it, then gcov-per-object aggregation
(union across the app binary, the instrument harness fixtures, and all unit
tests). Branch coverage uses gcov's branch counts (`gcov -b -c`).

## Workload exercised

- 16/16 meson unit suites (tests/dsp)
- 12/12 scripted instrument_harness fixtures
- One app boot under Xvfb (arranger screen, main loop, audio callback) closed
  cleanly so gcda flushed
- nav_harness + sample_analyser binaries built but NOT run (separate tools)

## Overall

| metric | union | note |
|---|---|---|
| lines   | 68.5% | across src/* (excl third_party/tools) |
| branches | 68.4% | taken-at-least-once semantics |

(The meson HTML report says 62.0% lines / 43.1% branches, but geninfo dedups
per-source objects and picks the *app-binary* copy for the GUI files, which
only saw a ~10s boot — so its per-file GUI numbers undercount. The union below
merges app + harness + tests and is the accurate picture.)

## Per-file (union, src/)

Sorted by branch coverage (weakest first):

| file | lines% | branch% |
|---|---|---|
| notes.c | 0.0 | 0.0 |
| distortion.c | 0.0 | 0.0 |
| filters.c | 32.8 | 42.9 |
| blit_synth.c | 38.9 | 50.0 |
| sample.c | 53.0 | 50.0 |
| wavetable.c | 65.9 | 50.0 |
| gui_inst_blep.c | 100.0 | 50.0 |
| main.c | 32.4 | 56.7 |
| gui_inst_sample.c | 95.1 | 60.0 |
| gui_layer.c | 67.2 | 61.8 |
| appstate.c | 67.3 | 62.5 |
| gui_core.c | 81.4 | 62.7 |
| gui_inst_mod.c | 64.5 | 64.3 |
| voice.c | 72.2 | 64.8 |
| gui_instrument.c | 67.1 | 68.2 |
| dstruct.c | 39.7 | 68.8 |
| gui_pattern.c | 50.0 | 69.2 |
| gui_arranger.c | 70.6 | 70.8 |
| modsystem.c | 79.4 | 70.9 |
| gui_style.c | 88.6 | 71.5 |
| sequencer.c | 67.4 | 72.8 |
| vizfx.c | 83.8 | 73.8 |
| oscillator.c | 64.4 | 75.0 |
| settings.c | 90.9 | 75.0 |
| dataviz.c | 65.3 | 75.0 |
| graph_gui.c | 82.7 | 77.0 |
| io.c | 72.4 | 77.1 |
| paths.c | 97.4 | 77.8 |
| input.c | 82.5 | 78.6 |
| fft.c | 57.3 | 84.6 |
| gui_inst_fm.c | 100.0 | 93.8 |
| **TOTAL (src)** | **68.5** | **68.4** |

## Areas needing additional testing (by priority)

### 1. Entirely untested modules — 0% (HIGH)

- **notes.c** (8 lines, 1 function): `getNoteString` (the pattern-screen note
  → label renderer). The pattern screen is barely exercised at all.
- **distortion.c**: no instrument uses it yet; dead until wired, but if it
  stays in the build it should get a unit test.
- **gui_inst_blep.c**: 100% lines but only 1 function; the BLEP instrument's
  control-builder graph. BLEP is a first-class voice type with no UI fixture.

### 2. DSP filters — 32.8% lines / 42.9% branch (HIGH)

Uncovered functions in `filters.c`:
- `processKTransposeCanonical`, `processKTransposeDirect`, `processKCanonical`,
  `processKDirect` — the whole K-processor family (0%).
- `checkFLoatUnderflow`.
- `createFilter` 55% / `createBiquadFilter` 50% — partial.

No unit test touches the K/biquad processing. There is a
`tests/dsp/test_...` gap here: a filter test that pushes known signals through
each processor would lift this cheaply.

### 3. Sequencer state functions — 0% each (HIGH)

Uncovered in `sequencer.c`:
- `stopPlaying`, `editStep`, `editCurrentNoteRelative`, `selectStep`,
  `getPatternIDfromArranger`, `selectArrangerCell`, `removeChannel`,
  `addChannel`.

The unit tests hammer `incrementSequencer`/`advanceSequencerStep` (100%) but
never exercise the state-changing paths the app uses: start/stop, step
selection, channel add/remove, arranger-cell selection. The pattern-screen
note editing path (`editCurrentNoteRelative`, `editStep`) is the one that
produced the recent note-delete segfault — direct unit coverage would have
caught it.

### 4. Mod system edge paths — several 0% functions (MEDIUM)

Uncovered in `modsystem.c`:
- `cleanupModSystem` (shutdown teardown)
- `saveRandPreset`, `initRandFromPreset`, `initRandPresetData` — RND source
  preset round-trip is never tested (LFO/ENV presets are).
- `modifyParameterBaseValue`, `modifyParameterValue` — param mutation path.
- `applyCurve` — the curve shape function.
- `generateDrunk`, `generateRandom`, `generateRamp`, `generateSquare` — LFO/RND
  generators: only the sine generator is exercised.
- `rewireModulationsForSource`, `setParameterMaxValue`, `setParameterMinValue`.

### 5. Voice/sample paths (MEDIUM)

Uncovered in `voice.c`:
- `setSamplePlaybackFunction`, `updateSampleReferences`, `selectSample`,
  `incrementSampleParam` — the sampler UI callback family.
- `cb_setInstrumentPreset` (0% — the preset-dial callback; unit tests call
  `applyInstrumentPreset` directly, bypassing the callback).
- `generateGranular`, `generateSpectral` — unused voice types (GRAIN/SPECTRAL).

### 6. Instrument-screen UI paths the fixtures never reach (MEDIUM)

Uncovered in `gui_instrument.c`:
- dirty-confirm flow: `guiBuildDirtyConfirmLayer`, `cbDirtyCancel`,
  `cbDirtySave`, `cbDirtyDiscard`, `cbOverwriteCancel`,
  `guiShowDirtyConfirmModal`, `guiCloseLoadList`, `drawPresetLoadListNode`.
- preset cycling: `cbTypePrev`, `prevVoiceType`, `cbPresetNext`.
- `effectiveNameLen` (name-node cursor math).

Uncovered in `gui_inst_mod.c` (harness object):
- `attenEditor*` (the double-tap attenuation editor),
  `routePickerIsOpen`, `guiRouteEraseMode`, `removeSelectedSource`,
  `removeSelectedEnvelope`, `drawWrappedCellText`, `findRouteButtonRect`.

### 7. FFT windowing — 0% (LOW)

`incWindowFunc`, `blackmanWindowExact`, `blackmanWindowEstimated`,
`hammingWindow`, `triangularWindow` all 0% (only `hannWindow` runs). The
spectral dataviz is a niche path.

### 8. Cleanup/save functions (LOW)

- `saveInstrumentAsPresetToSlot` (preset_io) — the blank-slot save path.
- `saveRandPreset` (modsystem) — above.
- `main.c` 32% lines: the audio callback + IO teardown are covered only by the
  boot; the frame loop's per-screen draw dispatch is mostly GUI-fixture
  territory.

## Recommendations

1. **New unit tests (highest value, cheap):** `filters.c` signal-processing
   tests; `notes.c` note-string round-trip; `sequencer.c` stop/step-select/
   channel add-remove; `modsystem.c` LFO/RND generators + RND preset
   round-trip. These are pure-data and need no window.
2. **One pattern-screen fixture:** `gui_pattern.c` 50% / the whole pattern
   screen is the biggest blind spot for the app itself (note editing,
   buffer scroller, song minimap all uncovered). A `pattern_edit.txt` fixture
   would cover the segfault-prone note path end-to-end.
3. **Dirty-confirm + attenuation-editor fixture:** both overlay flows are
   entirely uncovered in the GUI; they're the newest interactive code.
4. **Consider dropping dead modules** (distortion, GRAIN/SPECTRAL generators)
   or explicitly testing them — 0% on shipped code hides whether they work.

## Tooling notes

- The meson HTML coverage target (`ninja -C build-cov coverage-html`) exists
  and works after `gcov` is available, but its per-file GUI numbers are wrong
  (geninfo dedups per-source objects and picks the app-binary copy). The union
  aggregation (app + harness + tests per source) is the accurate picture.
- Harness fixtures run the app's real GUI code paths; their gcda data lands in
  `build-cov/.../instrument_harness.p/` and must be included in the merge.
- The coverage build dir + report machinery are temporary; the aggregation
  script lives at `.tmp_files/cov-merge.py`.