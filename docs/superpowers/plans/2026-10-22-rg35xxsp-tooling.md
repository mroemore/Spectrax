# Anbernic RG35XX SP Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the shared Spectrax device-target rig (device profiles, profile-generated meson cross files, one `deploy-spectrax.sh` with a transport ladder, one `spectrax_bench`, one probe suite) and plug **two** devices into it: the GKD Pixel 2 (retrofit, keeps working) and the Anbernic RG35XX SP (new).

**Architecture:** A device is a TOML profile in `devices/`. `scripts/lib/profile.py` merges, validates and exports it. `scripts/gen-cross-file.sh` turns a profile into a meson cross file (`build-<device>/cross.ini`) carrying the CPU flags, the target lib directory and the raylib platform. One `deploy-spectrax.sh` walks an ordered `transport` list (`usb-gadget` → `wifi-ssh` → `sd`) from the profile. One `spectrax_bench` takes `--device/--display/--workdir/--gpu` so the same binary measures the right thing on both machines. A suite of `probe-*.sh` scripts fills in the facts a profile cannot know until it meets hardware.

**Tech Stack:** C11 (gnu99 per project), meson 1.x + ninja, raylib 5.5 (vendored, `lib/<arch>/`), PortAudio (vendored per-arch), cJSON (`third_party/cjson/`), POSIX `sh` + Python 3 (host-side tooling only), QEMU 11 (host-installed), GKD Pixel 2 (RK3326S) and Anbernic RG35XX SP (Allwinner H700).

