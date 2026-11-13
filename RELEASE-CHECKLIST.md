# Release checklist — Spectrax

A short, human-in-the-loop gate for cutting a release. The automated tiers
(`./meson/check.sh --full`) prove the code is consistent with what the tests
check; this proves the feature is right.

## 1. Scope

- [ ] What is in this release, in one paragraph.
- [ ] Anything knowingly left out, and why.

## 2. Automated gates

- [ ] `./meson/check.sh --full` is green on the release commit.
- [ ] `meson/coverage-min.txt` floors were raised for any module that closed
      branches this cycle (the ratchet only goes up).
- [ ] No new mutation survivors on gated targets.
- [ ] Every fuzz crasher found this cycle is fixed and kept as a permanent
      `regress*` regression input.

## 3. Regression discipline

- [ ] Every user-reported bug this cycle has a test that fails on the old code
      and passes on the new, named for the symptom.

## 4. Real-app smoke (not automated)

- [ ] Build the shipped binary (`ninja -C build && ./build/spectrax` or your
      install step) and exercise the user-facing path by hand on a clean
      checkout. This is the step no automation replaces.

## 5. Notes and tag

- [ ] Release notes written.
- [ ] Version bumped.
- [ ] Tag cut; artifacts built from the tag.
