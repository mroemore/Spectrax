# Spectrax pattern-sequencer tracks — design

Date: 2026-09-14

## Problem

`MT_PATTERN` today is a single runtime mod source whose 16-step grid is
edited inline in the instrument mod-sources row. It is created on demand,
one source per add, and is not persisted.

The user wants pattern-sequence modulation to become a first-class,
per-channel feature:

- **Four pattern-seq tracks per channel**, selected by holding SHIFT
  (`KM_SELECT`) and pressing UP/DOWN on the pattern screen.
- The pattern screen gains pages: the existing note pattern stays page 0;
  pages 1..4 edit the four modulation tracks. A page indicator sits above
  the pattern to show which page is active.
- Each track is a full step-sequencer: per-step values plus per-track
  settings (`LEN`, `SHAPE`, `SLEW`, `POL`), edited on the pattern screen.
- The instrument screen shows the four tracks as **one grouped row**
  (label + readout + four ROUTE buttons) instead of four rows.
- The tracks **persist** in the song file (`s1.sng`), per channel, and
  survive preset load/change.

## Confirmed decisions

1. Pattern screen shows **5 pages**: `0 = NOTES` (unchanged) + `1..4 =
   pattern tracks`.
2. The four tracks are **fixed core** sources per instrument: always
   present, not deletable, not type-cyclable.
3. The four tracks are edited with **graph-navigable nodes** (Scheme A):
   step grid + a settings dial strip; `EDIT+UP/DOWN` edits the selected
   step value or dial.
4. Interpolation (`SHAPE`) is **per-track**, not per-step.
5. Persistence is in the **song file**, per channel.
6. The canonical runtime store is **`Instrument.patternTracks[4]`**.
7. The instrument row is **label + 4 ROUTE buttons + readout**, all
   settings/step editing happens on the pattern screen.

## A. Data model

New plain-data mirror for a pattern source's persistent fields
(`src/modsystem.h`, next to `PatternState`):

```c
#define PATTERN_TRACKS 4

typedef struct {
    float steps[MAX_PATTERN_STEPS]; /* stored 0..1 */
    int length;                     /* 1..16 */
    int shape;                      /* PatternShape */
    float slew;                     /* 0..1 */
    int polarity;                   /* PatternPolarity */
} PatternTrackData;
```

`PatternState` keeps its `Parameter *length/shape/slew/polarity` (the mod
system edits them through the normal dial path) plus `steps[]`,
`stepCount`, `channel`, and per-voice running state. It gains
`int track` (0..`PATTERN_TRACKS-1`), set at creation, so an edit can
mirror back into `inst->patternTracks[track]` and the grouped row can
order its buttons. `PatternTrackData` is the persistent/on-disk form.

Conversion helpers (`src/modsystem.c`):

```c
void initPatternTrackData(PatternTrackData *t);           /* defaults */
void patternStateToTrackData(const PatternState *p, PatternTrackData *t);
void patternTrackDataToState(PatternState *p, const PatternTrackData *t);
```

