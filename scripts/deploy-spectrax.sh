#!/bin/sh
# deploy-spectrax.sh — deploy the Spectrax port tree to a device.
#
# Walks the profile's transport.order and uses the first transport that is
# available. Prints which transport it used.
#
# Usage: deploy-spectrax.sh [--device ID] [--launch] [--out DIR]
#                            [--build-dir DIR] [--transport NAME]
set -eu

device=${SPECTRAX_DEVICE:-}
launch=0
out_dir=""
build_dir=""
force_transport=""

while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --launch) launch=1 ;;
        --out) shift; out_dir="${1:-}" ;;
        --out=*) out_dir="${1#--out=}" ;;
        --build-dir) shift; build_dir="${1:-}" ;;
        --build-dir=*) build_dir="${1#--build-dir=}" ;;
        --transport) shift; force_transport="${1:-}" ;;
        --transport=*) force_transport="${1#--transport=}" ;;
        --help|-h) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

if [ -n "$PROBES_NEEDED" ] && [ "${SPECTRAX_ALLOW_PENDING_PROBES:-0}" != "1" ]; then
    echo "profile $DEVICE_ID has unresolved probes:" >&2
    echo "$PROBES_NEEDED" | sed 's/^/  - /' >&2
    echo "run the matching scripts/probe-*.sh first, or set SPECTRAX_ALLOW_PENDING_PROBES=1" >&2
    exit 1
fi

[ -n "$build_dir" ] || build_dir="build-$DEVICE_ID"
[ -d "$build_dir" ] || { echo "build dir not found: $build_dir" >&2; exit 1; }

port_scripts="spectrax.sh spectrax_bench.sh"

# meson puts an executable next to the meson.build that declares it, so the
# binary is looked up in the places the build tree can hold it.
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

app_bin=""
for b in spectrax spectrax_bench instrument_harness; do
    p=$(find_binary "$b") || {
        echo "missing $b in $build_dir: run meson compile -C $build_dir" >&2
        exit 1
    }
    case "$b" in
        spectrax) app_bin=$p ;;
        *) ;;
    esac
done

ssh_target() {
    # $1 = ip
    echo "$CFW_USER@$1"
}

try_usb_gadget() {
    host_ip=$TRANSPORT_USB_GADGET_HOST_IP
    [ -n "$host_ip" ] || return 1
    ip link show usb0 >/dev/null 2>&1 || ip link show gadget >/dev/null 2>&1 || return 1
    ip addr show 2>/dev/null | grep -q "$host_ip/" || return 1
    ssh -o ConnectTimeout=5 -o BatchMode=yes "$(ssh_target "$host_ip")" true 2>/dev/null || return 1
    echo "$host_ip"
    return 0
}

