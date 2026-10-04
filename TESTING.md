# Spectrax testing stack

Piloting the SQLite / DO-178B-style testing discipline on Spectrax, on the
`testing-stack` branch (worktree `.slim/worktrees/testing-stack`).

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
after the full suite (**21 suites**) passes. Numbers are lower than in P1 because
the coverage build now defines `NDEBUG` (see I12), so `assert()` sites are not
instrumented:

```
lines 69.6% (3785/5436)   functions 73.9%   branches 55.7% (1731/3110)
```

Pilot modules (pure logic, worst-covered first). Rates move as the ratchet in
`meson/coverage-min.txt` closes each module:

```
notes.c 100%  wavetable.c 100%  filters.c 100%   dstruct.c 25%   vizfx.c 26%
io.c 25%      blit_synth.c 25%  sample.c 32%     sequencer.c 48% fft.c 51%
voice.c 54%   modsystem.c 61%
```

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

## Coverage gate

`meson/coverage-min.txt` lists a branch-coverage floor per module; it is a
**ratchet** (floors only go up). `./meson/coverage.sh --gate` fails if any
gated module drops below its floor. A module is listed at 100 only once its
suite closes every branch; until then it sits at its current level to block
regressions. `notes.c` and `wavetable.c` are at 100.

## Fault injection (OOM + I/O)

`tests/support/faultinject.{h,c}` injects allocation and I/O failures via the
linker's `--wrap` mechanism — no production-code hooks. A test executable adds
`-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=fopen,--wrap=fread,--wrap=fclose,--wrap=fopen64`
and links the support file; the module's calls then route through shims that can
fail the *n*-th operation onward and track outstanding allocations.

The pattern is **loop-until-clean**: for each index, fail exactly there, run the
operation, and assert it degrades cleanly (returns NULL, leaves state untouched,
leaks nothing, leaves the target struct at a valid default).

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
- `config` — `src/io/config_io.c` (settings / theme JSON)

Fuzz input reaches the file-opening loaders through an anonymous memfd
(`/proc/self/fd/N`), so nothing touches disk. Seeds and regression inputs live
in `fuzz/corpus/<target>/`. `meson/fuzz.sh` probes for a clang that has the
libFuzzer runtime; the Fil-C `clang` on this box does not ship compiler-rt, so
it selects `/usr/lib/llvm/21/bin/clang`.

The `wav` target immediately found four real defects (I13–I16). After fixing
them, the corpus plus 150 s of fuzzing (100k+ executions) run clean, and the
`config` target found nothing.

## Dynamic-analysis matrix (P6)

`meson/matrix.sh [config ...]` builds and runs the whole suite under each
configuration; it exits non-zero if any gating config fails.

| config | build | gates |
|--------|-------|-------|
| `asan` | clang `-fsanitize=address,undefined`, leaks off | yes |
| `trapv` | gcc `-ftrapv` (signed-overflow traps) | yes |
| `uchar` / `schar` | gcc `-funsigned-char` / `-fsigned-char` | yes |
| `valgrind` | plain gcc build, every test under valgrind (gates on memcheck errors; leaks reported) | yes |
| `m32` | gcc `-m32` | skipped (no multilib) |

Current state: **asan / trapv / uchar / schar / valgrind PASS**, m32 SKIP.
MSan is not viable here (needs an instrumented libc).

The matrix and valgrind found five real defects, all fixed (I20, I21): a
deref-after-free in `test_modsystem`, a signed-overflow in `voice.c` grain
positions, a `patterns[-1]` out-of-bounds in `sequencer.c`
`getStep`/`getCurrentStep`, an uninitialised-`voiceType` read in
`createVoiceManager`, and uninitialised FFT buffers read in `analysis`.

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
