# Release checklist

A short, human-in-the-loop gate for cutting a Spectrax release. This is the
"process" pillar of the testing stack (P8): the automated tiers can only tell
you the code is *consistent with what the tests check* — they cannot tell you
the feature is right. Everything below is a deliberate step, not a formality.

The automated side is one command:

```sh
./meson/check.sh --full      # suite + coverage gate + matrix + fuzz smoke + mutation
```

If that is not green, stop. Do not paper over a red gate to make a release.

---

## 0. Scope

- [ ] Every change in this release is described by a commit message or a
      `docs/superpowers/plans/*.md` entry. If you cannot say what it changes,
      it is not ready.
- [ ] No debug scaffolding left in the tree: `git diff` shows no `printf`
      added for debugging, no commented-out blocks, no temporary files
      (`mull-*.whl`, `.tmp_files/` leftovers) staged.
- [ ] The test corpus in `fuzz/corpus/` includes a regression input for every
      crash fixed this cycle.

## 1. Automated gates

- [ ] `./meson/check.sh --full` is green. In particular:
  - [ ] coverage gate passes (`src/notes.c`, `src/wavetable.c` at their floors);
  - [ ] the dynamic-analysis matrix passes (asan, trapv, uchar, schar, valgrind);
  - [ ] the fuzz smoke runs clean (no new crash/leak inputs).
- [ ] No new `TODO`/`FIXME` added without a `TESTING.md` or issue entry.

## 2. Coverage ratchet

- [ ] Any module that reached a higher branch-coverage floor this cycle has its
      floor raised in `meson/coverage-min.txt` (floors only go up).
- [ ] Any module deliberately *not* raised has the reason recorded in
      `TESTING.md` (e.g. needs a device/GL context).

## 3. Real-app smoke (the human step)

Run the actual binary, not the tests:

```sh
ninja -C build spectrax && ./build/spectrax
```

- [ ] It launches and the audio backend initialises (or degrades cleanly).
- [ ] Load a real save and play it: the specific bug fixed this cycle no longer
      reproduces.
- [ ] If a save format changed, an *old* save still loads (or the break is
      called out in the release notes).

## 4. Regression discipline

- [ ] For each **user-reported** bug fixed this cycle there is a test that
      fails on the old code and passes on the new. (See "Regression discipline"
      in `TESTING.md`.)
- [ ] The test names the symptom, not the fix (`test_note_delete_mid_playback_does_not_crash`
      beats `test_fix_3`).

## 5. Notes

- [ ] `TESTING.md` "Issues & solutions" has an entry for every defect found
      while building this cycle — symptom, cause, fix, takeaway.
- [ ] Release notes list behaviour changes explicitly (e.g. mono WAVs now load
      at full length — I14).
- [ ] Version bumped where the project keeps one.

## 6. Tag

- [ ] Commit, tag, push. The `pre-push` hook runs the fast tier automatically;
      a red fast tier blocks the push (bypass only with `--no-verify` and a
      reason).
