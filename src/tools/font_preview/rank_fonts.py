#!/usr/bin/env python3
"""
rank_fonts.py - rank Spectrax's pixel fonts against a rendering profile.

Profile semantics (defaults derived from the raylib bundled default font,
which is the "default bundled raylib font fallback" that Spectrax and its
sibling apps rely on whenever DrawText() is called without an explicit Font):

  GetFontDefault()  ->  baseSize 10, 224 glyphs on a 128x128 texture,
                        glyph cell 10px wide x 10px tall (ink <= 9x10 + 1px
                        advance).  So a replacement font is acceptable iff, at
                        some comparable render size, its rasterized glyph box
                        fits inside the profile box:

                        profile = (maxW, maxH) = (10, 10)
                        target size (comparable) = 10

Measurement: each TTF/OTF is rasterized by raylib itself (via the bundled
fontprobe.c harness) at every integer size in a sweep band around the target;
the best size for that font is the fitting size whose glyph box is closest to
the profile, width-first.  PNG sprite fonts are measured once via LoadFont()
(their cell is fixed by the image; they cannot be resized without losing
pixel-perfectness).

Ranking: acceptable fonts (fit inside the profile box at their best size) rank
above unacceptable ones.  A perfect match (maxW == maxW and maxH == maxH) is
rank 1; after that, closeness in width is weighted above closeness in height:
  key = (|maxW - PW|, |maxH - PH|, |bestSize - target|, maxAdv)

Usage:
  python3 rank_fonts.py [--spectrax ROOT] [--max-w N] [--max-h N]
                        [--target N] [--sizes FROM-TO] [--out FILE]
                        [--display :99] [--no-raylib]

Requirements: gcc + the vendored raylib under <spectrax>/include and
<spectrax>/lib/linux, and an X display (Xvfb works) for raylib's GL context.
Without raylib, pass --no-raylib to fall back to approximate fontTools math
(marked APPROX; numbers will NOT match raylib's rasterization).
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

PROBE_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fontprobe.c")

METRIC_RE = re.compile(
    r"\| (?P<kind>ttf|png) \| (?P<size>\d+) \| base=(?P<base>\d+) \| glyphs=(?P<glyphs>\d+)"
    r" \| maxW=(?P<maxw>[\d.]+) \| maxH=(?P<maxh>[\d.]+) \| maxAdv=(?P<maxadv>[\d.]+)"
    r" \| offX=(?P<offx>[\d.]+) \| offY=(?P<offy>[\d.]+)"
)


def find_font_files(spectrax_root):
    """All *.ttf/*.TTF/*.otf anywhere; *.png only under dirs named fonts/font."""
    fonts = []
    for ext in ("*.ttf", "*.TTF", "*.otf", "*.OTF"):
        fonts += glob.glob(os.path.join(spectrax_root, "**", ext), recursive=True)
    for d in glob.glob(os.path.join(spectrax_root, "**", "*"), recursive=True):
        if os.path.isdir(d) and os.path.basename(d).lower() in ("fonts", "font"):
            for ext in ("*.png",):
                fonts += glob.glob(os.path.join(d, ext))
    seen, out = set(), []
    for f in fonts:
        r = os.path.realpath(f)
        if r not in seen:
            seen.add(r)
            out.append(r)
    return sorted(out)


def build_probe(spectrax_root, probe_dir):
    inc = os.path.join(spectrax_root, "include")
    lib = os.path.join(spectrax_root, "lib", "linux")
    probe = os.path.join(probe_dir, "fontprobe")
    if not (os.path.isfile(PROBE_SRC) and os.path.isdir(inc) and os.path.isdir(lib)):
        return None
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc:
        return None
    if not (os.path.isfile(probe) and os.path.getmtime(probe) >= os.path.getmtime(PROBE_SRC)):
        cmd = [cc, PROBE_SRC, "-I", inc, "-L", lib, "-lraylib", "-lm", "-o", probe]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return None
    return probe


def run_probe(probe, path, size, display):
    env = dict(os.environ)
    if display:
        env["DISPLAY"] = display
    r = subprocess.run([probe, path, str(size)], capture_output=True, text=True, env=env)
    for line in r.stdout.splitlines():
        m = METRIC_RE.search(line)
        if m:
            g = m.groupdict()
            return {
                "path": path,
                "kind": g["kind"],
                "size": int(g["size"]),
                "base": int(g["base"]),
                "glyphs": int(g["glyphs"]),
                "maxW": float(g["maxw"]),
                "maxH": float(g["maxh"]),
                "maxAdv": float(g["maxadv"]),
            }
    return None


def measure_raylib(fonts, probe, sizes, display):
    rows = []
    for path in fonts:
        ext = os.path.splitext(path)[1].lower()
        if ext == ".png":
            row = run_probe(probe, path, 0, display)
            if row:
                row["isPng"] = True
                rows.append(row)
        else:
            for s in sizes:
                row = run_probe(probe, path, s, display)
                if row:
                    row["isPng"] = False
                    rows.append(row)
    return rows


def measure_fonttools(fonts, sizes):
    """Approximate fallback: ink bounds via fontTools. Marked APPROX because
    raylib/stb_truetype pads + clips glyph boxes, so these diverge."""
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.ttLib import TTFont
    rows = []
    for path in fonts:
        if os.path.splitext(path)[1].lower() == ".png":
            continue
        try:
            f = TTFont(path)
        except Exception:
            continue
        upem = f["head"].unitsPerEm
        glyphs = f.getGlyphSet()
        hmtx = f["hmtx"]
        cmap = f.getBestCmap()
        for s in sizes:
            maxw = maxh = maxadv = 0
            for cp, name in cmap.items():
                if not (32 <= cp <= 126):
                    continue
                try:
                    pen = BoundsPen(glyphs)
                    glyphs[name].draw(pen)
                    if pen.bounds:
                        x0, y0, x1, y1 = pen.bounds
                        maxw = max(maxw, round((x1 - x0) * s / upem))
                        maxh = max(maxh, round((y1 - y0) * s / upem))
                except Exception:
                    pass
                maxadv = max(maxadv, round(hmtx[name][0] * s / upem))
            rows.append({
                "path": path, "kind": "ttf", "size": s, "base": s,
                "glyphs": len(cmap), "maxW": maxw, "maxH": maxh,
                "maxAdv": maxadv, "isPng": False, "approx": True,
            })
    return rows


def classify_png(row):
    """PNG sprite sheets: return 'text', 'icon' (icon sheet, few glyphs) or
    'fallback' (raylib could not parse it and returned the default font:
    224 glyphs at exactly 9x10, base 10 -- e.g. the 72px-wide icon sheets)."""
    if row["glyphs"] < 32:
        return "icon"
    if row["glyphs"] == 224 and row["maxW"] == 9 and row["maxH"] == 10 and row["base"] == 10:
        return "fallback"
    return "text"


def best_size_for_font(rows, PW, PH, target):
    """Pick the fitting size whose box is closest to the profile, width-first.
    Returns (best_row, fits). For PNGs the single row is used as-is."""
    if not rows:
        return None, False
    if rows[0]["isPng"]:
        r = rows[0]
        fits = r["maxW"] <= PW and r["maxH"] <= PH
        return r, fits
    fitting = [r for r in rows if r["maxW"] <= PW and r["maxH"] <= PH]
    pool = fitting if fitting else rows
    key = lambda r: (abs(PW - r["maxW"]), abs(PH - r["maxH"]), abs(target - r["size"]), r["maxAdv"])
    return min(pool, key=key), bool(fitting)


def rank(entries, PW, PH, target):
    for e in entries:
        e["dW"] = abs(PW - e["maxW"])
        e["dH"] = abs(PH - e["maxH"])
    acc = [e for e in entries if e["fits"]]
    rej = [e for e in entries if not e["fits"]]
    acc.sort(key=lambda e: (e["dW"], e["dH"], abs(target - e["size"]), e["maxAdv"]))
    rej.sort(key=lambda e: (e["dW"], e["dH"], e["maxAdv"]))
    return acc, rej


def doc_path(root, sub):
    return os.path.relpath(sub, root)


def write_document(out_path, PW, PH, target, sizes, acc, rej, excluded, raylib, spectrax_root):
    lines = []
    W = lines.append
    W("# Spectrax pixel fonts vs. the raylib default-font profile\n")
    W("")
    W(f"- **Profile (max width x max height allowable):** `{PW} x {PH}` px")
    W(f"- **Comparable size (target render size):** `{target}` px")
    W(f"- **Measurement engine:** {'raylib rasterization (fontprobe.c harness)' if raylib else 'fontTools ink bounds (APPROXIMATE)'}")
    W(f"- **Size sweep:** sizes `{sizes[0]}..{sizes[-1]}`; the best size per font is the fitting size closest to the profile (width-first)")
    W(f"- **Spectrax root scanned:** `{spectrax_root}`")
    W("")
    W("## Profile derivation\n")
    W("raylib's bundled default font (`GetFontDefault()`) is the fallback used by every "
      "`DrawText()` call that does not pass an explicit `Font` — in Spectrax that is "
      "`spectrogram.c:50`, `graph_gui.c:213`, `modvisual.c`, `dataviz.c:65` and "
      "`gui.c:2301,2420,2435`. It is a 10px-tall, 224-glyph bitmap whose glyphs occupy at most "
      "9x10 px in a 10px-wide x 10px-tall cell (advance = glyph width + 1), which is where the "
      f"default `{PW}x{PH}` profile comes from. A replacement font is acceptable iff, at some "
      f"comparable render size, its rasterized glyph box fits inside `{PW} x {PH}` px; any font "
      f"that fills the box exactly (`{PW}x{PH}`) is a perfect match, and otherwise closeness in "
      "**width** outranks closeness in **height**.\n")
    W("")
    W("## Ranking (ordered by acceptability)\n")
    W("")
    W("| # | font | kind | best size | glyph box (W x H) | vs profile |")
    W("|---|------|------|-----------|-------------------|------------|")
    rank_no = 0
    prev_key = None
    for e in acc:
        key = (e["dW"], e["dH"])
        rank_no = rank_no if key == prev_key else rank_no + 1
        prev_key = key
        box = f"{e['maxW']:.0f}x{e['maxH']:.0f}"
        delta = f"dW={e['dW']:.0f} dH={e['dH']:.0f}"
        W(f"| {rank_no} | {os.path.basename(e['path'])} | {e['kind']} | {e['size']} | {box} | {delta} |")
    W("")
    W("### Not acceptable (exceed the profile box)\n")
    W("")
    W("| # | font | kind | size | glyph box (W x H) | vs profile |")
    W("|---|------|------|------|-------------------|------------|")
    rank_no = 0
    prev_key = None
    for e in rej:
        key = (e["dW"], e["dH"])
        rank_no = rank_no if key == prev_key else rank_no + 1
        prev_key = key
        box = f"{e['maxW']:.0f}x{e['maxH']:.0f}"
        delta = f"over W by {e['dW']:.0f}, over H by {e['dH']:.0f}"
        W(f"| {rank_no} | {os.path.basename(e['path'])} | {e['kind']} | {e['size']} | {box} | {delta} |")
    if excluded:
        W("")
        W("### Excluded from ranking (not usable as text fonts via raylib)\n")
        W("")
        W("| font | reason |")
        W("|------|--------|")
        for e in excluded:
            W(f"| {os.path.basename(e['path'])} | {e['reason']} |")
    W("")
    W("## Per-font notes\n")
    W("")
    for e in acc + rej:
        W(f"- **{os.path.basename(e['path'])}** ({e['kind']}, box {e['maxW']:.0f}x{e['maxH']:.0f} at size {e['size']})"
          f" — {'acceptable' if e['fits'] else 'too large for the profile'}"
          f"{', current Spectrax default pixelFont' if 'console.ttf' in e['path'] else ''}"
          f"{', sample-analyser tool font' if 'sample_analyser' in e['path'] else ''}")
    W("")
    W("## How to use\n")
    W("```")
    W(f"python3 rank_fonts.py --spectrax {spectrax_root} --max-w {PW} --max-h {PH} --target {target} --out {out_path}")
    W("```")
    W("Tune `--max-w/--max-h/--target` to the sibling app's actual pixel box and render size.")
    W("")
    W("## Caveats\n")
    W("- PNG sprite fonts cannot be resized without losing pixel-perfectness; their cell is fixed "
      "by the image (e.g. all 128x128 sheets here are 10x12 px or larger and exceed the 10x10 profile).")
    W("- Icon sheets (`iconzfin.png`, `synthicons.png`, `iconz.png`, `iconplay.png`) are not text "
      "fonts; raylib reports default-font metrics for the 72px-wide sheets (fallback) — they are excluded "
      "from the ranked text list where ambiguous.")
    W("- Vector pixel-style TTFs (e.g. `04B_03__.TTF`, `Daydream.ttf`) are crispest at integer multiples "
      "of their design grid; the chosen 'best size' is the closest fitting size to the target.")
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spectrax", default=os.path.expanduser("~/proj/Spectrax"))
    ap.add_argument("--max-w", type=int, default=10)
    ap.add_argument("--max-h", type=int, default=10)
    ap.add_argument("--target", type=int, default=10)
    ap.add_argument("--sizes", default="8-16")
    ap.add_argument("--display", default=None, help="X display for raylib (e.g. :99); defaults to $DISPLAY")
    ap.add_argument("--no-raylib", action="store_true")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    lo, hi = (int(x) for x in args.sizes.split("-"))
    sizes = list(range(lo, hi + 1))
    out_path = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        f"spectrax-font-profile-{args.max_w}x{args.max_h}.md")

    fonts = find_font_files(args.spectrax)
    if not fonts:
        print(f"no fonts found under {args.spectrax}")
        return 1

    rows = []
    raylib = False
    if not args.no_raylib:
        cache = os.path.expanduser("~/.cache/spectrax-font-profile")
        os.makedirs(cache, exist_ok=True)
        probe = build_probe(args.spectrax, cache)
        if probe:
            display = args.display or os.environ.get("DISPLAY")
            rows = measure_raylib(fonts, probe, sizes, display)
            raylib = bool(rows)
        if not rows:
            print("raylib probe unavailable (no vendored raylib / gcc / display) - falling back to fontTools (APPROXIMATE)")
    if not raylib:
        rows = measure_fonttools(fonts, sizes)

    by_font = {}
    for r in rows:
        by_font.setdefault(r["path"], []).append(r)

    entries = []
    excluded = []
    for path, rs in by_font.items():
        best, fits = best_size_for_font(rs, args.max_w, args.max_h, args.target)
        if best is None:
            continue
        if best.get("isPng"):
            cls = classify_png(best)
            if cls == "icon":
                excluded.append({"path": path, "reason": "icon sheet (not a text font)"})
                continue
            if cls == "fallback":
                excluded.append({"path": path, "reason": "raylib could not parse sheet; returned the bundled default font"})
                continue
        best["fits"] = fits
        entries.append(best)

    acc, rej = rank(entries, args.max_w, args.max_h, args.target)
    write_document(out_path, args.max_w, args.max_h, args.target, sizes, acc, rej, excluded, raylib, args.spectrax)
    print(f"wrote {out_path}: {len(acc)} acceptable, {len(rej)} not acceptable, {len(excluded)} excluded")
    return 0


if __name__ == "__main__":
    sys.exit(main())