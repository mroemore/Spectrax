#!/usr/bin/env bash
# meson/coverage.sh — configure a coverage build, run the test suite, and
# report gcov/lcov line + branch coverage for first-party sources only.
#
# Usage:
#   meson/coverage.sh [builddir] [--html] [--gate]
#
# builddir defaults to <root>/build-cov. --html also writes a browsable
# genhtml report to <builddir>/html/. --gate additionally enforces the
# per-module branch floors in meson/coverage-min.txt.
#
# NOTE: lcov on this box is the 1.0 series, whose rc knob is
# lcov_branch_coverage (not the lcov-2.x "branch_coverage"). Without it the
# capture silently drops all branch data.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build-cov"
HTML=0
GATE=0

for arg in "$@"; do
  case "$arg" in
    --html)
      HTML=1
      ;;
    --gate)
      GATE=1
      ;;
    -*)
      echo "coverage.sh: unknown option: $arg" >&2
      exit 2
      ;;
    *)
      BUILD="$arg"
      ;;
  esac
done

if [ ! -d "$BUILD" ]; then
  echo ">> meson setup $BUILD"
  meson setup "$BUILD" "$ROOT" -Db_coverage=true -Dbuildtype=debug
fi

echo ">> build"
ninja -C "$BUILD"

echo ">> test"
# A failing test still executes code, so keep going and report coverage;
# the suite's exit status is surfaced at the end.
TEST_RC=0
meson test -C "$BUILD" --print-errorlogs || TEST_RC=$?

INFO="$BUILD/coverage.info"
SRC_INFO="$BUILD/coverage-src.info"

echo ">> capture"
lcov --capture --directory "$BUILD" --output-file "$INFO" \
  --rc lcov_branch_coverage=1

# Keep only first-party sources: drop system, vendored, test, and helper code.
lcov --remove "$INFO" \
  '/usr/*' '*/third_party/*' '*/vendor/*' '*/tests/*' '*/include/*' '*/meson/*' \
  --output-file "$SRC_INFO" --rc lcov_branch_coverage=1

echo ">> summary (project sources)"
lcov --summary "$SRC_INFO" --rc lcov_branch_coverage=1

if [ "$HTML" = 1 ]; then
  OUT="$BUILD/html"
  genhtml --branch-coverage --output-directory "$OUT" "$SRC_INFO" \
    --rc genhtml_branch_coverage=1 >/dev/null
  echo ">> html report: $OUT/index.html"
fi

if [ "$GATE" = 1 ]; then
  echo ">> gate"
  GATE_RC=0
  "$ROOT/meson/coverage-gate.sh" "$BUILD" || GATE_RC=$?
  if [ "$GATE_RC" != 0 ]; then
    exit "$GATE_RC"
  fi
fi

exit "$TEST_RC"
