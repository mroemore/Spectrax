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
| P4 | Anomaly / fault injection (OOM, I/O fail-after-N) | done (OOM; I/O injection pending) |
| P5 | Fuzzing (config/JSON, wavetable) + seed corpus | in progress |
| P6 | Dynamic-analysis matrix (ASan/UBSan/valgrind/`-ftrapv`) | todo |
| P7 | Mutation testing | todo |
| P8 | Process (regression-per-bug, release checklist) | todo |

## Coverage baseline

First-party sources only (system, vendored, and test code excluded), captured
after the full suite (20 suites) passes. Numbers are lower than in P1 because
the coverage build now defines `NDEBUG` (see I12), so `assert()` sites are not
instrumented:

```
lines 69.6% (3785/5436)   functions 73.9%   branches 55.7% (1731/3110)
```

Pilot modules (pure logic, worst-covered first). Rates move as the ratchet in
`meson/coverage-min.txt` closes each module:

```
notes.c 100%  wavetable.c 100%  filters.c 27%   dstruct.c 25%   vizfx.c 26%
io.c 25%      blit_synth.c 25%  sample.c 32%    sequencer.c 48% fft.c 51%
voice.c 54%   modsystem.c 61%
```

`gui_*`, `graph_gui`, `dataviz`, and the `sample` audio-device paths are out
of pure-unit scope: they need raylib/device context.

## Coverage gate

`meson/coverage-min.txt` lists a branch-coverage floor per module; it is a
**ratchet** (floors only go up). `./meson/coverage.sh --gate` fails if any
gated module drops below its floor. A module is listed at 100 only once its
suite closes every branch; until then it sits at its current level to block
regressions. `notes.c` and `wavetable.c` are at 100.

## Fault injection (OOM)

`tests/support/faultinject.{h,c}` injects allocation failures via the linker's
`--wrap` mechanism — no production-code hooks. A test executable adds
`-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free` and links the
support file; the module's allocator calls then route through shims that can
fail the *n*-th allocation onward and track outstanding allocations.

The pattern is **loop-until-clean**: for each allocation index, fail exactly
there, run the operation, and assert it degrades cleanly (returns NULL, leaves
state untouched, leaks nothing). `tests/dsp/test_wavetable_fault.c` does this
for `createWavetablePool` (3 allocations) and `loadWavetable` (1), which is
what closes `wavetable.c` to 100% branch. The same harness is the plan for the
`io.c` `fopen`/`fread`/`malloc` fail-after-N work.

Because the shims only intercept calls from linked objects (not libc internals)
and are disabled before process exit, gcov profiling is unaffected.

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

### I6 — `ALWAYS(x);` as a bare statement breaks `-Wunused-value`

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
