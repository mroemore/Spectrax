#!/bin/sh
# probe.sh — shared helpers for the probe scripts.
#
# Sourced by scripts/probe-*.sh. A probe reports what it finds; it never
# asserts and never fails the build. Exit 0 when it ran, 2 on a connection
# failure.
set -eu

probe_device=""
probe_ip=""
probe_local=0
probe_apply=0

probe_parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --device) shift; probe_device="${1:-$probe_device}" ;;
            --device=*) probe_device="${1#--device=}" ;;
            --ip) shift; probe_ip="${1:-$probe_ip}" ;;
            --ip=*) probe_ip="${1#--ip=}" ;;
            --local) probe_local=1 ;;
            --apply) probe_apply=1 ;;
            *) echo "unknown arg: $1" >&2; exit 64 ;;
        esac
        shift
    done
    [ -n "$probe_device" ] || probe_device=${SPECTRAX_DEVICE:-}
    [ -n "$probe_device" ] || { echo "--device is required" >&2; exit 64; }
}

probe_repo_root() {
    # $0 is unchanged by sourcing in POSIX sh, so it names the calling probe
    # script (which lives in scripts/). Walk up from the cwd as a fallback so
    # the helpers also work when sourced from elsewhere.
    d=$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)
    if [ -z "$d" ] || [ ! -f "$d/scripts/lib/profile.py" ]; then
        d=$PWD
        while [ -n "$d" ] && [ "$d" != "/" ]; do
            if [ -f "$d/scripts/lib/profile.py" ]; then break; fi
            d=$(dirname "$d")
        done
    fi
    cd "$d" || exit 1
    export SPECTRAX_REPO_ROOT="$d"
    pwd
}

probe_run() {
    # $1 = command to run on the device (or locally)
    if [ "$probe_local" = 1 ]; then
        sh -c "$1" 2>&1
    else
        ssh -o ConnectTimeout=5 -o BatchMode=yes "$CFW_USER@$probe_ip" "$1" 2>&1
    fi
}

probe_heading() {
    echo "### $1"
}

# probe_apply <section> <key> <value>
# Writes the value into the host-local profile override. Without --apply it
# only reports what it would write.
probe_apply() {
    [ "$probe_apply" = 1 ] || { echo "  -> $1.$2 = $3 (not applied; pass --apply)"; return 0; }
    dir="$HOME/.config/spectrax/devices"
    mkdir -p "$dir"
    file="$dir/$probe_device.toml"
    [ -f "$file" ] || printf '' > "$file"
    python3 - "$file" "$1" "$2" "$3" <<'PY'
import re, sys
path, section, key, value = sys.argv[1:5]
text = open(path).read()
block = "[%s]\n" % section
if block not in text:
    if not text.endswith("\n"):
        text += "\n"
    text += "\n" + block
pat = re.compile(r'(?m)^%s\s*=.*$' % re.escape(key))
line = '%s = "%s"' % (key, value)
text, n = pat.subn(line, text)
if n == 0:
    text = text.replace(block, block + line + "\n")
open(path, "w").write(text)
PY
    echo "  -> $1.$2 = $3 (applied)"
}

# probe_resolve <probe-key>
# Drops a probe key from the [probes] needed list, so the profile stops
# asking for a fact that has been established.
probe_resolve() {
    [ "$probe_apply" = 1 ] || { echo "  -> probe '$1' resolved (not applied; pass --apply)"; return 0; }
    remaining=$(scripts/lib/profile.py --probes-needed "$probe_device" | grep -v "^$1$" || true)
    dir="$HOME/.config/spectrax/devices"
    mkdir -p "$dir"
    file="$dir/$probe_device.toml"
    [ -f "$file" ] || printf '' > "$file"
    python3 - "$file" "$remaining" <<'PY'
import re, sys
path, remaining = sys.argv[1], sys.argv[2]
keys = [k for k in remaining.split("\n") if k]
text = open(path).read()
block = "[probes]\n"
if block not in text:
    if text and not text.endswith("\n"):
        text += "\n"
    text += "\n" + block
pat = re.compile(r'(?ms)^\[probes\]\n(?:[^\[]*)')
body = "".join('needed = [%s]\n' % ", ".join('"%s"' % k for k in keys) if keys else "needed = []\n")
text = pat.sub(block + body, text)
open(path, "w").write(text)
PY
    echo "  -> probe '$1' resolved (applied)"
}
