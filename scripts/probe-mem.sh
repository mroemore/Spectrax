#!/bin/sh
# probe-mem.sh — resolve the RAM size and type and the free memory.
#
# Resolves: device.ram_mb, device.ram_type
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "meminfo"
probe_run "head -5 /proc/meminfo"
probe_heading "dram nodes"
probe_run "ls -d /sys/devices/platform/*dram* 2>/dev/null || echo '(no dram platform node)'"
probe_heading "dram notes"
probe_run "dmesg 2>/dev/null | grep -i -E 'dram|memory|ddr' | tail -20 || true"
probe_heading "free"
probe_run "free -m 2>/dev/null || true"

echo
echo "### suggested profile updates"
total=$(probe_run "awk '/MemTotal/{print \$2}' /proc/meminfo" | tr -d '\r')
case "$total" in
    ''|*[!0-9]*) ;;
    *) probe_apply device ram_mb "$((total / 1024))" ;;
esac
ddr=$(probe_run "dmesg 2>/dev/null | grep -i -E 'ddr[234]|lpddr' | head -1" | tr -d '\r')
if [ -n "$ddr" ]; then
    case "$ddr" in
        *LPDDR*|*lpddr*) probe_apply device ram_type LPDDR4 ;;
        *DDR3*|*ddr3*) probe_apply device ram_type DDR3 ;;
        *DDR4*|*ddr4*) probe_apply device ram_type DDR4 ;;
    esac
    probe_resolve ram_type
fi