**Branch:** `rg35xxsp-tooling` (already created from `main`, spec committed). All commits local until the user explicitly approves a push (project rule #1078).

**Companion documents:**
- Spec: `docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md`
- Sibling (GKD Pixel 2): `docs/superpowers/specs/2026-09-20-gkd-pixel2-tooling-design.md` and `docs/superpowers/plans/2026-09-20-gkd-pixel2-tooling.md` on branch `gkd-pixel2-tooling`.

---

## Global Constraints

- **Build system:** meson + ninja. `meson install -C build` copies binaries into `bin/` for cwd-relative resources (project rule #1312). Tests via `meson test -C build`.
- **Vendored libs:** static `libraylib.a` + `libkissfft-float.a` in `lib/<arch>/`; `portaudio` is vendored per-arch under `lib/<arch>/` (the `lib/arm/` set is the existing precedent — `libraylib.a`, `libportaudio.a`, `libasound.a`). `third_party/cjson/` is committed.
- **Per-arch lib directory:** `meson.build` must resolve the lib dir from the cross file's `spectrax_libdir` property, defaulting to `lib/linux` for native builds. **Never hardcode `lib/linux` in a target build.**
- **Target lib bundles are committed** (`lib/aarch64/`, `include/aarch64/`), exactly as `lib/arm/` and `include/arm/` are today. `lib/` and `include/` are not gitignored.
- **Test runner:** `meson test -C build` runs all `tests/dsp/test_*.c`. New `tests/dsp/test_bench_report.c` joins the suite. Shell/Python tests are registered the way the GKD plan registers `tests/dsp/test_qemu_smoke.sh`: `test('name', find_program('dsp/<file>.sh'), …)`.
- **LSP noise:** clangd reports `kiss_fft.h not found` because the LSP include path is incomplete; ignore — meson is the source of truth (project convention).
- **All testing/diagnostic code is opt-in via CLI flag** (project rule #1136). The bench binary is its own target, not embedded into the main `spectrax` binary.
- **Cross-compile flags (every cross-compile task):**
  - aarch64 / GKD Pixel 2: `-mcpu=cortex-a35 -mtune=cortex-a35 -O3 -ftree-vectorize -ffast-math -flto`
  - aarch64 / RG35XX SP: `-mcpu=cortex-a53 -mtune=cortex-a53 -O3 -ftree-vectorize -ffast-math -flto`
  - armhf (GKD fallback only): `-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O3 -ftree-vectorize -ffast-math -flto`
  - **These come from the device profile, not from the cross file.**
- **raylib flags (every target build):** `GRAPHICS_API_OPENGL_ES2`, `PLATFORM=PLATFORM_DRM` (DRM/KMS + GBM + EGL — the only raylib platform that talks to a handheld's display without X11).
- **Bench output dir layout:** `<out>/runs/<UTC-timestamp>/{summary.json, timeseries.csv, console.log, hostinfo.txt}`.
- **Device profile rules:**
  - A device is a TOML file under `devices/`. **Never fork a script per device.**
  - Committed facts live in `devices/<id>.toml`. Secrets, last-known IP and local build-dir overrides live in `~/.config/spectrax/devices/<id>.toml` and are never committed.
  - `scripts/lib/profile.py --lint <id>` must pass before any deploy script runs.
  - A profile key whose value depends on hardware must be listed in `[probes] needed` until a probe has filled it in. `scripts/lib/profile.py --lint --strict` fails while the list is non-empty; `scripts/deploy-spectrax.sh` refuses to deploy while it is non-empty; `deploy-sd.sh` warns but proceeds (the SD path is the bootstrap path).
- **Device-specific escape hatch:** genuinely device-specific scripts live under `scripts/devices/<id>/` and are added to `PATH` by `scripts/lib/profile.sh`. There are exactly two of them today (`gkd-pixel2`), both RK3326 system-emulation helpers. Anything else belongs in the shared `scripts/`.
- **No code comments unless they explain non-obvious behavior.** Project rule on minimal comments.
- **Project rule #1078:** never push to origin without explicit user approval. All commits stay local on `rg35xxsp-tooling`.

## File Structure

**Created (all paths relative to repo root):**
```
devices/
  gkd-pixel2.toml
  rg35xxsp.toml
devices/schema/
  device-profile.schema.md
scripts/
  lib/profile.py
  lib/profile.sh
  gen-cross-file.sh
  build-target-libs.sh
  discover-device.sh
  setup-device.sh
  deploy-spectrax.sh
  deploy-sd.sh
  inspect-sd.sh
  show-runs.sh
  gpu-check.sh
  portmaster/spectrax.sh
  portmaster/spectrax_bench.sh
  devices/gkd-pixel2/extract-armbian-rk3326.sh
  devices/gkd-pixel2/run-qemu-rk3326.sh
  probe-os.sh
  probe-paths.sh
  probe-usb.sh
  probe-display.sh
  probe-audio.sh
  probe-cpu.sh
  probe-mem.sh
  probe-lid.sh
  probe-boot.sh
  probe-storage.sh
src/tools/bench/
  meson.build
  main.c
  bench_report.h
  bench_report.c
  bench_xvfb.h
  bench_xvfb.c
  bench_audio.h
  bench_audio.c
  bench_fileio.h
  bench_fileio.c
  bench_gfx.h
  bench_gfx.c
tests/dsp/
  test_bench_report.c
  test_qemu_smoke.sh
  test_device_profile.sh
docs/superpowers/plans/RG35XXSP-RUNBOOK.md
```

**Modified:**
- `meson.build` (cross-file property lookup for the lib dir; bench subdir registration)
- `meson/cross/aarch64-linux-gnu.ini` (arch-generic template + `[properties]`)
- `meson/cross/arm-linux-gnueabihf.ini` (arch-generic template + `[properties]`)
- `tests/meson.build` (register `test_bench_report`, `test_qemu_smoke`, `test_device_profile`)
- `.gitignore` (add `build-<device>/`, `runs/`)
- `src/tools/bench/*` (created here; the GKD plan creates the same files — the two plans share the bench implementation, see Phase B note)

**Note on overlap with the GKD plan.** The GKD plan (branch `gkd-pixel2-tooling`) already specifies `src/tools/bench/*`, `tests/dsp/test_bench_report.c` and `tests/dsp/test_qemu_smoke.sh` in detail, and those files do not exist yet. This plan **does not re-specify them from scratch**; it adds the device-parameterisation layer on top and states the shared base in the *Interfaces* sections. When the two branches meet, the base comes from whichever lands first and the parameterisation is applied once. Where this plan gives code, it is the **device-aware** version.

---

# Phase A — The shared profile and build layer

*Free → low risk. Nothing here touches a device. Every phase boundary is a safe stopping point.*

---

### Task 1: Port the GKD cross files and turn them into arch-generic templates

**Files:**
- Create (from `gkd-pixel2-tooling`): `meson/cross/aarch64-linux-gnu.ini`, `meson/cross/arm-linux-gnueabihf.ini`

**Interfaces:**
- Consumes: nothing.
- Produces: two meson cross-file **templates** with no device-specific `-mcpu`, plus a `[properties]` section carrying `spectrax_libdir` and `spectrax_platform`.

**Background.** Commit `9648016` on `gkd-pixel2-tooling` added the two cross files with `-mcpu=cortex-a35` baked in. That is a device fact, not an architecture fact, so it has to move into the profile (Task 2) and be injected at setup time (Task 3). The committed file becomes the arch-generic base.

- [ ] **Step 1: Cherry-pick the cross files from the GKD branch**

```bash
git cherry-pick 9648016
```

Expected: two new files under `meson/cross/`, plus the `.gitignore` additions from that commit (`build-aarch64/`, `build-armhf/`, `runs/`). If the cherry-pick conflicts on `.gitignore` (the file differs between branches), resolve by keeping both sides' additions.

- [ ] **Step 2: Rewrite `meson/cross/aarch64-linux-gnu.ini` as a template**

```ini
# Template only. Device-specific flags (-mcpu/-mtune), the target lib dir and
# the raylib platform are injected by scripts/gen-cross-file.sh from the
# device profile. Do not add device facts to this file.
[binaries]
c = 'aarch64-linux-gnu-gcc'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[properties]
spectrax_libdir = 'lib/aarch64'
spectrax_platform = 'drm'

[built-in options]
c_args = ['-O3', '-ftree-vectorize', '-ffast-math', '-flto']
c_link_args = ['-flto']
```

- [ ] **Step 3: Rewrite `meson/cross/arm-linux-gnueabihf.ini` as a template**

```ini
[binaries]
c = 'arm-linux-gnueabihf-gcc'
ar = 'arm-linux-gnueabihf-ar'
strip = 'arm-linux-gnueabihf-strip'

[host_machine]
system = 'linux'
cpu_family = 'arm'
cpu = 'armv7'
endian = 'little'

[properties]
spectrax_libdir = 'lib/arm'
spectrax_platform = 'drm'

[built-in options]
c_args = ['-march=armv7-a', '-mfpu=neon-vfpv4', '-mfloat-abi=hard',
          '-O3', '-ftree-vectorize', '-ffast-math', '-flto']
c_link_args = ['-flto']
```

- [ ] **Step 4: Verify meson accepts the property block**

```bash
meson setup build-aarch64 --cross-file meson/cross/aarch64-linux-gnu.ini --buildtype=release 2>&1 | tail -5
```

Expected: a clean configure. `meson introspect --buildoptions build-aarch64` should not error.

- [ ] **Step 5: Verify the property is readable**

```bash
meson introspect build-aarch64 --buildoptions >/dev/null && echo "introspect ok"
```

Expected: `introspect ok`. (The property is read by `meson.get_external_property()`; Task 4 exercises it in the build itself.)

- [ ] **Step 6: Commit**

```bash
git add meson/cross/aarch64-linux-gnu.ini meson/cross/arm-linux-gnueabihf.ini .gitignore
git commit -m "build: arch-generic meson cross templates with per-device property block"
```

---

### Task 2: Device-profile schema, loader, linter, and the GKD profile

**Files:**
- Create: `devices/schema/device-profile.schema.md`
- Create: `devices/gkd-pixel2.toml`
- Create: `scripts/lib/profile.py`
- Create: `tests/dsp/test_device_profile.sh`

**Interfaces:**
- `scripts/lib/profile.py --list` — print one known profile id per line.
- `scripts/lib/profile.py --shell <id>` — print `KEY=value` shell assignments for a fixed allow-list of keys, merged with `~/.config/spectrax/devices/<id>.toml`. Exit 2 if the profile fails to lint.
- `scripts/lib/profile.py --get <id> <section.key>` — print one value.
- `scripts/lib/profile.py --lint <id>` — validate; exit 0 if valid, 1 otherwise with a diagnostic on stderr.
- `scripts/lib/profile.py --probes-needed <id>` — print the unresolved `[probes] needed` keys, one per line.

**Background.** The whole point of this plan is that the device is data. This task defines the data. The schema is deliberately flat and shallow so a POSIX-sh consumer can read it without a TOML library.

- [ ] **Step 1: Write `devices/schema/device-profile.schema.md`**

```markdown
# Device profile schema

A device profile is a TOML file under `devices/`. The host-local override
`~/.config/spectrax/devices/<id>.toml` is merged on top, key by key, and is
never committed.

Required sections and keys:

## [device]
id         string   slug; must equal the file stem
vendor     string
model      string
soc        string
arch       string   aarch64 | armhf
cpu_core   string   e.g. cortex-a53
cpu_mhz    int
cores      int
gpu        string
ram_mb     int
ram_type   string
storage    string

## [cfw]
family          string   rocknix | rogue | garlic | knulli | minui | anbernic-stock
hostname        string
user            string
password        string   may be empty
password_source string   fixed | user-set | device-display
ssh_enabled_by_default  bool

## [display]
width   int
height  int
scale   float
native  bool

## [gpu]
driver      string   panfrost | libmali | lima
api         string   gles2 | gles3
helper      string   optional GPU-switch helper path; may be empty
compositor  string   sway | x11
platform    string   raylib PLATFORM_ suffix, e.g. drm

## [paths]
storage_root string
ports_dir    string
data_dir     string
runs_dir     string

## [transport]
order                        array of strings; subset of usb-gadget, wifi-ssh, sd
mdns_names                   array of strings
usb_gadget_host_ip           string
usb_gadget_device_ip         string

## [build]
cross_file string   repo-relative path to the arch cross template
libdir     string   repo-relative target lib directory
c_args     array of strings

## [probes]
needed  array of strings   profile keys still awaiting a probe

The linter rejects: unknown top-level sections, unknown keys inside a known
section, a missing required key, an empty `device.id`, `device.arch` not in
{aarch64, armhf}, `transport.order` containing an unknown transport, a
`transport.order` entry of `usb-gadget` with no `usb_gadget_host_ip`, and a
`[probes] needed` list that is non-empty when `--strict` is passed.
```

- [ ] **Step 2: Write `devices/gkd-pixel2.toml`**

```toml
# GKD Pixel 2 — RK3326S, Cortex-A35, Mali-G31 MP2, 1 GB DDR3, 640x480 IPS.
# CFW: ROGUE OS (Ninoh-FOX), ROCKNIX-derived, EmulationStation + PortMaster.
# Facts carried over from docs/superpowers/specs/2026-09-20-gkd-pixel2-tooling-design.md.
# Hostname/user/password are the GKD spec's stated values; the password is
# still to be confirmed by scripts/probe-os.sh.

[device]
id        = "gkd-pixel2"
vendor    = "GKD"
model     = "Pixel 2"
soc       = "RK3326S"
arch      = "aarch64"
cpu_core  = "cortex-a35"
cpu_mhz   = 1500
cores     = 4
gpu       = "Mali-G31 MP2"
ram_mb    = 1024
ram_type  = "DDR3"
storage   = "microSD"

[cfw]
family         = "rogue"
hostname       = "rogue_pixel2"
user           = "root"
password       = "rogue"
password_source = "fixed"
ssh_enabled_by_default = true

[display]
width  = 640
height = 480
scale  = 1.0
native = true

[gpu]
driver     = "libmali"
api        = "gles2"
helper     = "/usr/bin/setgpu.sh"
compositor = "sway"
platform   = "drm"

[paths]
storage_root = "/roms"
ports_dir    = "/roms/ports"
data_dir     = "/roms/ports/spectrax"
runs_dir     = "/roms/ports/spectrax/runs"

[transport]
order       = ["wifi-ssh", "sd"]
mdns_names  = ["rogue_pixel2.local"]
usb_gadget_host_ip   = ""
usb_gadget_device_ip = ""

[build]
cross_file = "meson/cross/aarch64-linux-gnu.ini"
libdir     = "lib/aarch64"
c_args     = ["-mcpu=cortex-a35", "-mtune=cortex-a35", "-O3",
              "-ftree-vectorize", "-ffast-math", "-flto"]

[probes]
needed = ["userspace_arch", "ports_dir", "gpu_driver", "gpu_helper"]
```

- [ ] **Step 3: Write `scripts/lib/profile.py`**

```python
#!/usr/bin/env python3
"""Device profile loader, merger and linter for Spectrax target devices.

A device is data: this module reads devices/<id>.toml, merges the host-local
override ~/.config/spectrax/devices/<id>.toml on top of it, validates the
result against the schema in devices/schema/device-profile.schema.md, and
exports it for shell consumers.
"""
import argparse
import os
import re
import sys

try:
    import tomllib
except ModuleNotFoundError:
    print("profile.py: python 3.11+ (tomllib) is required", file=sys.stderr)
    raise

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEVICE_DIR = os.environ.get("SPECTRAX_DEVICE_DIR",
                            os.path.join(REPO_ROOT, "devices"))
OVERRIDE_DIR = os.path.expanduser("~/.config/spectrax/devices")

SCHEMA = {
    "device": {
        "id": str, "vendor": str, "model": str, "soc": str, "arch": str,
        "cpu_core": str, "cpu_mhz": int, "cores": int, "gpu": str,
        "ram_mb": int, "ram_type": str, "storage": str,
    },
    "cfw": {
        "family": str, "hostname": str, "user": str, "password": str,
        "password_source": str, "ssh_enabled_by_default": bool,
    },
    "display": {"width": int, "height": int, "scale": float, "native": bool},
    "gpu": {
        "driver": str, "api": str, "helper": str, "compositor": str,
        "platform": str,
    },
    "paths": {
        "storage_root": str, "ports_dir": str, "data_dir": str,
        "runs_dir": str,
    },
    "transport": {
        "order": list, "mdns_names": list, "usb_gadget_host_ip": str,
        "usb_gadget_device_ip": str,
    },
    "build": {"cross_file": str, "libdir": str, "c_args": list},
    "probes": {"needed": list},
}

REQUIRED = {
    "device": ["id", "vendor", "model", "soc", "arch", "cpu_core", "cpu_mhz",
               "cores", "gpu", "ram_mb", "ram_type", "storage"],
    "cfw": ["family", "hostname", "user", "password", "password_source",
            "ssh_enabled_by_default"],
    "display": ["width", "height", "scale", "native"],
    "gpu": ["driver", "api", "helper", "compositor", "platform"],
    "paths": ["storage_root", "ports_dir", "data_dir", "runs_dir"],
    "transport": ["order", "mdns_names", "usb_gadget_host_ip",
                  "usb_gadget_device_ip"],
    "build": ["cross_file", "libdir", "c_args"],
    "probes": ["needed"],
}

ARCHES = {"aarch64", "armhf"}
TRANSPORTS = {"usb-gadget", "wifi-ssh", "sd"}
PASSWORD_SOURCES = {"fixed", "user-set", "device-display"}
GPU_DRIVERS = {"panfrost", "libmali", "lima"}
GPU_APIS = {"gles2", "gles3"}
COMPOSITORS = {"sway", "x11"}


def _read(path):
    try:
        with open(path, "rb") as fh:
            return tomllib.load(fh)
    except tomllib.TOMLDecodeError as exc:
        raise SystemExit("profile.py: %s is not valid TOML: %s" % (path, exc))
    except OSError as exc:
        raise SystemExit("profile.py: cannot read %s: %s" % (path, exc))


def _merge(base, over):
    out = {k: dict(v) for k, v in base.items()}
    for section, values in over.items():
        out.setdefault(section, {}).update(values)
    return out


def load(device_id):
    base_path = os.path.join(DEVICE_DIR, device_id + ".toml")
    if not os.path.isfile(base_path):
        raise SystemExit("profile.py: no such profile: " + base_path)
    data = _read(base_path)
    over_path = os.path.join(OVERRIDE_DIR, device_id + ".toml")
    if os.path.isfile(over_path):
        data = _merge(data, _read(over_path))
    return data


def lint(data, strict=False, expected_id=None):
    errors = []
    for section in data:
        if section not in SCHEMA:
            errors.append("unknown section [%s]" % section)
            continue
        for key in data[section]:
            if key not in SCHEMA[section]:
                errors.append("unknown key [%s].%s" % (section, key))
    for section, keys in REQUIRED.items():
        present = data.get(section, {})
        for key in keys:
            if key not in present:
                errors.append("missing [%s].%s" % (section, key))
                continue
            want = SCHEMA[section][key]
            value = present[key]
            if want is int and isinstance(value, bool):
                errors.append("[%s].%s must be an int" % (section, key))
            elif want is not bool and isinstance(value, bool):
                errors.append("[%s].%s must be %s" % (section, key, want.__name__))
            elif want is float and isinstance(value, int):
                pass
            elif want is not list and not isinstance(value, want):
                errors.append("[%s].%s must be %s" % (section, key, want.__name__))
    dev = data.get("device", {})
    if dev.get("arch") not in ARCHES:
        errors.append("[device].arch must be one of %s" % sorted(ARCHES))
    if expected_id and dev.get("id") and dev["id"] != expected_id:
        errors.append("[device].id is %r but the file is %r"
                      % (dev["id"], expected_id))
    cfw = data.get("cfw", {})
    if cfw.get("password_source") not in PASSWORD_SOURCES:
        errors.append("[cfw].password_source must be one of %s"
                      % sorted(PASSWORD_SOURCES))
    if cfw.get("password_source") == "fixed" and not cfw.get("password"):
        errors.append("[cfw].password_source is 'fixed' but password is empty")
    gpu = data.get("gpu", {})
    if gpu.get("driver") not in GPU_DRIVERS:
        errors.append("[gpu].driver must be one of %s" % sorted(GPU_DRIVERS))
    if gpu.get("api") not in GPU_APIS:
        errors.append("[gpu].api must be one of %s" % sorted(GPU_APIS))
    if gpu.get("compositor") not in COMPOSITORS:
        errors.append("[gpu].compositor must be one of %s" % sorted(COMPOSITORS))
    tr = data.get("transport", {})
    order = tr.get("order", [])
    for entry in order:
        if entry not in TRANSPORTS:
            errors.append("[transport].order contains unknown transport %r" % entry)
    if "usb-gadget" in order and not tr.get("usb_gadget_host_ip"):
        errors.append("[transport].order has usb-gadget but no usb_gadget_host_ip")
    if strict and data.get("probes", {}).get("needed"):
        errors.append("[probes].needed is not empty: %s"
                      % ", ".join(data["probes"]["needed"]))
    return errors


def shell_key(section, key):
    return "%s_%s" % (section.upper(), key.upper())


def shell_value(value, sep=" "):
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, list):
        return sep.join(str(v) for v in value)
    return str(value)


def shell_quote(text):
    """Quote a value so that `eval` in POSIX sh reproduces it exactly."""
    return "'" + text.replace("'", "'\\''") + "'"


def emit_shell(data):
    # Lists are space separated, except [probes] needed which is newline
    # separated so it can be printed one key per line.
    for section, values in data.items():
        sep = "\n" if section == "probes" else " "
        for key, value in values.items():
            print("%s=%s" % (shell_key(section, key),
                            shell_quote(shell_value(value, sep))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--shell")
    ap.add_argument("--get")
    ap.add_argument("--lint")
    ap.add_argument("--probes-needed")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("value", nargs="?")
    args = ap.parse_args()

    if args.list:
        for name in sorted(os.listdir(DEVICE_DIR)):
            if name.endswith(".toml"):
                print(name[:-5])
        return 0

    if args.lint:
        errors = lint(load(args.lint), strict=args.strict,
                      expected_id=args.lint)
        for err in errors:
            print("profile %s: %s" % (args.lint, err), file=sys.stderr)
        return 1 if errors else 0

    if args.probes_needed:
        for key in load(args.probes_needed).get("probes", {}).get("needed", []):
            print(key)
        return 0

    if args.get:
        data = load(args.get)
        section, _, key = args.value.partition(".")
        if section not in data or key not in data[section]:
            print("profile.py: no such key: %s" % args.value, file=sys.stderr)
            return 2
        print(shell_value(data[section][key]))
        return 0

    if args.shell:
        data = load(args.shell)
        errors = lint(data, expected_id=args.shell)
        if errors:
            for err in errors:
                print("profile %s: %s" % (args.shell, err), file=sys.stderr)
            return 2
        emit_shell(data)
        return 0

    ap.print_help()
    return 64


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Make it executable and smoke-test the loader**

```bash
chmod +x scripts/lib/profile.py
scripts/lib/profile.py --list
scripts/lib/profile.py --lint gkd-pixel2
scripts/lib/profile.py --get gkd-pixel2 device.cpu_core
scripts/lib/profile.py --shell gkd-pixel2 | head -20
```

Expected: `--list` prints `gkd-pixel2`; `--lint` exits 0; `--get` prints `cortex-a35`; `--shell` prints `DEVICE_ID='gkd-pixel2'`, `DEVICE_ARCH='aarch64'`, `TRANSPORT_ORDER='wifi-ssh sd'`, `BUILD_C_ARGS='-mcpu=cortex-a35 -mtune=cortex-a35 …'`. **Values are single-quoted** so that `eval` survives values with spaces (the GPU name, the flag list, the transport list).

- [ ] **Step 5: Write `tests/dsp/test_device_profile.sh`**

```sh
#!/bin/sh
# test_device_profile.sh — device-profile loader, merger and linter contract.
set -eu

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

fail() { echo "FAIL: $*" >&2; exit 1; }

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
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
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

echo "test_device_profile: ok"
```

- [ ] **Step 6: Register the test in `tests/meson.build`**

Append near the other `test(...)` registrations:
```meson
test('test_device_profile',
  find_program('dsp/test_device_profile.sh'),
  args: [],
)
```

- [ ] **Step 7: Run the test**

```bash
chmod +x tests/dsp/test_device_profile.sh
meson setup build --reconfigure >/dev/null
meson test -C build test_device_profile
```

Expected: `Ok: 1  Fail: 0`.

- [ ] **Step 8: Commit**

```bash
git add devices/ scripts/lib/profile.py tests/dsp/test_device_profile.sh tests/meson.build
git commit -m "tooling: device-profile schema, loader, linter and the GKD Pixel 2 profile"
```

---

### Task 3: `gen-cross-file.sh` — profile to meson cross file

**Files:**
- Create: `scripts/gen-cross-file.sh`

**Interfaces:**
- `scripts/gen-cross-file.sh <device-id> [--out DIR]` — reads the profile, writes `<DIR>/cross.ini` (default `build-<device-id>`), prints the path on stdout. Exits 1 if the profile does not lint.
- The generated file contains: the template's `[binaries]`, `[host_machine]`, `[properties]`; the profile's `build.c_args` in `c_args`; `c_link_args` carrying `-flto`; and `spectrax_libdir` / `spectrax_platform` from the profile.

**Background.** `-mcpu=cortex-a35` is a device fact. The committed cross file is an arch template; this script injects the device's facts into a generated copy so meson gets exactly one cross file.

- [ ] **Step 1: Write `scripts/gen-cross-file.sh`**

```sh
#!/bin/sh
# gen-cross-file.sh — generate a meson cross file for a device profile.
# Usage: gen-cross-file.sh <device-id> [--out DIR]
# Writes DIR/cross.ini and prints its path.
set -eu

device=${1:-}
[ -n "$device" ] || { echo "usage: $0 <device-id> [--out DIR]" >&2; exit 64; }
shift

out="build-$device"
while [ $# -gt 0 ]; do
    case "$1" in
        --out) shift; out="${1:-$out}" ;;
        --out=*) out="${1#--out=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

lib="$repo_root/scripts/lib/profile.py"
[ -x "$lib" ] || { echo "$lib missing" >&2; exit 1; }

"$lib" --lint "$device" || { echo "profile $device does not lint" >&2; exit 1; }

template=$("$lib" --get "$device" build.cross_file)
[ -f "$template" ] || { echo "cross template not found: $template" >&2; exit 1; }

libdir=$("$lib" --get "$device" build.libdir)
platform=$("$lib" --get "$device" gpu.platform)
c_args=$("$lib" --get "$device" build.c_args)

mkdir -p "$out"
target="$out/cross.ini"

{
    echo "# Generated by scripts/gen-cross-file.sh from devices/$device.toml."
    echo "# Do not edit: regenerate instead."
    # Strip the template's [properties] and [built-in options] sections; they
    # are re-emitted below from the profile.
    awk '/^\[/ { skip = ($0 == "[properties]" || $0 == "[built-in options]") }
         !skip { print }' "$template"
    echo ""
    echo "[properties]"
    echo "spectrax_libdir = '$libdir'"
    echo "spectrax_platform = '$platform'"
    echo ""
    echo "[built-in options]"
    printf "c_args = ['%s']\n" "$(printf "%s" "$c_args" | sed "s/ /', '/g")"
    echo "c_link_args = ['-flto']"
} > "$target"

echo "$target"
```

- [ ] **Step 2: Make it executable and generate for the GKD profile**

```bash
chmod +x scripts/gen-cross-file.sh
scripts/gen-cross-file.sh gkd-pixel2
cat build-gkd-pixel2/cross.ini
```

Expected: the file contains `spectrax_libdir = 'lib/aarch64'`, `spectrax_platform = 'drm'`, and `c_args = ['-mcpu=cortex-a35', '-mtune=cortex-a35', '-O3', '-ftree-vectorize', '-ffast-math', '-flto']`.

- [ ] **Step 3: Verify meson accepts the generated file**

```bash
meson setup build-gkd-pixel2 --cross-file build-gkd-pixel2/cross.ini --buildtype=release 2>&1 | tail -3
meson introspect build-gkd-pixel2 --buildoptions >/dev/null && echo "introspect ok"
```

Expected: clean configure, `introspect ok`.

- [ ] **Step 4: Add a regression check to `tests/dsp/test_device_profile.sh`**

Append before the final `echo`:
```sh
# The generated cross file must carry the profile's CPU flags.
out=$(scripts/gen-cross-file.sh gkd-pixel2 --out "$tmp/crossout")
grep -q -- "-mcpu=cortex-a35" "$out" || fail "generated cross file lacks -mcpu"
grep -q "spectrax_libdir = 'lib/aarch64'" "$out" || fail "generated cross file lacks libdir"
```

- [ ] **Step 5: Run the test**

```bash
meson test -C build test_device_profile
```

Expected: `Ok: 1  Fail: 0`.

- [ ] **Step 6: Commit**

```bash
git add scripts/gen-cross-file.sh tests/dsp/test_device_profile.sh
git commit -m "build: generate meson cross files from device profiles"
```

---

### Task 4: `meson.build` reads the target lib dir from the cross file

**Files:**
- Modify: `meson.build`

**Interfaces:**
- Consumes: the cross file's `spectrax_libdir` property (default `lib/linux`).
- Produces: `libdir`, the directory meson searches for `raylib`, `kissfft-float` and `portaudio`.

**Background.** Today `meson.build` hardcodes `lib/linux`, so any cross build links x86-64 archives. The repo already ships an armhf set in `lib/arm/`; the property indirection is what lets a profile select it (or `lib/aarch64`).

- [ ] **Step 1: Patch the libdir lookup in `meson.build`**

Replace:
```meson
libdir = meson.current_source_dir() / 'lib' / 'linux'
```
with:
```meson
# The target lib directory comes from the cross file (spectrax_libdir) and
# falls back to the host bundle for native builds. Native files may also
# carry the property, so a native build can be pointed at an arm bundle.
spectrax_libdir = meson.get_external_property('spectrax_libdir', 'lib/linux')
libdir = meson.current_source_dir() / spectrax_libdir
```

- [ ] **Step 2: Verify the native build still finds its libraries**

```bash
meson setup build --reconfigure >/dev/null
meson compile -C build 2>&1 | grep -cE "error:"
```

Expected: `0`. And `ls build/libspectrax_core.a` exists.

- [ ] **Step 3: Verify the cross build resolves the target directory**

```bash
meson setup build-gkd-pixel2 --cross-file build-gkd-pixel2/cross.ini --reconfigure 2>&1 | tail -3
meson introspect build-gkd-pixel2 --buildoptions >/dev/null && echo ok
```

Expected: meson reports looking for raylib under `lib/aarch64`. (Until Task 5 lands that directory, the configure step will warn that `raylib` was not found — that is expected and is exactly what Task 5 fixes.)

- [ ] **Step 4: Commit**

```bash
git add meson.build
git commit -m "build: resolve the target lib directory from the cross-file property"
```

---

### Task 5: Build the aarch64 target lib bundle (raylib DRM/GLES2, PortAudio ALSA, kissfft)

**Files:**
- Create: `scripts/build-target-libs.sh`
- Create (generated, then committed): `lib/aarch64/libraylib.a`, `lib/aarch64/libportaudio.a`, `lib/aarch64/libasound.a`, `lib/aarch64/libkissfft-float.a`, `include/aarch64/raylib.h`, `include/aarch64/raymath.h`, `include/aarch64/portaudio.h`
- Modify: `.gitignore` (add `build-libs/`)

**Interfaces:**
- `scripts/build-target-libs.sh <arch> [--arch-dir DIR]` — cross-builds the target library bundle into `lib/<arch>/` and `include/<arch>/`. `<arch>` is `aarch64` or `armhf`.
- Produces a bundle whose `raylib` is built `PLATFORM=PLATFORM_DRM` + `GRAPHICS_API_OPENGL_ES2`, whose `portaudio` uses the ALSA backend, and whose `kissfft` is a plain C archive.

**Background — read this before touching the code.** The repository ships:
- `lib/linux/libraylib.a` — x86-64, desktop GL (`libGL.so`).
- `lib/arm/libraylib.a` + `libportaudio.a` + `libasound.a` — **ELF 32-bit ARM**, GLFW with EGL/GLESv2 *and* X11 symbols.
- **No aarch64 libraries at all.**

Both devices are aarch64 (the GKD's CFW arch is still to be probed; ROCKNIX H700 is aarch64 per its kernel config `CONFIG_CC_VERSION_TEXT="aarch64-rocknix-linux-gnu-gcc-16.2.0"`). So this bundle is the critical path for *both* devices and is **shared work**.

- [ ] **Step 1: Check the toolchain and the DRM/EGL dependencies**

```bash
which aarch64-linux-gnu-gcc || sudo xbps-install -y gcc-aarch64-linux-gnu
pkg-config --exists libdrm gbm egl glesv2 && echo "drm/egl deps present" || echo "need libdrm gbm egl glesv2"
```

Expected: aarch64 gcc present; the DRM/EGL dev packages reported (on Void: `mesa-dri-devel libdrm-devel libgbm-devel libegl-devel libglesv2-devel` — adjust names to the distro).

- [ ] **Step 2: Write `scripts/build-target-libs.sh`**

```sh
#!/bin/sh
# build-target-libs.sh — cross-build the target library bundle (lib/<arch>).
#
# Produces, for the given arch:
#   lib/<arch>/libraylib.a          raylib, PLATFORM_DRM, GRAPHICS_API_OPENGL_ES2
#   lib/<arch>/libportaudio.a       PortAudio with the ALSA backend
#   lib/<arch>/libasound.a          ALSA (host-side copy of the target libasound)
#   lib/<arch>/libkissfft-float.a   kissfft
#   include/<arch>/*.h              the matching headers
#
# The bundle is committed to the repo, exactly like lib/arm/ is today.
set -eu

arch=${1:-}
case "$arch" in
    aarch64)
        host=aarch64-linux-gnu
        sysroot=/usr/$host
        ;;
    armhf)
        host=arm-linux-gnueabihf
        sysroot=/usr/$host
        ;;
    *) echo "usage: $0 <aarch64|armhf>" >&2; exit 64 ;;
