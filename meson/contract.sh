#!/usr/bin/env bash
# meson/contract.sh — enforce the contracts.
#
# The coverage build compiles ALWAYS()/NEVER() away:
#
#   #if SPX_DEBUG && !defined(PREFIX_COVERAGE_TEST)
#   #  define ALWAYS(X) ((X) ? 1 : (assert(!"ALWAYS() failed"), 0))
#   #else
#   #  define ALWAYS(X) (X)
#   #endif
#
# so a coverage-only gate cannot see an inverted contract. This tier runs the
# suite in a plain DEBUG build, where the assertions are live, and the fast
# tier runs it alongside the coverage gate.
#
# The build: a plain debug configuration (no coverage, and therefore no
# NDEBUG) in build-contract/. It is configured ONCE and rebuilt incrementally
# after that, so the tier is affordable on every commit. A fresh checkout
# pays one full build here, the same one the coverage tier pays.
#
# The exclusions: a test that cannot reach a contract site does not need to
# run here, and on a large project one such test can cost more than every
# other test combined - a device smoke that cross-compiles and runs under an
# emulator, for instance. The project lists those suites in
# meson/contract-exclude.txt, one meson suite name per line, '#' for a
# comment. The name is a meson test suite, so the project declares it where
# it declares the test:
#
#   test('test_device_smoke', exe, suite: ['device'])
#
# Exit statuses:
#   0  the suite passed with the contracts live
#   1  the suite failed, or an assertion fired
#   2  the script could not run (configure or build failed)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-contract"
EXCLUDE_FILE="$ROOT/meson/contract-exclude.txt"

# shellcheck source=meson/lib.sh
. "$ROOT/meson/lib.sh"

# Configured once, then rebuilt incrementally. A `meson setup --wipe` here
# would make every run a full build from scratch, which is the difference
# between a tier people run and a tier they skip.
if [ ! -f "$BUILD/build.ninja" ]; then
  echo ">> configuring $BUILD (debug, no coverage)"
  meson setup "$BUILD" "$ROOT" --buildtype=debug >/dev/null 2>&1 || {
    echo "contract.sh: configure failed" >&2
    exit 2
  }
fi

echo ">> build"
meson compile -C "$BUILD" >/dev/null 2>&1 || {
  echo "contract.sh: build failed" >&2
  exit 2
}

args=()
if [ -f "$EXCLUDE_FILE" ]; then
  while IFS= read -r s; do
    s="${s%%#*}"
    s="$(printf '%s' "$s" | tr -d '[:space:]')"
    [ -n "$s" ] || continue
    echo ">> excluding suite: $s"
    args+=(--no-suite "$s")
  done <"$EXCLUDE_FILE"
fi

echo ">> test (contracts live)"
# A suite that matched nothing is not a tier with nothing to run: the run
# would report PASS over zero tests. Every way of having nothing here is a
# failure, and each says which.
#
# The guard lists with the SAME filter the run uses. A guard that lists the
# whole tree while the run excludes a suite that holds every test would
# pass over a run that never happened - which is the defect this tier
# exists to close, one level down.
if ! require_tests "$BUILD" "contract" "" "${args[@]}"; then
  exit 1
fi
meson test -C "$BUILD" --print-errorlogs "${args[@]}"
