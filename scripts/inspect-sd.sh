#!/bin/sh
# inspect-sd.sh — loop-mount an SD image and validate its PortMaster entries.
#
# The ports directory differs between CFW families (/roms/ports on some,
# /storage/roms/ports on others), so the inspector searches both shapes.
#
# Usage: inspect-sd.sh <img> [--device ID] [--read-only|--rw]
#        inspect-sd.sh --dir <mount-point> [--device ID]
#
# --dir inspects an already mounted card, which needs no privileges.
set -eu

mode=image
img=""
dir=""

case "${1:-}" in
    --dir) mode=dir; shift; dir=${1:-}; shift ;;
esac

if [ "$mode" = dir ]; then
    [ -n "$dir" ] && [ -d "$dir" ] || { echo "usage: $0 --dir <mount-point> [--device ID]" >&2; exit 64; }
else
    img=${1:-}
    [ -n "$img" ] && [ -f "$img" ] || { echo "usage: $0 <img> [--device ID] [--read-only|--rw]" >&2; exit 64; }
    shift
fi

device=${SPECTRAX_DEVICE:-}
ro="-o ro"
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --read-only) ro="-o ro" ;;
        --rw) ro="-o rw" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

if [ -n "$device" ]; then
    # shellcheck disable=SC1091
    SPECTRAX_DEVICE="$device"
    . "$repo_root/scripts/lib/profile.sh"
    ports_rel=$PATHS_PORTS_DIR
else
    ports_rel=""
fi

if [ "$mode" = dir ]; then
    mnt=$dir
    echo "inspecting $mnt"
else
    mnt=$(mktemp -d "${TMPDIR:-/tmp}/spectrax-inspect.XXXXXX")
    trap 'umount "$mnt" 2>/dev/null || true; rmdir "$mnt"' EXIT
    mount -o loop,$ro "$img" "$mnt" || {
        echo "error: cannot loop-mount $img (needs root: sudo $0 $*)" >&2
        exit 1
    }
    echo "mounted $img at $mnt"
fi

ports=""
rels=""
for rel in ${ports_rel:+"$ports_rel"} /roms/ports /storage/roms/ports; do
    case " $rels " in
        *" $rel "*) continue ;;
    esac
    rels="$rels $rel"
done
for rel in $rels; do
    for sub in "$mnt" "$mnt"/*/; do
        [ -d "$sub$rel" ] && ports="$ports $sub$rel"
    done
done

if [ -z "$ports" ]; then
    echo "no ports directory found (looked for ${ports_rel:-/roms/ports} and /storage/roms/ports)"
    exit 2
fi

ok=0; fail=0
for p in $ports; do
    echo "--- $p ---"
    for entry in "$p"/*; do
        [ -d "$entry" ] || continue
        name=$(basename "$entry")
        sh_file="$entry/$name.sh"
        bin_file="$entry/$name"
        if [ ! -f "$sh_file" ]; then
            echo "  $name: MISSING $sh_file"; fail=$((fail+1)); continue
        fi
        first=$(head -1 "$sh_file")
        if [ "$first" != "#!/bin/sh" ] && [ "$first" != "#!/bin/bash" ]; then
            echo "  $name: bad shebang: $first"; fail=$((fail+1)); continue
        fi
        if [ ! -x "$sh_file" ]; then
            echo "  $name: not executable"; fail=$((fail+1)); continue
        fi
        if [ ! -f "$bin_file" ]; then
            echo "  $name: NO BINARY at $bin_file"; fail=$((fail+1)); continue
        fi
        echo "  $name: OK ($bin_file $(stat -c%s "$bin_file") bytes)"; ok=$((ok+1))
    done
done

echo "--- summary: $ok ok, $fail fail ---"
exit $fail