esac

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

work="$repo_root/build-libs/$arch"
mkdir -p "$work" "lib/$arch" "include/$arch"

# --- raylib ---
if [ ! -d "$work/raylib" ]; then
    git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git "$work/raylib"
fi
cd "$work/raylib"
make clean >/dev/null 2>&1 || true
make \
    PLATFORM=PLATFORM_DRM \
    GRAPHICS_API_OPENGL_ES2 \
    CC=$host-gcc AR=$host-ar \
    CFLAGS="-O2 -I$sysroot/include" \
    LDFLAGS="-L$sysroot/lib" \
    -j"$(nproc)" >/dev/null
cp "$work/raylib/src/libraylib.a" "$repo_root/lib/$arch/"
cp "$work/raylib/src/raylib.h" "$work/raylib/src/raymath.h" "$repo_root/include/$arch/"

# --- PortAudio (ALSA backend) ---
if [ ! -d "$work/portaudio" ]; then
    git clone --depth 1 --branch v19.7.0 https://github.com/PortAudio/portaudio.git "$work/portaudio"
fi
cd "$work/portaudio"
./configure --host=$host --with-alsa --disable-shared --enable-static \
    --prefix="$sysroot" >/dev/null
make -j"$(nproc)" >/dev/null
cp "$work/portaudio/.libs/libportaudio.a" "$repo_root/lib/$arch/"
cp "$work/portaudio/include/portaudio.h" "$repo_root/include/$arch/"

# --- kissfft (arch-independent C, cross-compiled for a matching ABI) ---
cd "$repo_root"
if [ -d third_party/kissfft ]; then
    mkdir -p "$work/kissfft"
    cc_sources=$(find third_party/kissfft -name '*.c')
    $host-gcc -O3 -ffast-math -I third_party/kissfft -c $cc_sources
    mv ./*.o "$work/kissfft/" 2>/dev/null || true
    $host-ar rcs "$repo_root/lib/$arch/libkissfft-float.a" "$work/kissfft/"*.o
fi

echo "built lib/$arch"
ls -la "$repo_root/lib/$arch"
```

- [ ] **Step 3: Run it for aarch64**

```bash
chmod +x scripts/build-target-libs.sh
scripts/build-target-libs.sh aarch64
```

Expected: `lib/aarch64/libraylib.a` and friends exist.

- [ ] **Step 4: Verify the architecture of the produced archive**

```bash
mkdir -p build-libs/check && cd build-libs/check
ar x ../../lib/aarch64/libraylib.a rcore.o
file rcore.o
cd ../.. && rm -f build-libs/check/rcore.o && rmdir build-libs/check
```

Expected: `ELF 64-bit LSB relocatable, ARM aarch64, …`.

- [ ] **Step 5: Verify the bundle actually links**

```bash
cat > build-libs/linksmoke.c <<'EOF'
#include "raylib.h"
int main(void) { InitWindow(640, 480, "x"); CloseWindow(); return 0; }
EOF
aarch64-linux-gnu-gcc -O2 -I include/aarch64 build-libs/linksmoke.c \
    -o build-libs/linksmoke \
    -L lib/aarch64 -lraylib -lGLESv2 -lEGL -lgbm -ldrm -lm -lpthread -ldl
file build-libs/linksmoke
```

Expected: `ELF 64-bit LSB executable, ARM aarch64`.

- [ ] **Step 6: Ignore the scratch directory and commit**

```bash
printf 'build-libs/\n' >> .gitignore
git add scripts/build-target-libs.sh .gitignore lib/aarch64 include/aarch64
git commit -m "build: aarch64 target lib bundle (raylib DRM/GLES2, PortAudio ALSA, kissfft)"
```

---

# Phase B — The shared script and bench layer

*Low risk → medium risk. Still device-independent; every script reads its device facts from a profile.*

---

### Task 6: `scripts/lib/profile.sh` shim and the device-script override convention

**Files:**
- Create: `scripts/lib/profile.sh`

**Interfaces:**
- `scripts/lib/profile.sh` — **sourced**, not executed. The caller sets `SPECTRAX_DEVICE=<id>` and then runs `. scripts/lib/profile.sh`. The script exports `DEVICE_*`, `CFW_*`, `DISPLAY_*`, `GPU_*`, `PATHS_*`, `TRANSPORT_*`, `BUILD_*` and `PROBES_*` variables and prepends `scripts/devices/<id>` to `PATH`. It exits (returns) non-zero if the profile does not lint or if probes are unresolved (unless `SPECTRAX_ALLOW_PENDING_PROBES=1`).
- **The device id must travel through the environment, not through an argument.** `dash` — which is `/bin/sh` on Void Linux — does not pass arguments to a sourced file, so `. profile.sh <id>` would silently read the *caller's* first positional parameter. `$1` is honoured as a fallback for bash users, but every in-repo caller sets `SPECTRAX_DEVICE` first.

**Background.** Shell consumers need a sourced interface, and genuinely device-specific scripts need a home that does not fork the shared ones.

- [ ] **Step 1: Write `scripts/lib/profile.sh`**

```sh
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
```

- [ ] **Step 2: Smoke-test it**

```bash
chmod +x scripts/lib/profile.sh
SPECTRAX_DEVICE=gkd-pixel2
. scripts/lib/profile.sh
echo "$DEVICE_ID $DEVICE_CPU_CORE $PATHS_PORTS_DIR $TRANSPORT_ORDER"
```

Expected: `gkd-pixel2 cortex-a35 /roms/ports wifi-ssh sd`.

- [ ] **Step 3: Confirm it reports unresolved probes without refusing**

```bash
SPECTRAX_DEVICE=gkd-pixel2
. scripts/lib/profile.sh
echo "pending=$PROBES_NEEDED"
```

Expected: `pending=userspace_arch ports_dir gpu_driver gpu_helper` and a successful load. The GKD profile deliberately still has open probes (mirroring the GKD spec's open items), and the loader must not refuse: the probes themselves need to load the profile. `deploy-spectrax.sh` is where the refusal lives.

- [ ] **Step 3b: Confirm the strict mode refuses**

```bash
SPECTRAX_DEVICE=gkd-pixel2 SPECTRAX_STRICT_PROBES=1
. scripts/lib/profile.sh; echo "exit=$?"
```

Expected: a non-zero exit listing the unresolved probes.

- [ ] **Step 3c: Confirm the sourcing contract works under dash**

```bash
dash -c 'SPECTRAX_DEVICE=gkd-pixel2; . scripts/lib/profile.sh; echo "$DEVICE_ID"'
```

Expected: `gkd-pixel2`. (dash is `/bin/sh` on Void and does **not** pass arguments to a sourced file, so the device id must arrive through the environment — this step pins that behaviour.)

- [ ] **Step 4: Commit**

```bash
git add scripts/lib/profile.sh
git commit -m "tooling: shell shim for the device profile loader"
```

---

### Task 7: `deploy-spectrax.sh` transport ladder, `deploy-sd.sh`, `inspect-sd.sh`

**Files:**
- Create: `scripts/deploy-spectrax.sh`
- Create: `scripts/deploy-sd.sh`
- Create: `scripts/inspect-sd.sh`

**Interfaces:**
- `scripts/deploy-spectrax.sh [--device ID] [--launch] [--out DIR] [--build-dir DIR] [--transport NAME]` — walks `transport.order` from the profile and uses the first available transport. Prints the transport it used. `--transport` forces one.
- `scripts/deploy-sd.sh <mount-point> [--device ID] [--build-dir DIR]` — rsyncs the port tree into `<mount-point><paths.ports_dir>/spectrax/`.
- `scripts/inspect-sd.sh <img> [--device ID] [--read-only|--rw]` — loop-mounts an image, finds the ports directory (searching both `/roms/ports` and `/storage/roms/ports` shapes) and validates each port's `.sh` entry.

**Background.** The GKD plan's `deploy-spectrax.sh` hardcodes `root@$ip:/roms/ports/spectrax`. The transport *mechanism* differs per device (USB gadget vs WiFi SSH vs SD) but the deploy *body* does not. The transport list is data.

- [ ] **Step 1: Write `scripts/deploy-spectrax.sh`**

```sh
#!/bin/sh
# deploy-spectrax.sh — deploy the Spectrax port tree to a device.
#
# Walks the profile's transport.order and uses the first transport that is
# available. Prints which transport it used.
#
# Usage: deploy-spectrax.sh [--device ID] [--launch] [--out DIR]
#                            [--build-dir DIR] [--transport NAME]
set -eu

device=${SPECTRAX_DEVICE:-}
launch=0
out_dir=""
build_dir=""
force_transport=""

while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --launch) launch=1 ;;
        --out) shift; out_dir="${1:-}" ;;
        --out=*) out_dir="${1#--out=}" ;;
        --build-dir) shift; build_dir="${1:-}" ;;
        --build-dir=*) build_dir="${1#--build-dir=}" ;;
        --transport) shift; force_transport="${1:-}" ;;
        --transport=*) force_transport="${1#--transport=}" ;;
        --help|-h) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

[ -n "$device" ] || { echo "--device is required (or set SPECTRAX_DEVICE)" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

if [ -n "$PROBES_NEEDED" ] && [ "${SPECTRAX_ALLOW_PENDING_PROBES:-0}" != "1" ]; then
    echo "profile $DEVICE_ID has unresolved probes:" >&2
    echo "$PROBES_NEEDED" | sed 's/^/  - /' >&2
    echo "run the matching scripts/probe-*.sh first, or set SPECTRAX_ALLOW_PENDING_PROBES=1" >&2
    exit 1
fi

[ -n "$build_dir" ] || build_dir="build-$DEVICE_ID"
[ -d "$build_dir" ] || { echo "build dir not found: $build_dir" >&2; exit 1; }

port_files="spectrax spectrax_bench instrument_harness"
port_scripts="spectrax.sh spectrax_bench.sh"

have_binary() {
    for f in $port_files; do
        [ -f "$build_dir/$f" ] || return 1
    done
    return 0
}
have_binary || { echo "missing binaries in $build_dir: run meson compile -C $build_dir" >&2; exit 1; }

ssh_target() {
    # $1 = ip
    echo "$CFW_USER@$1"
}

try_usb_gadget() {
    host_ip=$TRANSPORT_USB_GADGET_HOST_IP
    [ -n "$host_ip" ] || return 1
    ip link show usb0 >/dev/null 2>&1 || ip link show gadget >/dev/null 2>&1 || return 1
    ip addr show 2>/dev/null | grep -q "$host_ip/" || return 1
    ssh -o ConnectTimeout=5 -o BatchMode=yes "$(ssh_target "$host_ip")" true 2>/dev/null || return 1
    echo "$host_ip"
    return 0
}

try_wifi_ssh() {
    ip=""
    for name in $TRANSPORT_MDNS_NAMES; do
        cand=$(getent hosts "$name" 2>/dev/null | awk '{print $1}' | head -1)
        if [ -n "$cand" ] && ssh -o ConnectTimeout=3 -o BatchMode=yes "$(ssh_target "$cand")" true 2>/dev/null; then
            echo "$cand"
            return 0
        fi
    done
    ifc=$(ip route 2>/dev/null | awk '/^default/ {print $5; exit}')
    [ -z "$ifc" ] && ifc=$(ip -o -4 addr 2>/dev/null | awk '{print $2}' | head -1)
    [ -z "$ifc" ] && return 1
    cidr=$(ip -o -4 addr show dev "$ifc" 2>/dev/null | awk '{print $4}' | head -1)
    base=$(echo "$cidr" | awk -F/ '{print $1}' | awk -F. '{print $1"."$2"."$3}')
    for last in $(seq 1 254); do
        cand="${base}.${last}"
        if ssh -o ConnectTimeout=0.4 -o BatchMode=yes -o StrictHostKeyChecking=no \
               "$(ssh_target "$cand")" true 2>/dev/null; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

deploy_over_ssh() {
    ip=$1
    dest="$(ssh_target "$ip"):$PATHS_DATA_DIR"
    echo "rsync -> $dest"
    for f in $port_files; do
        rsync -a --info=progress2 "$build_dir/$f" "$dest/" || return 1
    done
    for s in $port_scripts; do
        rsync -a --info=progress2 "scripts/portmaster/$s" "$dest/$s" || return 1
    done
    if [ -d bin/resources ]; then
        rsync -a --info=progress2 bin/resources/ "$dest/data/" || return 1
    fi
    if [ "$launch" = 1 ]; then
        remote_out="${out_dir:-$PATHS_RUNS_DIR/$(date -u +%Y%m%dT%H%M%SZ)}"
        echo "ssh-launch on $ip -> $remote_out"
        ssh "$(ssh_target "$ip")" "mkdir -p '$remote_out' && cd '$PATHS_DATA_DIR' && ./spectrax_bench --bench=audio --out='$remote_out'"
        if [ -n "$out_dir" ]; then
            mkdir -p "$out_dir"
            rsync -a "$(ssh_target "$ip"):$remote_out/" "$out_dir/"
            echo "pulled to $out_dir"
        fi
    fi
    return 0
}

ip=""
used=""
for t in $TRANSPORT_ORDER; do
    [ -z "$force_transport" ] || [ "$t" = "$force_transport" ] || continue
    case "$t" in
        usb-gadget) found=$(try_usb_gadget) && { ip=$found; used=$t; break; } ;;
        wifi-ssh)   found=$(try_wifi_ssh)   && { ip=$found; used=$t; break; } ;;
        sd)         echo "transport 'sd' selected: mount the card and use scripts/deploy-sd.sh"; used=$t; break ;;
        *) echo "unknown transport in profile: $t" >&2; exit 1 ;;
    esac
done

[ -n "$used" ] || { echo "no transport available for $DEVICE_ID" >&2; exit 1; }
[ "$used" != "sd" ] || exit 0

echo "transport: $used ($ip)"
deploy_over_ssh "$ip"
echo "deployed to $DEVICE_ID via $used"
```

- [ ] **Step 2: Write `scripts/deploy-sd.sh`**

```sh
#!/bin/sh
# deploy-sd.sh — rsync the Spectrax port tree onto a mounted SD card.
#
# The destination is derived from the profile's paths.ports_dir, so the same
# script serves devices whose SD layout differs.
#
# Usage: deploy-sd.sh <mount-point> [--device ID] [--build-dir DIR]
set -eu

mount=${1:-}
[ -n "$mount" ] || { echo "usage: $0 <mount-point> [--device ID] [--build-dir DIR]" >&2; exit 64; }
[ -d "$mount" ] || { echo "not a directory: $mount" >&2; exit 1; }
shift

device=${SPECTRAX_DEVICE:-}
build_dir=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --build-dir) shift; build_dir="${1:-}" ;;
        --build-dir=*) build_dir="${1#--build-dir=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

[ -n "$device" ] || { echo "--device is required (or set SPECTRAX_DEVICE)" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

if [ -n "$PROBES_NEEDED" ]; then
    echo "warning: profile $DEVICE_ID has unresolved probes ($PROBES_NEEDED)" >&2
    echo "         the port may land in the wrong directory" >&2
fi

[ -n "$build_dir" ] || build_dir="build-$DEVICE_ID"
[ -d "$build_dir" ] || { echo "build dir not found: $build_dir" >&2; exit 1; }

dest="$mount$PATHS_PORTS_DIR/spectrax"
mkdir -p "$dest/data"

for f in spectrax spectrax_bench instrument_harness; do
    [ -f "$build_dir/$f" ] || { echo "missing $build_dir/$f" >&2; exit 1; }
    rsync -a --info=progress2 "$build_dir/$f" "$dest/"
done
for s in spectrax.sh spectrax_bench.sh; do
    rsync -a --info=progress2 "scripts/portmaster/$s" "$dest/$s"
done
chmod +x "$dest/spectrax.sh" "$dest/spectrax_bench.sh"
[ -d bin/resources ] && rsync -a --info=progress2 bin/resources/ "$dest/data/"

echo "deployed to $dest"
ls -la "$dest"
```

- [ ] **Step 3: Write `scripts/inspect-sd.sh`**

```sh
#!/bin/sh
# inspect-sd.sh — loop-mount an SD image and validate its PortMaster entries.
#
# The ports directory differs between CFW families (/roms/ports on some,
# /storage/roms/ports on others), so the inspector searches both shapes.
#
# Usage: inspect-sd.sh <img> [--device ID] [--read-only|--rw]
set -eu

img=${1:-}
[ -n "$img" ] && [ -f "$img" ] || { echo "usage: $0 <img> [--device ID] [--read-only|--rw]" >&2; exit 64; }
shift

device=${SPECTRAX_DEVICE:-}
ro="-o ro"
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --read-only) ro="-o ro" ;;
        --rw) ro="-o rw" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

if [ -n "$device" ]; then
    # shellcheck disable=SC1091
    . "$repo_root/scripts/lib/profile.sh" "$device"
    ports_rel=$PATHS_PORTS_DIR
else
    ports_rel=""
fi

mnt=$(mktemp -d)
trap 'umount "$mnt" 2>/dev/null || true; rmdir "$mnt"' EXIT
mount -o loop,$ro "$img" "$mnt"
echo "mounted $img at $mnt"

ports=""
for rel in ${ports_rel:+"$ports_rel"} /roms/ports /storage/roms/ports; do
    for sub in "$mnt" "$mnt"/*/; do
        [ -d "$sub$rel" ] && ports="$ports $sub$rel"
    done
