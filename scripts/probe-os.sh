#!/bin/sh
# probe-os.sh — identify the CFW, the userspace ABI and the credential policy.
#
# Resolves: cfw.family, device.arch, cfw.hostname, cfw.password_source,
#           cfw.ssh_enabled_by_default
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "os-release"
probe_run "cat /etc/os-release 2>/dev/null || echo '(no /etc/os-release)'"
probe_heading "uname"
probe_run "uname -a; echo arch=\$(uname -m)"
probe_heading "hostname"
probe_run "hostname 2>/dev/null; cat /etc/hostname 2>/dev/null"
probe_heading "root account"
probe_run "getent passwd root 2>/dev/null || echo '(no root in passwd)'"
probe_heading "sshd"
probe_run "ps 2>/dev/null | grep -c '[s]shd' || true"
probe_heading "adb"
probe_run "command -v adb || ls /usr/bin/adb* 2>/dev/null || echo '(no adb)'"
probe_heading "storage roots"
probe_run "ls -ld /roms /storage 2>/dev/null || true"

echo
echo "### suggested profile updates"
arch=$(probe_run "uname -m" | tr -d '\r' | head -1)
[ -n "$arch" ] && probe_apply device arch "$arch"
[ -n "$arch" ] && probe_resolve userspace_arch
hostname=$(probe_run "hostname" | head -1 | tr -d '\r')
[ -n "$hostname" ] && probe_apply cfw hostname "$hostname"
