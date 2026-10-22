#!/bin/sh
# test_device_profile.sh — device-profile loader, merger and linter contract.
set -eu

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

fail() { echo "FAIL: $*" >&2; exit 1; }

# Scratch space stays inside the repository (see .tmp_files/ in .gitignore).
tmp_root="${SPECTRAX_TMPDIR:-$repo_root/.tmp_files}"
mkdir -p "$tmp_root"
tmp=$(mktemp -d "$tmp_root/test_device_profile.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# Every committed profile must lint clean (non-strict: probes may be pending).
for id in $(scripts/lib/profile.py --list); do
    scripts/lib/profile.py --lint "$id" || fail "profile $id does not lint"
    scripts/lib/profile.py --shell "$id" >/dev/null || fail "profile $id does not export"
done

# The GKD profile must carry the values the GKD spec states.
[ "$(scripts/lib/profile.py --get gkd-pixel2 device.cpu_core)" = "cortex-a35" ] \
    || fail "gkd-pixel2 cpu_core"
[ "$(scripts/lib/profile.py --get gkd-pixel2 paths.ports_dir)" = "/roms/ports" ] \
    || fail "gkd-pixel2 ports_dir"
[ "$(scripts/lib/profile.py --get gkd-pixel2 cfw.password_source)" = "fixed" ] \
    || fail "gkd-pixel2 password_source"

# The linter must reject a profile with an unknown key.
mkdir -p "$tmp/devices"
cp devices/gkd-pixel2.toml "$tmp/devices/badprofile.toml"
sed -i 's/^id        = "gkd-pixel2"/id        = "badprofile"\nbogus_key = 1/' \
    "$tmp/devices/badprofile.toml"
if SPECTRAX_DEVICE_DIR="$tmp/devices" scripts/lib/profile.py --lint badprofile; then
    fail "linter accepted an unknown key"
fi

# The linter must reject a profile that is not valid TOML, without a traceback.
printf 'this is not toml\n' > "$tmp/devices/broken.toml"
if SPECTRAX_DEVICE_DIR="$tmp/devices" scripts/lib/profile.py --lint broken 2>&1 \
   | grep -q 'Traceback'; then
    fail "a malformed profile produced a Python traceback"
fi

# The linter must reject a profile whose id does not match its file name.
cp devices/gkd-pixel2.toml "$tmp/devices/mismatched.toml"
sed -i 's/^id        = "gkd-pixel2"/id        = "somethingelse"/' \
    "$tmp/devices/mismatched.toml"
if SPECTRAX_DEVICE_DIR="$tmp/devices" scripts/lib/profile.py --lint mismatched; then
    fail "linter accepted a mismatched [device].id"
fi

# The linter must reject a profile whose probe list is non-empty under --strict.
if scripts/lib/profile.py --lint gkd-pixel2 --strict; then
    fail "linter accepted unresolved probes under --strict"
fi

# The generated cross file must carry the profile's CPU flags.
out=$(scripts/gen-cross-file.sh gkd-pixel2 --out "$tmp/crossout")
grep -q -- "-mcpu=cortex-a35" "$out" || fail "generated cross file lacks -mcpu"
grep -q "spectrax_libdir = 'lib/aarch64'" "$out" || fail "generated cross file lacks libdir"
grep -q "spectrax_incdir = 'include/aarch64'" "$out" || fail "generated cross file lacks incdir"

# The setup script must branch on cfw.password_source, not hardcode a password.
grep -q "CFW_PASSWORD_SOURCE" scripts/setup-device.sh || fail "setup-device.sh does not branch on password_source"
grep -q "fixed" scripts/setup-device.sh || fail "setup-device.sh lacks the fixed-password branch"

echo "test_device_profile: ok"
