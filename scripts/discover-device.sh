#!/bin/sh
# discover-device.sh — locate a device on the LAN (or over the USB gadget link).
#
# Usage: discover-device.sh [--device ID]
# Prints the device IP on stdout.
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
[ -n "$device" ] || { echo "--device is required (or set SPECTRAX_DEVICE)" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

reachable() {
    ssh -o ConnectTimeout=3 -o BatchMode=yes -o StrictHostKeyChecking=no \
        "$CFW_USER@$1" true 2>/dev/null
}

# 0) The IP recorded by setup-device.sh, if it is still reachable. This is
# the common case -- the device is on the same lease it had last time --
# and it costs one connect instead of a 254-host sweep.
if [ -n "${TRANSPORT_LAST_IP:-}" ]; then
    if reachable "$TRANSPORT_LAST_IP"; then
        echo "$TRANSPORT_LAST_IP"
        exit 0
    fi
fi

# 1) USB gadget link-local address.
if [ -n "$TRANSPORT_USB_GADGET_HOST_IP" ]; then
    if ip addr show 2>/dev/null | grep -q "$TRANSPORT_USB_GADGET_HOST_IP/"; then
        if reachable "$TRANSPORT_USB_GADGET_HOST_IP"; then
            echo "$TRANSPORT_USB_GADGET_HOST_IP"
            exit 0
        fi
    fi
fi

# 2) mDNS / hostname.
for name in $TRANSPORT_MDNS_NAMES; do
    cand=$(getent hosts "$name" 2>/dev/null | awk '{print $1}' | head -1)
    if [ -n "$cand" ] && reachable "$cand"; then
        echo "$cand"
        exit 0
    fi
done

# 3) Subnet scan.
ifc=$(ip route 2>/dev/null | awk '/^default/ {print $5; exit}')
[ -z "$ifc" ] && ifc=$(ip -o -4 addr 2>/dev/null | awk '{print $2}' | head -1)
[ -z "$ifc" ] && { echo "error: no network interface" >&2; exit 1; }
cidr=$(ip -o -4 addr show dev "$ifc" 2>/dev/null | awk '{print $4}' | head -1)
base=$(echo "$cidr" | awk -F/ '{print $1}' | awk -F. '{print $1"."$2"."$3}')
echo "scanning ${base}.0/24 for SSH..." >&2
for last in $(seq 1 254); do
    cand="${base}.${last}"
    if reachable "$cand"; then
        echo "$cand"
        exit 0
    fi
done
echo "error: no reachable $DEVICE_ID found on ${base}.0/24" >&2
exit 1