done

if [ -z "$ports" ]; then
    echo "no ports directory found (looked for ${ports_rel:-/roms/ports} and /storage/roms/ports)"
    exit 2
fi

ok=0; fail=0
for p in $ports; do
    echo "--- $p ---"
    for entry in "$p"/*; do
        [ -d "$entry" ] || continue
        name=$(basename "$entry")
        sh_file="$entry/$name.sh"
        bin_file="$entry/$name"
        if [ ! -f "$sh_file" ]; then
            echo "  $name: MISSING $sh_file"; fail=$((fail+1)); continue
        fi
        first=$(head -1 "$sh_file")
        if [ "$first" != "#!/bin/sh" ] && [ "$first" != "#!/bin/bash" ]; then
            echo "  $name: bad shebang: $first"; fail=$((fail+1)); continue
        fi
        if [ ! -x "$sh_file" ]; then
            echo "  $name: not executable"; fail=$((fail+1)); continue
        fi
        if [ ! -f "$bin_file" ]; then
            echo "  $name: NO BINARY at $bin_file"; fail=$((fail+1)); continue
        fi
        echo "  $name: OK ($bin_file $(stat -c%s "$bin_file") bytes)"; ok=$((ok+1))
    done
done

echo "--- summary: $ok ok, $fail fail ---"
exit $fail
```

- [ ] **Step 4: Syntax-check all three**

```bash
chmod +x scripts/deploy-spectrax.sh scripts/deploy-sd.sh scripts/inspect-sd.sh
for s in scripts/deploy-spectrax.sh scripts/deploy-sd.sh scripts/inspect-sd.sh; do
    sh -n "$s" || { echo "syntax error in $s" >&2; exit 1; }
done
```

Expected: silent.

- [ ] **Step 5: Exercise `deploy-sd.sh` against a synthetic image**

```bash
tmp=$(mktemp -d)
mkdir -p "$tmp/mnt"
truncate -s 64M "$tmp/img.img"
mkfs.ext4 -F "$tmp/img.img" >/dev/null
mount -o loop "$tmp/img.img" "$tmp/mnt"
mkdir -p "$tmp/mnt/roms/ports/spectrax"
touch "$tmp/mnt/roms/ports/spectrax/spectrax"
chmod +x "$tmp/mnt/roms/ports/spectrax/spectrax"
cp scripts/portmaster/spectrax.sh "$tmp/mnt/roms/ports/spectrax/spectrax.sh"
chmod +x "$tmp/mnt/roms/ports/spectrax/spectrax.sh"
umount "$tmp/mnt"

scripts/inspect-sd.sh "$tmp/img.img" --device gkd-pixel2
echo "inspect exit=$?"
```

Expected: `spectrax: OK …` and exit 0.

- [ ] **Step 6: Commit**

```bash
git add scripts/deploy-spectrax.sh scripts/deploy-sd.sh scripts/inspect-sd.sh
git commit -m "deploy: profile-driven transport ladder, SD deploy and SD inspector"
```

---

### Task 8: `discover-device.sh` and `setup-device.sh`

**Files:**
- Create: `scripts/discover-device.sh`
- Create: `scripts/setup-device.sh`

**Interfaces:**
- `scripts/discover-device.sh [--device ID]` — prints the device's IP on stdout; tries, in order: the profile's `transport.usb_gadget_host_ip` (if a matching NIC exists), the profile's `transport.mdns_names`, then a `/24` SSH scan of the host's subnet. Exits 1 if nothing is found.
- `scripts/setup-device.sh [--device ID] [--password PW]` — one-shot first-time setup: discover the device, install the host's SSH public key, optionally set a stable root password, and write `~/.config/spectrax/devices/<id>.toml` with the discovered IP.

**Background — the one real behavioural difference between the devices.** The GKD's ROGUE OS ships `sshd` on with a fixed default password (`rogue`, per the GKD spec). ROCKNIX ships with SSH **disabled** and **regenerates the root password on every reboot**, showing it in EmulationStation → System Settings; it also ships a `setrootpass` helper. So the *script* is shared; the credential source is a profile field (`cfw.password_source` = `fixed` | `user-set` | `device-display`).

- [ ] **Step 1: Write `scripts/discover-device.sh`**

```sh
#!/bin/sh
# discover-device.sh — locate a device on the LAN (or over the USB gadget link).
#
# Usage: discover-device.sh [--device ID]
# Prints the device IP on stdout.
set -eu

device=${SPECTRAX_DEVICE:-}
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done
[ -n "$device" ] || { echo "--device is required (or set SPECTRAX_DEVICE)" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

reachable() {
    ssh -o ConnectTimeout=3 -o BatchMode=yes -o StrictHostKeyChecking=no \
        "$CFW_USER@$1" true 2>/dev/null
}

# 1) USB gadget link-local address.
if [ -n "$TRANSPORT_USB_GADGET_HOST_IP" ]; then
    if ip addr show 2>/dev/null | grep -q "$TRANSPORT_USB_GADGET_HOST_IP/"; then
        if reachable "$TRANSPORT_USB_GADGET_HOST_IP"; then
            echo "$TRANSPORT_USB_GADGET_HOST_IP"
            exit 0
        fi
    fi
fi

# 2) mDNS / hostname.
for name in $TRANSPORT_MDNS_NAMES; do
    cand=$(getent hosts "$name" 2>/dev/null | awk '{print $1}' | head -1)
    if [ -n "$cand" ] && reachable "$cand"; then
        echo "$cand"
        exit 0
    fi
done

# 3) Subnet scan.
ifc=$(ip route 2>/dev/null | awk '/^default/ {print $5; exit}')
[ -z "$ifc" ] && ifc=$(ip -o -4 addr 2>/dev/null | awk '{print $2}' | head -1)
[ -z "$ifc" ] && { echo "error: no network interface" >&2; exit 1; }
cidr=$(ip -o -4 addr show dev "$ifc" 2>/dev/null | awk '{print $4}' | head -1)
base=$(echo "$cidr" | awk -F/ '{print $1}' | awk -F. '{print $1"."$2"."$3}')
echo "scanning ${base}.0/24 for SSH..." >&2
for last in $(seq 1 254); do
    cand="${base}.${last}"
    if reachable "$cand"; then
        echo "$cand"
        exit 0
    fi
done
echo "error: no reachable $DEVICE_ID found on ${base}.0/24" >&2
exit 1
```

- [ ] **Step 2: Write `scripts/setup-device.sh`**

```sh
#!/bin/sh
# setup-device.sh — first-time host setup for a device.
#
# Discovers the device, installs the host SSH key and records the IP in the
# host-local profile override.
#
# Credential handling depends on the profile's cfw.password_source:
#   fixed         the profile carries the password (ROGUE OS on the GKD)
#   user-set      the user has already set a stable root password on the
#                 device (ROCKNIX: System Settings -> set root password)
#   device-display the password rotates every boot and must be typed in
#                 (ROCKNIX default) -- the script prompts for it
#
# Usage: setup-device.sh [--device ID] [--password PW]
set -eu

device=${SPECTRAX_DEVICE:-}
password=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --password) shift; password="${1:-}" ;;
        --password=*) password="${1#--password=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done
[ -n "$device" ] || { echo "--device is required (or set SPECTRAX_DEVICE)" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

ip=$(scripts/discover-device.sh --device "$device") || {
    echo "error: device not discovered" >&2; exit 1; }
echo "discovered $ip"

ssh-keygen -t ed25519 -N '' -f "$HOME/.ssh/id_ed25519" 2>/dev/null || true

case "$CFW_PASSWORD_SOURCE" in
    fixed)
        [ -n "$password" ] || password=$CFW_PASSWORD
        [ -n "$password" ] || { echo "error: no password for a fixed-password device" >&2; exit 1; }
        command -v sshpass >/dev/null 2>&1 || {
            echo "error: sshpass is required (xbps-install -S sshpass)" >&2; exit 1; }
        sshpass -p "$password" ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$CFW_USER@$ip"
        ;;
    user-set)
        ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$CFW_USER@$ip"
        ;;
    device-display)
        if [ -z "$password" ]; then
            printf "root password for %s (shown on the device screen): " "$DEVICE_ID" >&2
            stty -echo 2>/dev/null || true
            read -r password
            stty echo 2>/dev/null || true
        fi
        command -v sshpass >/dev/null 2>&1 || {
            echo "error: sshpass is required (xbps-install -S sshpass)" >&2; exit 1; }
        sshpass -p "$password" ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$CFW_USER@$ip"
        ;;
    *)
        echo "error: unknown password_source: $CFW_PASSWORD_SOURCE" >&2; exit 1 ;;
esac

mkdir -p "$HOME/.config/spectrax/devices"
cat > "$HOME/.config/spectrax/devices/$device.toml" <<EOF
[device]
ip = "$ip"
EOF
echo "wrote $HOME/.config/spectrax/devices/$device.toml"
```

- [ ] **Step 3: Syntax-check and commit**

```bash
chmod +x scripts/discover-device.sh scripts/setup-device.sh
sh -n scripts/discover-device.sh
sh -n scripts/setup-device.sh
git add scripts/discover-device.sh scripts/setup-device.sh
git commit -m "deploy: profile-driven device discovery and first-time SSH setup"
```

- [ ] **Step 4: Add a regression check for the credential-source branching**

Append to `tests/dsp/test_device_profile.sh`:
```sh
# The setup script must branch on cfw.password_source, not hardcode a password.
grep -q "CFW_PASSWORD_SOURCE" scripts/setup-device.sh || fail "setup-device.sh does not branch on password_source"
grep -q "fixed" scripts/setup-device.sh || fail "setup-device.sh lacks the fixed-password branch"
```

---

### Task 9: PortMaster launch wrappers and `show-runs.sh`

**Files:**
- Create: `scripts/portmaster/spectrax.sh`
- Create: `scripts/portmaster/spectrax_bench.sh`
- Create: `scripts/show-runs.sh`

**Interfaces:**
- `scripts/portmaster/spectrax.sh` — the on-device wrapper. Reads `/etc/spectrax-device.toml` (a copy of the profile shipped to the device) to find `paths.data_dir`, optionally runs the GPU-switch helper, then `exec`s the app.
- `scripts/portmaster/spectrax_bench.sh` — the on-device bench wrapper; defaults to audio mode and writes into `paths.runs_dir`.
- `scripts/show-runs.sh <run-dir>` — pretty-print a pulled run directory on the host.

**Background.** The GKD plan hardcodes `/roms/ports/spectrax` and `/usr/bin/setgpu.sh panfrost` in the wrappers. The RG35XX SP's ports live under `/storage/roms/ports` and no GPU-switch helper is known (Panfrost is the default driver). The wrapper therefore reads a small device-side TOML that the deploy drops next to it.

- [ ] **Step 1: Write `scripts/portmaster/spectrax.sh`**

```sh
#!/bin/sh
# PortMaster / EmulationStation launch wrapper for spectrax.
#
# The device-side profile fragment (paths + gpu helper) is read from
# /etc/spectrax-device.toml, which the deploy drops onto the device.
set -eu

conf=/etc/spectrax-device.toml
data_dir=/roms/ports/spectrax
gpu_helper=""
gpu_driver=""

if [ -r "$conf" ]; then
    data_dir=$(sed -n 's/^data_dir *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_helper=$(sed -n 's/^gpu_helper *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_driver=$(sed -n 's/^gpu_driver *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    [ -n "$data_dir" ] || data_dir=/roms/ports/spectrax
fi

if [ -n "$gpu_helper" ] && [ -x "$gpu_helper" ]; then
    "$gpu_helper" "$gpu_driver" 2>/dev/null || true
fi

cd "$data_dir" || exit 1
exec ./spectrax "$@"
```

- [ ] **Step 2: Write `scripts/portmaster/spectrax_bench.sh`**

```sh
#!/bin/sh
# PortMaster / EmulationStation launch wrapper for spectrax_bench.
set -eu

conf=/etc/spectrax-device.toml
data_dir=/roms/ports/spectrax
runs_dir=""
gpu_helper=""
gpu_driver=""

if [ -r "$conf" ]; then
    data_dir=$(sed -n 's/^data_dir *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    runs_dir=$(sed -n 's/^runs_dir *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_helper=$(sed -n 's/^gpu_helper *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    gpu_driver=$(sed -n 's/^gpu_driver *= *"\(.*\)".*/\1/p' "$conf" | head -1)
    [ -n "$data_dir" ] || data_dir=/roms/ports/spectrax
