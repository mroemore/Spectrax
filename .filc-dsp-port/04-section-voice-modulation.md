# Section 4 — Voice & Modulation

**Goal:** Wire oscillators into voices with envelopes, and route modulation.
This is where DSP becomes a synth.

## Scope

- `src/voice.c|h` — voice state, ADSR envelope, polyphony management.
- `src/modsystem.c|h` — LFO sources, modulation matrix.
- `src/sample.c|h` — minimal sample player (if needed).
- Integration test: a single voice playing a single note through the mod
  system.

## Approach

1. Read voice.h to understand voice lifecycle (note_on, note_off, render).
2. Read modsystem.h to understand matrix layout (source → dest, depth).
3. Build a `test_voice_lifecycle` that:
   - Constructs a voice
   - Triggers note_on
   - Renders N samples
   - Verifies envelope attack phase
   - Triggers note_off
   - Verifies release phase
4. Build a `test_mod_routing` that wires an LFO to a parameter and checks
   the output oscillates.

## Expected fil-c gotchas

- Voice state is heap-allocated — verify free works without use-after-free
  panics.
- Modulation matrix may use function pointers (mod sources) — fil-c
  supports these but checks capability on the closure context.

## Tests added

- `test_voice_lifecycle`
- `test_mod_routing`
- `test_envelope_shape` (ADSR curve points)

## Subagent plan

- One subagent for voice tests
- One subagent for mod tests
- (Both can run in parallel.)

## Status

- [ ] Voice API mapped
- [ ] Mod API mapped
- [ ] Tests written
- [ ] Tests pass under fil-c
- [ ] Report file updated

## Blocker log

(Empty.)