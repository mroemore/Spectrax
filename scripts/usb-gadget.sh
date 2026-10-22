#!/bin/sh
# usb-gadget.sh — inspect or drive the device's USB gadget.
#
# Usage: usb-gadget.sh --device ID [--ip IP] [--mode network|file_transfer|disabled]
#
# Without --mode it reports the device's current gadget mode, the gadget
# address, and whether the host sees a matching NIC with the expected host
# address.
set -eu

device=${SPECTRAX_DEVICE:-}
ip=""
mode=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --ip) shift; ip="${1:-$ip}" ;;
        --ip=*) ip="${1#--ip=}" ;;
        --mode) shift; mode="${1:-$mode}" ;;
        --mode=*) mode="${1#--mode=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done
[ -n "$device" ] || { echo "--device is required" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

# The gadget link uses fixed link-local addresses, so the device side is
# known from the profile. A LAN scan is not wanted here.
[ -n "$ip" ] || ip=$TRANSPORT_USB_GADGET_DEVICE_IP
if [ -z "$ip" ]; then
    echo "no gadget address in the profile; pass --ip" >&2
    exit 1
fi

if [ -n "$mode" ]; then
    [ -n "$ip" ] || { echo "no device IP; pass --ip" >&2; exit 1; }
    echo "setting gadget mode to $mode on $ip"
    ssh "$CFW_USER@$ip" "usbgadget $mode"
    exit $?
fi

echo "=== device side ==="
if [ -n "$ip" ]; then
    ssh -o ConnectTimeout=5 "$CFW_USER@$ip" \
        "usbgadget 2>/dev/null; echo address=\$(usbgadget address 2>/dev/null)" || echo "(ssh unavailable)"
else
    echo "(no device IP yet)"
fi

echo
echo "=== host side ==="
host_ip=$TRANSPORT_USB_GADGET_HOST_IP
if [ -n "$host_ip" ]; then
    if ip addr show 2>/dev/null | grep -q "$host_ip/"; then
        echo "host has $host_ip"
        ip -br addr | grep "$host_ip/" || true
    else
        echo "host does NOT have $host_ip -- the gadget link is not up"
        echo "expected a usb0 or gadget interface; got:"
        ip -br addr | grep -E 'usb|gadget' || echo "  (none)"
    fi
fi
