# Spectrax testing stack

Piloting the SQLite / DO-178B-style testing discipline on Spectrax. It began on
the `testing-stack` branch and now lands on the main line.

The point of this file is twofold: describe the stack, and keep a running log
of every issue hit while building it and how it was solved (see
[Issues & solutions](#issues--solutions)).

## How to run

```sh
./meson/coverage.sh          # build + test + print line/branch summary
./meson/coverage.sh --html   # also write build-cov/html/index.html
meson test -C build-cov      # just the suite
```

## Phases

| Phase | What | Status |
|-------|------|--------|
| P1 | Coverage spine (build-integrated lcov, branch-aware) | done |
| P2 | Contract macros (`ALWAYS` / `NEVER` / `testcase`) + assert policy | done |
| P3 | Branch closure + boundary tests + coverage gate | done (mechanism; modules close incrementally via the ratchet) |
| P4 | Anomaly / fault injection (OOM, I/O fail-after-N) | done |
| P5 | Fuzzing (config/JSON, WAV) + seed corpus | done |
| P6 | Dynamic-analysis matrix (ASan/UBSan/valgrind/-ftrapv, char signedness) | done |
| P9 | Concurrency: extract the render path, TSan + helgrind gated | done |
| P10 | Untrusted-input hardening: fuzz the project loader, `--wrap` the binary loaders | done |
| P7 | Mutation testing (input-byte, exhaustive) | done |
| P8 | Process (regression-per-bug, release checklist, gates) | done |

## How to run — every gate

`meson/check.sh` is the single entry point; tiers are ordered cheap-first and
stop on the first failure.

```sh
./meson/check.sh          # fast tier (~1-2 min): coverage + gate
./meson/check.sh --full   # everything: + matrix + fuzz smoke + mutation
```

Individual tools:

```sh
./meson/coverage.sh --gate           # suite + coverage floors
./meson/matrix.sh                    # asan / trapv / uchar / schar / valgrind
./meson/fuzz.sh wav config --time 60 # libFuzzer targets
./meson/mutate.sh --seed 5           # seeded bit-flip mutation run
```

The `pre-push` hook (`meson/pre-push`) runs the fast tier and **fails closed**.
Install it once per clone:

```sh
ln -sf ../../meson/pre-push .git/hooks/pre-push
```

(Directory layout: `TESTING.md` documents the stack; `docs/superpowers/plans/`
holds the release checklist. `docs/*` is gitignored in this project (I3), so the
checklist is added with `git add -f docs/superpowers/plans/RELEASE-CHECKLIST.md`.)

## Mutation testing (P7)

`tests/support/mutate.{h,c}` + `meson/mutate.sh` provide deterministic,
dependency-free mutation testing. Each target is a self-contained
**input-mutation harness**: it flips bits of the input the code under test
consumes, runs the loader, and requires the harness's own assertions to notice.
`tests/dsp/test_mutate_input.c` is the worked example — it sweeps every bit of a
valid WAV (608 flips) and requires the loader to either refuse the image or
decode a different PCM.

```sh
./meson/mutate.sh                      # all targets (default: test_mutate_input)
MUTATE_VERBOSE=1 ./build-mutate/tests/test_mutate_input   # list survivors
```

Current result for the WAV loader: **killed 414, data-chunk survivors 0,
ignored-header survivors 194.** The 194 are flips in header fields the loader
does not read (`chunkSize`, `audioFormat`, `byteRate`, `blockAlign`, fmt size,
…), so an unchanged decode is *correct*; those are reported, not gated. Only
data-chunk survivors fail the target — a non-zero count there is a real gap
(a flip in the audio that the loader silently accepted).

Why input bytes and not allocator bytes: an allocation-time flip is overwritten
by the very `fread` that fills the buffer, so it never reaches an assertion —
the defeated first design, kept as I23 for the lesson. Why not Mull: there is no
mutation tool on this box, gcc ships no sanitizer runtime (I18), and a Mull
toolchain would pin the compiler. The exhaustive seeded harness is the trade-off
that runs anywhere.

## Process (P8)

The automated tiers prove the code is consistent with what the tests check; they
cannot prove the feature is right. P8 is the human-in-the-loop wrapper:

- **`meson/check.sh`** — one command, tiers ordered cheap-first, stops on first
  failure. `--full` adds matrix + fuzz smoke + mutation. This is the single
  definition of "gates pass".
- **`meson/pre-push`** — fast tier, **fails closed** (the point of I22). Install
  per clone: `ln -sf ../../meson/pre-push .git/hooks/pre-push`.
- **Release checklist** — `docs/superpowers/plans/RELEASE-CHECKLIST.md`:
  scope, automated gates, coverage ratchet, real-app smoke, regression
  discipline, notes, tag. The real-app smoke step is the one no automation
  replaces.

### Regression discipline

Every user-reported bug gets a test that **fails on the old code and passes on
the new**, and the test is named for the symptom, not the fix. Existing examples
in this repo: `test_note_delete_mid_playback_does_not_crash`,
`test_playhead_does_not_wrap_when_stopped`,
`test_increment_scene_requires_selected_pattern`. The crash fixes from P5
carry the crashing fuzz inputs as permanent regressions in
`fuzz/corpus/wav/regress*`.

## Coverage baseline

First-party sources only (system, vendored, and test code excluded), captured
after the full suite (**31 suites**) passes. The coverage build defines
`NDEBUG` (see I12), so `assert()` sites are not instrumented:

```
                         files   lines              functions          branches
app code (src/)             34   50.3% (4091/8134)   55.4% (319/576)   40.4% (2042/5052)
tools/vizulobe             11   76.7% (495/645)     87.2% (41/47)     48.8% (163/334)
```

**Do not compare against the P1-era figure (69.6% lines).** That number was
produced by a capture that silently dropped files: at P1, `gui_core.c`,
`gui_pattern.c`, `gui_arranger.c`, `gui_instrument.c` and `gui_layer.c` had no
`SF:` entry at all — invisible, not 0% (I27). Those five files are 1,831 lines
and sit at 27.5% today, so measurement alone moved the denominator. Recomputing
over only the files P1 could see still gives 55.6%, because the P1 set also
omitted files that are now measured (`gui_style.c`, `graph_gui.c`, `gui_inst_*.c`,
and `audio.c` after the render path moved out of `main.c`). **The 49.2% above is
the first number produced by a capture that sees the whole codebase.**

The average is also a poor prioritisation signal, because coverage here is
bimodal. Modules are either closed (100%) or barely touched:

```
closed     filters.c  notes.c  wavetable.c  gui_layer.c   gui_pattern.c 98.6%
           gui_arranger.c 97.8% line / 100% func / 81.5% branch
in progress gui_instrument.c 42.0% line / 57.9% func / 19.7% branch (I40, I44-I48)
in progress gui_inst_mod.c 42.9% line / 62.9% func / 29.2% branch (I49, I51, I52)
low        dataviz.c 3.2%   gui_core.c 22.7%
           gui_inst_fm.c 0%  gui_inst_blep.c 0%  gui_inst_sample.c 0%
```

Those seven low files are ~2,700 lines and hold the large majority of the
remaining headroom; `gui_inst_mod.c` alone is a third of the app's branches.

Worth noting where the io work landed: `io/sequencer_io.c` went from 55.8% to
86.5% line coverage and 64.0% to 86.5% branch, almost entirely from the fuzzer's regression corpus plus
the load-clamp tests, and `io.c` (the WAV loader) sits at 70.5%. **The format that
had the most defects was also the one with no fuzzer** — the correlation is not a
coincidence, and it is the argument for fuzzing every parser rather than the one
that happened to be popular.

Per-module rates move as the ratchet in `meson/coverage-min.txt` closes each one.

### Worked module: `gui_pattern.c` to 98.6% branch

Second GUI module, and the one that exposed a file-sourced memory-safety bug.
`gui_pattern.c` builds the pattern-step grid (a `Graph` of row/step nodes) and
handles arrow-key navigation of the selected step. It went **0% → 100% line,
100% function, 98.6% branch**.

Three tests, same techniques as `gui_layer`:

1. `tests/dsp/test_gui_pattern.c` — graph construction (row layout, partial last
   row, empty pattern, out-of-range `selectedPattern`, negative and oversized
   `pattern_size`), all four navigation directions plus the `default`
   (unmapped-key) case, edge clamping, `rebuildPatternGraph`, and the
   "called before any create" guards. `PatternList`/`Sequencer` are plain
   structs so the tests build them directly; `selectStep()` calls
   `patternList->onStepChange.f`, so the tests install a recording stub — that
   callback is part of the contract, not a detail.
2. `tests/dsp/test_gui_pattern_fault.c` — `createSongMinimapGui`'s malloc guard
   via `--wrap`.
3. `tests/dsp/test_gui_pattern_draw.c` — `drawStepGuiNode`, `drawSongMinimapGui`
   and `gui_pattern_draw` under Xvfb. The minimap test populates
   `arranger->song` so all four colour branches (selected / playing / default /
   blank) are taken, and the sequencer channels are arranged so the channel-scan
   operands are each exercised (the scan stops at the *first* match, so the
   "running" and "stopped" cases need two separate passes with a
   `rebuildPatternGraph` between them).

**Five defects fixed (I29)**, four of them memory-safety:

1. **Out-of-bounds read/write from an unvalidated `pattern_size`.**
   `createStepNode` writes `stepData[stepIndex]` and `stepNodes[stepIndex]`,
   both `MAX_SEQUENCE_LENGTH` (24) entries, for every step up to
   `pattern_size`. `pattern_size` comes from a project file
   (`io/sequencer_io.c` reads it with `fread` and **no range check**), so a
   corrupt file drives out-of-bounds writes at graph-build time and an
   out-of-bounds read of a garbage `GuiNode *` in `navigatePatternGraph`
   (segfault). Fixed at three layers:
   - `io/sequencer_io.c` — reject a `pattern_size` outside `[0,
     MAX_SEQUENCE_LENGTH]` on load (same class as the WAV `subchunk2Size` bound,
     I16).
   - `sequencer.c addPattern()` — clamp, plus NULL-argument rejection and a
     `pattern_count` bound. This is the chokepoint every pattern passes through,
     and its `notes[i][j]` copy loop was *itself* an out-of-bounds write for an
     oversized size.
   - `gui_pattern.c` — bound `size` to the array it indexes, and bound `next`
     in `navigatePatternGraph` (which clamps against `pattern_size`, not against
     the array) *before* storing it.
2. **Graph/data desync on clamp.** `createPatternGraph` clamped the selection
   locally but did not write it back, so the grid highlighted step 0 while the
   caller's `*selectedStep` still said −3 — and since `navigatePatternGraph`
   derives the next step from the *caller's* pointer, the first key press landed
   somewhere other than adjacent to the highlighted cell. Fixed by writing the
   clamped value back.
3. **Unchecked `malloc` in `createSongMinimapGui`** — dereferenced the result
   immediately; crashed under OOM instead of returning NULL.
4. **NULL deref in `gui_pattern_draw`** — `drawNode(patternGraph->root)` with no
   guard, so the first frame before `createPatternGraph` (screen switch, early
   frame) dereferenced NULL.
5. **NULL deref in `drawStepGuiNode`** — `getStep()` returns NULL for an
   out-of-range pattern or step, and the result was dereferenced immediately.
   That NULL contract is ours (P6/I20 made `getStep` bounds-guard); this caller
   never caught up.

Plus a lifecycle gap worth noting separately: the module had **no teardown**, so
`createPatternGraph` called twice orphaned the first tree — valgrind showed 12
leak records (one 210 KB). Added `destroyPatternGraph()` (paired with the
existing create, per the project's create/destroy convention) and made
`createPatternGraph` free any previous graph before replacing it. Valgrind now
reports zero leaks for the module's tests.

**Documented exclusion (one branch, 98.6% not 100%).**
`drawStepGuiNode`'s `st->note.fontSize ? (float)st->note.fontSize : (float)textFont.baseSize`.
The default step-cell style deliberately leaves `fontSize` at 0 so the ternary
falls through to `textFont.baseSize`; the styled path needs a `step-cell` class
in `layout.json`, and neither `src/layout.json` nor `bin/layout.json` defines
one — so production always takes the default. `coverage-min.txt` therefore floors
`gui_pattern.c` at 98 rather than manufacturing a config the product does not
ship. This is the DO-178 "record the exclusion and why" case, not a gap to paper
over.

### Worked module: `gui_layer.c` to 100%

The first *GUI* module closed by this stack, and the proof of the "test the
logic, stub the draw" approach: `gui_layer.c` is the modal-overlay layer stack —
pure container/state logic (push/pop/peek/find, passive-layer skipping, input
routing decisions) behind a single draw function. It went **0% → 100% line /
function / branch** with no stubbing of production code.

What it took:

1. `tests/dsp/test_gui_layer.c` — the stack lifecycle, growth, passive
   skipping, `findLayerByName`, the `*Free` variants, and every input-routing
   branch (arrow keys, SELECT pop, ROUTE bookkeeping, EDIT firing the selected
   action). Input is driven by **writing the synthetic `InputState`'s key flags
   directly** — `isKeyJustPressed()` reads `state->keys[]`, it does not call
   raylib — so no window is needed for the logic.
2. `tests/dsp/test_gui_layer_fault.c` — the two allocation-failure guards
   (`pushLayer` growth realloc, `popLayer`/`popTopNonPassiveLayer` mount
   malloc) via `--wrap`.
3. `tests/dsp/test_gui_layer_draw.c` — `layerStackDraw`, the one function with
   real raylib draw calls, run under **Xvfb** (the project already uses
   `xvfb-run` for the vizulobe GL tests). It opens a window, draws a stack
   covering every branch (dim, dim+graph-less, root-less graph), and asserts the
   path executed. Not a golden-image test — that is a different tool.
4. `src/gui_layer.c` fixes the framework surfaced — **four real bugs** (I28).

`meson/coverage-min.txt` now gates `gui_layer.c` at 100.

### Worked module: `filters.c` to 100%

`filters.c` was taken from **32.8% → 100% branch** as the first application
module closed by this stack (the earlier 100% modules, `notes.c`/`wavetable.c`,
are pure helpers; this one is real DSP with coefficient maths and an edge-case
surface). What it took:

1. `tests/dsp/test_filters.c` — drives every reachable branch: all four
   `BiquadType`s through both the free functions and the dispatch pointer, both
   `FilterType` coefficient generators over several frequencies, `secondOrderHPF`
   as well as LPF, the `checkFLoatUnderflow` sign/range matrix (including exact
   zero), the invalid-`FilterType` default, and behavioural checks (analytic DC
   gain; the LPF settles to its realised gain, the HPF inverts DC).
2. `tests/dsp/test_filters_fault.c` — the two allocation-failure branches
   (`!bf`, `!flt`) via the `--wrap` harness. These are unreachable without
   fault injection.
3. `src/filters.c` — `createBiquadFilter` had **no `default` case**: an
   out-of-range `BiquadType` left `processSample` pointing at uninitialised
   memory. The switch's implied default branch was the last uncovered branch;
   closing it meant fixing the latent bug (initialise `processSample = NULL`,
   free and return NULL on an unknown type). This is the framework doing its
   job — the missing branch *was* the missing guard.
4. `meson/coverage-min.txt` — `src/filters.c 100`, so the floor is now a
   ratchet.

Verified: 24 suites pass; matrix asan + valgrind PASS; mutation still clean.

`gui_*`, `graph_gui`, `dataviz`, and the `sample` audio-device paths are out
of pure-unit scope: they need raylib/device context.

**GUI sources were invisible until I27.** The nine `gui_*.c` files were
recompiled per test from `../src/` and their `..`-prefixed object names made
lcov's geninfo drop them silently, so they read as *unmeasured* (not 0%). After
the `gui_lib` fix they appear at their true, low rates (gui_core 4%,
gui_arranger 24%, the chip files ~0%) — honest numbers to work from. See I27.

## Coverage gate

`meson/coverage-min.txt` lists a branch-coverage floor per module; it is a
**ratchet** (floors only go up). `./meson/coverage.sh --gate` fails if any
gated module drops below its floor. A module is listed at 100 only once its
suite closes every branch; until then it sits at its current level to block
regressions. `notes.c` and `wavetable.c` are at 100.

## GUI module playbook

A GUI file looks untestable and usually isn't. Split it by what it touches:
**logic needs no window; only draw callbacks do.** Three test binaries per
module, in this order.

**1. Logic, headless** — `tests/dsp/test_<module>.c`. No `InitWindow`. The two
levers that make this possible here:

- *Input without raylib.* `isKeyJustPressed(state, KM_UP)` reads
  `state->keys[i].isPressed / .wasPressed`, not raylib. Write those fields
  directly (`press_once(is, KM_UP); release(is, KM_UP);`) and every routing
  branch is reachable with no display.
- *Don't boot the app.* Build `PatternList` / `Sequencer` / `LayerStack` /
  `InputState` directly rather than through the `create*` chain, which drags in
  the voice pool, arranger and config load. When a function calls through a
  callback slot (`pl->onStepChange.f(...)`) install a **recording stub** — that
  callback is part of the contract, and asserting what it received is the test.

Link with `gui_test_libs` (`src/meson.build`), which is the app's own GUI
library — no source recompilation (see I27).

**2. Allocation failures** — `tests/dsp/test_<module>_fault.c`, linked with the
same `--wrap` set as every other fault target. Loop-until-clean over the
module's `malloc`/`realloc` sites.

**3. Draw callbacks, under Xvfb** — `tests/dsp/test_<module>_draw.c`. Register
with `find_program('xvfb-run', required: false)`:

```meson
xvfb = find_program('xvfb-run', required: false)
if xvfb.found()
  test('test_<module>_draw', xvfb,
    args: ['-a', '-s', '-screen 0 640x480x24', exe], timeout: 60)
endif
```

These assert **reachability, not appearance**: build the data so each colour /
state branch is taken, draw a frame or two, assert no crash. Pixel goldens are a
different tool and not what this suite does.

**Traps worth knowing before you start:**

| Trap | Symptom | Fix |
|---|---|---|
| Test recompiles the source (`files('../src/gui_*.c')`) | meson names it `.._src_gui_x.c.o`; geninfo discards it; the file reads *unmeasured*, gate stays green | put shared sources in a static library (I27) |
| `if (a && b)` where `b` is dead by construction | uncoverable branch; `NEVER(b);` does **not** help | restructure so the condition doesn't exist; comment why (I30) |
| A loop that `break`s on first match | only the operands along the taken path are exercised | one rebuild per case — operand enumeration happens once per run |
| Module file-scope state | tests become order-dependent; hand-ordering "call this first" is the tell | pair `create*`/`free*`; make `create*` release what it replaces (I29) |
| Branch only reachable via a config we don't ship | 98% and a temptation to fabricate one | record the exclusion, floor the module just below 100 |

## Fault injection (OOM + I/O)

`tests/support/faultinject.{h,c}` injects allocation and I/O failures via the
linker's `--wrap` mechanism — no production-code hooks. A test executable adds
`-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=fopen,--wrap=fread,--wrap=fclose,--wrap=fopen64`
and links the support file; the module's calls then route through shims that can
fail the *n*-th operation onward and track outstanding allocations.

The pattern is **loop-until-clean**: for each index, fail exactly there, run the
operation, and assert it degrades cleanly (returns NULL, leaves state untouched,
leaks nothing, leaves the target struct at a valid default).

- `tests/dsp/test_io_fault2.c` — I/O and OOM injection for the two binary
  loaders (`io/sequencer_io.c`, `io/preset_io.c`). Feeds a real saved project
  through an anonymous memfd, so nothing touches disk. Two results and one
  harness trap in I33.
- `tests/dsp/test_wavetable_fault.c` — `createWavetablePool` (3 allocations)
  and `loadWavetable` (1); this is what closes `wavetable.c` to 100% branch.
- `tests/dsp/test_io_fault.c` — the WAV loader's full failure surface:
  `fopen`/`fread` short reads (`test_wav_io_faults`) and every allocation
  (`test_wav_oom`). Feed a valid WAV from an anonymous memfd
  (`/proc/self/fd/N`) so nothing touches disk.

Because the shims only intercept calls from linked objects (not libc internals)
and are disabled before process exit, gcov profiling is unaffected.

**This file powers P4 injection only.** Mutation testing is separate
(input-byte mutation in `tests/support/mutate.{h,c}` — see I23).

## Fuzzing (P5)

`fuzz/` holds libFuzzer harnesses; `meson/fuzz.sh [target ...] [--time S]`
builds and runs them with `-fsanitize=fuzzer,address,undefined`:

- `wav` — `src/io.c:load_wav_sample` (untrusted WAV files)
- `seqfile` — `src/io/sequencer_io.c:loadSequencerState` (untrusted project/song
  files), then the loaded data through `createSequencer` / `startPlaying` /
  `advanceSequencerStep`. **The consumer chain is the point**: the loader is a
  pure deserializer, so a loader-only harness has no teeth — the out-of-bounds
  access only happens when a consumer treats a loaded value as an index. Found
  five defects (I32).
- `config` — `src/io/config_io.c` (settings / theme JSON)

Fuzz input reaches the file-opening loaders through an anonymous memfd
(`/proc/self/fd/N`), so nothing touches disk. Seeds and regression inputs live
in `fuzz/corpus/<target>/`. `meson/fuzz.sh` probes for a clang that has the
libFuzzer runtime; the Fil-C `clang` on this box does not ship compiler-rt, so
it selects `/usr/lib/llvm/21/bin/clang`.

The `wav` target immediately found four real defects (I13–I16). After fixing
them, the corpus plus 150 s of fuzzing (100k+ executions) run clean, and the
`config` target found nothing. The `seqfile` target found five more (I32) and
now runs 24.2M executions clean.

**Where a fuzzer should aim.** For deserialised input, fuzz where the data is
*used*, not where it is *read*. The loader's own code is usually a short chain of
checked `fread`s; the defects live in the consumers that treat a loaded value as
an index without re-checking it. Confirm the harness has teeth before trusting a
clean run: revert the fix it is supposed to catch and check the fuzzer finds it.

## Dynamic-analysis matrix (P6)

`meson/matrix.sh [config ...]` builds and runs the whole suite under each
configuration; it exits non-zero if any gating config fails.

| config | build | gates |
|--------|-------|-------|
| `asan` | clang `-fsanitize=address,undefined`, leaks off | yes |
| `trapv` | gcc `-ftrapv` (signed-overflow traps) | yes |
| `uchar` / `schar` | gcc `-funsigned-char` / `-fsigned-char` | yes |
| `valgrind` | plain gcc build, every test under valgrind (gates on memcheck errors; leaks reported) | yes |
| `tsan` | clang `-fsanitize=thread`, concurrency suite only | yes |
| `helgrind` | plain build, concurrency suite under `valgrind --tool=helgrind` | yes |
| `m32` | gcc `-m32` | skipped (no multilib) |

Current state: **asan / trapv / uchar / schar / valgrind / tsan / helgrind
PASS**, m32 SKIP. MSan is not viable here (needs an instrumented libc).

The matrix and valgrind found five real defects, all fixed (I20, I21): a
deref-after-free in `test_modsystem`, a signed-overflow in `voice.c` grain
positions, a `patterns[-1]` out-of-bounds in `sequencer.c`
`getStep`/`getCurrentStep`, an uninitialised-`voiceType` read in
`createVoiceManager`, and uninitialised FFT buffers read in `analysis`.

### Concurrency: tsan and helgrind (I31)

Every other rung observes *outcomes*. Coverage counts executions; ASan/UBSan/
valgrind catch single-threaded memory errors after the fact. A data race reads
as "both branches were taken, everything green" — which is precisely the bug
class that produced the production type-cycle segfault recorded in `audio.c`'s
lock comment. TSan and helgrind observe *ordering*, so they are the only rungs
that can see it.

**The race detector subset is deliberate.** Both tools report races in
uninstrumented third-party code: raylib's GL/GLX/X11 paths and PortAudio's ALSA
shims are full of shared state that is thread-affine but not TSan-annotated, and
neither tool can see inside them. `valgrind.supp` already suppresses exactly
those for memcheck (`PA_*`, `LGL_*`); the same noise under a race detector
swamps first-party findings. So `tsan`/`helgrind` run `$RACE_TESTS` only —
currently `test_audio_concurrency` — which is first-party and calls no raylib
at all (`renderAudioBuffer` touches raylib only under `g_probeTempo`, which is
off). **Any test added to `RACE_TESTS` must stay free of window and audio-device
dependencies**, or it needs its own suppression budget.

`tsan.supp` and `helgrind.supp` hold one documented false positive each (see
I31); nothing else is suppressed. TSan needs `exitcode=66` so a detected race
fails the process rather than passing with warnings on stderr.

**What a clean run does and does not mean.** The two production-shaped
scenarios — the instrument type-cycle (`setInstrumentVoiceType`, which frees and
re-mallocs the Voice structs the audio thread is iterating) and
`rebuildVoicesForInstrument` — both passed on their first TSan run, which is
real evidence that the `g_audioLock` discipline in `voice.c` is sound. But TSan
reports races it *observes*: an interleaving this two-thread loop never reaches
stays hidden. A clean run is "no evidence of a race", not "no race".

The refactor that made this possible: the callback body was `static int
patestCallback(...)` inside `main.c`, so the only way to execute it was to start
a real PortAudio stream. It is now `renderAudioBuffer()` in `audio.c`, linked
into `core_lib` (with `dataviz.c`, which it depends on for the CPU-time
measurement). `main.c` keeps a four-line adapter. `main.c` is not in the
coverage data at all, so this also brought the render path under measurement
(`audio.c`: 58% line / 35% branch as of I31).

## Using the macros

`src/verify.h` provides the SQLite-style contract macros. `SPX_DEBUG` is
derived from `NDEBUG`; coverage builds additionally define `-DSPX_COVERAGE_TEST`
(from the `b_coverage` option). The three configs:

| Config | `ALWAYS`/`NEVER` | `testcase` |
|--------|------------------|------------|
| debug (default) | assert | off |
| release (`NDEBUG`) | compile away | off |
| coverage (`-Db_coverage=true`) | compile away | live |

`ALWAYS`/`NEVER` compile away in **coverage** builds on purpose: an asserting
ternary adds an assert edge that no test can take, i.e. an uncoverable branch
at every site (see I8). So run the suite twice — under debug to enforce the
contracts, under coverage to measure. `./meson/coverage.sh` builds a debug
build with coverage instrumentation, but with the macros in their
compile-away form.

- `assert(...)` — a precondition (something the caller must guarantee) or a
  postcondition. Present in debug only.
- `ALWAYS(X)` / `NEVER(X)` — X is an *invariant* the author asserts cannot be
  false / true, so a branch on it is unreachable by design. Do **not** wrap a
  genuinely reachable bound (caller-driven limits, malloc failure) — those are
  plain `if`s.
- `testcase(X)` — a reachable-but-undesirable edge (defensive fallbacks) that a
  test has deliberately exercised. Empty unless `SPX_COVERAGE_TEST` is set;
  X must be side-effect free.

`tests/dsp/test_verify.c` proves both the debug (abort on impossible path) and
release (compile-away) semantics.

## Issues & solutions

Each entry: **symptom** → **cause** → **fix** → **takeaway**.

### I1 — Coverage script reported 0% and no per-file data

- Symptom: `.tmp_files/cov-report.sh` printed `* 0% 0 0% 0`.
- Cause: throwaway script with a broken gcov-JSON filter; it also depended on a
  stale `build-cov/coverage.info`.
- Fix: replaced with committed `meson/coverage.sh` (lcov-based).
- Takeaway: never keep test tooling in a gitignored scratch dir — it rots
  silently and nobody notices until they try to use it.

### I2 — `lcov` reported "branches: no data found"

- Symptom: summary showed lines/functions but no branches, despite a
  `--rc branch_coverage=1`.
- Cause: this box has lcov **1.0**, which uses the rc name
  `lcov_branch_coverage=1`. The lcov 2.x name `branch_coverage=1` is silently
  ignored, so branch capture was dropped with no error.
- Fix: use the 1.x rc name in `meson/coverage.sh`.
- Takeaway: coverage tooling must be version-verified; a silent zero is worse
  than a hard error, because it looks like "nothing to measure".

### I3 — `docs/` is gitignored, so specs cannot live there

- Symptom: `.gitignore` line 1 is `docs/*`.
- Cause: project convention keeps `docs/` local-only.
- Fix: committed testing documentation lives at the repo root (`TESTING.md`).
- Takeaway: confirm where committed docs go before writing plan/spec files.

### I4 — Headless suite prints ALSA/JACK errors but passes

- Symptom: `meson test` logs `Unknown PCM sysdefault`, `jack server is not
  running`, `Pa_OpenStream: Invalid number of channels`.
- Cause: `DISPLAY` unset and no ALSA/JACK device; portaudio-backed paths log and
  disable themselves.
- Status: benign for now (16/16 pass). If a device-only test is ever added it
  must be skipped when no backend is present.

### I5 — `loadWavetable` capacity check is off by one (fixed)

- Symptom: `if (wtp->tableCount > MAX_WAVETABLES)`.
- Cause: `tables` has `MAX_WAVETABLES` slots, so the guard must be `>=`; at
  `tableCount == MAX_WAVETABLES` the next store writes out of bounds.
- Fix (P3): guard is now `>=`, and a new arena guard rejects a load that would
  exceed `MAX_WTPOOL_BYTES`. `tests/dsp/test_wavetable.c` fills exactly to each
  limit and asserts the next load is refused (`test_capacity_boundary`,
  `test_arena_boundary`).
- Still unguarded at this site: `length` vs the arena is checked, but the
  `name == NULL` early return leaves the copied region untracked (minor).
- Takeaway: this is exactly the class of defect the branch-coverage +
  fault-injection passes are meant to surface.

### I8 — `ALWAYS`/`NEVER` ternaries added uncoverable branches

- Symptom: after applying `ALWAYS` to `notes.c`, its branch coverage would not
  reach 100%. `lcov` showed the assert edge (`BRDA:…,0,1,0`) never taken.
- Cause: in a debug build `ALWAYS(X)` expands to `(X)?1:(assert(0),0)`, so gcov
  instruments the assert path — a branch no test can ever take.
- Fix: gate the asserting definition on `SPX_DEBUG && !defined(SPX_COVERAGE_TEST)`,
  so coverage builds compile `ALWAYS`/`NEVER` to the bare expression. Measured
  branches then match real code. Run debug for assertions and coverage for
  measurement.
- Takeaway: test instrumentation must not itself create dead branches; that is
  why SQLite keeps `SQLITE_DEBUG` and `SQLITE_COVERAGE_TEST` separate.

### I9 — test assumed `ALWAYS` normalises to 1

- Symptom: `ASSERT_TRUE(ALWAYS(2) == 1)` failed in the coverage build.
- Cause: outside the asserting config, `ALWAYS(X)` is `(X)` — it returns X, not
  `1`. (In the asserting config it returns `1`.)
- Fix: test truthiness (`ASSERT_TRUE(ALWAYS(2))`), not a specific value.
- Takeaway: document the return contract of the macro, not just its truth.

### I10 — gcov "overwriting existing profile data with a different checksum"

- Symptom: on rebuilding after a header change, a test logged this warning for
  its own `.gcda`.
- Cause: stale `.gcda` from a prior build with different instrumentation.
- Status: benign; `lcov` handles it and the numbers were correct. A clean
  `build-cov/` removes it. Not worth automating yet.

### I11 — `WavetablePool.tableSizes` is dead

- Symptom: `tableSizes[MAX_WAVETABLES]` is declared and never read or written.
- Cause: leftover from an earlier design.
- Status: flagged, left in place (out of scope). Remove or populate when the
  pool is next touched.

### I12 — active `assert()` in the coverage build added uncoverable branches

- Symptom: even after I8, `wavetable.c` stalled at 80% branch. The remaining
  uncovered edges were the `__assert_fail` paths of lines 50–53.
- Cause: the coverage build was `buildtype=debug`, so `NDEBUG` was unset and
  plain `assert()` was active — compiling an assert edge no passing test can
  take.
- Fix: define `NDEBUG` for the coverage build too, alongside
  `SPX_COVERAGE_TEST`. Coverage now instruments only real branches; `wavetable.c`
  hits 100%. Debug builds still run the asserts.
- Takeaway: same lesson as I8, one level up — *any* assertion mechanism
  (assert, ALWAYS/NEVER) is a coverage liability when enabled. Pin coverage
  builds to the asserted-off configuration.

### I13 — WAV loader read past the decoded buffer (heap-buffer-overflow)

- Symptom: ASan aborts on the first corpus seed: `READ of size 4`,
  heap-buffer-overflow, inside `load_wav_sample`.
- Cause: a debug loop printed `data[0..9]` unconditionally, but `data` holds
  `length` floats and `length` can be < 10.
- Fix: removed the debug print loop (both the 8- and 16-bit branches). Fixed
  by `meson/fuzz.sh wav`, regression input kept in `fuzz/corpus/wav/`.

### I14 — WAV frame count wrong for non-stereo files (OOB read)

- Symptom: fuzzer's next crash, `READ of size 2` (an int16 read) in the 16-bit
  path, e.g. a header with `numChannels = 1537`.
- Cause: `length = subchunk2Size / (bitsPerSample/4)` and the per-channel inner
  loop indexed `pcm_data[i*numChannels + ch]`. That formula only happens to be
  right for stereo; for any other channel count the loop read past `pcm_data`.
- Fix: `length` is now the true interleaved frame count,
  `subchunk2Size / (channels * bytesPerSample)`, and the index uses the
  validated `channels`. Note the behaviour change: mono WAVs previously loaded
  at half length; they are now correct.

### I15 — `Sample.name` leaked on pool teardown

- Symptom: LeakSanitizer: "16 byte(s) leaked in 1 allocation(s)" after a WAV
  load.
- Cause: `loadSample` mallocs `sample->name`, but `freeSamplePool` freed only
  the `Sample` structs.
- Fix: `freeSamplePool` now frees `samples[i]->name` before the struct.
  (`Sample.data` points into the pool arena and must not be freed separately.)

### I16 — untrusted `subchunk2Size` drove a multi-gigabyte malloc

- Symptom: libFuzzer "out-of-memory (malloc(8523326992))".
- Cause: the loader sized `data` from the file's header `subchunk2Size`
  without bounding it.
- Fix: reject a data chunk larger than `MAX_SAMPLE_POOL_BYTES` before any
  allocation — nothing larger than the pool can be stored anyway. Mallocs of
  `data`/`pcm_data` are also null-checked now (loop-until-clean applies here).

### I17 — no clang with libFuzzer on PATH

- Symptom: `-fsanitize=fuzzer,address` fails to link; the `clang` on PATH is
  Fil-C 20 and ships no compiler-rt.
- Cause: Fil-C is an instrumented clang without the `libclang_rt.*` archives.
- Fix: `meson/fuzz.sh` probes candidates and picks
  `/usr/lib/llvm/21/bin/clang` (LLVM 19 also present). Override with `FUZZ_CC`.

### I18 — gcc has no sanitizer runtime on this box

- Symptom: `meson setup -Db_sanitize=address,undefined` fails the compiler
  sanity check: `cannot find libasan_preinit.o / -lasan / -lubsan`. Also,
  ccache-as-linker makes meson reject `b_sanitize` outright ("Linker ccache cc
  does not support sanitizer arguments").
- Cause: Void's gcc 14 ships no `libasan`/`libubsan`, and meson can't probe the
  sanitizer through the ccache wrapper.
- Fix: the matrix's `asan` config passes `-fsanitize=…` via `c_args`/`c_link_args`
  (bypassing the `b_sanitize` probe) and builds with clang 21, which has
  compiler-rt.
- Takeaway: prefer explicit `c_args` over `b_sanitize` when a ccache-wrapped
  compiler or a runtime-less gcc is in play.

### I19 — making valgrind a gate

- Symptom: initially 7/20 tests failed under `valgrind --leak-check=full`,
  with three unrelated-looking causes.
- Cause + fix:
  1. **Valgrind stack heuristic.** `make_env`'s `TestEnv` is ~2.3 MB on the
     stack (over valgrind's 2 MB `--max-stackframe`), so valgrind reported
     ~8 M bogus "invalid read" errors and "client switching stacks" warnings
     in `test_voice`. Not a bug: pass `--max-stackframe=8000000`.
  2. **Un-freeable app singletons.** `createApplicationState` allocated an
     `InputState` with no `freeApplicationState`/`freeInputState`, so every
     test that built one leaked. Fixed by adding both destructors (and freeing
     `as` on the inputState-failure path).
  3. **Two real uninitialised-value bugs** — see I21.
- Result: valgrind now **gates** on memcheck errors (invalid read/write, UAF,
  uninitialised value) via `--errors-for-leak-kinds=none`. The repo's
  `valgrind.supp` already carries comprehensive third-party suppressions
  (portaudio/ALSA/pipewire, gallium/GLX/glfw), so no extra signal suppression
  was needed — the "noise" was one mis-sized stack frame plus real bugs.
- Still reported, not gated: **leaks**. What remains is mostly *test-harness*
  cleanup (tests build large `TestEnv`/param structures and don't free them),
  not product leaks. Gating on leaks needs per-test teardown first (backlog).
  First-party leak coverage for the fuzzed paths comes from libFuzzer's LSan
  (which is how I15 surfaced).
- Note: valgrind slows the suite a lot, so `matrix.sh` runs it with
  `--timeout-multiplier=20` (the vizulobe GL tests otherwise time out).

### I20 — three defects found and fixed by the matrix

- **test UAF** (`test_modsystem.c`, `test_lfo_shape_param`): the test read
  `lfo->data.lfo.shape` *after* `removeMod()` freed the LFO (ASan
  heap-use-after-free, readable at +112 of a 952-byte region). Fix: capture the
  shape pointer before `removeMod`.
- **signed overflow** (`voice.c`, granular grain positions):
  `rand() * GRANULAR_BUFFER_SIZE` overflows `int` (UBSan). Fix: scale in
  floating point (`(float)rand() * …`). Behaviour note: the previous value was
  UB anyway; grain start positions are now a well-defined (larger) float.
- **negative array index** (`sequencer.c` `getStep` / `getCurrentStep`):
  `patterns[patternIndex]` with `patternIndex == -1` (the arranger's "no
  pattern" marker) → `index -1 out of bounds for type 'Pattern[255]'` (UBSan),
  which aborted `test_voice`. Fix: bounds-guard both functions (return NULL on
  out-of-range, incl. negative / `noteIndex` out of range) and null-check the
  callers (`sequencer.c` trigger loop, `test_voice.c`).

### I21 — two uninitialised-value bugs found by valgrind

- **Uninitialised `voiceType`** (`voice.c`, `createVoiceManager`): when the
  `presetBank` is empty the code zeroed only `patches[0].name`, then
  `applyInstrumentPreset` switched on the still-uninitialised
  `patches[0].voiceType` (valgrind: "conditional jump depends on uninitialised
  value" — 16 898 errors in `test_mod_voice`). Fix: `memset` the whole
  `patches[0]`; a zeroed Preset is a deterministic, silent default.
- **Uninitialised FFT buffers** (`fft.c`, `initFFT`): `tbuf`/`fbuf`/`vals`/
  `cpxvals` were `malloc`ed and then read by `processFFTData` before being
  populated, so `analysis.c`'s `sqrtf` consumed garbage (valgrind: 4096 errors
  in `test_analysis`; the test had even worked around the symptom via
  `MALLOC_PERTURB_`). Fix: `calloc` the buffers.

### I22 — pre-push hook that could never fail

- Symptom: the existing `scripts/pre-push` built and ran the tests but ended in
  `|| true`, so it exited 0 no matter what — a red suite still pushed.
- Cause: `|| true` used to "not annoy" the pusher defeats the entire purpose of
  a pre-push gate.
- Fix (P8): `meson/pre-push` runs the fast tier (`meson/check.sh --quiet`) and
  exits 1 on failure, printing how to bypass once (`git push --no-verify`).
  Install per clone: `ln -sf ../../meson/pre-push .git/hooks/pre-push`.
- Takeaway: a gate that cannot fail is documentation, not a gate. If a hook
  must tolerate an environment, it should *skip loudly*, never swallow a real
  failure.

### I23 — mutation over allocator bytes is inert (retargeted to input bytes)

- Symptom: the first mutation runner flipped a byte of every allocation and
  reported `killed: 0 survived: N` for every seed, on every target.
- Root cause (proven, not guessed): the loader's asserted values come from the
  file, and the path is `malloc(pcm_data); fread(pcm_data, …)`. The
  allocation-time flip is overwritten by `fread` before any assertion can see
  it. Instrumenting the conversion loop and the post-`fread` buffer showed the
  bytes byte-identical to the clean run across 200 seeds — the mutation never
  survived to the value under test. A second flaw compounded it: the free-time
  flip wrote to already-freed memory (useless, and a latent corruption risk).
  The earlier "the mutation reaches the data" reading was wrong because the
  verbose log printed the buffer *at allocation time*, before `fread` filled
  it.
- Fix: mutation is now **input-byte** mutation, which provably reaches the
  asserted values. `mutate.{h,c}` expose `mutate_bytes()` over the input;
  `test_mutate_input.c` sweeps every bit of a valid WAV and requires the loader
  to notice. The allocator hooks were removed from
  `tests/support/faultinject.c` (that file is injection only now).
- Result: killed 414 / 608, data-chunk survivors 0. The 194 header-field
  survivors (bytes the loader does not read) are reported but not gated.
- Takeaway: mutation testing is only meaningful when the mutation provably
  reaches an asserted value. "All mutants survive" almost always means the
  mutations miss the asserted data, not that the suite is weak — verify the
  mutation reaches the oracle before drawing any conclusion about coverage.

### I24 — sample table could be written out of bounds

- Symptom: `loadSample` (sample.c) stored `samples[sp->sampleCount]` and
  incremented without checking `sampleCount` against `samples`' capacity
  (`maxSamples` = `MAX_LOADED_SAMPLES`). The arena bound was enforced, but the
  *table* bound was not.
- Cause: the table guard was simply missing; the pool arena guard above it gave
  a false sense of bounds safety.
- Fix (P4b): refuse the load (`sampleCount >= maxSamples`) before touching the
  table, mirroring the arena check.
- Takeaway: a resource usually has *two* limits — the bytes and the slot count;
  guarding one does not guard the other.

### I25 — `--wrap=fopen` silently never fired (fopen64)

- Symptom: fault injection on the WAV loader reported `clean after 2 io ops` and
  never triggered an injected `fopen` failure — while `strace` showed the file
  being opened.
- Cause: meson builds with `_FILE_OFFSET_BITS=64`, so glibc's headers rewrite
  `fopen` to `fopen64` at the call site; the linker only has `--wrap=fopen`, so
  the real `fopen64` ran un-wrapped. `objdump` confirmed the relocation was
  `R_X86_64_PLT32 fopen64`.
- Fix: add `__wrap_fopen64` (and `-Wl,--wrap=fopen64`). Only `fopen` is remapped
  by glibc — `fread`/`fclose` have no `*64` libc symbols — so their plain
  wrappers are correct, but `__real_fread` must be defined with `#undef fread`
  first so the macro cannot rewrite it into a non-existent `__real_fread64`.
- Takeaway: verify with `objdump -dr ... | grep R_X86_64_PLT32` that the symbol
  you intend to wrap is the symbol the linker actually sees; `--wrap` fails
  silently when the compiler resolved a suffixed name.

### I33 — extending `--wrap` to a new loader: the harness allocates too

Extending the `--wrap` fault-injection harness from the WAV loader to the two
binary loaders surfaced a trap worth writing down, because it cost a segfault
before it cost a diagnosis.

`--wrap=malloc` wraps **every** allocation in the test executable, including the
test's own. So the natural shape of a loop-until-clean test — allocate a
destination, arm injection, call the function — runs the *harness* out of memory
first, gets NULL back, and then dereferences it. The crash looks like a bug in
the code under test.

The fix is structural: build the destination **once, outside the injection
window**, and reset it in place between iterations (`memset` plus restoring the
handful of pointers the loader writes). `test_io_fault2.c`'s `Dest` struct is the
pattern. This also makes the loop measure the *loader's* allocation surface
rather than the harness's — which turned out to be the interesting result.

Two findings from `test_io_fault2.c`:

- **`loadSequencerState` makes zero allocations of its own.** Loop-until-clean
  converged on the first iteration. That is a result, not a broken harness: the
  loader writes into caller-owned structs, so there is nothing for it to
  allocate. The WAV loader's loop runs four iterations because it owns its
  buffers.
- **The read sequence has a precise optional tail.** The `LABL` chip-label chunk
  is optional (older V2 files predate it), so a failed read there legitimately
  still returns `SEQ_OK`. Rather than assert "no failed read may be followed by
  success" — which is false — the test walks forward to the first index where a
  failure *is* tolerated and asserts that boundary sits past the required region.
  It lands at **read #16**, matching the 16 required reads exactly (magic, PATT
  header, count, size, notes, ARRG header, playhead_indices, enabled, selx,
  sely, loop, bpm, playing, song, channelSlots, LABL peek). `loadPresetFile` has
  no optional chunk, so it tolerates no failure at all.

### I34 — a test that patches file offsets can pass for the wrong reason

The I32 regression tests patch specific ints into a saved project file. Two of
those offsets were wrong, and **the tests passed anyway** — a mis-placed patch
exercises nothing and asserts nothing, which looks exactly like success.

Three separate mistakes, in order:

1. `song[]` was placed one int past its real offset, so the "switch to a
   nonexistent pattern" case patched a row the sequencer never read.
2. The assertion loop checked all `MAX_SEQUENCER_CHANNELS` channels, including
   ones past `enabledChannels` that `startPlaying` never initialised. A file
   legitimately naming pattern 1 of a 1-pattern project tripped it.
3. Once those were fixed, the tests still passed with the fixes reverted —
   because `startPlaying` reads `song[i][selected_y]`, not `song[i][0]`, so the
   patched row was not on the playback path at all.

**Rules this produced:**

- **Assert the patch landed.** Every patched cell is read back through the loader
  before the test relies on it. A wrong offset now fails loudly and names itself
  instead of silently testing nothing.
- **Derive offsets from named constants, not literals**, so a format change moves
  them rather than making the test lie.
- **Verify a regression test by reverting its fix** (`.tmp_files/verify_i32.sh`).

The verification script itself had two bugs worth noting, because both produced
*false passes*:

- It ignored the `ninja` exit status, so a revert producing uncompilable code
  left the previous binary in place and every case looked like a pass.
- It removed one lock at a time, but the fixes are defence in depth: the loader
  clamp *and* the consumer clamp both hold the door shut. Only removing both
  reproduces the out-of-bounds write.

Final result: all five I32 cases are caught, with the right failure mode —
removing the `enabledChannels` guards gives SEGV, removing the
`pattern_index`/`pattern_size` guard gives SIGFPE (which is how the
division-by-zero is confirmed rather than assumed).

### I32 — fuzzing the project loader: five file-driven defects

The project/song format was the only untrusted binary input with no fuzzer, and
it is where I29 lived. `fuzz/fuzz_seqfile.c` feeds `loadSequencerState` a memfd
and then drives the loaded data through `createSequencer` + `startPlaying` +
`advanceSequencerStep` — the consumers a real project reaches immediately.

**That consumer chain is what gives the harness teeth.** A loader-only harness
found nothing even with the I29 check removed, because the loader is a pure
deserializer: it stores `pattern_size` verbatim and the out-of-bounds access
happens when a *consumer* treats it as a step count. Two minutes of fuzzing to no
effect was the evidence for changing the shape. The general lesson: **when the
input is deserialised, fuzz where the data is used, not where it is read.**

Five defects, all fixed, each with a regression test verified to fail without its
fix:

1. **`createSequencer` out-of-bounds WRITE.** `enabledChannels` comes from the
   file and bounded a loop writing `sequencer->playhead_index[i]`, an array of
   `MAX_SEQUENCER_CHANNELS`.
2. **`getPatternIDfromArranger` out-of-bounds READ.** `selected_x` / `selected_y`
   indexed `song[][]`. These clamp rather than reject — they are cursors, so a
   project saved by a future build with more channels should still open.
3. **`incrementSequencer` out-of-bounds READ.** `playhead_indices[i] + 1` indexed
   `song[][row+1]`, and the loaded value became `pattern_index`, so the file
   steered playback.
4. **`incrementSequencer` out-of-bounds READ and division by zero.** The size
   lookup validated the *old* pattern index; a mid-step pattern switch reassigned
   `pattern_index` and the next line used the *new* one unchecked.
5. **`editStep` wrote `notes[patternIndex]` instead of `notes[noteIndex]`.** The
   guard bounded `noteIndex` and said nothing about `patternIndex`, so editing
   step 2 of pattern 200 wrote outside that pattern's note array. The two indices
   only coincided when they were equal, which is why it went unnoticed.

`createSequencer` also now `calloc`s instead of `malloc` (entries past
`enabledChannels` were uninitialised) and bounds its loop by the array as well as
by the file value — defence in depth, so removing the loader clamp alone is not
enough to reintroduce the write.

24.2M runs / 421 s clean after the fixes. Corpus is seeds plus three crash
artifacts, matching the existing curate-what-you-track rule.

### I36 — a destructor that cannot accept NULL is a crash under memory pressure

`freeList(List *)` dereferenced its argument immediately. `initGuiNode` calls it
on `gn->items` and `gn->itemWeights` — the two lists it had just tried to
create — from its own cleanup path when the `name` allocation fails. So creating
any GUI node during memory pressure segfaulted *inside the cleanup code*, which
is the worst place for it: the caller is already handling a failure and now has
to handle a crash instead.

Found by `test_gui_arranger_fault.c`'s first run. The fix is one line: a
destructor must tolerate NULL, exactly like `free()`. Worth stating as a rule
rather than a patch, because the same shape will recur in every `free*` in the
codebase — `if(!list) return;` at the top, no exceptions.

**The broader lesson: OOM injection finds crashes in error paths, which is
where crash-hiding is most likely.** The happy path had been exercised
millions of times. The `malloc` returning NULL branch had never run.

### I35 — gui_arranger: three accessors that assumed a graph existed

Testing the module headlessly surfaced three `agui`-deref-without-a-guard
sites, each inconsistent with its neighbours:

- `printArrGraph()` — `printGraph(agui->root, 0)`, no check.
- `gui_arranger_draw()` — `drawNode(agui->root)`, no check.
- `createArrangerGraph()` — replaced `agui` without freeing the old graph, so a
  second call orphaned ~370 KB of nodes and left every `g_chipNodes[]` entry
  pointing into freed memory.

All three fixed the same way, matching what `isInstChipNode`,
`getArrangerSelectedNode`, `expandChip` and `isChipExpanded` already did:
`if(!agui || !agui->root) return;`, plus a new `destroyArrangerGraph()` that
`createArrangerGraph` now calls before rebuilding. Valgrind went from 371 KB
leaked to zero.

The leak was found by valgrind rather than by a test, which is the usual
division of labour: an assertion would have needed to know the graph *should*
be freed, and nobody had written that down.

### I37 — a helgrind suppression must name the access site on both sides

`helgrind.supp` had one entry for the `g_volume` relaxed-atomic race, naming
only the writer (`fun:audioSetMasterVolume`). It passed the matrix, then failed
it on a later run with 24,315 errors — the same race, attributed to the
*reader* that time.

A helgrind suppression matches the frame at the **access site**, and a two-sided
race has two of them. Which side helgrind prints first depends on the
interleaving, so a one-sided entry suppresses the report only while the
threads happen to line up. It is a flaky gate wearing a suppression's clothes.

Fixed by adding an entry per side (`g_volume-relaxed-atomic-write` and
`-read`), then running the target three times to confirm stability. General
rule: **if a race has two participants, suppress two access points, and verify
a suppression by running the target more than once.**

### I40 — gui_instrument: two unindexed accessors, and a GUI with no destructor

Testing the instrument screen found three things, one of which is a design
constraint worth knowing before anyone tries to test this module again.

**Accessors indexed an attacker-writable field with no bound.** `selectedInstrument`
is a caller-owned `int*` written by the load list, the modal callbacks and the
channel-switch gestures. `getSelectedInstGraph()` and
`getSelectedInstInstrument()` both did `array[*igui->selectedInstrument]` on
arrays fixed at `MAX_SEQUENCER_CHANNELS`. Because `instrumentScreenGraphs[]` is
the *first* field of `InstrumentGui`, an out-of-range selection did not read off
the allocation — it read `instrumentCount` and `selectedInstrument`
reinterpreted as a `Graph *`, which the caller then dereferences. A wild pointer,
not an obvious crash, which is why ASan would never have caught it either.

Fixed with a shared `clampSelectedInstrument()`. Clamping rather than returning
NULL is deliberate: these are read accessors whose contract is "the selected
channel's thing", and NULL would push a guard onto every caller for a case that
is a bug in the *writer*, not the reader.

**`createInstrumentGui` had no destructor.** 184 bytes plus one node tree plus a
render-texture pair per mod strip, leaked on every rebuild. Added
`freeInstrumentGui()`; the mod-strip textures come free because
`createModStripGuiNode` installs `freeModStripGuiNode` as the node destructor.

**The VoiceManager is process-lifetime state, and the test must respect that.**
`gui_inst_mod.c`'s file-static `g_sourceCtx[]` holds `Instrument *` pointers
into the VoiceManager. Freeing it leaves a dangling global that the next
`createInstrumentGui` dereferences — ASan reported it as a heap-use-after-free
when the test freed the VM as part of its teardown. `main.c` never frees it
either, so that is the product's actual contract and the test now honours it.
The fragility is real (any future "recreate the VoiceManager" feature hits this)
but fixing it belongs with that feature, not with a test.

**The constraint that was here — now fixed.** `gui_instrument.c` used to be the
one module whose *construction* needed a GL context: `createInstGraph` →
`createModStripGuiNode` → `initModStrip` → `LoadRenderTexture`. That inverted the
usual split (node construction free, drawing needs a display) and forced this
test under Xvfb for logic that never draws. **I41** makes `ModStrip`'s render
textures lazily allocated, and this test is now headless like its siblings.

### I39 — see I40: the unclamped `selectedInstrument` accessors

(Recorded together with I40 because they were found in the same pass and are the
same shape: a caller-owned index used against a fixed array without a bound.)

### I42 — `fmaxf` where a clamp was meant, flashing a UI element near-black

`updateModStripTexture` built its feedback tint as:

```c
(Color){ fmaxf(249.0f + tht_x * 8.0f, 255.0f), ... }
```

`fmaxf(a, b)` returns the *larger* argument, so this is not a clamp in any
direction — it raises the floor to 255 and lets the top run away. The
arithmetic ranges over roughly [218, 279], so most of the cycle produced a
value above 255, which was then narrowed into a `Color`'s `unsigned char`.
That is undefined behaviour, and on x86 it wrapped: 259 became 3.

**Visible effect:** the mod strip's feedback tint was near-white for part of its
cycle and flashed near-black for the rest, instead of pulsing smoothly between
~219 and 255.

Fixed with `fminf(fmaxf(v, 0.0f), 255.0f)`. This is a *visual change*: the strip
now shimmers through a range of near-whites rather than flashing. Worth
eyeballing in the app.

Found by UBSan through `test_modstrip_lazy_draw`, on its first run. The code was
written in `59df0a1` and, so far as the history shows, never executed under a
sanitizer — because nothing called `updateModStripTexture` from any test until
now. A one-character-shaped bug that survived because its path had no test.

### I41 — a node that owns GPU state makes its whole screen untestable headlessly

`createInstGraph` reached `createModStripGuiNode` → `initModStrip`, which called
`LoadRenderTexture` and then *drew* (`BeginTextureMode`/`ClearBackground`) at
construction time. `LoadRenderTexture` dispatches through `rlgl`'s
function-pointer table, which is `NULL` before `InitWindow`, so calling
`createInstrumentGui` headless was a jump through address zero.

The split every other GUI module here gets for free — construct headless, draw
under Xvfb — did not apply to `gui_instrument.c`, because construction *was* the
draw. Its test had to `InitWindow` and run under Xvfb for logic that never draws.

**The fix** is a two-phase resource:

- `initModStrip` now only records `voicePool`/`voiceCount`/`width`/`height` and
  zeroes the textures, adding a `bool loaded` to `ModStrip`.
- `ensureModStripLoaded()` — static, called at the top of
  `updateModStripTexture` and `drawModStrip` — does the allocation and the
  black clear once. The update path is the real first toucher: `main.c` calls
  `updateModStripTextures()` at the top of every frame, before `BeginTextureMode`.
- `freeModStrip` returns early unless `loaded`. `UnloadRenderTexture`
  dereferences `rlgl` state, so unloading a zeroed texture would crash rather
  than no-op — the flag is what makes lazy construction symmetric with teardown.

The black clear had to move with it: `updateModStripTexture`'s first act is to
blit one buffer over the other, so whichever is not the clear target would show
uninitialised GPU memory on frame 1.

**Two tests, deliberately split.** `test_modstrip_lazy` is headless and asserts
construction touches no GL — if anyone makes `initModStrip` eager again it
segfaults with no display, which is the signal we want.
`test_modstrip_lazy_draw` runs under Xvfb and asserts the lazy load actually
happens, that a second update *reuses* the textures rather than reallocating
(every frame would otherwise leak a framebuffer), that a never-updated strip can
still be presented, and that teardown is idempotent.

General lesson: **a node that owns a GPU resource is not a data structure, and it
makes its whole screen un-testable headlessly.** The cheap fix is not to stub the
resource — it is to stop constructing it until something uses it.

### I48 — a "no-op" API call that silently did nothing, and one that aliased its own buffer

Two different shapes of the same mistake, both found by the same test file.

**`cbDirtyDiscard` called `markPresetLoaded(inst, NULL)`.** `markPresetLoaded`
opens with `if(!inst || !name || !name[0]) return;` — so the DISCARD button on
the dirty-confirm dialog was a complete no-op. It did not restore the edits it
exists to throw away, and it left the instrument marked dirty, which is exactly
the state the dialog offers an exit from.

Fixed to `applyInstrumentPreset(inst, inst->loaded.snapshot)` followed by
`markPresetLoaded(inst, inst->loaded.name)`. Both halves matter: restoring the
snapshot *and* re-baselining. Marking it clean without restoring would just
relabel the user's edits as the new baseline.

**That fix then aliased a buffer.** `markPresetLoaded(inst, inst->loaded.name)`
is the natural way to re-baseline without changing the name — and it makes
`strncpy(inst->loaded.name, name, ...)` overlap itself, which is undefined
behaviour. ASan reported it as `strncpy-param-overlap`.

Fixed in the callee rather than the caller: a one-line local copy at the top of
`markPresetLoaded`. The call that triggered it is the obvious one to write, so
the API should accept it.

The general lesson is the NULL one: **a defensive early-return in a helper turns
a caller's mistake into silence.** `markPresetLoaded` cannot report that it did
nothing, so the caller has no way to know. Where a call is load-bearing, either
make the failure visible or assert it at the call site.

### I47 — the dirty dialog claimed the legacy modal state and never gave it back

`guiBuildDirtyConfirmLayer` sets `g_modalState = MODAL_CONFIRM_OVERWRITE` with a
comment about keeping a legacy field in sync. Two of its three callbacks then
released the *layer* without releasing the *state*:

- `cbDirtyCancel` popped the layer, leaving `g_modalState` set.
- `cbDirtyDiscard` did the same (before I46 fixed it).

With no dialog on screen but the flag still set, `handlePresetUiInput` falls
into the legacy overwrite handler: it swallows every keypress, and `START`
overwrites the user's preset. A phantom modal.

Fixed in both callbacks. The underlying design smell is that a layer and a
file-static flag are two representations of "a modal is open", and nothing
enforces that they are changed together.

### I44 — a non-passive layer made the overwrite dialog's keyboard handler unreachable

`handlePresetUiInput` routes input to the topmost non-passive overlay layer
*before* it reaches the `g_modalState == MODAL_CONFIRM_OVERWRITE` block that
implements the dialog's keyboard contract (LEFT/RIGHT toggle YES/NO, START
commits, SELECT escapes). `guiSetOverwritePending` always pushes a non-passive
layer, so the keyboard block was **dead code**. The dialog could only be
dismissed with the mouse.

Fixed by letting the legacy handler run for that dialog and gating on the
layer's *name*:

```c
bool overwriteLayerUp = topIdx >= 0
                        && ig->overlayLayers.layers[topIdx].name
                        && strcmp(ig->overlayLayers.layers[topIdx].name,
                                  "OVERWRITE") == 0;
if(topIdx >= 0 && !overwriteLayerUp) { /* layer routing */ }
```

Gating on `g_modalState` instead looks equivalent and is not: the dirty dialog
sets the same flag (I47), so every keystroke in the dirty dialog would be routed
to the *overwrite* handler, where START overwrites the preset instead of saving
or discarding. Identity, not a shared flag.

Also: START and SELECT now pop the layer as well as clearing the flag. Clearing
only the flag left the layer up and the modal still swallowing input.

### I46 — see I48: DISCARD was a no-op.

### I45 — the dirty bit is inert without a loaded name

`isInstrumentDirty` returns false whenever `inst->loaded.name` is empty, on the
sound reasoning that an instrument with no on-disk baseline has no unsaved edits
to lose. Setting `loaded.dirty = true` on a fresh instrument therefore does
nothing, and every assertion about the dirty path passes vacuously.

It bit three ways in one test file: `initDefaultFmPreset` leaves the preset name
empty (so the bank lookup found nothing and `guiSavePreset` never returned
`PRESET_EXISTS`), the dirty bit never latched, and the dirty dialog never opened
— which a conditional `if(modal) {...}` block then skipped silently.

The rule that actually fixed it: **make the precondition part of the test.**
`make_dirty()` now refuses an empty name, and each call site asserts
`isInstrumentDirty()` afterwards. And never wrap the body of a test in
`if(thing_might_be_up)`: assert that the thing came up, then test it.

### I49 — one array, two index spaces, and an uninitialised tail

`inst->envelopes[]` is a legacy mirror of the envelope mods (the code says so
itself: "Task 4 will remove the mirror entirely"). Three separate bugs live in
how it is maintained, all found by the same first test run.

**1. `envelopeCount` counted the wrong thing.** `addRuntimeSource` and
`removeSource` both set it to `modSourceCount(inst->modList)`, which counts
every non-atten mod — *including the LFO*, which has no mirror slot. On a
default instrument that made `envelopeCount` exceed the number of populated
slots by one. Any loop over `envelopeCount` then walked a slot that was never
written — which is exactly how the new test segfaulted, the first time it ran
under `MALLOC_PERTURB_`.

The invariant it should have is `envelopeCount == number of non-NULL entries in
envelopes[]`, so all three sites now increment / recount instead.

**2. The mirror was cleared by an index from the wrong space.**
`removeSource` cleared `envelopes[mi]` where `mi = modIndexAt(modList, srcIndex)`
is a *modList* position, while `addRuntimeSource` writes
`envelopes[envelopeCount]`, a *source* count. They diverge as soon as anything
is appended: a runtime source lands at modList index 11 but is mirrored at
`envelopes[3]`, so the `if(mi < MAX_ENVELOPES)` guard skipped the clear entirely
and `envelopes[3]` kept pointing at freed memory. ASan: heap-use-after-free via
`rebuildInstrumentGraph`.

Fixed without trying to reconcile the spaces — scan the array for the pointer
being removed and NULL that. Works however the array was populated, which
matters because this is code on its way out.

**3. The array was never zeroed.** `init_instrument` uses `malloc` and only
writes slots `0..envelopeCount-1`, leaving the tail as whatever malloc returned.
A scan of the whole array reads uninitialised bytes. That is bug 1's crash
surfacing again: with the count fixed, the recount loop still walked
`envelopes[5]` and found garbage where NULL was expected.

Fixed at the source — `memset` both mirror arrays in `init_instrument`. This is
the one worth generalising: **a partially-initialised array with a separate
count needs both halves defended.** Zero the tail, *and* make the count mean
"entries actually written". Doing only one leaves a scan that finds
`0xd1d1d1d1d1d1d1` and counts it as populated.

### I50 — `MALLOC_PERTURB_` is a test, not just a meson detail

`test_gui_inst_mod` passed when run directly and segfaulted under
`meson test`. The difference was one environment variable in meson's default
test wrapper: `MALLOC_PERTURB_`, which makes glibc fill freshly-allocated and
freed memory with a byte pattern.

Without it, an uninitialised read usually lands on a zeroed page and looks
correct. With it, the same read is garbage immediately. It is the cheapest
uninitialised-memory detector available — no sanitizer build, no second
compiler, no slower test — and meson already sets it for every test by default,
so the whole suite already has it.

Worth knowing because the failure looks like flakiness: "works from the shell,
crashes under the runner". When a test does that, diff the wrapper's
environment before suspecting the code.

### I51 — a callback whose parameter type is not in its own name

`cbOpenRouteLayer` and `cbOpenClearAllLayer` both take `void *ctx` and cast it
to **`SourceCtx *`** — a module-private struct holding an `Instrument *` and a
source position. Neither is exported.

Passing an `Instrument *` is the obvious mistake, and it fails *silently*:
`resolveSourceCtx` writes through the pointer, so the "instrument" gets
overwritten with the displayed instrument while its second field is read as a
source index. The picker then refuses to open, and the caller sees nothing at
all — no crash, no error, just a function that does not work.

**A `void *` context that is really a two-field struct is an undocumented
contract.** When a module's callbacks are the intended entry points for tests or
a harness, the context type belongs in the header next to the function, or the
function takes the struct directly. Both of these already take `void *` because
they are `ActionCallback`s, which is a real constraint — so the fix is at least
to name the expected type in the comment *and* in the test, which is what
`test_gui_inst_mod_picker.c` now does with a mirrored `SourceCtx` (same
convention `test_graph_nav` uses for private structs).

### I52 — `probePickFirstRoute` is not a query, and a harness hook reads like one

`probePickFirstRoute` is named and shaped like "probe for a route", but it is a
*harness hook that creates* routes: it picks the last real source and wires it
into up to `PROBE_ROUTE_COUNT` unmodulated parameters so the gradient overlay
has something to draw.

A test that asserted the intuitive contract — "returns 0 when there is nothing
to pick" — therefore failed. The fix was to read the function rather than its
name.

Worth recording because it is a trap for whoever tests this module next, and
because the general shape is common: **harness hooks that seed state read like
queries about it.** If they must not be mistaken for one, say so in the comment
*and* give them a verb (`seedPickRoutes`, not `probePickFirstRoute`) — the name
is the part that misleads.

Also worth noting while testing it: a successful probe legitimately grows
`paramList`, because `addModulation` creates a per-connection attenuator
parameter (the `MT_ATTEN` entry that interleaves the modList). An assertion that
the parameter count is stable across routing is wrong.

### I53 — a gate that passed for the wrong reason

The seqfile fuzz smoke linked against `build/src/libspectrax_{core,seq,gui,vizfx}.a`
and `build/libspectrax_cjson.a`. Nothing in `check.sh --full` ever configures or
compiles plain `build/`: the full tier builds `build-cov` for coverage and the
`build-matrix-*` dirs for the sanitizer matrix, but never the default build
directory.

So the target only worked because a hand-built `build/` happened to be lying
around in the working tree. On a clean checkout it fails with
`clang: error: no such file or directory`.

**This was found by integrating, not by testing.** The branch was fast-forwarded
into `main`, verified in a fresh worktree of `main`, and the seqfile smoke failed
there — while passing, every single time, in the lane where it was developed.
That is the specific failure mode this whole framework is supposed to catch, and
it got away because "the suite is green" is only ever a statement about the tree
it ran on.

Fixed by having `build_seqfile` ensure the tree it needs exists
(`ensure_libs()`: configure `build/` if there is no `build.ninja`, then compile
it). A gate that depends on undeclared ambient state is not a gate.

Worth generalising: **any step that consumes build output must create it, or be
reached by something that does.** "I had it built already" is invisible in
review and invisible in CI, which is exactly where it costs you.

### I31 — a race is the one bug class no other rung here can see

- Symptom: none. Coverage read green, ASan/UBSan/valgrind read green, and the
  type-cycle use-after-free that `audio.c`'s lock comment describes was fixed by
  adding a mutex with **nothing that could regress it**. A race reads as "both
  branches were taken" to every coverage-based tool.
- Two independent gaps:
  1. *Untestable code.* The whole PortAudio callback body was `static int
     patestCallback(...)` in `main.c`; the only way to execute it was to open a
     real audio device. Fixed by extracting `renderAudioBuffer()` into
     `audio.c`, in `core_lib` (plus `dataviz.c`, which it calls for the CPU-time
     measurement), leaving a four-line adapter in `main.c`. `main.c` is not in
     the coverage data, so the render path is now measured at all.
  2. *No race detector in the matrix.* Added `tsan` (clang
     `-fsanitize=thread`) and `helgrind` (`valgrind --tool=helgrind`), both
     gated, both scoped to `$RACE_TESTS` because uninstrumented third-party
     noise otherwise swamps first-party findings.
- TSan found one real race on its first run: `g_volume`, the master volume.
  `renderAudioBuffer` read it once per stereo frame under `g_audioLock`, but
  `audioSetMasterVolume` wrote it with no lock — the lock protects a variable
  only if the writer takes it. Latent at the time (the sole writer was argv,
  which runs before `Pa_StartStream`), but the setter had just become public
  API, so the obvious next caller — a volume keybind — would have inherited it.
  Fixed with a relaxed `_Atomic float`: the aligned 4-byte store/load is exactly
  what the guarantee needs on x86, so it costs nothing on the hot path, whereas
  locking for a per-frame volume read would add a mutex round-trip to the audio
  loop for no benefit.
- Helgrind reported the same thing and that is a **false positive worth
  documenting**: helgrind models pthread primitives only and does not understand
  C11 `_Atomic`, so a relaxed atomic (a plain aligned `movss` in the disassembly)
  looks like an unprotected shared write. Suppressed by naming the racing frame
  pair in `helgrind.supp`, with the reason inline. The lesson is that TSan and
  helgrind are not redundant: they model different things, so the same code can
  be clean under one and flagged under the other.
- The two production-shaped races (type-cycle, voice-pool rebuild) came back
  clean, which is genuine evidence the existing lock discipline in `voice.c` is
  right. It is not proof: TSan reports interleavings it observes, and an
  unexercised one stays invisible.

Takeaway: coverage-based evidence is silent about concurrency. If a subsystem
has two threads, an ordering tool is not an optional extra — it is the only
rung that can fail. And a mutex around a variable protects it only if the
writer takes the mutex too.

### I30 — `NEVER(x);` as a bare statement asserts nothing and still costs a branch

- Symptom: using `NEVER(...)` to mark a dead condition left an uncoverable
  branch in the coverage data *and* did nothing at runtime — two failures at once.
- Why: in a coverage build both contract macros expand to the bare expression
  `(X)` (see `verify.h` — they are compiled out so their assert edge cannot
  pollute coverage). As a standalone statement `NEVER(next < 0);` is therefore
  just a discarded comparison: no assertion, no warning, and the branch is still
  there.
- Rule: use `ALWAYS`/`NEVER` only *inside* a larger expression, where the macro
  removes an assert edge from a condition that is genuinely part of the logic —
  e.g. `if (NEVER(head == NULL)) return;`. For "dead by construction", prefer to
  restructure so the condition does not exist: in `rebuildPatternGraph` a
  three-operand guard (`!patternPl || !patternSeq || !patternGraph`) became a
  one-operand guard on `patternGraph` because the first two are assigned
  together with the third — with the reasoning left in a comment. The same
  applies to `gui_pattern_draw`, where `if (g && g->root)` became `if (g)`
  because `createGraph` always installs a root.
- Corollary: `(void)ALWAYS(x);` is the other trap — it silences the unused-value
  warning without changing the semantics. Neither form is a contract.

### I29 — gui_pattern.c: unvalidated pattern_size from file → OOB write/read

Testing `gui_pattern.c` surfaced a file-driven memory-safety bug with a
three-layer fix. Full detail in the worked-module section above; the essentials:

- `io/sequencer_io.c` reads `pattern_size` straight from a project file with
  `fread` and no range check, while `pat_count` right above it *is* validated.
- Every consumer treats `pattern_size` as a step count into a fixed
  `MAX_SEQUENCE_LENGTH` (24) array: `Pattern.notes`, `gui_pattern.c`'s
  `stepData[]`/`stepNodes[]`, `selectStep`'s clamp, the sequencer's playhead
  modulo. So a corrupt file causes out-of-bounds writes at graph-build time and
  an out-of-bounds read of a garbage `GuiNode *` in `navigatePatternGraph`.
- Fixed at the loader (reject), at `addPattern` (clamp — the chokepoint, whose
  copy loop was itself an OOB write), and in the GUI (bound the size it indexes
  and the value it stores).

Takeaway: **"a resource has two limits — the count and the capacity."** `pat_count`
was validated against `MAX_PATTERNS`; `pattern_size` was validated against
nothing. Validating one field of a file record is not validating the record.
Two adjacent unchecked-alloc sites (`createSongMinimapGui`) and two NULL-deref
sites (`gui_pattern_draw`, `drawStepGuiNode`) came out of the same pass.

Also worth recording as a methodology note: the module had **no destructor**, so
`createPatternGraph` called twice orphaned the first node tree (valgrind: 12 leak
records, one 210 KB). Adding the paired `destroyPatternGraph()` and having
`createPatternGraph` free a previous graph first removed the whole class. When a
`create*` replaces module state, it should release what it replaces.

### I28 — four memory bugs in gui_layer.c found while testing it

Writing `test_gui_layer` and running it under valgrind surfaced four real
defects in `gui_layer.c`, all fixed:

1. **Invalid free in `destroyLayerStack`.** `pushLayer` stores a Layer *value*
   into the `stack->layers` array, but `destroyLayerStack` called
   `destroyLayer(&stack->layers[i])`, whose `free(layer)` got an interior
   pointer into the array — `free(): invalid pointer`, an immediate abort.
   Fix: split out `destroyLayerContents()` (frees the owned graph + name) and
   have `destroyLayerStack` free contents in place, not the interior pointer.
2. **Leak on every modal dismissal (17 call sites).** `popLayer` /
   `popTopNonPassiveLayer` return a heap copy the caller owns, but every call
   site in `gui_instrument.c` / `gui_inst_mod.c` discarded it — leaking the
   Layer, its `strdup`'d name, and (for owned layers) the whole Graph. Fix: add
   `popLayerFree` / `popTopNonPassiveLayerFree` (pop + `destroyLayer` the copy)
   and route the 17 discard sites to them; the internal SELECT handler now calls
   `destroyLayer` instead of `free`.
3. **Unchecked `malloc` in `createLayer` / `popLayer` /
   `popTopNonPassiveLayer`.** All three dereferenced the allocation without a
   null check; `createLayer` crashed outright under OOM (caught by
   `test_gui_layer_fault`). Fix: null-check and return NULL.
4. **Dead `!l` guard.** `layerStackInputLayer` checked `!l || !l->graph`, but
   `l = &stack->layers[index]` after an index bounds-check can never be NULL;
   the dead half was an uncoverable branch. Fix: `ALWAYS(l != NULL)` and keep
   the real `!l->graph` guard. (Same class as I8/I12 — an unreachable branch on
   an invariant.)

Also noted: `fi_live()` (the fault shim's live-allocation counter) is skewed by
`strdup`, whose internal malloc is libc-internal and bypasses `--wrap` while its
`free` does not — so valgrind, not the counter, is the leak authority for code
that uses `strdup`.

Takeaway: the GUI "logic" a first pass dismisses as untestable (a layer stack!)
hid four memory bugs including an abort-on-teardown. Testing it needed no window
for the logic and one Xvfb test for the draw call.

### I27 — GUI sources carried zero measured coverage (invisible, not uncovered)

- Symptom: `ctol_status` reported "34 first-party files" and a passing gate, but
  `src/gui_core.c`, `gui_pattern.c`, `gui_arranger.c`, `gui_instrument.c`,
  `gui_layer.c` … appeared nowhere in the coverage data — not at 0%, simply
  absent. Whole modules were unmeasured while the gate read green.
- Root cause: the GUI sources were recompiled per test from `../src/gui_*.c`
  (`tests/meson.build`), so meson named the objects `.._src_gui_core.c.o`. At
  capture time lcov's `geninfo` runs `gcov <file> -b -x -i`, which writes
  `.._src_gui_core.c##<hash>.gcov.json.gz` — a filename starting with `..`.
  geninfo then treats that leading-dot file as hidden and discards the data:
  `WARNING: GCOV did not produce any data`. Proven by copying the identical
  gcda/gcno to `xgui_core.c.*` (no leading dots) — geninfo then emits
  `SF:.../src/gui_core.c` correctly. The `core_lib` copies of the same sources
  were never executed (no `.gcda`), so nothing else supplied the data.
- Fix: hoist the nine GUI sources into their own `gui_lib` static library
  (`src/meson.build`), which the app and the GUI tests both link, replacing the
  per-test `files('../src/gui_*.c')` recompilation. One compilation, normal
  object names, coverage lcov can read. First-party file count went 34 → 43.
- Takeaway: a coverage gate can pass while whole files are *unmeasured*; the
  difference between "0% covered" and "not instrumented" is invisible unless
  something reconciles the compiled-source list against the capture. (CTOL
  improvement noted: flag compiled-but-absent sources explicitly.)

### I26 — `createBiquadFilter` had no `default` case

- Symptom: `filters.c` stalled at 95.5% branch; the one uncovered branch was
  the switch's implicit default in `createBiquadFilter` (line 13).
- Cause: the `switch(type)` handled the four `BiquadType`s and had no `default`,
  so an out-of-range type fell through with `bf->processSample` never assigned —
  an uninitialised function pointer that a caller could invoke.
- Fix: initialise `bf->processSample = NULL` before the switch and add a
  `default:` that frees and returns NULL. The test
  (`test_create_biquad_invalid_type`) drives `(BiquadType)99` through it.
- Takeaway: an uncovered default branch on an enum switch is usually a missing
  guard, not an unreachable edge. Chasing 100% surfaced a real latent bug.

- Symptom: `(void)ALWAYS(note >= 0);` compiles; `ALWAYS(note >= 0);` warns
  (release expansion is just `(note >= 0);`, a no-effect statement).
- Cause: the macros are expressions; a discarded expression statement warns
  under `-Wall` (meson `warning_level=1`).
- Fix: discard explicitly with `(void)ALWAYS(...)`, or use it in a condition.
- Takeaway: treat contract macros as expressions, not statements.

### I7 — meson test already sets sanitizer/alloc env vars

- Symptom: test command lines carry `ASAN_OPTIONS`, `UBSAN_OPTIONS`,
  `MSAN_OPTIONS`, and `MALLOC_PERTURB_=106` even in a non-sanitized build.
- Cause: meson's default test environment.
- Note: `MALLOC_PERTURB_` is a free memory-safety aid — freed blocks are
  poisoned, so use-after-free tends to misbehave loudly. Relevant to P4/P6;
  no action needed.
