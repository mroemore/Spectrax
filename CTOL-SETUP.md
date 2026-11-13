# CTOL setup — Spectrax

- root: `/home/krang/proj/Spectrax`
- symbol prefix: `SPX` (macros are `SPX_DEBUG`, `SPX_COVERAGE_TEST`, `SPX_MUTATE`)
- coverage build dir: `build-cov`

## Files added

- `.clang-format`
- `RELEASE-CHECKLIST.md`
- `TESTING.md`
- `fuzz/corpus/.gitignore`
- `meson/check.sh`
- `meson/contract.sh`
- `meson/coverage-gate.sh`
- `meson/coverage-min.txt`
- `meson/coverage.sh`
- `meson/fuzz.sh`
- `meson/lib.sh`
- `meson/matrix.sh`
- `meson/mutate.sh`
- `meson/pre-push`
- `src/verify.h`
- `tests/meson.build`
- `tests/support/faultinject.c`
- `tests/support/faultinject.h`
- `tests/support/mutate.c`
- `tests/support/mutate.h`
- `valgrind.supp`

`tests/harness.h` is deliberately absent: this project uses Unity
(`tests/unity.c`), so the template's rung-1 harness is not its reference.

## The wiring this project has, and what still needs doing

Done:

- [x] the coverage build defines `SPX_COVERAGE_TEST` and `NDEBUG`
      (`meson.build`)
- [x] the contract tier has its own debug build, `build-contract/`, and
      excludes the `device` suite (`meson/contract-exclude.txt`)
- [x] the race suite is declared with `suite: ['race']` in
      `tests/meson.build`; the race detectors run that suite only
- [x] the mutation harness is `tests/dsp/test_mutate_input.c`, built with
      `-DSPX_MUTATE`
- [x] `meson/check.sh` is the pre-push hook's entry point

Still to do:

- [ ] The coverage gate's floors are a ratchet: every module whose suite
      closes a branch must have its floor raised in `meson/coverage-min.txt`.
      The floors there were rebaselined when the pattern-track editor landed;
      they should climb as that editor gets unit tests of its own.
- [ ] The sampler control-panel modules (`gui_inst_sample.c`,
      `gui_instrument.c`) are exercised by the instrument_harness fixtures,
      not by the unit suite, so their coverage comes from a union pipeline
      that is still manual (see `docs/coverage/`). Gate them once that
      pipeline is scripted.

## Running it

```sh
./meson/check.sh          # fast tier: the contract tier + the coverage gate
./meson/check.sh --full   # + the matrix, the fuzz smoke, mutation
```

Install the fast pre-push gate once per clone:

```sh
ln -sf ../../meson/pre-push .git/hooks/pre-push
```
