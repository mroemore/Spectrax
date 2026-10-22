#!/bin/sh
# probe-boot.sh — measure the time from power-on to a usable SSH endpoint.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

echo "### boot timing"
echo "  power the device on now"
start=$(date +%s)
while [ $(( $(date +%s) - start )) -lt 180 ]; do
    if dev_ip=$(scripts/discover-device.sh --device "$probe_device" 2>/dev/null); then
        end=$(date +%s)
        echo "  reachable after $((end - start))s at $dev_ip"
        exit 0
    fi
    sleep 2
done
echo "  not reachable within 180s"
exit 1
