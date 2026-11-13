#!/usr/bin/env bash
# meson/matrix.sh [config ...]
#
# Dynamic-analysis matrix: build the test suite under each configuration and
# run it. Exits non-zero if any configuration fails.
#
#   asan      clang -fsanitize=address,undefined   (memory + UB; leaks off —
#             third-party libs leak, first-party leaks are covered by valgrind)
#   trapv     gcc -ftrapv                          (signed-overflow traps)
#   uchar     gcc -funsigned-char                  (char signedness)
#   schar     gcc -fsigned-char
#   valgrind  plain build, every test under valgrind. Gates on memcheck errors
#             (invalid read/write, use-after-free, uninitialised value); leaks
#             are reported but not gated (test-harness cleanup backlog, see
#             TESTING.md I19). --max-stackframe covers the suite's ~2.3 MB
#             local TestEnv, which otherwise trips valgrind's stack heuristic.
#   tsan      clang -fsanitize=thread. The concurrency gate: catches data
#             races between two threads of the program, which no other rung in
#             this matrix can observe - a race reads as "both branches taken,
#             all green".
#   helgrind  valgrind --tool=helgrind. The same class of check without
#             recompiling, so it validates a plain build. It also catches
#             lock-order inversions.
#   m32       gcc -m32                             (skipped without multilib)
#
# The race detectors run a SUBSET of the suite: the tests that exercise real
# threads. The project DECLARES that subset with a meson suite tag:
#
#   test('test_concurrency', exe, suite: ['race'])
#
# and the tier asks meson for it. A hand-maintained list of test names drifts
# - a new concurrency test that nobody added to the list is silently
# uncovered by the race detector - so the tier also CHECKS the declaration:
# every test whose source creates a thread must be in the suite. A test that
# creates no thread cannot create a data race of its own, so that is a
# superset rule, and it fails loudly when the declaration falls behind.
#
# Why the subset: both detectors report races in uninstrumented third-party
# code - a GL/GLX/X11 layer or an audio library is full of benign shared
# state, and neither tool can see inside it. The same noise under a race
# detector swamps the real first-party findings. The project's suppression
# file (tsan.supp, helgrind.supp) records every accepted exception, and each
# entry must name the access point it silences.
#
# In this project the race suite is one test: test_audio_concurrency, which
# is declared with suite: ['race'] in tests/meson.build. It is
# self-contained by construction - renderAudioBuffer makes no raylib calls
# at all with g_probeTempo false - so a clean report means something.
#
# helgrind is the second rung here for a reason beyond atomics: it runs on
# a plain, uninstrumented build, so it also catches a lock-order inversion
# the tsan build does not. It does NOT model C11 _Atomic, so a relaxed
# atomic load or store reads as a race to it and to nothing else - see
# helgrind.supp, and the ctol-race skill for the rule.
#
# Not viable on a stock Void box (documented, skipped): MSan needs an
# instrumented libc; gcc here ships no libasan/libubsan, so sanitizer builds use
# clang (see TESTING.md I18/I19).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG="${FUZZ_CC:-/usr/lib/llvm/21/bin/clang}"
[ -x "$CLANG" ] || CLANG="/usr/lib/llvm/19/bin/clang"

# shellcheck source=meson/lib.sh
. "$ROOT/meson/lib.sh"

CONFIGS=("$@")
[ "${#CONFIGS[@]}" -eq 0 ] && CONFIGS=(asan trapv uchar schar valgrind tsan helgrind m32)

# The meson suite that holds the project's race-checkable tests.
RACE_SUITE=race

SUMMARY=()

setup_build() { # <dir> <cc-or-empty> <meson args...>
  local dir="$1" cc="$2"
  shift 2
  rm -rf "$ROOT/$dir"
  if [ -n "$cc" ]; then
    CC="$cc" meson setup "$ROOT/$dir" "$ROOT" "$@" >/dev/null 2>&1 || return 1
  else
    meson setup "$ROOT/$dir" "$ROOT" "$@" >/dev/null 2>&1 || return 1
  fi
  ninja -C "$ROOT/$dir" >/dev/null 2>&1 || return 2
  return 0
}

# race_gate <builddir> <tier-name> — the checks every race tier shares.
#
# Returns 0 to run the tier, 1 to fail it, 3 to skip it.
race_gate() {
  local build="$1" name="$2"
  if ! project_declares_suite "$ROOT" "$RACE_SUITE"; then
    echo "$name: the project declares no '$RACE_SUITE' suite; there is nothing to race-check" >&2
    return 3
  fi
  if ! require_tests "$build" "$name" "$RACE_SUITE"; then
    return 1
  fi
  if ! race_suite_complete "$ROOT" "$build" "$RACE_SUITE"; then
    echo "$name: the tests named above create a thread but are not in the '$RACE_SUITE' suite; the race detector would not cover them" >&2
    return 1
  fi
  return 0
}

