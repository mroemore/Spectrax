#!/bin/sh
# profile.sh — source a device profile into the current shell.
#
#   SPECTRAX_DEVICE=<id> ; . scripts/lib/profile.sh
#
# Exports DEVICE_*, CFW_*, DISPLAY_*, GPU_*, PATHS_*, TRANSPORT_*,
# BUILD_* and PROBES_* variables, and prepends scripts/devices/<id> to PATH.
#
# The profile always loads, even when [probes] needed is non-empty: the
# probes themselves need to load the profile to find the device, so refusing
# here would deadlock the first-run workflow. The deploy scripts are the ones
# that refuse to act on unresolved facts (deploy-spectrax.sh), or warn loudly
# (deploy-sd.sh). Set SPECTRAX_STRICT_PROBES=1 to make this loader refuse too.
#
# This file is SOURCED, so it must not change the caller's shell options
# (no `set -e`/`set -u` here) and must return rather than exit.
#
# The device id arrives through the ENVIRONMENT, not through an argument:
# dash (which is /bin/sh on Void) does not pass arguments to a sourced file,
# so `. profile.sh <id>` silently gives $1 the caller's first argument.
# $1 is honoured as a fallback for bash users.
#
# $0 is unchanged by sourcing in POSIX sh, so it still names the calling
# script -- that is what locates this library and the repo root.

device=${SPECTRAX_DEVICE:-${1:-}}
if [ -z "$device" ]; then
    echo "usage: SPECTRAX_DEVICE=<id> ; . profile.sh" >&2
    return 1 2>/dev/null || exit 1
fi

# A directory is the repo root when it holds both the loader and the profiles.
profile_is_repo() {
    [ -f "$1/scripts/lib/profile.py" ] && [ -d "$1/devices" ]
}

repo_root=${SPECTRAX_REPO_ROOT:-}
if [ -z "$repo_root" ]; then
    repo_root=$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)
fi
if [ -z "$repo_root" ] || ! profile_is_repo "$repo_root"; then
    # Fallback: walk up from the current directory looking for the repo
    # marker (scripts/lib/profile.py next to a devices/ directory).
    d=$PWD
    while [ -n "$d" ] && [ "$d" != "/" ]; do
        if profile_is_repo "$d"; then
            repo_root=$d
            break
        fi
        d=$(dirname "$d")
    done
fi
if [ -z "$repo_root" ] || ! profile_is_repo "$repo_root"; then
    echo "profile.sh: cannot locate the repo root" >&2
    return 1 2>/dev/null || exit 1
fi
lib="$repo_root/scripts/lib/profile.py"

if ! "$lib" --lint "$device"; then
    echo "profile $device does not lint" >&2
    return 1 2>/dev/null || exit 1
fi

pending=$("$lib" --probes-needed "$device")
if [ -n "$pending" ] && [ "${SPECTRAX_STRICT_PROBES:-0}" = "1" ]; then
    echo "profile $device has unresolved probes:" >&2
    echo "$pending" | sed 's/^/  - /' >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1090
eval "$("$lib" --shell "$device")"

case ":$PATH:" in
    *":$repo_root/scripts/devices/$device:"*) ;;
    *) PATH="$repo_root/scripts/devices/$device:$PATH" ;;
esac
export PATH
