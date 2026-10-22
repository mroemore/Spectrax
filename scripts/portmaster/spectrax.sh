#!/bin/sh
# PortMaster / EmulationStation launch wrapper for spectrax.
#
# The device-side profile fragment (paths + gpu helper) is read from
# /etc/spectrax-device.toml, falling back to device.toml beside the
# binaries -- most of these CFWs mount a read-only squashfs at /, so the
# deploy writes into the data partition instead.
set -eu

conf=/etc/spectrax-device.toml
data_dir=/roms/ports/spectrax
gpu_helper=""
gpu_driver=""

[ -r "$conf" ] || { [ -r "$data_dir/device.toml" ] && conf="$data_dir/device.toml"; }

if [ -r "$conf" ]; then
    data_dir=$(sed -n 's/^data_dir *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_helper=$(sed -n 's/^gpu_helper *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_driver=$(sed -n 's/^gpu_driver *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    [ -n "$data_dir" ] || data_dir=/roms/ports/spectrax
fi

if [ -n "$gpu_helper" ] && [ -x "$gpu_helper" ]; then
    "$gpu_helper" "$gpu_driver" 2>/dev/null || true
fi

cd "$data_dir" || exit 1
exec ./spectrax "$@"
