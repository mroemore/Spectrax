#!/bin/sh
# Copy freshly built binaries into bin/ and provision the XDG config/data
# dirs so the app works on machines with no ~/.config/spectrax or
# ~/.local/share/spectrax (the app resolves resources cwd-relative, so
# without a data dir it finds nothing when launched outside bin/).
#
# Meson runs install scripts with cwd = build dir and exports
# MESON_SOURCE_ROOT / MESON_BUILD_ROOT.
#
# POSIX sh + coreutils only — deliberately no Python (the project targets
# low-power devices where Python may be unavailable).

set -eu

SOURCE_ROOT="${MESON_SOURCE_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_ROOT="${MESON_BUILD_ROOT:-$(pwd)}"
BIN="$SOURCE_ROOT/bin"

mkdir -p "$BIN"

for rel in \
    "src/spectrax" \
    "src/tools/nav_harness/nav_harness" \
    "src/tools/instrument_harness/instrument_harness" \
    "src/tools/sample_analyser/inspectro_wavget" \
    "src/tools/vizulobe/vizulobe"; do
    src="$BUILD_ROOT/$rel"
    if [ -f "$src" ]; then
        # cp + mv (not a direct cp): a running copy of the binary keeps the
        # old inode, so a plain cp would fail with ETXTBSY ("text file busy")
        # and set -e would abort the whole install.
        name="$(basename "$rel")"
        tmp="$BIN/.$name.tmp.$$"
        cp "$src" "$tmp"
        mv -f "$tmp" "$BIN/$name"
        echo "installed $name -> bin/"
    else
        echo "skipped $rel (not built)"
    fi
done

# Sample vizulobe project + example vizzes, kept in sync with the source
# copies so `cd bin && ./vizulobe -p sample_project.json` works out of the
# box. Project paths resolve relative to the project file, so copying all
# three together keeps the references valid.
for name in sample_project.json smoke_viz.c smoke_viz.frag; do
    src="$SOURCE_ROOT/src/tools/vizulobe/$name"
    if [ -f "$src" ]; then
        cp "$src" "$BIN/$name"
        echo "installed $name -> bin/"
    fi
done

# --- XDG provisioning -------------------------------------------------

# Never touch the real $HOME during a destdir (packaging) install.
if [ -n "${DESTDIR:-}" ]; then
    echo "xdg: skipped (destdir install)"
    exit 0
fi
if [ -z "${HOME:-}" ]; then
    echo "xdg: skipped (HOME unset)"
    exit 0
fi

XDG_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_DIR="$XDG_CONFIG/spectrax"
DATA_DIR="$XDG_DATA/spectrax"
mkdir -p "$CONFIG_DIR" "$DATA_DIR"

# Config: copy the shipped defaults only if absent (never clobber user config).
for name in cfg.json clr.json; do
    if [ ! -e "$CONFIG_DIR/$name" ] && [ -f "$BIN/$name" ]; then
        cp "$BIN/$name" "$CONFIG_DIR/$name"
        echo "xdg: config $name -> $CONFIG_DIR/$name"
    fi
done

# Data: symlink assets into the data dir pointing at bin/ (the checkout is
# the single source of truth). Idempotent; never replaces a real file.
for name in data resources s1.sng; do
    if [ -L "$DATA_DIR/$name" ]; then
        continue
    fi
    if [ -e "$DATA_DIR/$name" ]; then
        echo "xdg: skip $name (exists at $DATA_DIR/$name, not a symlink)"
        continue
    fi
    if [ -e "$BIN/$name" ]; then
        ln -s "$BIN/$name" "$DATA_DIR/$name"
        echo "xdg: data $name -> $DATA_DIR/$name"
    fi
done