#!/bin/sh
# probe-paths.sh — resolve the ports directory and the storage layout.
#
# Resolves: paths.storage_root, paths.ports_dir
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "candidate ports directories"
probe_run "for d in /roms/ports /storage/roms/ports /roms /storage; do
    if [ -e \"\$d\" ]; then printf '%s -> %s\\n' \"\$d\" \"\$(readlink -f \"\$d\")\"; fi
  done"
probe_heading "mounts"
probe_run "cat /proc/mounts | grep -E 'storage|roms|sdcard|mmc' || true"
probe_heading "PortMaster"
probe_run "ls -d /storage/.config/PortMaster /roms/ports/PortMaster 2>/dev/null || echo '(no PortMaster dir)'"
probe_heading "emulationstation ports section"
probe_run "ls -d /storage/roms/ports /roms/ports 2>/dev/null || true"

echo
echo "### suggested profile updates"
for cand in /storage/roms/ports /roms/ports; do
    if probe_run "test -d $cand && echo yes" | grep -q yes; then
        probe_apply paths storage_root "$(dirname "$(dirname "$cand")")"
        probe_apply paths ports_dir "$cand"
        probe_apply paths data_dir "$cand/spectrax"
        probe_apply paths runs_dir "$cand/spectrax/runs"
        probe_resolve ports_dir
        break
    fi
done
