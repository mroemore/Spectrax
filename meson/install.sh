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

# The target lib dir (arg 1). A cross build targets a device and must not
# overwrite the host binaries in bin/; the deploy takes the binaries
# straight from the build directory.
TARGET_LIBDIR="${1:-lib/linux}"
if [ "$TARGET_LIBDIR" != "lib/linux" ]; then
    echo "cross build ($TARGET_LIBDIR): skipping the bin/ install"
    echo "cross build ($TARGET_LIBDIR): the device deploy uses $BUILD_ROOT directly"
    exit 0
fi

mkdir -p "$BIN"

for rel in \
    "src/spectrax" \
    "src/tools/nav_harness/nav_harness" \
    "src/tools/instrument_harness/instrument_harness" \
    "src/tools/sample_analyser/inspectro_wavget" \
    "src/tools/vizulobe/vizulobe" \
    "src/tools/bench/spectrax_bench"; do
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

# Shipped config defaults — copy from src/ to bin/ on every install so
# the XDG provisioning below can find them. clr.json / cfg.json are
# already tracked in bin/; layout.json is the new kid in town.
if [ -f "$SOURCE_ROOT/src/layout.json" ] && [ ! -e "$BIN/layout.json" ]; then
    cp "$SOURCE_ROOT/src/layout.json" "$BIN/layout.json"
    echo "installed layout.json -> bin/"
fi

# --- XDG provisioning -------------------------------------------------

# Two modes:
#   developer checkout (no DESTDIR, HOME set): provision the user's XDG dirs
#     from bin/ so the app starts working immediately;
#   packaging (DESTDIR set): lay the same tree under the prefix, so a
#     packaged install is self-contained and relocatable.
#
# Assets are COPIED, never symlinked. A symlink into a checkout breaks the
# moment the checkout moves, is garbage after a rebuild, and is invisible to
# any packager walking the tree. Copies also mean a shipped font or theme fix
# reaches an existing install on the next install run, which is the whole
# point of re-installing.
if [ -n "${DESTDIR:-}" ]; then
    PREFIX="${MESON_INSTALL_PREFIX:-/usr/local}"
    TARGET_DATA="$DESTDIR$PREFIX/share/spectrax"
    TARGET_CONFIG="$DESTDIR$PREFIX/share/spectrax/config"
    MODE="destdir"
else
    if [ -z "${HOME:-}" ]; then
        echo "xdg: skipped (HOME unset and no DESTDIR)"
        exit 0
    fi
    TARGET_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/spectrax"
    TARGET_DATA="${XDG_DATA_HOME:-$HOME/.local/share}/spectrax"
    MODE="xdg"
fi
mkdir -p "$TARGET_DATA" "$TARGET_CONFIG"

# A previous install may have left symlinks here; a link would be followed by
# the copy below and the checkout would stay the source of truth.
for name in resources data s1.sng; do
    if [ -L "$TARGET_DATA/$name" ]; then
        rm -f "$TARGET_DATA/$name"
        echo "$MODE: replaced legacy symlink $name"
    fi
done

# Resources are shipped art, fonts and samples: always refreshed, so a fixed
# or replaced asset lands on the next install. --delete keeps the copy honest
# when a file is removed from the repo.
if [ -d "$BIN/resources" ]; then
    if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete "$BIN/resources/" "$TARGET_DATA/resources/"
    else
        rm -rf "$TARGET_DATA/resources"
        mkdir -p "$TARGET_DATA/resources"
        cp -R "$BIN/resources/." "$TARGET_DATA/resources/"
    fi
    echo "$MODE: resources -> $TARGET_DATA/resources"
fi

# Presets are content the user may add to, so shipped ones are copied only
# when the target lacks them.
if [ -d "$BIN/data" ]; then
    if command -v rsync >/dev/null 2>&1; then
        rsync -a --ignore-existing "$BIN/data/" "$TARGET_DATA/data/"
    else
        cp -Rn "$BIN/data/." "$TARGET_DATA/data/" 2>/dev/null || true
    fi
    echo "$MODE: data -> $TARGET_DATA/data"
fi

# The shipped song is a starting point, not an asset: never overwrite a
# user's saved song.
if [ -f "$BIN/s1.sng" ] && [ ! -e "$TARGET_DATA/s1.sng" ]; then
    cp "$BIN/s1.sng" "$TARGET_DATA/s1.sng"
    echo "$MODE: s1.sng -> $TARGET_DATA/s1.sng"
fi

# Config: shipped defaults only when absent (never clobber user config).
for name in cfg.json clr.json layout.json; do
    if [ ! -e "$TARGET_CONFIG/$name" ] && [ -f "$BIN/$name" ]; then
        cp "$BIN/$name" "$TARGET_CONFIG/$name"
        echo "$MODE: config $name -> $TARGET_CONFIG/$name"
    fi
done

echo "$MODE: provisioned $TARGET_DATA"
