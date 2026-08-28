# Spectrax: Instrument Preset Save / Load + UI

Date: 2026-08-27
Status: Approved design

## Summary

Make instrument preset saving and loading actually work end-to-end, expose it
through the instrument screen UI (save / load buttons, a selectable name-entry
node, a selectable load-list node), and add an overwrite-confirmation modal.
Today `applyInstrumentPreset` ignores the preset's parameter data (FM ops are
hardcoded, sampler/blep/LFO/RND never applied), `savePresetFile` is never
called (saving is unimplemented), `Preset` has no name, and the project file
records no per-channel preset slot. This work fixes all of that.

## Terminology

- **Slot** — a bank index (`patches[i]`). The preset dial navigates slots and
  applying a slot's preset is what plays.
- **Preset file** (`.ipb`) — source of truth for a preset's **name** and
  **parameter data**.
- **Project file** (`s1.sng`) — source of truth for **which preset occupies
  which slot** (per channel).
- **Name** — user-facing preset identifier, entered via the arcade name entry.

## 1. Data model & file formats

### 1.1 `Preset` gains a name

Add `char name[33]` (32 chars + NUL) as the first field of `Preset`
(src/voice.h). Bump the `.ipb` magic to `PRESET_MAGIC_HEADER_V2`
(src/io/io.h). `savePresetFile` writes the V2 header + `sizeof(Preset)`.
`loadPresetFile` accepts both magics:

- V1 files: read the old-size struct, derive the name from the filename
  (e.g. `lead_pad.ipb` → `lead_pad`), then re-write the file as V2
  (auto-migration).
- V2 files: read the full struct including `name`.

The 5 shipped `.ipb` files are byte-identical default-FM presets, so migration
is trivial and lossless.

### 1.2 Project file gains per-channel slot assignments

Add `int channelSlots[MAX_SEQUENCER_CHANNELS]` to the arranger section of
`s1.sng` (saved/loaded in src/io/sequencer_io.c). Bump the song format magic.
Old files: `channelSlots` defaults to 0 for every channel.

On project load, each channel's instrument applies
`patches[channelSlots[ch]]` (rebuilds the instrument for that channel).

### 1.3 Bank model

The bank stays directory-loaded: all `.ipb` files in `data/instrument_presets/`
populate slots `0..presetCount-1` at startup. Saving writes a named `.ipb` into
the dir, registers/updates the bank slot, and points the current channel's slot
at it.

## 2. Preset engine

### 2.1 Fix `applyInstrumentPreset` fidelity (src/voice.c)

Today the function ignores preset data. Fix:

- **FM**: after `createOperator`, apply `p.pd.fm.ops[i]` (`feedbackAmount`,
  `level`, `outLevel`, `ratio`) to the op params, and set
  `selectedAlgorithm` from `p.pd.fm.selectedAlgorithm`. Remove the hardcoded
  ratio defaults.
- **Sampler**: apply `p.pd.sampler` (`bitDepth`, `sampleRate`, `loopSample`,
  `sampleIndex`, `playbackType`, `loopStartIndex`, `loopEndIndex`) to the
  sampler params.
- **BLEP**: apply `p.pd.blep.shape`.
- **LFO / RND**: create real mods via the existing
  `initLfoFromPreset` / `initRandFromPreset` (currently only counted).
- The `modSettings` loop becomes the single source of envelopes — drop the
  hardcoded `envelopeCount = 4` in the FM case.

### 2.2 New `presetFromInstrument(Instrument *) -> Preset` (src/voice.c)

The reverse, for saving:

- `voiceType` from `inst->voiceType`.
- FM ops values + algo from `inst->id.fm` (or sampler / blep data from
  `inst->id.sampler` / `inst->id.blep`).
- `modSettings[]` serialized from every mod in `inst->modList` (envelopes via
  the existing save helpers, LFOs via `saveLfoPreset`, RND via
  `saveRandPreset`). This covers runtime-added SP-C envelopes and routes.
- `name` from the slot's current name.

Scope note: `panning` / `detune` / `volumeAttenuation` are instrument-level
params not in `Preset`. They are intentionally excluded for now (future: record
more destinations like Pan in a preset).

### 2.3 Save / load plumbing

- `saveInstrumentAsPreset(inst, name, dir)`:
  1. Build the Preset via `presetFromInstrument`, set `name`.
  2. Sanitize the name → filename (`lead pad` → `lead_pad.ipb`).
  3. If a `.ipb` with that name already exists → return `EXISTS` (UI prompts).
  4. Else write the file (V2), register/update the bank slot, point the
     channel slot at it, return `OK`.
- `savePresetFile` must check the `fwrite` return value (existing latent bug).
- LOAD list enumerates on-disk presets via `DirectoryList` (sorted).

## 3. UI (instrument screen)

### 3.1 Graph elements

- **SAVE button** — `createBtnGuiNode` in the PRESET controls row. Selecting
  it focuses the name entry node for editing (the name node is also directly
  selectable/editable without pressing SAVE; SAVE is a discoverable label).
  Committing the edited name runs the save flow below.
- **LOAD list node** — a selectable graph node showing a scrollable list of
  on-disk preset names. Up/down scrolls, KM_START loads the highlighted preset
  (applies to the instrument + points the channel's slot at it), KM_SELECT
  cancels.
- **Name entry node** — a selectable graph node in the PRESET controls row,
  showing the current preset's name as **red characters on a black
  background with an inverted cursor block** on the active character. While
  it is the selected node:
  - Up/down cycles the active char through `A-Z 0-9 space _ - .`
  - Left/right moves the cursor slot
  - KM_START commits the name → runs the save flow (overwrite check)
  - KM_SELECT exits edit mode back to normal navigation
  - An empty/whitespace name defaults to `UNNAMED` on commit.

### 3.2 Overwrite modal (the only true modal)

After committing a name that matches an existing `.ipb`: a centered overlay
panel `OVERWRITE [NAME]?` with YES/NO. Left/right selects, KM_START confirms.
YES → overwrite the file; NO → return to the name entry.

### 3.3 Modal / edit state machine

```
enum ModalState {
  MODAL_NONE,
  MODAL_NAME_EDIT,          // name node is selected (graph-level, not a popup)
  MODAL_CONFIRM_OVERWRITE,  // true modal overlay
  MODAL_LOAD_LIST,          // load list node is selected (graph-level)
}
```

State lives in gui.c, shared by the app and the instrument harness. In the
instrument scene's input path, a non-NONE state takes priority over graph nav.

## 4. Testing

### 4.1 File I/O (tests/dsp/test_io.c)

- V2 `.ipb` round-trip preserves the name.
- V1 → V2 auto-migration: load an old-format file, name derived from filename,
  file re-saved as V2.
- Project file channel-slot round-trip + migration (old `.sng` → slot 0).
- Filename sanitization.

### 4.2 Preset fidelity (tests/dsp/test_mod_voice.c)

- `applyInstrumentPreset` applies FM op values (ratio/level/feedback/outLevel/
  algo), a sampler patch, a BLEP shape, and creates LFO/RND mods.
- `presetFromInstrument` → `applyInstrumentPreset` round-trip reproduces the
  instrument's params.

### 4.3 UI (instrument_harness scripted fixture)

- Navigate to the name node, edit a name, commit → save.
- Save over an existing name → overwrite modal → confirm / cancel.
- Load list node scroll + load.

## Non-goals

- Layer / z-index system for graphs (future, noted separately).
- Recording panning/detune in presets (future).
- Sampler/spectral/granular voice-type preset fidelity beyond the sampler
  patch fields (spectral/granular remain FM-default placeholders, unchanged
  from today).