fi

if [ -n "$gpu_helper" ] && [ -x "$gpu_helper" ]; then
    "$gpu_helper" "$gpu_driver" 2>/dev/null || true
fi

out_dir="${1:-${runs_dir:-/roms/ports/spectrax/runs}/$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p "$out_dir"
cd "$data_dir" || exit 1
exec ./spectrax_bench --bench=audio --out="$out_dir"
```

- [ ] **Step 3: Write `scripts/show-runs.sh`**

```sh
#!/bin/sh
# show-runs.sh — pretty-print a pulled benchmark run directory.
# Usage: show-runs.sh <run-dir>
set -eu

dir=${1:-}
[ -n "$dir" ] && [ -d "$dir" ] || { echo "usage: $0 <run-dir>" >&2; exit 64; }

if [ -f "$dir/summary.json" ]; then
    echo "=== summary ==="
    if command -v python3 >/dev/null 2>&1; then
        python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for section, values in d.items():
    print("[%s]" % section)
    for k, v in values.items():
        print("  %-28s %s" % (k, v))
' "$dir/summary.json"
    else
        cat "$dir/summary.json"
    fi
fi

if [ -f "$dir/timeseries.csv" ]; then
    echo
    echo "=== timeseries (head) ==="
    head -20 "$dir/timeseries.csv"
fi

if [ -f "$dir/hostinfo.txt" ]; then
    echo
    echo "=== hostinfo (head) ==="
    head -20 "$dir/hostinfo.txt"
fi
```

- [ ] **Step 4: Syntax-check and commit**

```bash
chmod +x scripts/portmaster/spectrax.sh scripts/portmaster/spectrax_bench.sh scripts/show-runs.sh
for s in scripts/portmaster/*.sh scripts/show-runs.sh; do sh -n "$s" || exit 1; done
git add scripts/portmaster/ scripts/show-runs.sh
git commit -m "deploy: device-side PortMaster wrappers and run pretty-printer"
```

- [ ] **Step 5: Add the device-side profile fragment to the deploy**

In `scripts/deploy-spectrax.sh`'s `deploy_over_ssh`, after the rsyncs, add:

```sh
    ssh "$(ssh_target "$ip")" "cat > /etc/spectrax-device.toml <<'EOF'
data_dir = \"$PATHS_DATA_DIR\"
runs_dir = \"$PATHS_RUNS_DIR\"
gpu_helper = \"$GPU_HELPER\"
gpu_driver = \"$GPU_DRIVER\"
EOF"
```

---

### Task 10: The probe suite

**Files:**
- Create: `scripts/probe-os.sh`, `scripts/probe-paths.sh`, `scripts/probe-usb.sh`, `scripts/probe-display.sh`, `scripts/probe-audio.sh`, `scripts/probe-cpu.sh`, `scripts/probe-mem.sh`, `scripts/probe-lid.sh`, `scripts/probe-boot.sh`, `scripts/probe-storage.sh`, `scripts/probe-gpu.sh`

**Interfaces:**
- Every probe: `scripts/probe-<name>.sh [--device ID] [--ip IP]`. It connects over SSH to the device (or runs locally when `--local`), prints a report to stdout, and — with `--apply` — writes the resolved values back into the host-local profile override `~/.config/spectrax/devices/<id>.toml` and removes the corresponding key from `[probes] needed`.
- Every probe exits 0 when it ran (whether or not it resolved anything) and 2 on a connection failure. **A probe never fails the build.**

**Background.** These are the scripts that turn "unknown" into "known". The spec lists every open item and the probe that resolves it; this task implements them. They are shared — the same probe serves both devices, because a probe *reports* rather than *asserts*.

- [ ] **Step 1: Write the shared probe scaffolding as a helper**

`scripts/lib/probe.sh`:
```sh
#!/bin/sh
# probe.sh — shared helpers for the probe scripts.
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

probe_apply() {
    # $1 = section, $2 = key, $3 = value
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
```

- [ ] **Step 2: Write `scripts/probe-os.sh`**

```sh
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
arch=$(probe_run "uname -m" | awk -F= '/^arch=/{print $2}' | tr -d '\r')
[ -n "$arch" ] && probe_apply device arch "$arch"
hostname=$(probe_run "hostname" | head -1 | tr -d '\r')
[ -n "$hostname" ] && probe_apply cfw hostname "$hostname"
```

- [ ] **Step 3: Write `scripts/probe-paths.sh`**

```sh
#!/bin/sh
# probe-paths.sh — resolve the ports directory and the storage layout.
#
# Resolves: paths.storage_root, paths.ports_dir
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "candidate ports directories"
probe_run "for d in /roms/ports /storage/roms/ports /roms /storage; do
    if [ -e \"\$d\" ]; then printf '%s -> %s\\n' \"\$d\" \"\$(readlink -f \"\$d\")\"; fi
  done"
probe_heading "mounts"
probe_run "cat /proc/mounts | grep -E 'storage|roms|sdcard|mmc' || true"
probe_heading "PortMaster"
probe_run "ls -d /storage/.config/PortMaster /roms/ports/PortMaster 2>/dev/null || echo '(no PortMaster dir)'"
probe_heading "emulationstation ports section"
probe_run "ls -d /storage/roms/ports /roms/ports 2>/dev/null || true"

echo
echo "### suggested profile updates"
for cand in /storage/roms/ports /roms/ports; do
    if probe_run "test -d $cand && echo yes" | grep -q yes; then
        probe_apply paths ports_dir "$cand"
        probe_apply paths data_dir "$cand/spectrax"
        probe_apply paths runs_dir "$cand/spectrax/runs"
        break
    fi
done
```

- [ ] **Step 4: Write `scripts/probe-usb.sh`**

```sh
#!/bin/sh
# probe-usb.sh — decide whether the device exposes USB networking.
#
# Resolves: transport.usb_gadget_host_ip, transport.usb_gadget_device_ip,
#           and whether 'usb-gadget' belongs in transport.order.
#
# Device side facts this probe checks:
#   * /sys/class/udc/ is populated
#   * the UDC's current function
#   * /sys/class/usb_role/*/role  (the Type-C role switch)
#   * a gadget NIC in /sys/class/net
#   * `usbgadget` with no arguments prints the current mode
#   * `usbgadget address` prints the link-local address
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "UDC"
probe_run "ls -1 /sys/class/udc/ 2>/dev/null || echo '(no /sys/class/udc)'"
probe_heading "UDC function"
probe_run "cat /sys/class/udc/*/function 2>/dev/null || true"
probe_heading "UDC state"
probe_run "cat /sys/class/udc/*/state 2>/dev/null || true"
probe_heading "USB role switch"
probe_run "for f in /sys/class/usb_role/*/role; do [ -e \"\$f\" ] && echo \"\$f = \$(cat \$f)\"; done"
probe_heading "gadget NICs"
probe_run "ls -1 /sys/class/net/ 2>/dev/null || true"
probe_heading "usbgadget mode"
probe_run "command -v usbgadget && usbgadget || echo '(no usbgadget helper)'"
probe_heading "usbgadget address"
probe_run "command -v usbgadget && usbgadget address || true"
probe_heading "usb_configfs functions"
probe_run "ls -1 /sys/kernel/config/usb_gadget/ 2>/dev/null || echo '(no configfs)'"

echo
echo "### host side"
if [ "$probe_local" = 1 ]; then
    probe_run "ip -br addr 2>/dev/null | grep -E '169\.254|usb0|gadget' || echo '(no gadget link on the host)'"
else
    echo "  (run this script on the host with the USB cable attached to check the link)"
fi

echo
echo "### suggested profile updates"
addr=$(probe_run "command -v usbgadget >/dev/null 2>&1 && usbgadget address" | head -1 | tr -d '\r')
if [ -n "$addr" ]; then
    host_ip=$(echo "$addr" | awk -F. '{print $1"."$2"."$3".1"}')
    probe_apply transport usb_gadget_device_ip "$addr"
    probe_apply transport usb_gadget_host_ip "$host_ip"
fi
```

- [ ] **Step 5: Write `scripts/probe-display.sh`, `scripts/probe-audio.sh`, `scripts/probe-cpu.sh`, `scripts/probe-mem.sh`**

`scripts/probe-display.sh`:
```sh
#!/bin/sh
# probe-display.sh — resolve the panel mode, the compositor scale and the GL API.
#
# Resolves: display.width, display.height, display.scale, display.native,
#           gpu.driver, gpu.api
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "drm modes"
probe_run "cat /sys/class/drm/card0/modes 2>/dev/null | head -5 || echo '(no drm card0)'"
probe_heading "drm connector"
probe_run "cat /sys/class/drm/card0-HDMI-A-1/modes 2>/dev/null; cat /sys/class/drm/card0-DSI-1/modes 2>/dev/null || true"
probe_heading "compositor output"
probe_run "command -v swaymsg >/dev/null 2>&1 && swaymsg -g outputs || echo '(no swaymsg)'"
probe_heading "GL renderer"
probe_run "command -v es2_info >/dev/null 2>&1 && es2_info || echo '(no es2_info)'"
probe_heading "egl vendor"
probe_run "dmesg 2>/dev/null | grep -i -E 'panfrost|mali|drm|gpu' | tail -20 || true"

echo
echo "### suggested profile updates"
mode=$(probe_run "cat /sys/class/drm/card0-DSI-1/modes 2>/dev/null | head -1" | tr -d '\r')
if [ -n "$mode" ]; then
    w=$(echo "$mode" | cut -d'x' -f1)
    h=$(echo "$mode" | cut -d'x' -f2)
    probe_apply display width "$w"
    probe_apply display height "$h"
fi
```

`scripts/probe-audio.sh`:
```sh
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
```

`scripts/probe-cpu.sh`:
```sh
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
```

`scripts/probe-mem.sh`:
```sh
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
```

- [ ] **Step 6: Write `scripts/probe-lid.sh`, `scripts/probe-boot.sh`, `scripts/probe-storage.sh`, `scripts/probe-gpu.sh`**

`scripts/probe-lid.sh`:
```sh
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
```

`scripts/probe-boot.sh`:
```sh
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
    if ip=$(scripts/discover-device.sh --device "$probe_device" 2>/dev/null); then
        end=$(date +%s)
        echo "  reachable after $((end - start))s at $ip"
        exit 0
    fi
    sleep 2
