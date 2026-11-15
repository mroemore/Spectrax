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

cd "$here" || exit 1
out="${scratch}/run.log"
timeout -k 2 60 xvfb-run -a -s "-screen 0 1920x1440x24" \
    ./spectrax --ui-preview --ui-preview-frames 60 \
    --config-dir "$scratch/cfg" --data-dir "$scratch/data" > "$out" 2>&1
rc=$?

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
if [ -f "${scratch}/data/s1.sng" ]; then
    echo "FAIL preview wrote a song save"
    exit 1
fi
if ! cmp -s "${here}/clr.json" "${scratch}/cfg/clr.json"; then
    echo "FAIL preview rewrote the theme"
    exit 1
fi
grep '^UI_PREVIEW: panels=' "$out"
echo "PASS ui_preview_smoke"