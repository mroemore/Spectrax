#!/usr/bin/env python3
"""Copy freshly built binaries into bin/ so the app can be launched with
cwd=bin/ (Spectrax loads resources/ and data/ cwd-relative).

Meson exports MESON_SOURCE_ROOT / MESON_BUILD_ROOT to install scripts and
runs them with cwd = build dir. The executables live under the subdir where
their executable() was declared, so the paths below are relative to the
build root."""
import os
import shutil
import sys

_ = sys.argv  # argv[1] is the meson install prefix; not used

source_root = os.environ.get("MESON_SOURCE_ROOT") or os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))
)
build_root = os.environ.get("MESON_BUILD_ROOT") or os.getcwd()

BIN = os.path.join(source_root, "bin")
os.makedirs(BIN, exist_ok=True)

TARGETS = [
    "src/spectrax",
    "src/tools/nav_harness/nav_harness",
    "src/tools/instrument_harness/instrument_harness",
    "src/tools/sample_analyser/inspectro_wavget",
]

for rel in TARGETS:
    src = os.path.join(build_root, rel)
    if os.path.exists(src):
        shutil.copy2(src, os.path.join(BIN, os.path.basename(rel)))
        print(f"installed {os.path.basename(rel)} -> bin/")
    else:
        print(f"skipped {rel} (not built)")


def provision_xdg_dirs():
    """Provision the XDG config/data dirs so the app works on machines that
    have no ~/.config/spectrax or ~/.local/share/spectrax yet.

    Idempotent and never destructive:
      - config: copies the shipped defaults (bin/cfg.json, bin/clr.json)
        into the config dir ONLY if absent (user config is never clobbered);
      - data:   symlinks data/, resources/, s1.sng into the data dir
        pointing at bin/ (the checkout is the single source of truth);
      - skips when HOME is unset or this is a destdir (packaging) install,
        so `meson install --destdir` never touches the real $HOME.
    """
    if os.environ.get("DESTDIR"):
        print("xdg: skipped (destdir install)")
        return
    home = os.environ.get("HOME")
    if not home:
        print("xdg: skipped (HOME unset)")
        return

    xdg_config = os.environ.get("XDG_CONFIG_HOME") or os.path.join(home, ".config")
    xdg_data = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")

    config_dir = os.path.join(xdg_config, "spectrax")
    data_dir = os.path.join(xdg_data, "spectrax")
    os.makedirs(config_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)

    for name in ("cfg.json", "clr.json"):
        dst = os.path.join(config_dir, name)
        if os.path.exists(dst):
            continue
        src = os.path.join(BIN, name)
        if os.path.exists(src):
            shutil.copy2(src, dst)
            print(f"xdg: config {name} -> {dst}")

    for name in ("data", "resources", "s1.sng"):
        dst = os.path.join(data_dir, name)
        if os.path.islink(dst):
            continue
        if os.path.exists(dst):
            print(f"xdg: skip {name} (exists at {dst}, not a symlink)")
            continue
        src = os.path.join(BIN, name)
        if os.path.exists(src):
            os.symlink(src, dst)
            print(f"xdg: data {name} -> {dst}")


provision_xdg_dirs()