try_wifi_ssh() {
    if [ -n "$TRANSPORT_LAST_IP" ] && \
       ssh -o ConnectTimeout=3 -o BatchMode=yes "$(ssh_target "$TRANSPORT_LAST_IP")" true 2>/dev/null; then
        echo "$TRANSPORT_LAST_IP"
        return 0
    fi
    for name in $TRANSPORT_MDNS_NAMES; do
        cand=$(getent hosts "$name" 2>/dev/null | awk '{print $1}' | head -1)
        if [ -n "$cand" ] && ssh -o ConnectTimeout=3 -o BatchMode=yes "$(ssh_target "$cand")" true 2>/dev/null; then
            echo "$cand"
            return 0
        fi
    done
    ifc=$(ip route 2>/dev/null | awk '/^default/ {print $5; exit}')
    [ -z "$ifc" ] && ifc=$(ip -o -4 addr 2>/dev/null | awk '{print $2}' | head -1)
    [ -z "$ifc" ] && return 1
    cidr=$(ip -o -4 addr show dev "$ifc" 2>/dev/null | awk '{print $4}' | head -1)
    base=$(echo "$cidr" | awk -F/ '{print $1}' | awk -F. '{print $1"."$2"."$3}')
    for last in $(seq 1 254); do
        cand="${base}.${last}"
        if ssh -o ConnectTimeout=0.4 -o BatchMode=yes -o StrictHostKeyChecking=no \
               "$(ssh_target "$cand")" true 2>/dev/null; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

deploy_over_ssh() {
    target_ip=$1
    dest="$(ssh_target "$target_ip"):$PATHS_DATA_DIR"
    echo "rsync -> $dest"
    # The ports tree is often on a FAT or exFAT volume (vfat here), which has
    # no concept of POSIX owners or permission bits: -a tries to preserve
    # both, and the receiver fails with "chown ... Operation not permitted".
    rsync_opts="-rlt --no-owner --no-group --no-perms --info=progress2"
    for b in spectrax spectrax_bench instrument_harness; do
        p=$(find_binary "$b") || return 1
        # shellcheck disable=SC2086
        rsync $rsync_opts "$p" "$dest/" || return 1
    done
    for s in $port_scripts; do
        # shellcheck disable=SC2086
        rsync $rsync_opts "scripts/portmaster/$s" "$dest/$s" || return 1
    done
    if [ -d bin/resources ]; then
        # shellcheck disable=SC2086
        rsync $rsync_opts bin/resources/ "$dest/data/" || return 1
    fi
    # The style and theme files, and a minimal cfg.json. resolveConfigDir()
    # falls back to the cwd only when no config dir holds a cfg.json, and
    # the styles are then never loaded -- the app would run on baked
    # defaults with no layouts. Ship them so a deployed build is styled.
    for j in layout.json clr.json; do
        [ -f "bin/$j" ] || continue
        # shellcheck disable=SC2086
        rsync $rsync_opts "bin/$j" "$dest/$j" || return 1
    done
    if [ -f bin/cfg.json ]; then
        # Never clobber a cfg.json the device already has: it may carry the
        # user's own settings from a previous run.
        if ! ssh "$(ssh_target "$target_ip")" "test -f '$dest/cfg.json'" 2>/dev/null; then
            # shellcheck disable=SC2086
            rsync $rsync_opts bin/cfg.json "$dest/cfg.json" || return 1
        fi
    fi
    # The device-side launch wrappers read their paths and the GPU helper
    # from this fragment. Write it to the writable data partition: the
    # rootfs on ROCKNIX (and on most of these handheld CFWs) is a read-only
    # squashfs, so /etc is not writable. The wrappers check /etc first (the
    # GKD has a writable /etc) and then this path.
    ssh "$(ssh_target "$target_ip")" "cat > '$PATHS_DATA_DIR/device.toml' <<'EOF'
data_dir = \"$PATHS_DATA_DIR\"
runs_dir = \"$PATHS_RUNS_DIR\"
gpu_helper = \"$GPU_HELPER\"
gpu_driver = \"$GPU_DRIVER\"
EOF" || return 1
    if [ "$launch" = 1 ]; then
        remote_out="${out_dir:-$PATHS_RUNS_DIR/$(date -u +%Y%m%dT%H%M%SZ)}"
        echo "ssh-launch on $target_ip -> $remote_out"
        ssh "$(ssh_target "$target_ip")" "mkdir -p '$remote_out' && cd '$PATHS_DATA_DIR' && ./spectrax_bench --bench=audio --out='$remote_out'"
        if [ -n "$out_dir" ]; then
            mkdir -p "$out_dir"
            rsync -a "$(ssh_target "$target_ip"):$remote_out/" "$out_dir/"
            echo "pulled to $out_dir"
        fi
    fi
    return 0
}

dev_ip=""
used=""
for t in $TRANSPORT_ORDER; do
    [ -z "$force_transport" ] || [ "$t" = "$force_transport" ] || continue
    case "$t" in
        usb-gadget) found=$(try_usb_gadget) && { dev_ip=$found; used=$t; break; } ;;
        wifi-ssh)   found=$(try_wifi_ssh)   && { dev_ip=$found; used=$t; break; } ;;
        sd)         echo "transport 'sd' selected: mount the card and use scripts/deploy-sd.sh"; used=$t; break ;;
        *) echo "unknown transport in profile: $t" >&2; exit 1 ;;
    esac
done

[ -n "$used" ] || {
    echo "no transport available for $DEVICE_ID" >&2
    echo "mount the SD card and run: scripts/deploy-sd.sh <mount-point> --device $DEVICE_ID" >&2
    exit 1
}
[ "$used" != "sd" ] || exit 0

echo "transport: $used ($dev_ip)"
deploy_over_ssh "$dev_ip"
echo "deployed to $DEVICE_ID via $used"
