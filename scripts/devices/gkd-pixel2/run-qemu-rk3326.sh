#!/bin/sh
# run-qemu-rk3326.sh — boot an extracted Armbian RK3326 rootfs in qemu-system-aarch64.
# Usage: run-qemu-rk3326.sh <rootfs-dir>
#
# GKD Pixel 2 only: QEMU has no H700 machine model, so the RG35XX SP has no
# Tier C equivalent. This script lives under scripts/devices/gkd-pixel2/ and
# is put on PATH by scripts/lib/profile.sh for that device only.
#
# Starter: it needs tuning to boot a specific Armbian image. The rootfs
# directory must hold boot/vmlinuz-*, boot/initrd.img-* and the rootfs image
# itself (armbian_rootfs.img).
set -eu

rootfs=${1:-}
[ -z "$rootfs" ] && { echo "usage: $0 <rootfs-dir>" >&2; exit 64; }

kernel=$(ls "$rootfs"/boot/vmlinuz-* 2>/dev/null | head -1)
initrd=$(ls "$rootfs"/boot/initrd.img-* 2>/dev/null | head -1)
dtb="$rootfs/boot/dtb/rk3326-gkd-pixel2.dtb"
[ -z "$kernel" ] && { echo "no vmlinuz in $rootfs/boot" >&2; exit 1; }
[ -z "$initrd" ] && { echo "no initrd in $rootfs/boot" >&2; exit 1; }
[ -f "$dtb" ] || dtb=$(find "$rootfs" -name '*.dtb' | head -1)

qemu-system-aarch64 \
    -M virt -cpu cortex-a35 -smp 4 -m 1024 \
    -kernel "$kernel" -initrd "$initrd" -dtb "$dtb" \
    -append "root=/dev/vda rw console=ttyAMA0 earlycon" \
    -drive file="$rootfs/armbian_rootfs.img",format=raw,if=virtio \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net,netdev=net0 \
    -device virtio-gpu-pci \
    -display gtk