done
echo "  not reachable within 180s"
exit 1
```

`scripts/probe-storage.sh`:
```sh
#!/bin/sh
# probe-storage.sh — characterise the writable volume (SD card) throughput.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "mounts"
probe_run "df -h 2>/dev/null | head -20"
probe_heading "card identity"
probe_run "cat /sys/block/mmcblk*/device/{name,cid,manfid,oemid,serial} 2>/dev/null || true"
probe_heading "write throughput"
probe_run "mkdir -p /tmp/probe-storage && dd if=/dev/zero of=/tmp/probe-storage/blob bs=1M count=32 conv=fsync 2>&1 | tail -3"
probe_heading "read throughput"
probe_run "dd if=/tmp/probe-storage/blob of=/dev/null bs=1M 2>&1 | tail -3"
probe_run "rm -rf /tmp/probe-storage"
```

`scripts/probe-gpu.sh`:
```sh
#!/bin/sh
# probe-gpu.sh — report the GPU driver state and whether a GL context works.
#
# Extends the GKD plan's gpu-check.sh. The branch is selected by the
# profile's gpu.driver: 'libmali' probes the Mali userspace library,
# 'panfrost' probes the Panfrost kernel driver.
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "os-release"
probe_run "cat /etc/os-release 2>/dev/null || true"
probe_heading "uname"
probe_run "uname -a"
probe_heading "drm devices"
probe_run "ls -l /dev/dri 2>/dev/null || echo '(no /dev/dri)'"
probe_heading "gpu helper script"
probe_run "ls /usr/bin/*gpu* /usr/local/bin/*gpu* 2>/dev/null || echo '(no gpu helper script)'"
probe_heading "loaded modules"
probe_run "lsmod 2>/dev/null | grep -iE 'mali|panfrost|drm|gpu' || echo '(lsmod unavailable)'"

case "$GPU_DRIVER" in
    panfrost)
        probe_heading "panfrost"
        probe_run "dmesg 2>/dev/null | grep -i panfrost | tail -10 || true"
        probe_run "ls -l /sys/kernel/debug/dri/0/panfrost 2>/dev/null || true"
        ;;
    libmali)
        probe_heading "libmali"
        probe_run "ls /usr/lib/libmali* /usr/lib/aarch64-linux-gnu/libmali* 2>/dev/null || echo '(no libmali)'"
        probe_run "cat /usr/bin/setgpu.sh 2>/dev/null || echo '(no setgpu.sh)'"
        ;;
esac

probe_heading "EGL / GLES libraries"
probe_run "ls /usr/lib/*/libEGL* /usr/lib/*/libGLESv2* /usr/lib/libEGL* /usr/lib/libGLESv2* 2>/dev/null || echo '(no EGL/GLESv2 libs)'"
probe_heading "GL info"
probe_run "command -v es2_info >/dev/null 2>&1 && es2_info | head -30 || echo '(no es2_info)'"
probe_heading "DRM/GBM"
probe_run "ls /usr/lib/*/libgbm* /usr/lib/libgbm* 2>/dev/null || echo '(no libgbm)'"
```

- [ ] **Step 7: Syntax-check every probe and commit**

```bash
chmod +x scripts/probe-*.sh scripts/lib/probe.sh
for s in scripts/probe-*.sh scripts/lib/probe.sh; do sh -n "$s" || { echo "syntax error in $s" >&2; exit 1; }; done
git add scripts/probe-*.sh scripts/lib/probe.sh
git commit -m "probe: device fact discovery suite (os, paths, usb, display, audio, cpu, mem, lid, boot, storage, gpu)"
```

- [ ] **Step 8: Add a probe-listing check to `tests/dsp/test_device_profile.sh`**

Append:
```sh
# Every probe named in the spec must exist and be syntactically valid.
for p in probe-os probe-paths probe-usb probe-display probe-audio \
         probe-cpu probe-mem probe-lid probe-boot probe-storage probe-gpu; do
    [ -f "scripts/$p.sh" ] || fail "missing probe: $p"
    sh -n "scripts/$p.sh" || fail "probe $p has a syntax error"
done
```

---

### Task 11: Parameterise `spectrax_bench`

**Files:**
- Create: `src/tools/bench/meson.build`, `main.c`, `bench_report.h/.c`, `bench_xvfb.h/.c`, `bench_audio.h/.c`, `bench_fileio.h/.c`, `bench_gfx.h/.c`
- Modify: `meson.build` (register the bench subdir)
- Modify: `tests/meson.build` (register `test_bench_report`)
- Create: `tests/dsp/test_bench_report.c`

**Interfaces:**
- `spectrax_bench --bench={audio|gfx|fileio|all} [--device ID] [--display WxH] [--workdir DIR] [--gpu {gles2|gles3}] [--duration SECS] [--voices N] [--channels N] [--out DIR] [--no-neon] [--seed N]`
- `--device ID` resolves the profile and stamps `meta.device`, `meta.device_id`, `meta.display`, `meta.gpu_api` into `summary.json`.
- `--workdir DIR` overrides the fileio working directory (default: the profile's `paths.data_dir` when a device is selected, else a `mkdtemp` under `/tmp`).
- `--display WxH` overrides the gfx window size (default: the profile's `display.width`×`display.height`).
- The bench **never** spawns Xvfb when a device is selected with `--device` (the device has a real display); it only does so on the host.

**Background — the base implementation.** The GKD plan (`docs/superpowers/plans/2026-09-20-gkd-pixel2-tooling.md`, Tasks 4–8) specifies the bench in full: `bench_report` (JSON + CSV + console.log + hostinfo.txt), `bench_xvfb` (auto-spawn Xvfb on `:97` when `DISPLAY` is unset), `bench_audio` (a manual DSP loop that deliberately avoids PortAudio, with a `--no-neon` scalar/vector toggle), `bench_fileio` (1 KB → 8 MB read/write cycles) and `bench_gfx` (a 640×480 representative render loop). **That base is not re-specified here.** This task states only what changes to make it device-aware.

- [ ] **Step 1: Bring in the bench base**

If the GKD plan's bench has already landed on this branch, keep it and apply Steps 2–5. If not, implement the GKD plan's Tasks 4–8 first and then apply Steps 2–5. Either way the final files must match the interfaces above.

- [ ] **Step 2: Add the device flags to `main.c`**

Add to the option table and the parsing switch:
```c
    {"device", required_argument, 0, 'D'},
    {"display", required_argument, 0, 'W'},
    {"workdir", required_argument, 0, 'K'},
    {"gpu", required_argument, 0, 'G'},
```
and to `usage()`:
```c
"  --device=ID        device profile id (stamps the report, picks the display)\n"
"  --display=WxH      gfx window size (default: the profile's panel size)\n"
"  --workdir=DIR      fileio working directory (default: the profile's data dir)\n"
"  --gpu=API          gles2 | gles3 (default: the profile's gpu.api)\n"
```

- [ ] **Step 3: Resolve the profile and stamp it into the report**

`main.c` gains a small resolver that shells out to the profile loader and
parses the values it needs out of the shell assignments:

```c
/* Resolves the device profile on the HOST, not on the device: the profile is
   a build/deploy artefact and the device does not carry the TOML. The host
   stamps the resolved values into the report so on-device runs are still
   attributable. Returns 0 when no profile could be resolved. */
static int bench_resolve_profile(const char *device, int *w, int *h,
                                  char *api, size_t api_sz, char *data_dir,
                                  size_t dir_sz);
```

The resolver runs `scripts/lib/profile.py --shell <id>` from the repository
root and reads `DISPLAY_WIDTH`, `DISPLAY_HEIGHT`, `GPU_API` and
`PATHS_DATA_DIR` out of the output. When `--device` is absent, or the
resolver fails, the defaults are 640×480, `gles2` and a `mkdtemp` under
`/tmp`. The resolved values are written into the report with the existing
`bench_report_str` / `bench_report_metric` calls:

```c
    bench_report_str(r, "meta", "device_id", device ? device : "host");
    bench_report_metric(r, "meta", "display_width", w);
    bench_report_metric(r, "meta", "display_height", h);
    bench_report_str(r, "meta", "gpu_api", api);
```

- [ ] **Step 4: Make `bench_xvfb` device-aware**

```c
pid_t bench_xvfb_start_if_needed(int headless_device);
```
Returns 0 immediately when `headless_device` is non-zero (the device has a real display and must not spawn an X server), 0 when `DISPLAY` is already set, the child PID when it spawned Xvfb, and -1 on failure. The screen size passed to `Xvfb` comes from the resolved profile rather than being hardcoded to 640×480.

- [ ] **Step 5: Make `bench_fileio` use `--workdir`**

Replace the hardcoded `char dir[] = "/tmp/spectrax_bench_fileio_XXXXXX"; mkdtemp(dir)` with a resolved working directory: the `--workdir` value if given, else the profile's data dir if a device was selected, else a `mkdtemp` under `/tmp`. **Rationale:** on a handheld `/tmp` is commonly a tmpfs, so the default on-device measurement must target the real storage volume or the fileio numbers describe RAM, not the SD card.

- [ ] **Step 6: Make `bench_gfx` use the resolved window size**

Replace `const int W = 640, H = 480;` with values resolved from `--display` or the profile, and record them in the report as `gfx.window_size`.

- [ ] **Step 7: Extend `tests/dsp/test_bench_report.c`**

Add a case asserting that the report writer accepts a device stamp:
```c
static int test_device_stamp(void) {
    char tmpl[] = "/tmp/spectrax_bench_XXXXXX";
    char *out = mkdtemp(tmpl);
    ASSERT_TRUE(out != NULL, "mkdtemp");

    BenchReport *r = bench_report_new(out);
    bench_report_str(r, "meta", "device_id", "rg35xxsp");
    bench_report_str(r, "meta", "device", "Anbernic RG35XX SP");
    bench_report_metric(r, "meta", "display_width", 640);
    bench_report_metric(r, "meta", "display_height", 480);
    ASSERT_TRUE(bench_report_flush(r) == 0, "flush");
    bench_report_free(r);

    char path[1024];
    snprintf(path, sizeof path, "%s/summary.json", out);
    char *json = slurp(path);
    cJSON *root = cJSON_Parse(json);
    ASSERT_TRUE(root != NULL, "json parses");
    cJSON *meta = cJSON_GetObjectItemCaseSensitive(root, "meta");
    ASSERT_TRUE(meta != NULL, "meta section");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(meta, "device_id");
    ASSERT_TRUE(cJSON_IsString(id) && strcmp(id->valuestring, "rg35xxsp") == 0,
                "device_id stamp");
    cJSON_Delete(root);
    free(json);
    return 0;
}
```
and call `test_device_stamp();` from `main`.

- [ ] **Step 8: Build and test**

```bash
meson setup build --reconfigure >/dev/null
meson compile -C build spectrax_bench
meson test -C build test_bench_report
```

Expected: `Ok: 1  Fail: 0`.

- [ ] **Step 9: Smoke-test the new flags**

```bash
./build/spectrax_bench --help | grep -E "device|display|workdir|gpu"
./build/spectrax_bench --bench=fileio --device gkd-pixel2 --out=/tmp/bench_dev
python3 -c "import json;d=json.load(open('/tmp/bench_dev/summary.json'));print(d['meta'])"
```

Expected: the help lists the four flags; `meta` contains `device_id`.

- [ ] **Step 10: Commit**

```bash
git add src/tools/bench/ meson.build tests/dsp/test_bench_report.c tests/meson.build
git commit -m "bench: device-profile aware display, workdir and report stamping"
```

---

### Task 12: Parameterise `tests/dsp/test_qemu_smoke.sh`

**Files:**
- Create: `tests/dsp/test_qemu_smoke.sh`
- Modify: `tests/meson.build`

**Interfaces:**
- `tests/dsp/test_qemu_smoke.sh [--device ID]` — cross-compiles one test for the profile's arch and runs it under `qemu-<arch>-static`, asserting exit 0.

- [ ] **Step 1: Write the script**

```sh
#!/bin/sh
# test_qemu_smoke.sh — verify the cross-compile + qemu-user path works.
#
# Usage: test_qemu_smoke.sh [--device ID]
set -eu

device=${SPECTRAX_DEVICE:-}
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"

[ -n "$device" ] || device=gkd-pixel2

arch=$(scripts/lib/profile.py --get "$device" device.arch)
case "$arch" in
    aarch64) qemu=qemu-aarch64-static ;;
    armhf)   qemu=qemu-arm-static ;;
    *) echo "unsupported arch: $arch" >&2; exit 1 ;;
esac

cross=build-$device
if [ ! -f "$cross/cross.ini" ]; then
    scripts/gen-cross-file.sh "$device" --out "$cross" >/dev/null
fi

meson setup "$cross" --cross-file "$cross/cross.ini" --reconfigure >/dev/null
meson compile -C "$cross" test_voice

test_bin="$cross/test_voice"
[ -x "$test_bin" ] || { echo "test_voice not built" >&2; exit 1; }

