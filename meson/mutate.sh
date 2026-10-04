#!/usr/bin/env bash
# meson/mutate.sh [test-name ...] [--seed N] [--rate N]
#
# Heuristic-free mutation testing. Builds the test binaries with the allocator
# wrappers replaced by ones that flip a byte of allocated/freed memory on a
# seeded schedule (-DSPX_MUTATE), then runs the suite N times with different
# seeds. A run that CRASHES or FAILS proves the tests constrain the mutated
# state ("killed"); a run that passes means the mutation survived that seed.
#
# This is the deterministic, dependency-free alternative to Mull: no clang
# plugin, no colouring, no toolchain coupling. Full-strength mutation scores
# need a real framework (and gcc here has no libasan, so a Mull toolchain is
# not viable — see TESTING.md).
#
# Default: 5 seeds, rate 1 (mutate every allocation). Targets default to the
# pure-module suites (no third-party allocator traffic to drown the signal).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/build-mutate"
SEEDS=5
RATE=1
TARGETS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --seed) SEEDS="$2"; shift 2 ;;
    --rate) RATE="$2"; shift 2 ;;
    -*) echo "mutate.sh: unknown option: $1" >&2; exit 2 ;;
    *) TARGETS+=("$1"); shift ;;
  esac
done
[ "${#TARGETS[@]}" -eq 0 ] && TARGETS=(test_wavetable_fault test_io_fault)

echo ">> configuring $DIR (-DSPX_MUTATE)"
CCACHE_DISABLE=1 meson setup --wipe "$DIR" "$ROOT" -Dc_args=-DSPX_MUTATE >/dev/null 2>&1 || {
  echo "mutate.sh: configure failed" >&2; exit 2; }
CCACHE_DISABLE=1 ninja -C "$DIR" >/dev/null 2>&1 || {
  echo "mutate.sh: build failed" >&2; exit 2; }

killed=0
survived=0
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
  for ((seed = 1; seed <= SEEDS; seed++)); do
    out=$(MUTATE_SEED=$seed MUTATE_RATE=$RATE "$exe" 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
      survived=$((survived + 1))
      printf '  %-20s seed=%-3d SURVIVED\n' "$t" "$seed"
    else
      killed=$((killed + 1))
      printf '  %-20s seed=%-3d killed (rc=%d)\n' "$t" "$seed" "$rc"
    fi
  done
done

echo
echo "=== mutation summary ==="
echo "targets: ${TARGETS[*]}"
echo "seeds:   $SEEDS   rate: $RATE"
echo "killed:  $killed   survived: $survived"
if [ "${#missing[@]}" -gt 0 ]; then
  echo "missing: ${missing[*]}"
fi
[ "$survived" -eq 0 ] && exit 0 || exit 1