run_one() {
  local cfg="$1" rc=0 dir
  echo ">> $cfg"
  case "$cfg" in
    asan)
      dir=build-matrix-asan
      setup_build "$dir" "$CLANG" \
        -Dc_args='["-fsanitize=address,undefined","-fno-omit-frame-pointer"]' \
        -Dc_link_args='["-fsanitize=address,undefined"]' || rc=1
      [ "$rc" = 0 ] && { ASAN_OPTIONS=detect_leaks=0 meson test -C "$ROOT/$dir" >/dev/null 2>&1 || rc=1; }
      ;;
    trapv|uchar|schar)
      dir="build-matrix-$cfg"
      case "$cfg" in
        trapv) flag=-ftrapv ;;
        uchar) flag=-funsigned-char ;;
        schar) flag=-fsigned-char ;;
      esac
      setup_build "$dir" "" "-Dc_args=$flag" "-Dc_link_args=$flag" || rc=1
      [ "$rc" = 0 ] && { meson test -C "$ROOT/$dir" >/dev/null 2>&1 || rc=1; }
      ;;
    valgrind)
      dir=build-matrix-valgrind
      setup_build "$dir" "" || rc=1
      [ "$rc" = 0 ] && { meson test -C "$ROOT/$dir" --timeout-multiplier=20 \
        --wrapper="valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=none --max-stackframe=8000000 --suppressions=$ROOT/valgrind.supp" \
        >/dev/null 2>&1 || rc=1; }
      ;;
    tsan)
      dir=build-matrix-tsan
      setup_build "$dir" "$CLANG" \
        -Dc_args='["-fsanitize=thread","-fno-omit-frame-pointer","-g"]' \
        -Dc_link_args='["-fsanitize=thread"]' || rc=1
      if [ "$rc" = 0 ]; then
        race_gate "$ROOT/$dir" "matrix (tsan)"
        rc=$?
        if [ "$rc" = 0 ]; then
          # TSan cannot be combined with ASan; this build is tsan-only.
          # exitcode=66 is what TSAN_OPTIONS uses to make a detected race
          # fail the process, so meson sees a non-zero exit instead of a
          # pass-with-warnings.
          TSAN_OPTIONS="exitcode=66 suppressions=$ROOT/tsan.supp halt_on_error=0 second_deadlock_stack=1" \
            meson test -C "$ROOT/$dir" --timeout-multiplier=20 --suite "$RACE_SUITE" \
            >/dev/null 2>&1 || rc=1
        fi
      fi
      ;;
    helgrind)
      dir=build-matrix-helgrind
      setup_build "$dir" "" || rc=1
      if [ "$rc" = 0 ]; then
        race_gate "$ROOT/$dir" "matrix (helgrind)"
        rc=$?
        if [ "$rc" = 0 ]; then
          meson test -C "$ROOT/$dir" --timeout-multiplier=40 \
            --wrapper="valgrind --tool=helgrind --error-exitcode=1 --max-stackframe=8000000 --suppressions=$ROOT/valgrind.supp --suppressions=$ROOT/helgrind.supp" \
            --suite "$RACE_SUITE" \
            >/dev/null 2>&1 || rc=1
        fi
      fi
      ;;
    m32)
      if printf 'int main(void){return 0;}\n' | cc -m32 -x c - -o "$ROOT/.tmp_files/.m32" >/dev/null 2>&1; then
        dir=build-matrix-m32
        setup_build "$dir" "" -Dc_args=-m32 -Dc_link_args=-m32 || rc=1
        [ "$rc" = 0 ] && { meson test -C "$ROOT/$dir" >/dev/null 2>&1 || rc=1; }
      else
        SUMMARY+=("$(printf '%-9s SKIP  (no 32-bit multilib)' "$cfg")")
        return 0
      fi
      ;;
    *)
      SUMMARY+=("$(printf '%-9s SKIP  (unknown config)' "$cfg")")
      return 0
      ;;
  esac
  case "$rc" in
    0) SUMMARY+=("$(printf '%-9s PASS' "$cfg")") ;;
    3) SUMMARY+=("$(printf '%-9s SKIP  (nothing to race-check)' "$cfg")") ;;
    *) SUMMARY+=("$(printf '%-9s FAIL' "$cfg")") ;;
  esac
  return "$rc"
}

overall=0
for cfg in "${CONFIGS[@]}"; do
  run_one "$cfg"
  rc=$?
  # A skip is not a failure: a tier with nothing to run is reported apart
  # from the passes, and it does not make the matrix red.
  case "$rc" in
    0 | 3) ;;
    *) overall=1 ;;
  esac
done

echo
echo "=== matrix ==="
printf '%s\n' "${SUMMARY[@]}"
exit "$overall"
