#!/usr/bin/env bash
# meson/check.sh [--full] [--quiet]
#
# One command that runs every gate for the Spectrax testing stack, in cheap-
# first order. This is the single entry point CI, the pre-push hook, and a
# human should use before calling work "done".
#
# Default (fast, ~1-2 min): build + suite + coverage gate.
# --full (slow, many minutes): everything below, including the fuzz smoke,
#   the sanitizer/valgrind matrix, and mutation.
#
# The tiers are deliberately separate: a pre-push hook must stay under ~10 s
# when nothing changed, and nobody runs valgrind (minutes) on every commit.
#
# Every tier runs, and the run does not stop at the first failure: one broken
# gate must not hide the state of the ones after it. The summary at the end
# reports what passed, what failed, and what had nothing to run. The exit
# status is non-zero if anything FAILED - which is what the pre-push hook
# reads - so a failure anywhere still blocks, it just does not blind the rest
# of the report.
#
# A tier that had nothing to run is reported as SKIP and counted apart from
# the passes: "PASS fuzz smoke" over zero fuzzed targets is a green light for
# work that never happened, which is the one thing a gate must never be.
# The tiers signal it with exit status 3:
#
#   0  the tier passed
#   1  the tier ran and failed
#   2  the tier could not run (missing tool, missing report)
#   3  the tier had nothing to run
#
# --build <dir> points the directory-taking tier at a configuration other
# than the default, so "everything passes" is answerable for a second tree
# and not only for the default one. Only the tier that runs against a
# directory someone else prepared takes it: the coverage tier. The matrix,
# fuzz and mutation tiers each build the tree they run in, so they are
# unaffected by which tree this run is pointed at.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FULL=0
QUIET=0
# The build directory the directory-taking tiers run against. A build
# directory is named the way meson names it, relative to the repo root
# (build-cov, build-raylib), and the tiers below are reached from wherever
# the caller happens to stand - so it is resolved against the root rather
# than letting the caller's working directory decide which tree it means.
BUILD="build-cov"

while [ $# -gt 0 ]; do
  case "$1" in
    --full)
      FULL=1
      shift
      ;;
    --quiet)
      QUIET=1
      shift
      ;;
    --build)
      # Checked rather than trusted: a bare `shift 2` with one argument left
      # fails and shifts nothing, which leaves $1 unchanged and spins this
      # loop forever with no output.
      [ $# -ge 2 ] || { echo "check.sh: --build needs a directory" >&2; exit 2; }
      BUILD="$2"
      shift 2
      ;;
    -*)
      echo "check.sh: unknown option: $1" >&2
      exit 2
      ;;
    *)
      echo "check.sh: unexpected argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$BUILD" in
  /*) ;;
  *) BUILD="$ROOT/$BUILD" ;;
esac

LOG="$ROOT/.tmp_files/check.log"
mkdir -p "$ROOT/.tmp_files"

# Chosen so it cannot collide with a script's own failure statuses (1 = a
# gate failed, 2 = the script could not run). meson/fuzz.sh and
# meson/mutate.sh document this value.
SKIP_RC=3

pass=0
fail=0
skip=0
run() {
  local name="$1"
  shift
  local rc
  [ "$QUIET" = 1 ] || printf '>> %s\n' "$name"
  "$@" >"$LOG" 2>&1
  rc=$?
  case "$rc" in
    0)
      printf '   PASS  %s\n' "$name"
      pass=$((pass + 1))
      ;;
    "$SKIP_RC")
      printf '   SKIP  %s (nothing to run)\n' "$name"
      skip=$((skip + 1))
      ;;
    2)
      # The tier could not RUN: a missing tool, a missing report, a tree
      # that cannot be read. That is a different failure from a tier that
      # ran and found something wrong, and the reader needs to tell them
      # apart - "the gate failed" and "the gate could not run" send them to
      # different places.
      printf '   ERROR %s (could not run)\n' "$name"
      [ "$QUIET" = 1 ] || tail -30 "$LOG"
      fail=$((fail + 1))
      ;;
    *)
      printf '   FAIL  %s\n' "$name"
      [ "$QUIET" = 1 ] || tail -30 "$LOG"
      fail=$((fail + 1))
      ;;
  esac
}

echo "=== spectrax checks (tier: $([ "$FULL" = 1 ] && echo full || echo default)) ==="
run "contracts (debug build)" "$ROOT/meson/contract.sh"
run "coverage + gate" "$ROOT/meson/coverage.sh" "$BUILD" --gate

if [ "$FULL" = 1 ]; then
  run "dynamic-analysis matrix" "$ROOT/meson/matrix.sh"
  run "fuzz smoke (20s/target)" "$ROOT/meson/fuzz.sh" --time 20
  run "mutation" "$ROOT/meson/mutate.sh"
fi

echo
echo "=== $pass passed, $fail failed, $skip skipped ==="
[ "$fail" -eq 0 ]
