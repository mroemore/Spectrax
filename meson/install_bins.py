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
    "src/tools/vizulobe/vizulobe",
]

for rel in TARGETS:
    src = os.path.join(build_root, rel)
    if os.path.exists(src):
        shutil.copy2(src, os.path.join(BIN, os.path.basename(rel)))
        print(f"installed {os.path.basename(rel)} -> bin/")
    else:
        print(f"skipped {rel} (not built)")