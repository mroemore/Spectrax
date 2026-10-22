#!/bin/sh
# test_qemu_smoke.sh — verify the cross-compile + qemu-user path works.
#
# Cross-compiles the DSP test and the bench for the profile's arch and runs
# them under qemu-<arch>-static, asserting exit 0.
#
# Usage: test_qemu_smoke.sh [--device ID]
set -eu

device=${SPECTRAX_DEVICE:-}
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

[ -n "$device" ] || device=gkd-pixel2

arch=$(scripts/lib/profile.py --get "$device" device.arch)
case "$arch" in
    aarch64) qemu=qemu-aarch64-static; triple=aarch64-linux-gnu ;;
    armhf)   qemu=qemu-arm-static;     triple=arm-linux-gnueabihf ;;
    *) echo "unsupported arch: $arch" >&2; exit 1 ;;
esac

command -v "$qemu" >/dev/null 2>&1 || {
    echo "SKIP: $qemu is not installed (xbps-install -S qemu-user-static)" >&2
    exit 77
}

cross=build-$device
mkdir -p "$cross"
if [ ! -f "$cross/cross.ini" ]; then
    scripts/gen-cross-file.sh "$device" --out "$cross" >/dev/null
fi
meson setup "$cross" --cross-file "$cross/cross.ini" --buildtype=release \
    > "$cross/setup.log" 2>&1 || {
        echo "FAIL: meson setup $cross failed" >&2
        tail -20 "$cross/setup.log" >&2
        exit 1
    }
meson compile -C "$cross" test_voice spectrax_bench > "$cross/compile.log" 2>&1 || {
    echo "FAIL: cross build failed" >&2
    tail -20 "$cross/compile.log" >&2
    exit 1
}

test_bin="$cross/tests/test_voice"
[ -x "$test_bin" ] || { echo "FAIL: test_voice not built" >&2; exit 1; }
bench_bin="$cross/src/tools/bench/spectrax_bench"
[ -x "$bench_bin" ] || { echo "FAIL: spectrax_bench not built" >&2; exit 1; }

# The guest loader needs a sysroot: the target's dynamic loader and libc,
# plus the bundle's link-time stubs for the libraries the device provides
# (EGL, GLESv2, GBM, DRM, ALSA). The stubs carry no code, so the smoke
# test only exercises the parts that do not touch the GPU.
sysroot="$repo_root/build-libs/qemu-$arch"
mkdir -p "$sysroot/lib"
for l in ld-linux-$triple.so.1 libc.so.6 libm.so.6 libmvec.so.1 libdl.so.2 \
         libpthread.so.0 librt.so.1; do
    [ -e "/usr/$triple/lib/$l" ] && cp -f "/usr/$triple/lib/$l" "$sysroot/lib/$l" 2>/dev/null || true
done
for s in "$repo_root/lib/$arch/stub"/*.so; do
    [ -e "$s" ] || continue
    soname=$(readelf -d "$s" 2>/dev/null | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
    [ -n "$soname" ] && cp -f "$s" "$sysroot/lib/$soname"
done

tmp_out=$(mktemp -d "${TMPDIR:-/tmp}/spectrax-qemu.XXXXXX")
trap 'rm -rf "$tmp_out"' EXIT

echo "== $test_bin under $qemu =="
LD_LIBRARY_PATH=/lib "$qemu" -L "$sysroot" "$test_bin" > "$tmp_out/voice.log" 2>&1 || {
    echo "FAIL: test_voice failed under $qemu" >&2
    tail -20 "$tmp_out/voice.log" >&2
    exit 1
}

echo "== $bench_bin under $qemu =="
LD_LIBRARY_PATH=/lib "$qemu" -L "$sysroot" "$bench_bin" --bench=fileio \
    --device "$device" --out="$tmp_out/report" > "$tmp_out/bench.log" 2>&1 || {
    echo "FAIL: spectrax_bench failed under $qemu" >&2
    tail -20 "$tmp_out/bench.log" >&2
    exit 1
}
[ -f "$tmp_out/report/summary.json" ] || {
    echo "FAIL: no summary.json from the cross-built bench" >&2
    exit 1
}

echo "OK: test_voice and spectrax_bench passed under $qemu"
