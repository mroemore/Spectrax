#!/bin/sh
# probe-gpu.sh — report the GPU driver state and whether a GL context works.
#
# Extends the GKD plan's gpu-check.sh. The branch is selected by the
# profile's gpu.driver: 'libmali' probes the Mali userspace library,
# 'panfrost' probes the Panfrost kernel driver.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "os-release"
probe_run "cat /etc/os-release 2>/dev/null || true"
probe_heading "uname"
probe_run "uname -a"
probe_heading "drm devices"
probe_run "ls -l /dev/dri 2>/dev/null || echo '(no /dev/dri)'"
probe_heading "gpu helper script"
probe_run "ls /usr/bin/*gpu* /usr/local/bin/*gpu* 2>/dev/null || echo '(no gpu helper script)'"
probe_heading "loaded modules"
probe_run "lsmod 2>/dev/null | grep -iE 'mali|panfrost|drm|gpu' || echo '(lsmod unavailable)'"

case "$GPU_DRIVER" in
    panfrost)
        probe_heading "panfrost"
        probe_run "dmesg 2>/dev/null | grep -i panfrost | tail -10 || true"
        probe_run "ls -l /sys/kernel/debug/dri/0/panfrost 2>/dev/null || true"
        ;;
    libmali)
        probe_heading "libmali"
        probe_run "ls /usr/lib/libmali* /usr/lib/aarch64-linux-gnu/libmali* 2>/dev/null || echo '(no libmali)'"
        probe_run "cat /usr/bin/setgpu.sh 2>/dev/null || echo '(no setgpu.sh)'"
        ;;
esac

probe_heading "EGL / GLES libraries"
probe_run "ls /usr/lib/*/libEGL* /usr/lib/*/libGLESv2* /usr/lib/libEGL* /usr/lib/libGLESv2* 2>/dev/null || echo '(no EGL/GLESv2 libs)'"
probe_heading "GL info"
probe_run "command -v es2_info >/dev/null 2>&1 && es2_info | head -30 || echo '(no es2_info)'"
probe_heading "DRM/GBM"
probe_run "ls /usr/lib/*/libgbm* /usr/lib/libgbm* 2>/dev/null || echo '(no libgbm)'"
probe_heading "egl vendor files"
probe_run "ls /usr/share/glvnd/egl_vendor.d/ 2>/dev/null || echo '(no egl_vendor.d)'"
probe_heading "connector mode"
probe_run "for f in /sys/class/drm/card*-*/modes; do [ -r \"\$f\" ] && echo \"\$f: \$(head -1 \$f)\"; done"

echo
echo "### suggested profile updates"
helper=$(probe_run "ls /usr/bin/*gpu* /usr/local/bin/*gpu* 2>/dev/null | head -1" | tr -d '\r')
if [ -n "$helper" ]; then
    probe_apply gpu helper "$helper"
    probe_resolve gpu_helper
fi
