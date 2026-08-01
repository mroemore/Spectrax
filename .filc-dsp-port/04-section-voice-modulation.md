# Section 4 — Voice & Modulation

**Status:** ✅ **complete** — 53 new tests added via 2 parallel subagents. All pass.

## Outcome

`src/voice.c` and `src/modsystem.c` build clean under fil-c and are
covered by 53 new tests. **~17 pre-existing bugs surfaced** (most in
modsystem.c). The single most important finding:

### 🚨 SECURITY-CRITICAL: double-free in `freeVoice`

`src/voice.c:50-75` — `freeVoice` double-frees envelope data and parameter
structs. fil-c's runtime caught this **live** during testing:

```
filc safety error: cannot read pointer to free object.
    src/modsystem.c:769 freeParamList   ← src/voice.c:52 freeVoice
```

The test had to be fork-isolated (`fork` + `waitpid` + `WIFSIGNALED`) so
the parent could observe the child's SIGTRAP and report the bug without
the test runner itself dying. This is exactly the class of bug fil-c is
designed to catch, and we caught it on the first real test.

## Subagent dispatch (parallel)

2 subagents in parallel:

| Subagent | Tests added | PASS | FAIL | Bugs found |
|----------|-------------|------|------|------------|
| modsystem | 42 | 42 | 0 | ~12 |
| voice | 11 | 11 | 0 | ~5 (incl. double-free) |

Wall-clock: ~5 min for both in parallel.

## Files added

| File | Tests |
|------|-------|
| `tests/dsp/test_modsystem.c` | 42 (~1200 LOC, largest test file) |
| `tests/dsp/test_voice.c` | 11 (~430 LOC) |

## Files modified

| File | Change |
|------|--------|
| `Makefile` | Added `test_modsystem` and `test_voice` to `FILC_TESTS`. `test_modsystem` links `src/modsystem.o`, `src/wavetable.o`, `src/dstruct.o`. `test_voice` links the full chain (voice → modsystem → oscillator → wavetable → dstruct → blit_synth → filters → fft → notes → sample + kissfft). |

## Build & test results

```sh
$ make filc-clean && make filc-test
# All 10 test binaries built. Pre-existing warnings from src/sample.c:76
# (printf format mismatch — not introduced by us).

$ for t in test_wav_writer test_notes test_fft test_oscillator test_wavetable \
           test_blit_synth test_distortion test_filters \
           test_modsystem test_voice; do ./bin/$t; done

GRAND TOTAL: 125 pass, 0 fail
```

## Pre-existing bugs found

### 🚨 CRITICAL: double-free in `freeVoice` (voice.c)

**File:** `src/voice.c:50-75`

Sequence of the bug:
1. `freeVoice` calls `freeModList(v->env.baseMod)` which frees
   `env.baseMod->output` (the param).
