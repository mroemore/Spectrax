# SDD ledger — plan: docs/superpowers/plans/2026-11-02-adaptive-arm-handheld.md

Executor: inline (superpowers:executing-plans). Spec:
docs/superpowers/specs/2026-11-02-adaptive-arm-handheld-design.md and
docs/superpowers/specs/2026-11-03-canonical-synthesis-spec.md.

## Status

Phase A (the canonical core): A1, A2, A5, A6 done; A3 done; A4 done (as a cost
fix, not a data structure); A7's golden half done, its scripted half belongs to
the GUI phase. Phase B's DSP tasks done, the OTA filter and the GUI are the GUI
phase. Phase C: C1 done, C2 partly, C3 open. Phase D: D1, D2 done, D3's golden
leg done, its scalar-vs-NEON leg open. Phase E: open.

Commits so far (branch pattern-mod, base 7032ed5): see
docs/superpowers/plans/2026-10-09-sampler-emulation-engine-progress.md for the
full history; the newest are 5d93b5f (A3), f506a22 (A4), 62c919a (D2),
10416b2 (D1), 9308852 (C1/C2), 7272fb9 (the budget's coverage floor).

## Tasks in this session

- Task 1: the calibration path's coverage (C1's gap: calibrateDevice lives in
  main.c and the harness never runs it).
- Task 2: C3 -- the runtime governor.
- Task 3: E1 -- the sequencer's note velocity into the voice.
- Task 4: C2's remainder -- the permitted voice types and the sample pool's
  ceiling.
- Task 5: D3's remainder -- the scalar-versus-NEON leg of the golden gate.

## Pre-flight: shared interfaces

- Task 1 (calibration) consumes Task C1's device_budget API
  (deviceBudgetDerive/Store/Load) and produces the measurement function the
  app calls. C1 put the measurement inside main.c; this task moves it into a
  testable unit.
- Task 2 (the governor) consumes the voice manager's voice allocation
  (getFreeVoice) and the callback's timing; it must not alter a surviving
  voice's output (criterion C).
- Task 3 (the velocity) consumes the sequencer's note events and the voice's
  trigger path; it must not alter the pitch or the envelope (criterion C).
- Task 4 (the voice types) consumes the budget's voiceTypes mask and the
  instrument's voice type; Task 5 consumes the golden render's hash and the
  cross build's flags.

---

## Session 2 (2026-11-03): the remaining pre-GUI work

Commits: `a1e6106` the calibration's coverage · `be4744a` C3, the governor ·
`885b59e` E1, the velocity · `015c36f` C2's remainder · `cc93944` D3's scalar
leg · `aebb2b1` the plan's status.

### Done

- **Task 1 -- the calibration path's coverage.** The calibration lived inside
  main.c, so the harness (which drives initApplication, not main) never ran
  the measure -> derive -> store path. It now lives in
  src/device_calibrate.{c,h} and a test drives it. The measurement uses a
  scratch voice, allocated and freed for the measurement: the old version
  used the app's own voice pool and triggered a note in it, so the app could
  start with a note sounding.
- **Task 2 -- C3, the runtime governor.** src/voice_governor.{c,h}. The
  callback's own wall-clock duration is the measurement (clock() counts every
  thread, so the GUI's rendering would have counted against the audio
  callback's deadline). The decision is on a smoothed utilisation with a
  hysteresis band; the floor is one voice. The steal is pinned, and it
  prefers a voice whose envelope has run out -- see the velocity commit.
- **Task 3 -- E1, the velocity.** The pattern gains a per-step velocity
  (1..127, zero = unset = full). It is saved in its own optional trailing
  chunk (VELO), so an older file loads unchanged and a song that never uses
  velocity stays byte-identical. The sequencer's trigger carries it; the
  voice keeps it. The MPC60's two rules (the start address moves with the
  velocity; the level is the machine's curve) and the no-choke rule are
  implemented.
