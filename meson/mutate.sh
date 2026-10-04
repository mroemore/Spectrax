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
[ "${#TARGETS[@]}" -eq 0 ] && TARGETS=(test_mutate_input)

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
  for cand in "$DIR/tests/$t" "$DIR/src/tools/vizulobe/$t"; do
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
[ "$failed" -eq 0 ]
