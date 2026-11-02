#!/usr/bin/env bash
# meson/fuzz.sh [target ...] [--time SECONDS] [--build-only]
#
# Build and run the libFuzzer targets in fuzz/ with ASan+UBSan.
# Targets: wav (WAV loader), seqfile (binary project/song loader),
#         config (JSON settings/theme). Default: all.
#
# Requires a clang with the libFuzzer runtime (compiler-rt). Override the
# compiler with FUZZ_CC.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build-fuzz"
TIME=60
BUILD_ONLY=0
TARGETS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --time)
      TIME="$2"
      shift 2
      ;;
    --build-only)
      BUILD_ONLY=1
      shift
      ;;
    -*)
      echo "fuzz.sh: unknown option: $1" >&2
      exit 2
      ;;
    *)
      TARGETS+=("$1")
      shift
      ;;
  esac
done

if [ "${#TARGETS[@]}" -eq 0 ]; then
  TARGETS=(wav seqfile config)
fi

have_fuzzer() {
  printf 'int LLVMFuzzerTestOneInput(const unsigned char*d,unsigned long s){return (int)(d?s:0);}\n' \
    | "$1" -x c -fsanitize=fuzzer -o /dev/null - >/dev/null 2>&1
}

find_clang() {
  for c in ${FUZZ_CC:-} /usr/lib/llvm/21/bin/clang /usr/lib/llvm/20/bin/clang \
           /usr/lib/llvm/19/bin/clang clang; do
    [ -n "$c" ] || continue
    command -v "$c" >/dev/null 2>&1 || continue
    if have_fuzzer "$c"; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

CC="$(find_clang)" || {
  echo "fuzz.sh: no clang with libFuzzer runtime found (set FUZZ_CC)" >&2
  exit 2
}
echo ">> compiler: $CC"

CFLAGS=(-g -O1 -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
        -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT/src/io" -I"$ROOT/third_party/cjson")

mkdir -p "$OUT"

build_wav() {
  "$CC" "${CFLAGS[@]}" "$ROOT/fuzz/fuzz_wav.c" "$ROOT/src/io.c" "$ROOT/src/sample.c" \
    -lm -o "$OUT/fuzz_wav"
}

build_config() {
  "$CC" "${CFLAGS[@]}" "$ROOT/fuzz/fuzz_config.c" "$ROOT/src/io/config_io.c" \
    "$ROOT/third_party/cjson/cJSON.c" -lm -o "$OUT/fuzz_config"
}

# Link against the project's built static libraries rather than hand-listing
# sources: the sequencer reaches appstate -> input -> gui_style -> graph_gui,
# so a source list here would be both long and silently incomplete as the
# dependency graph changes. The libs are the dependency graph.
#
# That means this target depends on a *built* tree, which nothing else in the
# --full tier provides: check.sh builds build-cov (coverage) and the
# build-matrix-* dirs (the sanitizer matrix), but never plain `build/`. So a
# clean checkout would fail here with "no such file or directory" on the
# archives. It passed for as long as it did only because a developer had a
# hand-built `build/` lying around. Ensure it ourselves. (TESTING.md I53.)
ensure_libs() {
  if [ ! -f "$ROOT/build/build.ninja" ]; then
    echo "   (configuring build/ for the static libs)"
    meson setup "$ROOT/build" "$ROOT" >/dev/null 2>&1 || {
      echo "meson setup build failed" >&2
      return 1
    }
  fi
  meson compile -C "$ROOT/build" >/dev/null 2>&1 || {
    echo "compiling build/ failed" >&2
    return 1
  }
}

build_seqfile() {
  ensure_libs || return 1
  "$CC" "${CFLAGS[@]}" "$ROOT/fuzz/fuzz_seqfile.c" \
    -Wl,--start-group \
    "$ROOT/build/src/libspectrax_core.a" \
    "$ROOT/build/src/libspectrax_seq.a" \
    "$ROOT/build/src/libspectrax_gui.a" \
    "$ROOT/build/src/libspectrax_vizfx.a" \
    "$ROOT/build/libspectrax_cjson.a" \
    -Wl,--end-group \
    "$ROOT/lib/linux/libraylib.a" "$ROOT/lib/linux/libkissfft-float.a" \
    -lm -lpthread -lportaudio -lGL -lX11 -lrt -ldl -o "$OUT/fuzz_seqfile"
}

for t in "${TARGETS[@]}"; do
  echo ">> build $t"
  "build_$t"
  if [ "$BUILD_ONLY" = 1 ]; then
    continue
  fi
  corpus="$ROOT/fuzz/corpus/$t"
  mkdir -p "$corpus"
  echo ">> fuzz $t for ${TIME}s"
  ASAN_SYMBOLIZER_PATH="$("$CC" --print-prog-name=llvm-symbolizer 2>/dev/null)" \
    "$OUT/fuzz_$t" -max_total_time="$TIME" -artifact_prefix="$OUT/" "$corpus"
done
