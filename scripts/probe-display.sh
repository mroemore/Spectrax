#!/bin/sh
# probe-display.sh — resolve the panel mode, the compositor scale and the GL API.
#
# Resolves: display.width, display.height, display.scale, display.native,
#           gpu.driver, gpu.api
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "drm modes"
probe_run "cat /sys/class/drm/card0/modes 2>/dev/null | head -5 || echo '(no drm card0)'"
probe_heading "drm connector"
probe_run "cat /sys/class/drm/card0-HDMI-A-1/modes 2>/dev/null; cat /sys/class/drm/card0-DSI-1/modes 2>/dev/null || true"
probe_heading "compositor output"
probe_run "command -v swaymsg >/dev/null 2>&1 && swaymsg -g outputs || echo '(no swaymsg)'"
probe_heading "GL renderer"
probe_run "command -v es2_info >/dev/null 2>&1 && es2_info || echo '(no es2_info)'"
probe_heading "egl vendor"
probe_run "dmesg 2>/dev/null | grep -i -E 'panfrost|mali|drm|gpu' | tail -20 || true"

echo
echo "### suggested profile updates"
mode=$(probe_run "cat /sys/class/drm/card0-DSI-1/modes 2>/dev/null | head -1" | tr -d '\r')
if [ -n "$mode" ]; then
    w=$(echo "$mode" | cut -d'x' -f1)
    h=$(echo "$mode" | cut -d'x' -f2)
    probe_apply display width "$w"
    probe_apply display height "$h"
    probe_resolve display_mode
fi
renderer=$(probe_run "command -v es2_info >/dev/null 2>&1 && es2_info | grep -i renderer | head -1" | tr -d '\r')
case "$renderer" in
    *panfrost*) probe_apply gpu driver panfrost; probe_resolve gpu_driver ;;
    *mali*|*Mali*) probe_apply gpu driver libmali; probe_resolve gpu_driver ;;
esac
