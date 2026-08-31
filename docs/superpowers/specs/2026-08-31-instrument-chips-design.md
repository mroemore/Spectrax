# Spectrax instrument chips + meta row — design

Date: 2026-08-31

## Problem

The arranger screen has no per-channel instrument identity: the grid shows
only cell numbers, and there is no way to see or act on what instrument
type each channel runs, what it's called, or its status. The original
"instrument type selection" idea evolved (with the user) into a richer
per-channel **chip** that is both a shortcut to the instrument page and a
customizable label/status display, plus a **meta row** on the instrument
page where instrument-level parameters (type, voice count, width-later)
live.

## Scope

Two screens, one data model.

### A. Data model (per channel, persisted)

Each of the `MAX_SEQUENCER_CHANNELS` channels gains:
- `labelColourIdx` — index into the preset label-colour palette (below).
- `label[9]` — the 8-char label + NUL.

Stored on the `Arranger` (alongside `channelSlots`). Persisted in the
song file's arranger section: the SEQ2 file gains a `LABL` chunk (magic +
count + `MAX_SEQUENCER_CHANNELS * (int + char[9])`). Old files (no LABL
chunk) default to `labelColourIdx = 0` + empty labels. The arranger
section reader must accept both with/without the chunk (mirror the
channelSlots pattern: read the chunk if present, else defaults).

**Label-colour palette**: a fixed table of 8 named colours (theme-agnostic
defaults, e.g. red/green/blue/cyan/magenta/yellow/white/grey), defined in
one place so both the swatch row and the chip draw use it. (A themed
`cs.*` entry could map the palette later; for now the palette is a plain
`Color` array.)

**Chip label input character set**: reuse the arcade input rules from the
preset-name node (A-Z, 0-9, space, `_`, `-`, `.`), capped at 8 chars.

### B. Arranger side — the chip row

The arranger graph gains a row of per-channel chip nodes directly above
the grid, one per `enabledChannels` channel, each horizontally bound to
its grid column (the chip x-range aligns with the channel column below).

**Chip contents** (drawn top-to-bottom):
- Top-left: `V:{n}` where `n = vm->voiceCount[channel]`.
- Top-right: the type tag — `FM` / `SMP` / `BLP` (text tag; the corrupt
  `synthicon_sheet.png` sprite is NOT used — regenerating the sprite sheet
  is a separate future task).
- Centre: the 8-char label (rendered in the chip's label colour, inverted
  for legibility on the coloured background).
- Bottom: the current patch name (`inst->loaded.name`, or the preset bank
  name for the selected slot).
- Bottom corner: a **voice-active light** — a small filled dot lit when
  any voice in that channel's voice pool has `active` set.

**Chip background**: the channel's label colour from the palette. The
label text + info draws in a contrasting colour (black on light chips,
white on dark — or a fixed high-contrast choice; pick one in the spec).

**Navigation** (from the grid):
- UP from row 0: `selectArrangerCell` clamps + returns false at the top
  row, so the graph nav takes over and the selection moves to the chip for
  the cursor's column.
- On the chip row: LEFT/RIGHT move between chips; DOWN returns to the grid
  (same column); the grid cursor stays hidden while a chip is focused
  (consistent with the tempo/swing cursor-hide).
- **EDIT+LEFT / EDIT+RIGHT**: jump to the instrument page — set the scene
  to SCENE_INSTRUMENT, sync `selectedArrangerCell` to this chip's channel,
  and select that channel's instrument graph.
- **EDIT+UP**: expand the chip upward into its editing controls.

**Expanded chip** (the chip grows upward):
- **Colour swatch row**: 8 swatches (one per palette colour). LEFT/RIGHT
  moves the swatch focus; the focused swatch sets `labelColourIdx` for the
  channel live (the chip background updates immediately). DOWN collapses.
- **8-char label input**: the preset-name arcade input node, capped at 8
  chars. Z (KM_EDIT) enters edit mode; arrows move the cursor / cycle
  chars; KM_START commits; KM_SELECT exits edit mode. DOWN collapses.
- Collapse on: DOWN, or navigating LEFT/RIGHT to a different chip.

### C. Instrument page side — the meta row

A new row **above** the existing FM/sample controls (the top of the
instrument graph, above the preset controls row):
- **Type selector**: cycles SAMPLE → FM → BLEP (the three working types;
  GRAIN/SPECTRAL stay out of the cycle until implemented). KM_EDIT fires
  the next type (wrapping). Displayed as the type tag text.
- **Voice count**: displays + adjusts `vm->voiceCount[channel]` (the
  channel's voice pool size, currently seeded from
  `settings->defaultVoiceCount`). Adjusting it resizes the channel's voice
  pool (`initVoicePool`), wrapped in the `rebuilding` guard.
- **Width**: reserved (future; drawn disabled or omitted).

**Type-change semantics**: swapping `vm->instruments[channel]` to the new
type re-inits the instrument (`init_instrument` with the new type —
parameters reset, no loaded preset), rebuilds the channel's voices
(`rebuildVoicesForInstrument`), and rebuilds the instrument graphs
(`rebuildInstrumentGraph`). The whole swap runs with
`inst->rebuilding = true` so the audio thread skips the channel (the
established preset-apply pattern). A type change also refreshes the
arranger chips (the type tag + voice count read live state on draw, so
only the chip labels/colours need the graph rebuilt if changed).

## Error handling

- sng with a truncated LABL chunk → treat as absent (defaults), never
  crash the load.
- Voice-count adjust clamped to [1, MAX_VOICES_PER_CHANNEL=8].
- Palette index clamped to [0, 7] on load (defensive).

## Testing

- Unit: label-chunk round-trip in the sng (write → read → same values);
  old-file (no LABL) → defaults; palette index clamp; label truncation at
  8.
- Voice pool: `initVoicePool` resize correctness on voice-count adjust.
- Harness scripted fixture: navigate up to a chip; EDIT+UP expands; set a
  colour; type an 8-char label; DOWN collapses; EDIT+LEFT jumps to the
  instrument page; verify the chip reflects the colour/label; verify the
  type tag + V count on the chip.
- Gate: `ninja` clean, `meson test` 8/8, both existing fixtures PASS, app
  boots.

## Out of scope

- Regenerating the corrupt `synthicon_sheet.png` sprite (separate task).
- GRAIN/SPECTRAL instrument types (not built out).
- "Width" on the meta row (reserved).
- Themed label palette (`cs.*` mapping) — fixed `Color` array for now.
