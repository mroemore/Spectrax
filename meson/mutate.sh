#!/usr/bin/env bash
# meson/mutate.sh [test-name ...] [--quiet]
#
# Deterministic, dependency-free mutation testing. Every listed target is a
# self-contained input-mutation harness: it flips bits of the input the code
# under test consumes, runs the loader/parser, and requires its own assertions
# to notice. A survivor the target does not accept fails the target.
#
# Mutation is over *input bytes*, not allocator bytes: an allocation-time flip
# is overwritten by the very fread that fills the buffer, so it never reaches
# an assertion (TESTING.md I23). Input bytes are what the asserted values are
# derived from.
#
# Not Mull: there is no mutation tool on this box, gcc ships no sanitizer
# runtime, and a Mull toolchain would pin the compiler. The seeded, exhaustive
# harnesses run anywhere and need no plugin. They report a definite
# killed/survived count, and `--verbose` (MUTATE_VERBOSE) lists survivors.
#
# Targets are deterministic (they sweep, they do not sample), so there is no
# seed sweep here: MUTATE_SEED only matters when a harness chooses to sample.
#
# Exit statuses:
#   0  every mutation harness that was asked for ran and passed
#   1  a harness failed, or a target that was named did not build
#   2  the script could not run (configure or build failed)
#   3  there was nothing to mutate
#
# 3 is the SKIP status, and it is the same value meson/check.sh counts apart
# from passes: a surface that does not exist yet, not a tier that quietly
# passed. A target NAMED on the command line that never built is a failure,
# not a shrug — the summary would otherwise report "passed: 0 failed: 0" and
# return success after mutating nothing.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/build-mutate"
QUIET=0
TARGETS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --quiet) QUIET=1; shift ;;
    -*) echo "mutate.sh: unknown option: $1" >&2; exit 2 ;;
    *) TARGETS+=("$1"); shift ;;
  esac
done

# Discovery, when nothing was named. The extension is stripped: the target
# is the harness's BINARY name, and `basename "$f"` with the .c still
# attached names a file that does not exist, so the tier would report the
# harness as missing and mutate nothing.
if [ "${#TARGETS[@]}" -eq 0 ]; then
  while IFS= read -r f; do
    TARGETS+=("$(basename "$f" .c)")
  done < <(find "$ROOT/tests" -maxdepth 2 -type f -name 'test_mutate_*.c' 2>/dev/null | sort)
  if [ "${#TARGETS[@]}" -eq 0 ]; then
    # Nothing to mutate is a surface that does not exist yet. It exits 3, so
    # check.sh reports it as SKIP and counts it apart from passes. It used to
    # substitute the literal name "test_mutate_input" here - a target that
    # has never existed in any project - so discovery finding nothing turned
    # into a failure on a missing executable with no mutation code anywhere
    # near it.
    echo "mutate.sh: no mutation harness discovered under tests/test_mutate_*.c; nothing to mutate" >&2
    exit 3
  fi
fi

echo ">> configuring $DIR (-DSPX_MUTATE)"
CCACHE_DISABLE=1 meson setup --wipe "$DIR" "$ROOT" -Dc_args=-DSPX_MUTATE >/dev/null 2>&1 || {
  echo "mutate.sh: configure failed" >&2; exit 2; }
CCACHE_DISABLE=1 ninja -C "$DIR" >/dev/null 2>&1 || {
  echo "mutate.sh: build failed" >&2; exit 2; }

passed=0
failed=0
missing=()
for t in "${TARGETS[@]}"; do
  exe=""
  for cand in "$DIR/tests/$t"; do
    [ -x "$cand" ] && exe="$cand" && break
  done
  if [ -z "$exe" ]; then
    missing+=("$t")
    continue
  fi
  out=$(MUTATE_SEED="${MUTATE_SEED:-1}" "$exe" 2>&1)
  rc=$?
  if [ "$QUIET" = 0 ]; then
    echo "$out" | grep -E "mutation target|killed=|data-chunk" | sed 's/^/  /'
  fi
  if [ "$rc" -eq 0 ]; then
    passed=$((passed + 1))
    printf '  %-20s PASS\n' "$t"
  else
    failed=$((failed + 1))
    printf '  %-20s FAIL (rc=%d)\n' "$t" "$rc"
  fi
done

echo
echo "=== mutation summary ==="
echo "targets: ${TARGETS[*]}"
echo "passed:  $passed   failed: $failed"
if [ "${#missing[@]}" -gt 0 ]; then
  echo "missing: ${missing[*]}"
fi

# A target that was asked for but never built is a failure, not a shrug. The
# summary above would otherwise report "passed: 0 failed: 0" and return
# success after mutating nothing.
if [ "$failed" -eq 0 ] && [ "${#missing[@]}" -eq 0 ]; then
  exit 0
fi
exit 1
