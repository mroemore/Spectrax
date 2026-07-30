# Section 5 — Sequencing & Full Render

**Goal:** End-to-end render. Sequence a pattern → trigger voices through
mod system → write the result to a WAV file. Compare against gcc baseline.

## Scope

- `src/sequencer.c|h` — pattern/sequence player.
- `src/io/preset_io.c|h` — preset loading (if simple enough).
- `src/io/sequencer_io.c|h` — sequence file I/O.
- Integration: full render → WAV.
- Reference: build same render under gcc, diff output WAVs.

## Approach

1. Read sequencer.h to understand pattern format (steps, notes, length).
2. Hardcode a tiny test pattern (e.g., 4 steps, 1 voice per step, 440 Hz).
3. Build under fil-c, render to `bin/sequences/test1_filc.wav`.
4. Build same under gcc, render to `bin/sequences/test1_gcc.wav`.
5. Diff samples — must be byte-identical (or within a tiny tolerance for
   any rounding differences).

## Expected fil-c gotchas

- Sequencer might use `time()` or clock APIs for timing — fil-c supports.
- File I/O — fully supported.
- Any floating point drift between musl libm and glibc libm. The render
  should still come out exact if fil-c's musl libm is bit-equivalent.

## Tests added

- `test_sequencer` — pattern plays correct number of notes.
- `test_full_render_byte_identical` — gcc vs fil-c WAV diff == 0 bytes.

## Status

- [ ] Sequencer API mapped
- [ ] Hardcoded pattern test
- [ ] Reference build (gcc)
- [ ] Fil-c build
- [ ] WAV diff == 0
- [ ] Report file updated

## Blocker log

(Empty.)