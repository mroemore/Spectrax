#!/bin/sh
# probe-os-level.sh — the OS knobs that bound Spectrax's performance.
#
# Reports the preconditions every benchmark depends on, so two runs can be
# compared honestly: CPU governor and per-core clocks, the GPU devfreq
# governor and OPP, IRQ affinity (the SDIO WiFi IRQ is a real competitor for
# the core the audio callback lands on), the RT priority limit, zram, and
# the thermal state. It reports; it never asserts.
#
# With --apply it records the values into the host-local profile so a later
# benchmark knows which environment produced its numbers.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_run '
echo "== cpu =="
for c in /sys/devices/system/cpu/cpu[0-9]*; do
    [ -d "$c" ] || continue
    n=$(basename "$c")
    gov=$(cat "$c/cpufreq/scaling_governor" 2>/dev/null || echo -)
    cur=$(cat "$c/cpufreq/scaling_cur_freq" 2>/dev/null || echo -)
    max=$(cat "$c/cpufreq/scaling_max_freq" 2>/dev/null || echo -)
    printf "%s gov=%s cur=%s max=%s\n" "$n" "$gov" "$cur" "$max"
done
printf "govs=%s\n" "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors 2>/dev/null)"
printf "opps=%s\n" "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies 2>/dev/null)"

echo "== gpu =="
for d in /sys/class/devfreq/*.gpu; do
    [ -d "$d" ] || continue
    printf "cur=%s\n" "$(cat "$d/cur_freq" 2>/dev/null)"
    printf "opps=%s\n" "$(cat "$d/available_frequencies" 2>/dev/null)"
    printf "gov=%s\n" "$(cat "$d/governor" 2>/dev/null)"
    printf "govs=%s\n" "$(cat "$d/available_governors" 2>/dev/null)"
done

echo "== thermal =="
for z in /sys/class/thermal/thermal_zone*; do
    [ -e "$z" ] || continue
    printf "%s %s\n" "$(cat "$z/type" 2>/dev/null)" "$(cat "$z/temp" 2>/dev/null)"
done

echo "== irq (top 5 busiest) =="
grep -E "sunxi-mmc|GICv2" /proc/interrupts 2>/dev/null | awk "{print \$1, \$NF, \$2, \$3, \$4, \$5}" | head -5

echo "== sched =="
printf "rt_runtime_us=%s\n" "$(cat /proc/sys/kernel/sched_rt_runtime_us 2>/dev/null)"
printf "autogroup=%s\n" "$(cat /proc/sys/kernel/sched_autogroup_enabled 2>/dev/null)"
printf "rtprio_limit=%s\n" "$(ulimit -r 2>/dev/null)"

echo "== memory =="
grep -E "MemTotal|MemAvailable" /proc/meminfo
printf "swaps=%s\n" "$(cat /proc/swaps 2>/dev/null | tail -n +2 | wc -l)"
printf "zram_loaded=%s\n" "$(lsmod 2>/dev/null | grep -c "^zram" || echo 0)"

echo "== kernel =="
printf "preempt=%s\n" "$(zcat /proc/config.gz 2>/dev/null | grep -E "^CONFIG_PREEMPT=" | cut -d= -f2)"
printf "hz=%s\n" "$(zcat /proc/config.gz 2>/dev/null | grep -E "^CONFIG_HZ=" | cut -d= -f2)"
' | tee "${TMPDIR:-.}/os-level.txt"

# --apply records the governor and GPU OPP actually in force, since those are
# the two knobs that move a benchmark most and are the ones a tuning run
# changes. The rest is context rather than a profile fact.
if [ "$probe_apply" = 1 ]; then
    gov=$(grep -m1 '^cpu0 gov=' "${TMPDIR:-.}/os-level.txt" 2>/dev/null | sed 's/.*gov=\([^ ]*\).*/\1/')
    gpu_gov=$(grep -m1 '^gov=' "${TMPDIR:-.}/os-level.txt" 2>/dev/null | sed 's/^gov=//')
    [ -n "${gov:-}" ] && probe_apply system cpu_governor "$gov"
    [ -n "${gpu_gov:-}" ] && probe_apply system gpu_governor "$gpu_gov"
fi
