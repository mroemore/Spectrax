#!/bin/sh
# deploy-sd.sh — rsync the Spectrax port tree onto a mounted SD card.
#
# The destination is derived from the profile's paths.ports_dir, so the same
# script serves devices whose SD layout differs.
#
# Usage: deploy-sd.sh <mount-point> [--device ID] [--build-dir DIR]
set -eu

mount=${1:-}
[ -n "$mount" ] || { echo "usage: $0 <mount-point> [--device ID] [--build-dir DIR]" >&2; exit 64; }
[ -d "$mount" ] || { echo "not a directory: $mount" >&2; exit 1; }
shift

device=${SPECTRAX_DEVICE:-}
build_dir=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --build-dir) shift; build_dir="${1:-}" ;;
        --build-dir=*) build_dir="${1#--build-dir=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

[ -n "$device" ] || { echo "--device is required (or set SPECTRAX_DEVICE)" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

if [ -n "$PROBES_NEEDED" ]; then
    echo "warning: profile $DEVICE_ID has unresolved probes ($PROBES_NEEDED)" >&2
    echo "         the port may land in the wrong directory" >&2
fi

[ -n "$build_dir" ] || build_dir="build-$DEVICE_ID"
[ -d "$build_dir" ] || { echo "build dir not found: $build_dir" >&2; exit 1; }

find_binary() {
    for cand in "$build_dir/$1" "$build_dir/src/$1" \
                "$build_dir/src/tools/bench/$1" \
                "$build_dir/src/tools/instrument_harness/$1"; do
        if [ -f "$cand" ]; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

dest="$mount$PATHS_PORTS_DIR/spectrax"
mkdir -p "$dest/data"

for b in spectrax spectrax_bench instrument_harness; do
    p=$(find_binary "$b") || { echo "missing $b in $build_dir" >&2; exit 1; }
    rsync -a --info=progress2 "$p" "$dest/"
done
for s in spectrax.sh spectrax_bench.sh; do
    rsync -a --info=progress2 "scripts/portmaster/$s" "$dest/$s"
done
chmod +x "$dest/spectrax.sh" "$dest/spectrax_bench.sh"
[ -d bin/resources ] && rsync -a --info=progress2 bin/resources/ "$dest/data/"

echo "deployed to $dest"
ls -la "$dest"
