#!/bin/sh
# probe-lid.sh — determine what closing the lid does to a running process.
#
# The RG35XX SP is a clamshell. The mainline device tree defines a lid switch
# (SW_LID on GPIO PE7). ROCKNIX has no hardware suspend, so closing the lid
# triggers "fake suspend": the screen and inputs go away and a timed shutdown
# may follow.
#
# This probe starts a sleeper on the device, asks the operator to close and
# reopen the lid, and reports whether the process survived.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "lid switch input device"
probe_run "grep -R . /proc/bus/input/devices 2>/dev/null | grep -A3 -i lid || echo '(no lid switch found)'"
probe_heading "evdev"
probe_run "ls -l /dev/input/ 2>/dev/null || true"

echo
echo "### live lid test"
echo "  starting a 120s sleeper on the device, then close and reopen the lid"
probe_run "nohup sleep 120 >/dev/null 2>&1 & echo started"
sleep 3
echo "  close the lid now (10s), then reopen it"
sleep 10
if probe_run "pgrep -f 'sleep 120' >/dev/null && echo alive || echo gone" | grep -q alive; then
    echo "  result: the process survived the lid close"
else
    echo "  result: the process did NOT survive the lid close"
    probe_run "dmesg 2>/dev/null | tail -20"
fi
probe_resolve lid_switch
