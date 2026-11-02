#!/bin/sh
# setup-device.sh — first-time host setup for a device.
#
# Discovers the device, installs the host SSH key and records the IP in the
# host-local profile override.
#
# Credential handling depends on the profile's cfw.password_source:
#   fixed         the profile carries the password (ROGUE OS on the GKD)
#   user-set      the user has already set a stable root password on the
#                 device (ROCKNIX: System Settings -> set root password)
#   device-display the password rotates every boot and must be typed in
#                 (ROCKNIX default) -- the script prompts for it
#
# Usage: setup-device.sh [--device ID] [--password PW]
set -eu

device=${SPECTRAX_DEVICE:-}
password=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --password) shift; password="${1:-}" ;;
        --password=*) password="${1#--password=}" ;;
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

dev_ip=$(scripts/discover-device.sh --device "$device") || {
    echo "error: device not discovered" >&2
    echo "       check that it is powered on and reachable (see scripts/discover-device.sh)" >&2
    exit 1
}
echo "discovered $dev_ip"

ssh-keygen -t ed25519 -N '' -f "$HOME/.ssh/id_ed25519" 2>/dev/null || true

case "$CFW_PASSWORD_SOURCE" in
    fixed)
        [ -n "$password" ] || password=$CFW_PASSWORD
        [ -n "$password" ] || { echo "error: no password for a fixed-password device" >&2; exit 1; }
        command -v sshpass >/dev/null 2>&1 || {
            echo "error: sshpass is required (xbps-install -S sshpass)" >&2; exit 1; }
        sshpass -p "$password" ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$CFW_USER@$dev_ip"
        ;;
    user-set)
        ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$CFW_USER@$dev_ip"
        ;;
    device-display)
        if [ -z "$password" ]; then
            printf "root password for %s (shown on the device screen): " "$DEVICE_ID" >&2
            stty -echo 2>/dev/null || true
            read -r password
            stty echo 2>/dev/null || true
        fi
        command -v sshpass >/dev/null 2>&1 || {
            echo "error: sshpass is required (xbps-install -S sshpass)" >&2; exit 1; }
        sshpass -p "$password" ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$CFW_USER@$dev_ip"
        ;;
    *)
        echo "error: unknown password_source: $CFW_PASSWORD_SOURCE" >&2; exit 1 ;;
esac

mkdir -p "$HOME/.config/spectrax/devices"
cat > "$HOME/.config/spectrax/devices/$device.toml" <<EOF
[transport]
last_ip = "$dev_ip"
EOF
echo "wrote $HOME/.config/spectrax/devices/$device.toml"
