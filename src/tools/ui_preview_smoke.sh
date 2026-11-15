#!/bin/sh
# --ui-preview smoke: the panel window boots, renders its frame budget and
# exits 0 without touching the shipped song, theme or settings; and each
# display mode shows the panels it claims to.
#
# Runs from bin/ (resources/, layout.json and the theme are cwd-relative),
# against a scratch config + data dir so the run writes nothing, and pins a
# screen big enough for the panel grid (xvfb-run's default 1280x1024 clips a
# 2x2 grid of 640x480 cells).
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")/../../bin" && pwd)
repo=$(CDPATH= cd -- "$here/.." && pwd)
scratch="${repo}/.tmp_files/ui_preview_smoke"
rm -rf "$scratch"
mkdir -p "$scratch/cfg" "$scratch/data"
# A genuinely empty config dir renders the baked default theme, which is not
# what the preview exists to show. Seed it with the shipped copies so the run
# exercises the real theme and layout, while still writing nothing back.
for f in cfg.json clr.json layout.json; do
    [ -f "$here/$f" ] && cp "$here/$f" "$scratch/cfg/$f"
done
# A song with content, so the note page and the arranger have something to
# draw: an empty project renders a legitimately empty pattern page.
[ -f "$here/s1.sng" ] && cp "$here/s1.sng" "$scratch/data/s1.sng"
# The app chdir()s into the data dir and resolves resources/ and presets
# from there. Without these the run has no font, no dial art and no presets
# -- every panel would render in raylib's 10px default face, which is not a
# preview of the app.
ln -sfn "$here/resources" "$scratch/data/resources"
[ -d "$here/data" ] && ln -sfn "$here/data" "$scratch/data/data"
# The font is watched by absolute path so a reload can be provoked without
# touching the tracked file in bin/.
mkdir -p "$scratch/fonts"
cp "$here/resources/fonts/pixel_times.ttf" "$scratch/fonts/pixel_times.ttf"
sed -i "s|\"path\": *\"resources/fonts/pixel_times.ttf\"|\"path\": \"$scratch/fonts/pixel_times.ttf\"|" "$scratch/cfg/clr.json"

# Hash what the app should leave alone, after seeding: the theme copy is
# rewritten above (font path), so comparing it against bin/ would always fail.
seeded_theme=$(md5sum "${scratch}/cfg/clr.json" | cut -d' ' -f1)
seeded_song=$(md5sum "${scratch}/data/s1.sng" | cut -d' ' -f1)

cd "$here" || exit 1

run_mode() {
    mode=$1
    frames=$2
    shots=$3
    out="${scratch}/run-${mode}.log"
    rm -f "${scratch}/shot-${mode}"-*.png
    timeout -k 2 60 xvfb-run -a -s "-screen 0 1920x1440x24" \
        ./spectrax --ui-preview --ui-preview-mode "$mode" \
        --ui-preview-frames "$frames" --ui-preview-shot "${scratch}/shot-${mode}" \
        --config-dir "$scratch/cfg" --data-dir "$scratch/data" > "$out" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL $mode: exit $rc"
        tail -20 "$out"
        return 1
    fi
    if ! grep -q "^UI_PREVIEW: panels=.* mode=${mode} " "$out"; then
        echo "FAIL $mode: marker does not report the mode"
        grep '^UI_PREVIEW:' "$out"
        return 1
    fi
    got=$(ls "${scratch}/shot-${mode}"-*.png 2>/dev/null | wc -l)
    if [ "$got" -ne "$shots" ]; then
        echo "FAIL $mode: $got shots, want $shots"
        ls "${scratch}/shot-${mode}"-*.png 2>/dev/null
        return 1
    fi
    for f in "${scratch}/shot-${mode}"-*.png; do
        colors=$(identify -format '%k' "$f" 2>/dev/null)
        if [ "${colors:-0}" -le 2 ]; then
            echo "FAIL $mode: panel looks blank: $f"
            return 1
        fi
    done
    return 0
}

# The all-panels mode also exercises hot reload: touch the live layout and the
# font copy mid-run and require both reload paths to fire.
( sleep 1.5; touch "${scratch}/cfg/layout.json"
  sleep 0.7; touch "${scratch}/fonts/pixel_times.ttf" ) > /dev/null 2>&1 &
touch_pid=$!
run_mode all 300 5 || exit 1
wait "$touch_pid" 2>/dev/null

out="${scratch}/run-all.log"
if ! grep -q '^UI_PREVIEW: reload mask=0x5' "$out"; then
    echo "FAIL a layout edit did not recompile styles and layouts"
    grep '^UI_PREVIEW:' "$out"
    exit 1
fi
if ! grep -Eq '^UI_PREVIEW: reload mask=0x[2367]' "$out"; then
    echo "FAIL a font edit did not reload the atlas"
    grep '^UI_PREVIEW:' "$out"
    exit 1
fi
if grep -q 'reload failed' "$out"; then
    echo "FAIL a reload failed"
    grep 'reload failed' "$out"
    exit 1
fi

# Five panels, four per page: paged shows a full page, single shows one.
run_mode paged 60 4 || exit 1
run_mode single 60 1 || exit 1

# The preview must not save. The theme is seeded, so compare it rather than
# looking for its presence: a session that fell through the normal teardown
# rewrites clr.json and writes s1.sng.
if [ "$(md5sum "${scratch}/data/s1.sng" | cut -d' ' -f1)" != "$seeded_song" ]; then
    echo "FAIL preview rewrote the song"
    exit 1
fi
if [ "$(md5sum "${scratch}/cfg/clr.json" | cut -d' ' -f1)" != "$seeded_theme" ]; then
    echo "FAIL preview rewrote the theme"
    exit 1
fi

for f in "$scratch"/run-*.log; do
    printf '%s: %s\n' "$(basename "$f" .log)" \
        "$(grep '^UI_PREVIEW: panels=' "$f" | tail -1)"
done
echo "PASS ui_preview_smoke"