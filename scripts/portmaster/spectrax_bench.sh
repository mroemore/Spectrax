#!/bin/sh
# PortMaster / EmulationStation launch wrapper for spectrax_bench.
set -eu

conf=/etc/spectrax-device.toml
data_dir=/roms/ports/spectrax
runs_dir=""
gpu_helper=""
gpu_driver=""

if [ -r "$conf" ]; then
    data_dir=$(sed -n 's/^data_dir *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    runs_dir=$(sed -n 's/^runs_dir *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_helper=$(sed -n 's/^gpu_helper *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_driver=$(sed -n 's/^gpu_driver *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    [ -n "$data_dir" ] || data_dir=/roms/ports/spectrax
fi

if [ -n "$gpu_helper" ] && [ -x "$gpu_helper" ]; then
    "$gpu_helper" "$gpu_driver" 2>/dev/null || true
fi

out_dir="${1:-${runs_dir:-/roms/ports/spectrax/runs}/$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p "$out_dir"
cd "$data_dir" || exit 1
exec ./spectrax_bench --bench=audio --out="$out_dir"
