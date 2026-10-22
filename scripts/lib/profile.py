#!/usr/bin/env python3
"""Device profile loader, merger and linter for Spectrax target devices.

A device is data: this module reads devices/<id>.toml, merges the host-local
override ~/.config/spectrax/devices/<id>.toml on top of it, validates the
result against the schema in devices/schema/device-profile.schema.md, and
exports it for shell consumers.
"""
import argparse
import os
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
    "audio": {
        "device": str, "sample_rate": int, "periods_max": int,
        "engine_follows_device_rate": bool,
    },
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
    "build": {"cross_file": str, "libdir": str, "incdir": str, "c_args": list},
    "probes": {"needed": list},
}

REQUIRED = {
    "device": ["id", "vendor", "model", "soc", "arch", "cpu_core", "cpu_mhz",
               "cores", "gpu", "ram_mb", "ram_type", "storage"],
    "cfw": ["family", "hostname", "user", "password", "password_source",
            "ssh_enabled_by_default"],
    "display": ["width", "height", "scale", "native"],
    "audio": ["device", "sample_rate", "periods_max",
              "engine_follows_device_rate"],
    "gpu": ["driver", "api", "helper", "compositor", "platform"],
    "paths": ["storage_root", "ports_dir", "data_dir", "runs_dir"],
    "transport": ["order", "mdns_names", "usb_gadget_host_ip",
                  "usb_gadget_device_ip"],
    "build": ["cross_file", "libdir", "incdir", "c_args"],
    "probes": ["needed"],
}

# Keys the host-local override may add. They are never required.
OPTIONAL = {
    "device": ["ip"],
    "transport": ["last_ip"],
    "build": ["build_dir"],
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
        allowed = SCHEMA[section].keys() | set(OPTIONAL.get(section, []))
        for key in data[section]:
            if key not in allowed:
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