2. `freeVoice` then calls `freeParamList(v->params)` which tries to free
   that same param AGAIN (it's still in the list).
3. `freeVoice` then calls `freeEnvelope(v->env)` which tries to free the
   envelope struct AGAIN (already freed by freeModList).
4. Also: `freeParameter(v->volume)` after `freeParamList` already freed it.

**Fil-c runtime caught it:**
```
filc safety error: cannot read pointer to free object.
    src/modsystem.c:769 freeParamList
    src/voice.c:52 freeVoice
```

**Severity:** CRITICAL — exploitable use-after-free → arbitrary write
primitive. Anyone who triggers a voice release in the running app gets
a panic; under a non-fil-c build, this is silent heap corruption.

**Pinned by test:** `test_free_voice_manager_crashes` (fork-isolated so
the test runner survives).

**Fix sketch:** Drop the duplicate frees. The pattern should be:
1. `freeModList` → free all mods and their internal params
2. Remove the mod-owned params from `paramList` first
3. `freeParamList` → free remaining params
4. `freeEnvelope` (just the struct)

### modsystem.c bugs (12)

| Bug | File | Issue | Severity |
|-----|------|-------|----------|
| BUG-MOD-1 | `modsystem.c:456` | `initMod()` ignores `generate` arg, always installs `generateEnvelope`. LFO/Random mods get wrong generator → UB | high |
| BUG-MOD-2 | `modsystem.c:173` | `setParameterMaxValue()` check inverted: raises ignored, lowering below min accepted | medium |
| BUG-MOD-3 | `modsystem.c:188, 673` | `createConnection()` ignores `amount` (always 1.0); `processModulations()` never reads `conn->amount` | high |
| BUG-MOD-4 | `modsystem.c:455` | mod output param range [0,1] clamps negative swings → sine/square/ramp/random all unipolar | high |
| BUG-MOD-5 | `modsystem.c:148` | callback change-detection `fabs(fabs(old)-fabs(clamped))` misses sign-crossings | medium |
| BUG-MOD-6 | `modsystem.c:445` | `abs()` on float truncates \|rel\| in (1,2) → wrong fine increment | low |
| BUG-MOD-7 | `modsystem.c:385` | envelope `loop` field never honored | medium |
| BUG-MOD-8 | `modsystem.c:493` | `addEnvelopeStage` duration ≤ 0 clamps to 0.001 (can't have 0-duration stage) | low |
| BUG-MOD-9 | `modsystem.c:65, 80` | `clearParamList`/`clearModList` reset count without freeing → leak | medium |
| BUG-MOD-10 | `modsystem.c:403` | stage-completing sample reads `wt->data[index0+1]` past 1024-entry table → OOB (lands in pool buffer, silent corruption) | medium |
| BUG-MOD-11 | `modsystem.h:237-238` | `incrementConnectionType`/`decrementConnectionType` declared, no implementation → link error if called | low |
| BUG-MOD-12 | `modsystem.c:668-671` | `initRandFromPreset`/`saveRandPreset` are empty no-ops | low |

### voice.c bugs (5)

| Bug | File | Issue | Severity |
|-----|------|-------|----------|
| 🚨 BUG-VC-1 | `voice.c:50-75` | **CRITICAL double-free in freeVoice** (see above) | critical |
| BUG-VC-2 | `voice.c:94-113` | `generateBlep` never advances `leftPhase` → BLEP voice outputs constant (silent) | high |
| BUG-VC-3 | `voice.c:186-190` | `getFreeVoice` returns LAST free voice (index overwritten, no `break`) | medium |
| BUG-VC-4 | `voice.c:544-548` | `createGranularProcessor` seeds `grainReadPos` with `rand() * GRANULAR_BUFFER_SIZE / 4.0` → signed-int overflow → positions far outside sample; `granularProcess` wraps only once → OOB read | high |
| BUG-VC-5 | `voice.c:63-68` | `freeVoice` SAMPLE case calls `freeSample` on a pool-arena-owned sample (invalid free) | medium |

### Architectural finding: Voice struct is NOT opaque

The `Voice` typedef uses `struct Voice Voice;` (forward decl) but the
struct is fully defined in voice.h. The forward-decl trick suggests the
author intended opacity. Tests work either way but document this.

## Fil-c gotchas hit

1. **Double-free caught at runtime** — fil-c's pointer capability
   tracking identified the UAF on the first attempt. The test was
   rewired to use `fork` + `waitpid` + `WIFSIGNALED` so the runner
   itself didn't die. This is a *feature*: fil-c doesn't just *not
   crash*, it tells you exactly where the bug is.

2. **Cleanup is a double-free minefield in general** — voice.c,
   modsystem.c both have cleanup paths that free overlapping objects.
   The test helper `free_test_lists()` (modsystem test) had to unlink
   mod-owned params from the ParamList before calling the standard
   cleanup, to avoid exactly this kind of double-free.

3. **Envelope stage timing is float-drift dependent** — the
   `currentTime += dt` accumulator drifts ~0.5 ulp/add. Sustain at 0.7s
   completes after 30848 calls, not the naive 30840. Counts are
   deterministic so tests assert as-is.

4. **`initModSystem()` must be called once before any envelope
   generation** — `generateEnvelope` reads the global `envTables`
   pool. The test sets this up; forgot in voice test first pass and
   got segfault.

## Decisions made in this section

1. **Subagent fan-out again** — 2 parallel agents → 53 tests in ~5 min.
   Pattern continues to pay off.
2. **Test voice via the working generator path** — Option B from the
   user's call. Default voice type is FM (which works); BLEP is
   exercised but the silent-BLEP bug is locked in as a regression
   test, not bypassed.
3. **Document the double-free loudly** — this is exactly what fil-c is
   for. The fix is upstream, not on this branch.
4. **No source modifications** — all 17 bugs are upstream. We surface
   them, lock them in, defer fixes.

## Section exit criteria — all met

- [x] voice.c and modsystem.c build under fil-c
- [x] 53 new tests pass
- [x] Double-free bug caught live by fil-c
- [x] All 17 pre-existing bugs documented
- [x] Section report updated

## Cross-section implications

- **🚨 BUG-VC-1 (double-free)** — this is the highest-priority finding
  in the entire port. Any user-facing release of the app would be
  vulnerable. Should be filed as a security issue on the upstream repo
  *before* anything else ships. Doesn't block Section 5 (we test render,
  not cleanup), but it does mean the end-to-end "release the voice"
  path can't be exercised in Section 5 without crashes.
- **BUG-MOD-1 (`initMod` ignores `generate`)** — Section 5 may try to
  sequence LFO-driven voices. If so, the LFO modulation will produce
  envelope-shaped output, not sine. The byte-identical render check
  vs. gcc will catch this. If we want exact parity, fix this upstream.
- **BUG-VC-4 (granular OOB)** — if Section 5's sequence uses granular
  voices, expect crashes. Avoid granular path in Section 5.