#!/bin/sh
# probe-audio.sh — resolve the ALSA card, the routes and the native sample rates.
#
# Resolves: the ALSA device used by the bench and by the app's audio path.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "sound cards"
probe_run "cat /proc/asound/cards 2>/dev/null || echo '(no /proc/asound/cards)'"
probe_heading "playback devices"
probe_run "command -v aplay >/dev/null 2>&1 && aplay -l || echo '(no aplay)'"
probe_heading "pcm devices"
probe_run "command -v aplay >/dev/null 2>&1 && aplay -L | head -30 || true"
probe_heading "mixer (the codec's analog mux shows up here)"
probe_run "command -v amixer >/dev/null 2>&1 && amixer -c 0 contents | head -40 || echo '(no amixer)'"
probe_heading "sample rates"
probe_run "command -v aplay >/dev/null 2>&1 && aplay --dump-hw-params -D default 2>&1 | grep -A3 'Sample Rate' || true"

echo
echo "### suggested profile updates"
card=$(probe_run "cat /proc/asound/cards 2>/dev/null | head -1" | tr -d '\r')
if [ -n "$card" ]; then
    echo "  ALSA card: $card"
    probe_resolve audio_device
fi
