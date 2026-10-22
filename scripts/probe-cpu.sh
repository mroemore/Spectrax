#!/bin/sh
# probe-cpu.sh — resolve the clock, the governor and the available OPPs.
#
# Resolves: device.cpu_mhz
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "cpufreq"
probe_run "for c in /sys/devices/system/cpu/cpu[0-9]*/cpufreq; do
    [ -d \"\$c\" ] || continue
    echo \"\$c\"
    for f in scaling_driver scaling_governor scaling_available_frequencies scaling_max_freq scaling_cur_freq; do
        [ -r \"\$c/\$f\" ] && echo \"  \$f = \$(cat \$c/\$f)\"
    done
  done"
probe_heading "cpuinfo"
probe_run "grep -m1 'model name' /proc/cpuinfo 2>/dev/null; grep -c '^processor' /proc/cpuinfo"
probe_heading "clock notes"
probe_run "dmesg 2>/dev/null | grep -i -E 'cpufreq|opp|clock' | tail -20 || true"

echo
echo "### suggested profile updates"
mhz=$(probe_run "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null" | tr -d '\r')
case "$mhz" in
    ''|*[!0-9]*) ;;
    *) probe_apply device cpu_mhz "$((mhz / 1000))" ;;
esac
gov=$(probe_run "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null" | tr -d '\r')
if [ -n "$gov" ]; then
    echo "  governor: $gov"
    probe_resolve cpu_governor
fi
