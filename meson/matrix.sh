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
#   m32       gcc -m32                             (skipped without multilib)
#
# Not viable on a stock Void box (documented, skipped): MSan needs an
# instrumented libc; gcc here ships no libasan/libubsan, so sanitizer builds use
# clang (see TESTING.md I18/I19).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG="${FUZZ_CC:-/usr/lib/llvm/21/bin/clang}"
[ -x "$CLANG" ] || CLANG="/usr/lib/llvm/19/bin/clang"

CONFIGS=("$@")
[ "${#CONFIGS[@]}" -eq 0 ] && CONFIGS=(asan trapv uchar schar valgrind m32)

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
  if [ "$rc" = 0 ]; then
    SUMMARY+=("$(printf '%-9s PASS' "$cfg")")
  else
    SUMMARY+=("$(printf '%-9s FAIL' "$cfg")")
  fi
  return "$rc"
}

overall=0
for cfg in "${CONFIGS[@]}"; do
  run_one "$cfg" || overall=1
done

echo
echo "=== matrix ==="
printf '%s\n' "${SUMMARY[@]}"
exit "$overall"