- **Task 4 -- C2's remainder.** The sample pool no longer mallocs a fixed
  32 MB arena up front: it grows as samples arrive, up to the budget's
  ceiling, and allocates nothing until the first sample needs it. The
  budget's permitted-voice-types mask is enforced by the engine, so the
  GUI's greying out is backed by a real refusal.
- **Task 5 -- D3's scalar leg.** The gate now also builds the target without
  the vectorizer and requires the same hash: the vectorized build has 6619
  vector instructions in the golden test, the scalar one has none, and they
  agree bit for bit. The gate was verified by breaking it.

### Rulings

- **Task 1: Ruling:** the calibration moves into its own module rather than
  becoming a function of the app's main(). — *Why:* the app's main() is not
  what the harness drives, so a mechanism that lives there is a mechanism
  nothing can test. — *Cost if wrong:* one more module in the tree.
- **Task 2: Ruling:** the governor is out of force in the golden render. The
  governor is a load policy -- deliberately load-dependent, which is
  criterion B -- and the golden render is criterion C: the engine's output
  must be the same bits on every machine. — *Cost if wrong:* the golden
  render would not cover the governor, which is what it is for.
- **Task 2: Ruling:** the steal policy replaces the old "always voice 0"
  with "the oldest sounding voice". — *Why:* the old policy was arbitrary:
  a note that happened to land on voice 0 was always the one that got cut.
  — *Cost if wrong:* the callback render's recorded hash changed, and it was
  re-recorded; the change is ledgered in the commit.
- **Task 3: Ruling:** the velocity is a separate array in the pattern, saved
  in its own optional chunk, rather than a third note column. — *Why:* the
  note columns are the song file's layout; changing them would break every
  existing song. The optional chunk is the tree's own established extension
  path (LABL, PTRK). — *Cost if wrong:* the velocity is not in the PATT
  chunk, so a tool reading that chunk does not see it.
- **Task 3: Ruling:** the velocity's convention is 1..127 with zero meaning
  "unset", and an unset velocity plays at full. — *Why:* a note with no
  velocity information must sound exactly as it did before velocity existed,
  or every existing song changes. — *Cost if wrong:* a file that explicitly
  stores a velocity of 0 is read as unset.
- **Task 3: Ruling:** the MPC60's level curve is a pinned table, not a
  computed power. — *Why:* the corpus gives the rule but no numbers, and a
  table is the same number on every platform (criterion C); a computed
  power would be a libm call on the audio path. — *Cost if wrong:* the
  curve's shape is a guess, and it is a guess the corpus does not pin.
- **Task 4: Ruling:** the sample pool's arena grows geometrically and the
  samples' data pointers are fixed up as it grows. — *Why:* the arena is one
  block, so growing it moves the memory every sample points into. — *Cost
  if wrong:* a realloc per sample instead of a doubling.
- **Task 5: Ruling:** the scalar leg is a second cross build with
  -fno-tree-vectorize, not a flag on the first. — *Why:* the two builds
  must be genuinely different code for the comparison to mean anything --
  and they are: 6619 vector instructions against none. — *Cost if wrong:*
  another cross build in the smoke test, about five seconds.

### Deferred

- **The armhf leg of the golden gate.** Deferred by the human partner's
  decision (the Miyoo Mini is best-effort, much later). qemu-arm-static is
  not installed on this machine, and the script already skips cleanly when
  an emulator is missing.
- **The velocity's editor.** The data, the file and the plumbing are done;
  the UI that sets a step's velocity is the GUI phase's work, like the
  sampler's character panel.
- **The sampler's envelope-attack coupling.** The corpus says velocity moves
  the sample's start address "as well as envelope attack". The start address
  is implemented; the envelope's attack scaling is not, because the
  envelope's stage durations are instrument parameters re-synced into the
  voice every buffer, so a per-voice velocity-scaled attack needs the sync
  path to carry a per-voice scale. That is a change to the sync path's
  contract, which is criterion-C-relevant, and it belongs with the
  sampler's own character work.
