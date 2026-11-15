#!/bin/sh
# --ui-preview smoke: the panel window boots, renders its frame budget and
# exits 0 without touching the shipped song, theme or settings.
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

cd "$here" || exit 1
out="${scratch}/run.log"
# Touch the live layout mid-run: the preview must notice and recompile
# without dying or losing the panels.
( sleep 1.5; touch "${scratch}/cfg/layout.json" ) > /dev/null 2>&1 &
touch_pid=$!
timeout -k 2 60 xvfb-run -a -s "-screen 0 1920x1440x24" \
    ./spectrax --ui-preview --ui-preview-frames 300 \
    --ui-preview-shot "${scratch}/shot" \
    --config-dir "$scratch/cfg" --data-dir "$scratch/data" > "$out" 2>&1
rc=$?
wait "$touch_pid" 2>/dev/null
if ! grep -q '^UI_PREVIEW: reload mask=' "$out"; then
    echo "FAIL the layout edit was not picked up"
    tail -20 "$out"
    exit 1
fi

if [ "$rc" -ne 0 ]; then
    echo "FAIL exit $rc"
    tail -20 "$out"
    exit 1
fi
if ! grep -q '^UI_PREVIEW: panels=' "$out"; then
    echo "FAIL no UI_PREVIEW marker line"
    tail -20 "$out"
    exit 1
fi
# The preview must not save. The theme is seeded, so compare it rather than
# looking for its presence: a session that fell through the normal teardown
# rewrites clr.json and writes s1.sng.
if ! cmp -s "${here}/s1.sng" "${scratch}/data/s1.sng"; then
    echo "FAIL preview rewrote the song"
    exit 1
fi
if ! cmp -s "${here}/clr.json" "${scratch}/cfg/clr.json"; then
    echo "FAIL preview rewrote the theme"
    exit 1
fi
# Every panel must have produced a PNG. A blank panel is a panel that did not
# draw, which exit 0 and the marker line cannot tell apart.
count=$(sed -n 's/^UI_PREVIEW: panels=\([0-9]*\).*/\1/p' "$out" | tail -1)
[ -n "$count" ] || { echo "FAIL marker has no panel count"; exit 1; }
shots=$(ls "${scratch}"/shot-*.png 2>/dev/null | wc -l)
[ "$shots" -eq "$count" ] || {
    echo "FAIL $shots shots for $count panels"
    ls -l "${scratch}"/shot-*.png 2>/dev/null
    exit 1
}
for f in "${scratch}"/shot-*.png; do
    # A uniformly coloured panel is a draw that never happened.
    colors=$(identify -format '%k' "$f" 2>/dev/null)
    [ "${colors:-0}" -gt 2 ] || { echo "FAIL panel looks blank: $f"; exit 1; }
done
echo "UI_PREVIEW: panels=$count shots=$shots"
echo "PASS ui_preview_smoke"