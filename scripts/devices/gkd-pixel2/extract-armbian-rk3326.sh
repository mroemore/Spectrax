#!/bin/sh
# extract-armbian-rk3326.sh <img> <out-dir>
# Pulls the first EXT4 partition out of an Armbian .img into out-dir/.
#
# GKD Pixel 2 only: the Tier C system-emulation tier needs an Armbian
# RK3326 rootfs. There is no Armbian H700 image, so the RG35XX SP has no
# equivalent. This script lives under scripts/devices/gkd-pixel2/ and is
# put on PATH by scripts/lib/profile.sh for that device only.
set -eu

img=${1:-}; out=${2:-}
[ -z "$img" ] || [ -z "$out" ] && { echo "usage: $0 <img> <out-dir>" >&2; exit 64; }
[ -f "$img" ] || { echo "not a file: $img" >&2; exit 1; }

mkdir -p "$out"

# Find the first ext4 partition offset.
start=$(parted -s "$img" unit B print | awk '/ext4/ {gsub("B",""); print $2; exit}')
[ -z "$start" ] && { echo "no ext4 partition found" >&2; exit 1; }
echo "ext4 starts at $start bytes"

mount=$(mktemp -d)
mount -o loop,offset=$start,ro "$img" "$mount"
trap 'umount "$mount" 2>/dev/null || true; rmdir "$mount"' EXIT

rsync -a "$mount/" "$out/"
echo "extracted to $out ($(du -sh "$out" | cut -f1))"