- defaults: 16 steps of `0.5`, length 16, `SH_HOLD`, slew `0.2`, `PP_UNIPOLAR`.
- `patternTrackDataToState` writes `steps`, sets `stepCount = length`,
  and `setParameterBaseValue` on all four params (so both the display
  read and the DSP read see the restored values — rule #1051).

`Instrument` gains:

```c
PatternTrackData patternTracks[PATTERN_TRACKS];
```

Initialised in `init_instrument` for all `PATTERN_TRACKS`. It is **not**
cleared by `applyInstrumentPreset`, so it survives preset load.

## B. Source creation + cap

- New `MAX_MOD_SOURCES` (16) in `src/voice.h`. It sizes
  `Instrument.envelopes[]` and `gui_inst_mod.c`'s `g_sourceCtx[]`, and
  replaces `MAX_ENVELOPES` in the `addRuntimeSource` / `addRuntimePattern`
  guards. `MAX_ENVELOPES` and `Preset.modSettings[]` are untouched (the
  preset-facing envelope/LFO arrays keep their current sizing).
- A new `applyPatternTracksToInstrument(Instrument *inst)` helper creates
  the four core pattern sources (`"PTN1".."PTN4"`, type `MT_PATTERN`,
  bound to `inst->metaChannel`) via `createPattern`, then
  `patternTrackDataToState` from `inst->patternTracks`. It appends them
  after the preset's sources.
- `applyInstrumentPreset` calls it at the end (after the preset sources
  and voice drivers are rebuilt), then snapshots
  `coreEnvelopeCount = modSourceCount(modList)`. The four patterns are
  therefore core (protected from DELETE/type-cycle) and always present.
- `init_instrument` initialises `patternTracks[]`; `createVoiceManager`
  calls `applyInstrumentPreset` for every channel, so all 16 instruments
  get four tracks at boot.
- `MT_PATTERN` is removed from the UI type cycle (`cbCycleSourceType`);
  non-core sources cycle `ENV → LFO → RND → ENV`. The modsystem
  implementation stays for DSP/tests and for internal creation.

## C. Preset interaction

Patterns are **song-level, not patch-level**:

- `presetFromInstrument` **skips** `MT_PATTERN` sources when building
  `modSettings` (compacts the count) — patterns never enter a `.ipb`.
- `applyInstrumentPreset` restores only env/LFO/RND from the preset, then
  re-creates the four pattern sources from `inst->patternTracks`, so a
  preset load never destroys the user's sequences.

## D. Song-file persistence

New optional `PTRK` chunk in `s1.sng`, written after the existing `LABL`
chunk and read with the same peek/rewind pattern `LABL` uses (older songs
without `PTRK` load with default tracks).

- `PatternTrackSet` (in `src/modsystem.h`):
  ```c
  typedef struct {
      PatternTrackData track[MAX_SEQUENCER_CHANNELS][PATTERN_TRACKS];
  } PatternTrackSet;
  ```
- `saveSequencerState` / `loadSequencerState` gain a nullable
  `PatternTrackSet *` parameter (writes nothing / skips when `NULL`).
  Existing `test_io.c` call sites pass `NULL`; new tests cover the round
  trip and backward compatibility.
- `main.c` and the harness assemble the set from
  `vm->instruments[ch]->patternTracks` before save, and after load copy
  each channel's tracks back into `inst->patternTracks` and re-apply them
  to the live pattern sources (`patternTrackDataToState`).
- Capturing the live edits: any pattern-screen edit writes the
  `MT_PATTERN` source **and** mirrors into `inst->patternTracks[track]`
  via `patternStateToTrackData`. A save-time capture pass is a safety net.

## E. Instrument grouped row

`appendModSourceEntry` (`src/gui_inst_mod.c`):

- When `mod->type == MT_PATTERN`: only the **first** pattern source emits
  a row; subsequent pattern sources emit nothing.
- The grouped row contains: a `PTN` label, a compact readout string
  (each track's shape abbreviation + length), and **four ROUTE buttons**
  (one per track). Each ROUTE button's `SourceCtx` points at that track's
  pattern source index, so routing reuses the existing picker unchanged.
- No TYPE or DEL button (patterns are core).
- The inline `PatternGridNode` / `handlePatternGridInput` become dead code
  and are removed (`gui_inst_internal.h` declarations too).

## F. Pattern screen pages

`ApplicationState` gains `int patternPage` (0..4), initialised to 0 and
clamped to `0..PATTERN_TRACKS` whenever the selected cell changes
(`setSelectedPattern`).

`gui_pattern.c`:

- `createPatternGraph` branches on `patternPage`:
  - **page 0 (NOTES)**: existing graph (buffer scroller + 4×4 note grid),
    unchanged.
  - **page >0 (track)**: indicator node + dial strip (`LEN`, `SHAPE`,
    `SLEW`, `POL` via `createDialGuiNode` + `incParameterBaseValue`) +
    16-cell grid (4 rows × 4, same geometry as the note grid). Cells are
    selectable nodes carrying `{PatternState *pattern; int stepIndex;
    int *selectedStepPtr; Instrument *inst;}`.
- The **indicator** is a node above the grid drawn with the page label:
  `NOTES` or `PTN k/4`, plus a compact settings readout
  (`LEN 16 · LINE · UNI`).
- Selection state: `selectedStep` is shared with the note page (it is a
  0..15 step index); a small static records whether the cursor is on the
  grid or in the dial strip and which dial is selected.
- `navigatePatternGraph` is extended for track pages:
  - grid: LEFT/RIGHT ±1, UP/DOWN ±4; UP from row 0 → dial strip; DOWN from
    the dial strip → grid.
  - dial strip: LEFT/RIGHT between the four dials.
  - `EDIT+UP/DOWN` on a step cell changes its value by ±1/16 (clamped
    0..1) and marks the instrument dirty; on a dial it adjusts the param
    (same semantics as the instrument screen).
- Page switch rebuilds the graph (`rebuildPatternGraph`).

## G. Input grammar (`SCENE_PATTERN`, main.c + harness)

- `SHIFT (KM_SELECT) + UP/DOWN` (just-pressed): cycle `patternPage`
  `0 → 1 → 2 → 3 → 4 → 0`, rebuild the graph. Consumed before other
  handling.
- Page 0: existing note input unchanged.
- Track pages: arrows navigate; `EDIT+UP/DOWN` edits; `FUNCTION+arrows`
  still switches the arranger cell (channel) as today.

## H. Testing

Ground-up order (rule #1063):

1. **DSP** (`tests/dsp/test_modsystem.c`, `test_mod_voice.c`):
   `initPatternTrackData` defaults; `patternTrackDataToState` round trip;
   `applyPatternTracksToInstrument` yields 4 `MT_PATTERN` sources bound to
   the channel with restored step data; `applyInstrumentPreset` preserves
   `inst->patternTracks` and re-creates the four sources; `MAX_MOD_SOURCES`
   guard; `presetFromInstrument` excludes `MT_PATTERN`.
2. **IO** (`tests/dsp/test_io.c`): `PTRK` round trip; an old file with no
   `PTRK` loads defaults; existing call sites pass `NULL` and still pass.
3. **Graph nav** (`tests/dsp/test_graph_nav.c`): track-page graph builds
   the indicator + 4 dials + 16 cells; page clamping.
4. **Harness fixture** (`pattern_screen.txt`): cycle pages with
   `SHIFT+UP/DOWN`, edit a step value, cycle `SHAPE`, assert the page
   indicator and edited values; a song save/load round trip preserving
   tracks. Migrate count/layout-sensitive fixtures (`mod_sources`,
   `add_route_delete`, `clear_routes`, `pagenav`,
   `route_lines_follow_selection`, `chip_meta`, `preset_save_load`,
   `save_overwrite`, `task4_size_verify`).
5. **Gate**: `ninja` clean, `meson test` green, all fixtures PASS, app
   boots under Xvfb.

## I. Phasing

- **A**: data model (`PatternTrackData`, `Instrument.patternTracks`,
  helpers) + `MAX_MOD_SOURCES` + `applyPatternTracksToInstrument` +
  preset skip/reseed.
- **B**: song-file `PTRK` chunk + I/O signature + save/load sync.
- **C**: pattern-screen pages, nav, indicator.
- **D**: grouped instrument row; remove the inline grid.
- **E**: fixture/test migration + full gate.

Each phase ends with the relevant tests green and is independently
reviewable.

## J. Out of scope

- Per-step interpolation flags.
- Tempo divisions / rate multipliers (tracks stay 1:1 with the song
  pattern step).
- Changes to the note pattern itself.
- Pattern chains / track chaining.
- Editing pattern tracks from the instrument screen (settings dials were
  removed from the row in favour of the pattern screen).