out=$(mktemp)
trap 'rm -f "$out"' EXIT
"$test_bin" > "$out" 2>&1 || {
    echo "FAIL: test_voice failed under $qemu" >&2
    cat "$out" >&2
    exit 1
}
echo "OK: test_voice passed under $qemu"
```

- [ ] **Step 2: Register and run it**

```bash
chmod +x tests/dsp/test_qemu_smoke.sh
# in tests/meson.build:
test('test_qemu_smoke',
  find_program('dsp/test_qemu_smoke.sh'),
  args: [],
)
meson test -C build test_qemu_smoke
```

Expected: `Ok: 1  Fail: 0` (requires `qemu-aarch64-static` and the aarch64 bundle from Task 5).

- [ ] **Step 3: Commit**

```bash
git add tests/dsp/test_qemu_smoke.sh tests/meson.build
git commit -m "test: profile-driven cross-compile smoke test"
```

---

### Task 13: Retrofit the GKD Pixel 2 onto the shared rig

**Files:**
- Move: `scripts/extract-armbian-rk3326.sh` → `scripts/devices/gkd-pixel2/extract-armbian-rk3326.sh`
- Move: `scripts/run-qemu-rk3326.sh` → `scripts/devices/gkd-pixel2/run-qemu-rk3326.sh`
- Modify: `docs/superpowers/plans/2026-09-20-gkd-pixel2-tooling.md` (a note pointing at the new home) — only if that plan has already landed on this branch.

**Interfaces:**
- `scripts/devices/gkd-pixel2/run-qemu-rk3326.sh` is reachable as `run-qemu-rk3326.sh` once `scripts/lib/profile.sh` has prepended `scripts/devices/gkd-pixel2` to `PATH`.

**Background.** These two scripts are the **only** genuinely device-specific code in the whole rig, and they are device-specific for a real reason: the Tier C system-emulation tier needs an Armbian RK3326 rootfs and a QEMU RK3326 machine model. There is no Armbian H700 image and QEMU has no H700 model, so the RG35XX SP cannot reuse them. Everything else — including `gpu-check.sh`, which the GKD plan wrote for libmali — moves into the shared, profile-driven probe suite.

- [ ] **Step 1: Move the two scripts under the device directory**

```bash
mkdir -p scripts/devices/gkd-pixel2
git mv scripts/extract-armbian-rk3326.sh scripts/devices/gkd-pixel2/
git mv scripts/run-qemu-rk3326.sh scripts/devices/gkd-pixel2/
chmod +x scripts/devices/gkd-pixel2/*.sh
```

- [ ] **Step 2: Fix the internal path references in the moved scripts**

`run-qemu-rk3326.sh` looks for a DTB at `"$rootfs/boot/dtb/rk3326-gkd-pixel2.dtb"` — leave that; it is correct. `extract-armbian-rk3326.sh` uses `mount`/`rsync` only — leave that. No path fix is needed; the move is purely organisational.

- [ ] **Step 3: Prove the override directory is on `PATH` for the right device only**

```bash
SPECTRAX_DEVICE=gkd-pixel2
. scripts/lib/profile.sh
command -v run-qemu-rk3326.sh
SPECTRAX_DEVICE=rg35xxsp
. scripts/lib/profile.sh 2>/dev/null || true
command -v run-qemu-rk3326.sh || echo "not on PATH for the RG35XX SP (correct)"
```

Expected: the first prints a path under `scripts/devices/gkd-pixel2`; the second prints "not on PATH … (correct)".
- [ ] **Step 4: Run the GKD dry-run regression**

```bash
# The GKD deploy path must still resolve to its own profile values.
SPECTRAX_DEVICE=gkd-pixel2
. scripts/lib/profile.sh
echo "$PATHS_PORTS_DIR $CFW_PASSWORD_SOURCE $TRANSPORT_ORDER"
```

Expected: `/roms/ports fixed wifi-ssh sd`.

- [ ] **Step 5: Commit**

```bash
git add scripts/devices/gkd-pixel2
git commit -m "deploy: move the RK3326 system-emulation helpers under the GKD device profile"
```

---

# Phase C — The Anbernic RG35XX SP specifics

*Medium risk → high risk. These tasks touch real hardware and are the ones that can fail.*

---

### Task 14: `devices/rg35xxsp.toml` — the verified fact set

**Files:**
- Create: `devices/rg35xxsp.toml`

**Interfaces:**
- A profile that lints clean and whose `[probes] needed` list is exactly the set of facts that could not be established without the device.

**Background — every value below is sourced.** The full source table lives in the spec (§5.1). Sources in brief: Anbernic's product page and the ROCKNIX wiki for the marketing specs; `sun50i-h616.dtsi`, `sun50i-h616-cpu-opp.dtsi`, `sun50i-h700-anbernic-rg35xx-sp.dts`, `sun50i-h700-anbernic-rg35xx-plus.dts` and `sun50i-h700-anbernic-rg35xx-2024.dts` from the mainline kernel for the device tree; ROCKNIX's `linux.aarch64.conf` for the kernel config; ROCKNIX's `usbgadget` source and `081-usbgadget` autostart for the USB gadget story; the ROCKNIX wiki for the SSH/credential policy and the fake-suspend behaviour.

- [ ] **Step 1: Write the profile**

```toml
# Anbernic RG35XX SP — clamshell, Allwinner H700, Cortex-A53, Mali-G31 MP2,
# 1 GB RAM, 3.5" IPS 640x480.
#
# Sources for every value are recorded in
# docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md section 5.1.
# Values marked "probed" below are still open and are listed in [probes].

[device]
id        = "rg35xxsp"
vendor    = "Anbernic"
model     = "RG35XX SP"
soc       = "Allwinner H700"
arch      = "aarch64"        # ROCKNIX H700 kernel is aarch64 (linux.aarch64.conf)
cpu_core  = "cortex-a53"     # sun50i-h616.dtsi: compatible = "arm,cortex-a53" x4
cpu_mhz   = 1500             # OPP table tops out at 1512 MHz; ES offers a 1.5 GHz OC
cores     = 4
gpu       = "Mali-G31 MP2"   # DT node: "allwinner,sun50i-h616-mali", "arm,mali-bifrost"
ram_mb    = 1024             # Anbernic: 1 GB DDR3. ROCKNIX wiki says LPDDR4 -- disputed.
ram_type  = "DDR3"
storage   = "microSD"        # ROCKNIX wiki: no eMMC

[cfw]
family          = "rocknix"
hostname        = "RG35XX-SP"
user            = "root"
password        = ""
password_source = "user-set"  # ROCKNIX regenerates the root password every boot
ssh_enabled_by_default = false # ROCKNIX ships with SSH off; enable it in ES

[display]
width  = 640
height = 480
scale  = 1.0
native = true                 # the panel is 640x480, matching the Spectrax surface

[gpu]
driver     = "panfrost"       # ROCKNIX wiki: Panfrost, GL 3.1 / GLES 3.1
api        = "gles3"
helper     = ""               # no GPU-switch helper is known on this device
compositor = "sway"           # ROCKNIX uses Sway (Wayland)
platform   = "drm"

[paths]
storage_root = "/storage"     # ROCKNIX writable root
ports_dir    = "/storage/roms/ports"
data_dir     = "/storage/roms/ports/spectrax"
runs_dir     = "/storage/roms/ports/spectrax/runs"

[transport]
order       = ["usb-gadget", "wifi-ssh", "sd"]
mdns_names  = ["rocknix.local", "rg35xxsp.local"]
usb_gadget_host_ip   = "169.254.170.1"   # ROCKNIX usbgadget default
usb_gadget_device_ip = "169.254.170.2"

[build]
cross_file = "meson/cross/aarch64-linux-gnu.ini"
libdir     = "lib/aarch64"
c_args     = ["-mcpu=cortex-a53", "-mtune=cortex-a53", "-O3",
              "-ftree-vectorize", "-ffast-math", "-flto"]

[probes]
needed = ["userspace_arch", "ports_dir", "usb_gadget", "display_mode",
          "audio_device", "cpu_governor", "lid_switch", "ram_type"]
```

- [ ] **Step 2: Lint it**

```bash
scripts/lib/profile.py --lint rg35xxsp
```

Expected: exit 0, no output.

- [ ] **Step 3: Check it into the test suite's profile sweep**

`tests/dsp/test_device_profile.sh` already lints every profile in `devices/`. Run it:

```bash
meson test -C build test_device_profile
```

Expected: `Ok: 1  Fail: 0`.

- [ ] **Step 4: Commit**

```bash
git add devices/rg35xxsp.toml
git commit -m "tooling: Anbernic RG35XX SP device profile with sourced facts and open probes"
```

---

### Task 15: USB-gadget transport

**Files:**
- Modify: `scripts/deploy-spectrax.sh` (already walks `transport.order`; verify the `usb-gadget` branch is complete)
- Modify: `scripts/discover-device.sh` (already tries the gadget address first; verify)
- Create: `scripts/usb-gadget.sh` — a small host-side helper that drives the device's `usbgadget` over SSH and reports the link state from the host.

**Interfaces:**
- `scripts/usb-gadget.sh --device ID [--mode {network|file_transfer|disabled}] [--ip IP]` — with `--mode`, runs `usbgadget <mode>` on the device over SSH. Without `--mode`, reports: the device's current gadget mode, the gadget address, and whether the host sees a matching NIC with the expected host IP.

**Background — the highest-value probe in the plan.** The chain of evidence is:

1. The device tree wires the single USB-C receptacle to `&usbotg` with `dr_mode = "peripheral"` and comments it as "USB type-C receptacle" (`sun50i-h700-anbernic-rg35xx-2024.dts`).
2. The AXP717 PMIC provides a USB Type-C role switch (`x-powers,axp717-usb-power-supply`).
3. The ROCKNIX H700 kernel is built with `CONFIG_USB_GADGET=y`, `CONFIG_USB_F_NCM=y`, `CONFIG_USB_F_ECM=y`, `CONFIG_USB_F_RNDIS=y`, `CONFIG_USB_CONFIGFS_ECM=y`, `CONFIG_USB_CONFIGFS_RNDIS=y`, `CONFIG_USB_OTG=y`, `CONFIG_USB_MUSB_SUNXI=y`, `CONFIG_USB_MUSB_DUAL_ROLE=y`.
4. ROCKNIX ships `usbgadget` with a `network` mode that builds a **USB NCM** gadget and assigns `169.254.170.2` to the device and `169.254.170.1` to the host, started by the `081-usbgadget` autostart script.
5. The H700 platform ships a `sleep.d/post/005-usbgadget` quirk because "musb drops the USB session on suspend and does not restart it on resume" — which only makes sense if the H700 runs a gadget in practice.

What is **not** established: that the physical port and cable enumerate on this particular unit, and that EmulationStation's gadget-mode toggle is wired up for the H700. Those are what Task 15's first step measures.

- [ ] **Step 1: Run the probe on the device**

```bash
# With the device booted, the USB-C cable attached and a data-capable cable:
scripts/probe-usb.sh --device rg35xxsp --ip <device-ip>
```

Expected (if the probe works): `/sys/class/udc/` is populated, the UDC function is `cdc` or `mtp`, `usbgadget` prints `network`, and `usbgadget address` prints `169.254.170.2`.

**If the probe shows the gadget is disabled**, switch it on from EmulationStation (Main Menu → Network Settings → gadget mode → network) and re-run the probe. If it still shows nothing, the port is charge-only on this unit and the `usb-gadget` transport must be removed from `transport.order` — record that in the profile and in the spec's open-item list.

- [ ] **Step 2: Apply the probe's findings**

```bash
scripts/probe-usb.sh --device rg35xxsp --ip <device-ip> --apply
```

Expected: the host-local override records the gadget addresses; `devices/rg35xxsp.toml`'s `[probes] needed` loses `usb_gadget`.

- [ ] **Step 3: Write `scripts/usb-gadget.sh`**

```sh
#!/bin/sh
# usb-gadget.sh — inspect or drive the device's USB gadget.
#
# Usage: usb-gadget.sh --device ID [--ip IP] [--mode network|file_transfer|disabled]
set -eu

device=${SPECTRAX_DEVICE:-}
ip=""
mode=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device) shift; device="${1:-$device}" ;;
        --device=*) device="${1#--device=}" ;;
        --ip) shift; ip="${1:-$ip}" ;;
        --ip=*) ip="${1#--ip=}" ;;
        --mode) shift; mode="${1:-$mode}" ;;
        --mode=*) mode="${1#--mode=}" ;;
        *) echo "unknown arg: $1" >&2; exit 64 ;;
    esac
    shift
done
[ -n "$device" ] || { echo "--device is required" >&2; exit 64; }

repo_root=$(cd "$(dirname "$0")/.." && pwd)
export SPECTRAX_REPO_ROOT="$repo_root"
cd "$repo_root"

# shellcheck disable=SC1091
SPECTRAX_DEVICE="$device"
. "$repo_root/scripts/lib/profile.sh"

[ -n "$ip" ] || ip=$(scripts/discover-device.sh --device "$device" 2>/dev/null || true)

if [ -n "$mode" ]; then
    [ -n "$ip" ] || { echo "no device IP; pass --ip" >&2; exit 1; }
    echo "setting gadget mode to $mode on $ip"
    ssh "$CFW_USER@$ip" "usbgadget $mode"
    exit $?
fi

echo "=== device side ==="
if [ -n "$ip" ]; then
    ssh -o ConnectTimeout=5 "$CFW_USER@$ip" \
        "usbgadget 2>/dev/null; echo address=\$(usbgadget address 2>/dev/null)" || echo "(ssh unavailable)"
else
    echo "(no device IP yet)"
fi

echo
echo "=== host side ==="
host_ip=$TRANSPORT_USB_GADGET_HOST_IP
if [ -n "$host_ip" ]; then
    if ip addr show 2>/dev/null | grep -q "$host_ip/"; then
        echo "host has $host_ip"
        ip -br addr | grep "$host_ip/" || true
    else
        echo "host does NOT have $host_ip -- the gadget link is not up"
        echo "expected a usb0 or gadget interface; got:"
        ip -br addr | grep -E 'usb|gadget' || echo "  (none)"
    fi
fi
```

- [ ] **Step 4: Verify the end-to-end USB deploy**

```bash
scripts/deploy-spectrax.sh --device rg35xxsp --transport usb-gadget
```

Expected: `transport: usb-gadget (169.254.170.1)` and a successful rsync.

- [ ] **Step 5: Commit**

```bash
git add scripts/usb-gadget.sh scripts/deploy-spectrax.sh
git commit -m "deploy: USB gadget transport (NCM) for devices with an OTG-capable port"
```

- [ ] **Step 6: Update the spec's open-item list**

In `docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md`, move §9.2 from "open" to "resolved" (or record that the port is charge-only), citing the probe output. Commit:

```bash
git add -f docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md
git commit -m "docs: record the outcome of the USB gadget probe"
```

---

### Task 16: Panfrost / DRM bring-up on the device

**Files:**
- Modify: `scripts/probe-gpu.sh` (already has a `panfrost` branch; extend it with the EGL/DRM environment checks)
- Modify: `scripts/build-target-libs.sh` (if the raylib build needs adjusting for the observed GL stack)

**Interfaces:**
- `scripts/probe-gpu.sh --device rg35xxsp --ip <ip>` — reports the DRM card, the connector mode, the GL renderer string and whether a GLES2 context can be created.

- [ ] **Step 1: Run the GPU probe**

```bash
scripts/probe-gpu.sh --device rg35xxsp --ip <ip>
```

Expected: `/dev/dri/card0` and `/dev/dri/renderD128` exist; the connector mode is `640x480`; the renderer string names `panfrost`; `es2_info` (if installed) reports GLES 3.x.

- [ ] **Step 2: Confirm the DRM/GBM/EGL stack is present on the device**

```bash
ssh root@<ip> 'ls /usr/lib/*/libgbm* /usr/lib/*/libEGL* /usr/lib/*/libGLESv2* 2>/dev/null; ls /dev/dri'
```

Expected: the libraries and the DRM nodes exist. If `libgbm` is missing, add it to the ROCKNIX image (Packages → mesa/mesa-dri) and record that as a device prerequisite in the spec.

- [ ] **Step 3: Establish the environment the bench needs**

Under Sway the bench inherits `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR` from the compositor. When launched from EmulationStation's Ports menu those are set; when launched over SSH they are not. Record the working invocation in the runbook:

```sh
# From EmulationStation / PortMaster: the compositor env is already present.
# Over SSH:
export XDG_RUNTIME_DIR=/run/user/0
export WAYLAND_DISPLAY=wayland-1
export EGL_PLATFORM=surfaceless   # only if the DRM path needs it
```

- [ ] **Step 4: Run a one-frame gfx smoke on the device**

```bash
scripts/deploy-spectrax.sh --device rg35xxsp
ssh root@<ip> 'cd /storage/roms/ports/spectrax && ./spectrax_bench --bench=gfx --duration=1 --out=/tmp/gfxsmoke'
ssh root@<ip> 'cat /tmp/gfxsmoke/summary.json'
```

Expected: the `gfx` section reports a finite `frame_ms_avg` and `fps_avg`; no GL errors in `console.log`.

- [ ] **Step 5: Adjust `gpu.api` in the profile from the measurement**

If the device reports GLES 3.1 but the bench's GLES2 path is what works, set `gpu.api = "gles2"` in the profile and note why. Commit:

```bash
git add devices/rg35xxsp.toml
git commit -m "tooling: record the measured GL API for the RG35XX SP"
```

---

### Task 17: Clamshell — lid switch and fake suspend

**Files:**
- Modify: `src/tools/bench/main.c` (signal-safe flush already required by the GKD spec; extend the signal set)
- Modify: `scripts/portmaster/spectrax_bench.sh` (document the suspend interaction)
- Create: `docs/superpowers/plans/RG35XXSP-RUNBOOK.md`

**Interfaces:**
- The bench flushes a partial report on `SIGINT`, `SIGTERM` **and** `SIGHUP`, so a suspend-driven kill still leaves a usable report.

**Background.** The RG35XX SP's device tree defines a `lid-switch` (`SW_LID`, GPIO PE7, `wakeup-source`) and ROCKNIX has no hardware suspend, so the lid triggers "fake suspend": the screen and controls go away, and a timed shutdown may follow depending on the ES System Settings → Suspend configuration. The GKD Pixel 2 has no lid at all, so this is genuinely device-specific behaviour — but the *tooling* response is shared, because the GKD spec already requires a signal-safe report flush.

- [ ] **Step 1: Extend the bench's signal handling**

In `src/tools/bench/main.c`, install handlers for `SIGHUP` alongside `SIGINT`/`SIGTERM`:
```c
    signal(SIGINT,  bench_signal_handler);
    signal(SIGTERM, bench_signal_handler);
    signal(SIGHUP,  bench_signal_handler);
