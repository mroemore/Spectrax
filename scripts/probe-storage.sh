#!/bin/sh
# probe-storage.sh — characterise the writable volume (SD card) throughput.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "mounts"
probe_run "df -h 2>/dev/null | head -20"
probe_heading "card identity"
probe_run "cat /sys/block/mmcblk*/device/{name,cid,manfid,oemid,serial} 2>/dev/null || true"
probe_heading "write throughput"
probe_run "mkdir -p /tmp/probe-storage && dd if=/dev/zero of=/tmp/probe-storage/blob bs=1M count=32 conv=fsync 2>&1 | tail -3"
probe_heading "read throughput"
probe_run "dd if=/tmp/probe-storage/blob of=/dev/null bs=1M 2>&1 | tail -3"
probe_run "rm -rf /tmp/probe-storage"

echo
echo "### suggested profile updates"
mounts=$(probe_run "df -h 2>/dev/null | awk 'NR>1{print \$NF}' | tr -d '\r' | tr '\n' ' '")
echo "  mounts: $mounts"
