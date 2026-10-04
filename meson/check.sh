#!/usr/bin/env bash
# meson/check.sh [--full] [--quiet]
#
# One command that runs every gate for the Spectrax testing stack, in cheap-
# first order, and stops at the first failure. This is the single entry point
# CI, the pre-push hook, and a human should use before calling work "done".
#
# Default (fast, ~1-2 min): build + suite + coverage gate.
# --full (slow, many minutes): everything below, including the fuzz smoke,
#   the sanitizer/valgrind matrix, and mutation.
#
# The tiers are deliberately separate: a pre-push hook must stay under ~10 s
# when nothing changed, and nobody runs valgrind (minutes) on every commit.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FULL=0
QUIET=0
for arg in "$@"; do
  case "$arg" in
    --full) FULL=1 ;;
    --quiet) QUIET=1 ;;
    -*) echo "check.sh: unknown option: $arg" >&2; exit 2 ;;
  esac
done

pass=0
fail=0
run() {
  local name="$1"; shift
  [ "$QUIET" = 1 ] || printf '>> %s\n' "$name"
  if "$@" >/tmp/spectrax-check.log 2>&1; then
    printf '   PASS  %s\n' "$name"
    pass=$((pass + 1))
  else
    printf '   FAIL  %s\n' "$name"
    [ "$QUIET" = 1 ] || tail -30 /tmp/spectrax-check.log
    fail=$((fail + 1))
  fi
}

echo "=== spectrax checks (tier: $([ "$FULL" = 1 ] && echo full || echo default)) ==="
run "coverage + gate" "$ROOT/meson/coverage.sh" --gate

if [ "$FULL" = 1 ]; then
  run "dynamic-analysis matrix" "$ROOT/meson/matrix.sh"
  run "fuzz smoke (wav 20s)" "$ROOT/meson/fuzz.sh" wav --time 20
  run "fuzz smoke (config 20s)" "$ROOT/meson/fuzz.sh" config --time 20
  run "mutation (scaffold)" "$ROOT/meson/mutate.sh"
fi

echo
echo "=== $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]