```
and document that on the RG35XX SP a lid close reaches the process as a hangup/termination during fake suspend.

- [ ] **Step 2: Run the lid probe**

```bash
scripts/probe-lid.sh --device rg35xxsp --ip <ip>
```

Expected: the report says whether the process survived the lid close and dumps the relevant `dmesg` lines.

- [ ] **Step 3: Write `docs/superpowers/plans/RG35XXSP-RUNBOOK.md`**

```markdown
# RG35XX SP runbook

## Before the first deploy
1. Flash ROCKNIX (H700 aarch64) and copy the right DTB
   (`sun50i-h700-anbernic-rg35xx-sp.dtb`, or the `-v2-panel` variant) to the
   root of the ROCKNIX partition, renamed `dtb.img`. The two are not
   interchangeable: the wrong one gives a garbled screen.
2. Boot. In EmulationStation: Main Menu -> Network Settings -> Enable Network,
   pick the SSID, enter the key.
3. Main Menu -> Network Settings -> Network Services -> Enable SSH.
4. Main Menu -> System Settings -> set a **root password** (ROCKNIX regenerates
   it on every boot otherwise).
5. USB-C: if the transport is `usb-gadget`, Main Menu -> Network Settings ->
   gadget mode -> network.

## Probing the device
    scripts/probe-os.sh      --device rg35xxsp --ip <ip>
    scripts/probe-paths.sh   --device rg35xxsp --ip <ip>
    scripts/probe-usb.sh     --device rg35xxsp --ip <ip> --apply
    scripts/probe-display.sh --device rg35xxsp --ip <ip>
    scripts/probe-audio.sh   --device rg35xxsp --ip <ip>
    scripts/probe-cpu.sh     --device rg35xxsp --ip <ip>
    scripts/probe-mem.sh     --device rg35xxsp --ip <ip>
    scripts/probe-lid.sh     --device rg35xxsp --ip <ip>
    scripts/probe-storage.sh --device rg35xxsp --ip <ip>
    scripts/probe-gpu.sh     --device rg35xxsp --ip <ip>

## Building
    scripts/gen-cross-file.sh rg35xxsp
    meson setup build-rg35xxsp --cross-file build-rg35xxsp/cross.ini --buildtype=release
    meson compile -C build-rg35xxsp spectrax spectrax_bench instrument_harness

## Deploying
    scripts/deploy-spectrax.sh --device rg35xxsp            # auto transport
    scripts/deploy-spectrax.sh --device rg35xxsp --launch --out=runs/

## Benchmarking
    ssh root@<ip> 'cd /storage/roms/ports/spectrax && \
      ./spectrax_bench --bench=all --device rg35xxsp --out=/storage/roms/ports/spectrax/runs'

## Known device quirks
- The lid triggers ROCKNIX fake suspend. A bench run interrupted this way
  leaves a partial report (the bench flushes on SIGHUP/SIGTERM).
- The speaker and the headphone jack share an analog mux on the codec; the
  default ALSA route is the speaker.
- `/tmp` may be a tmpfs, so the fileio bench must be pointed at the real
  storage volume (`--workdir`).
```

- [ ] **Step 4: Commit**

```bash
git add src/tools/bench/main.c scripts/portmaster/spectrax_bench.sh
git add -f docs/superpowers/plans/RG35XXSP-RUNBOOK.md
git commit -m "bench: signal-safe flush across suspend, plus the RG35XX SP runbook"
```

---

### Task 18: Tier D device run and the Tier C decision record

**Files:**
- Modify: `docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md` (§8.4 and §9 — record the probe outcomes)
- Create: `docs/superpowers/plans/RG35XXSP-RUNBOOK.md` (already created in Task 17; extend with the measured results)

**Interfaces:**
- None. This task closes the loop: every open item in the spec's §9 is either resolved with a probe output or explicitly carried forward.

- [ ] **Step 1: Run the full device bench**

```bash
scripts/deploy-spectrax.sh --device rg35xxsp --launch --out=runs/rg35xxsp
```

Expected: `runs/rg35xxsp/<timestamp>/summary.json` with populated `audio`, `gfx` and `fileio` sections.

- [ ] **Step 2: Record the Tier C decision**

Tier C (QEMU system emulation) **does not apply** to the RG35XX SP. There is no Armbian H700 image and QEMU has no H700 machine model; the closest approximation would be `qemu-system-aarch64 -M virt -cpu cortex-a53` with a mainline kernel and the H700 DTB, which is a research project rather than a smoke test. The GKD keeps its RK3326 Tier C helpers under `scripts/devices/gkd-pixel2/`. Write this decision into the spec's §8.4.

- [ ] **Step 3: Update the spec's open-item list with the probe outcomes**

For each of §9.1–§9.14, record either the resolved value (with the probe's output) or "still open, probe re-run on <date>".

- [ ] **Step 4: Update the reuse matrix if reality disagreed with the spec**

If any component turned out to need separate code, record it in §7 with the reason.

- [ ] **Step 5: Commit**

```bash
git add -f docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md
git commit -m "docs: close the RG35XX SP open items with the probe results"
```

---

## Self-Review Checklist

- **Spec coverage:**
  - Device-profile abstraction (schema, loader, linter, two profiles side by side) ✓ Task 2, Task 14
  - Reuse matrix — shared / parameterise / device-specific ✓ §7 of the spec, implemented by Tasks 3, 7, 8, 9, 13
  - Cross-compile story composing with `meson/cross/*.ini` ✓ Tasks 1, 3, 4, 5
  - Deploy story: primary + fallback, shared `deploy-spectrax.sh` unchanged for both devices ✓ Task 7, Task 15
  - Bench suite: shared, device-specific concerns (panel, workdir) ✓ Task 11
  - Testing tiers A/B/C/D — which apply here ✓ Task 12, Task 18 (Tier C explicitly skipped, with a reason)
  - Error handling, risks/open items with a named probe each ✓ Task 10, Task 18
  - How this changes the GKD plan's interpretation ✓ §10 of the spec, Task 13
- **Placeholder scan:** every code block is complete and runnable. The deliberately-loose items are exactly the ones the probes resolve, and each carries the probe that resolves it.
- **Type consistency:**
  - `DEVICE_*` / `CFW_*` / `DISPLAY_*` / `GPU_*` / `PATHS_*` / `TRANSPORT_*` / `BUILD_*` / `PROBES_*` are produced by `scripts/lib/profile.py --shell` and consumed by `scripts/lib/profile.sh`; the schema in Task 2 Step 1 is the normative list.
  - `bench_report_*` signatures unchanged from the GKD plan; the device stamp uses the existing `bench_report_str` / `bench_report_metric` calls.
  - The bench CLI flags are the union of the GKD plan's flags and the four device flags; the GKD flags keep their original meanings.
- **YAGNI:** the probe suite is the minimum needed to close the spec's open items — eleven scripts, each answering one question. No bench history dashboard, no CI, no run-on-save.
- **TDD:** every task has at least one explicit test step (lint, compile, run, assert).
- **Frequent commits:** 18 commits, one per task.

## Final Gate

```bash
# Host build clean.
meson setup build --reconfigure
meson compile -C build 2>&1 | grep -cE "error:"          # expect 0

# Every profile lints.
for id in $(scripts/lib/profile.py --list); do
    scripts/lib/profile.py --lint "$id" || exit 1
done

# Cross builds clean, for both devices.
for id in gkd-pixel2 rg35xxsp; do
    scripts/gen-cross-file.sh "$id" >/dev/null
    meson setup "build-$id" --cross-file "build-$id/cross.ini" --buildtype=release
    meson compile -C "build-$id" spectrax spectrax_bench instrument_harness 2>&1 | grep -cE "error:"
done                                                    # expect 0 for each

# Host tests pass.
meson test -C build 2>&1 | grep -E "Ok:|Fail:"

# Cross-compiled test under QEMU.
sudo scripts/setup-qemu-binfmt.sh --arch all
./build-rg35xxsp/spectrax_bench --bench=fileio --out=/tmp/final_smoke
test -f /tmp/final_smoke/summary.json && echo "FINAL: OK"

# Both device profiles resolve their shared scripts.
for id in gkd-pixel2 rg35xxsp; do
    sh -n scripts/deploy-spectrax.sh
    sh -n scripts/deploy-sd.sh
    sh -n scripts/inspect-sd.sh
done
```

Expected: zero build errors, all tests pass, `summary.json` produced, both profiles lint.

## Final Commit (after all tasks)

```bash
git status --short
git log --oneline main..HEAD
git show --stat HEAD
```

Confirm with the user before any push (project rule #1078).

---

## Plan corrections (applied during execution, 2026-10-22)

The plan was executed task by task. These are the points where reality
diverged from the plan; each correction is in the same branch, with the
change visible in the commit that made it.

1. **Task 1 — the cherry-pick was not needed.** The plan cherry-picks
   `9648016` and then rewrites both cross files as templates. The end state
   is the same as writing the templates directly, so the commit was made
   directly (the `.gitignore` additions from `9648016` were kept).

2. **Task 5 — raylib is compiled directly, not through its Makefile.**
   raylib 5.5's Makefile builds and *links* a shared library, which a cross
   build cannot do without the target's GL stack. The sources are compiled
   straight into `libraylib.a` with `-DPLATFORM_DRM -DGRAPHICS_API_OPENGL_ES2`.

3. **Task 5 — the bundle ships link-time stubs, not the device's libraries.**
   The target's EGL/GLES2/GBM/DRM/ALSA live in the device's userspace; the
   cross sysroot has no aarch64 copy of them and shipping foreign distro
   builds of them would be worse than not shipping them. `lib/<arch>/stub/`
   therefore holds shared objects with the right SONAME and the right
   exported symbol names but no code. They satisfy the link; the loader
   resolves the symbols from the device's real libraries at run time. The
   stubs double as the runtime library set for the QEMU smoke test.

4. **Task 4 — the cross file carries a second property, `spectrax_incdir`.**
   The target headers (`raylib.h`, `portaudio.h`) live in `include/<arch>/`,
   and the host's `include/raylib.h` would otherwise shadow them. The
   property is in the schema (`[build] incdir`) and in both profiles.

5. **Task 4 — `meson install` refuses to run for a cross build.** The install
   script copies the binaries into `bin/`, which holds the host build. It now
   takes the target lib dir as an argument and skips the copy (and the XDG
   provisioning) for a cross target.

6. **Task 7 — the deploy scripts look the binaries up in the build tree.**
   meson puts an executable next to the `meson.build` that declares it, so
   the binaries are at `build-<device>/src/spectrax`,
   `build-<device>/src/tools/bench/spectrax_bench` and
   `build-<device>/src/tools/instrument_harness/instrument_harness`, not at
   the top of the build directory. The plan's `build_dir/$f` paths would
   never have found them.

7. **Task 7 — the plan's `deploy-spectrax.sh` shadows the `ip` command.**
   `ip=""` replaces the command with an empty variable, so every later
   `ip addr show` inside the transport functions fails and the USB-gadget
   transport can never be selected. The variable is now `dev_ip`.

8. **Task 8 — the host override's keys have to be in the schema.** The plan's
   `setup-device.sh` writes `[device] ip` into the override, which the
   linter would reject as an unknown key. The schema now has an explicit
   optional-key set (`device.ip`, `transport.last_ip`, `build.build_dir`).

9. **Task 10 — `inspect-sd.sh` gained a `--dir` mode.** Loop mounting needs
   root, so the image path could not be exercised on the host. `--dir`
   inspects an already mounted card.

10. **Task 11 — the per-mode re-parse of the command line does not work.**
    `main()` parses every option with `getopt_long`, so the per-mode
    re-parse from `optind` finds nothing and the mode always ran with its
    defaults. The modes now take a `BenchOptions` struct that `main()`
    fills in. The CLI contract (the plan's interface list) is unchanged.

11. **Task 11 — the bench's DSP entry points differ from the GKD plan's.**
    `initVoiceManager()` does not exist in this tree: the constructor is
    `createVoiceManager(settings, samplePool, wavetablePool, presetBank)`.
    The bench uses the real constructor, and the bench links
    `core_lib + seq_lib + vizfx_lib + gui_lib` (the app's own set) because
    `createSettings` lives in `seq_lib`.

12. **Task 11 — the `--no-neon` toggle needed a real kernel to measure.** The
    GKD plan toggles an attribute on a wrapper function, which cannot
    change the vectorisation of the library code it calls. The bench now
    mixes the per-voice sample buffers in its own code, with an explicit
    NEON kernel on ARM and a `no-tree-vectorize` scalar fallback, so the
    flag measures something.

13. **Task 12 — the QEMU smoke test needs a guest sysroot.** The cross
    binary's `DT_NEEDED` entries (libEGL.so.1 and friends) are not on the
    host, so the test assembles a sysroot from the cross toolchain's loader
    and libc plus the bundle's stubs and runs the binary with
    `qemu-<arch>-static -L <sysroot>`.

14. **Task 13 — the RK3326 helpers did not exist to move.** The GKD branch
    carries only the two documents and the two cross files, so the helpers
    were written from the GKD plan's code blocks directly into
    `scripts/devices/gkd-pixel2/`.

15. **Cross build — the dev harnesses depend on X11.** `nav_harness` and
    `instrument_harness` include `<X11/Xlib.h>` for their screenshot verb,
    which has no meaning on a DRM/KMS device. The X11 path is now behind
    `-DSPECTRAX_HAVE_X11`, which only a build with X11 defines.

16. **Cross build — `vizulobe` used pkg-config for cJSON.** There is no
    target pkg-config in a cross build, so the subproject now falls back to
    the vendored cJSON.

17. **Task 5 — PortAudio's objects depend on the configure flags.** A
    reconfigure that enables the ALSA backend does not rebuild the objects,
    and the host-API table stays empty (no audio at run time, and the ALSA
    library is dropped from the link by `--as-needed`). The build script
    always cleans first and verifies that the table references
    `PaAlsa_Initialize`.
