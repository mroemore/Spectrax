#!/usr/bin/env python3
"""make_report.py — build + POST an appik article from nav-harness eval results.

Reads results.jsonl (one result per step) and the harness corpus, composes
before/after screenshot strips, and posts an article to the appik server
with a results table + per-step detail.

Usage:
  make_report.py --results results.jsonl --corpus .tmp_files/nav_harness_out
                 [--host http://localhost:8903]
"""

import argparse
import base64
import io
import json
import os
import sys
import urllib.error
import urllib.request

from PIL import Image

STRIP_GAP = 4
STRIP_LABEL_H = 22


def load_results(path):
    return [json.loads(l) for l in open(path) if l.strip()]


def read_prompt(corpus, s1):
    d = os.path.join(corpus, s1.rsplit("/", 1)[0])
    with open(os.path.join(d, "prompt.txt")) as f:
        return f.read().strip()


def make_strip(corpus, s1, s2, key):
    a = Image.open(os.path.join(corpus, s1)).convert("RGB")
    b = Image.open(os.path.join(corpus, s2)).convert("RGB")
    w = a.width + b.width + STRIP_GAP + 2
    h = a.height + STRIP_LABEL_H + 2
    canvas = Image.new("RGB", (w, h), (20, 20, 20))
    canvas.paste(a, (1, 1 + STRIP_LABEL_H))
    canvas.paste(b, (1 + a.width + STRIP_GAP, 1 + STRIP_LABEL_H))
    canvas.paste((255, 255, 255), (1, 1 + STRIP_LABEL_H - 2,
                                  w - 1, 1 + STRIP_LABEL_H))
    label = Image.new("RGB", (w, STRIP_LABEL_H), (30, 30, 30))
    canvas.paste(label, (0, 0))
    buf = io.BytesIO()
    canvas.save(buf, format="PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def build_blocks(corpus, results):
    blocks = []
    correct = sum(1 for r in results if r["correct"])

    blocks.append({"type": "heading", "level": 2, "text": "Overview"})
    blocks.append({"type": "paragraph", "text": (
        "First batch from the Spectrax nav harness: a visual eval of the "
        "graph navigation code. The harness generates varied GuiNode layout "
        "trees, then for each (selection, arrow-key) step captures a "
        "before/after screenshot pair and records the prompt template plus "
        "the ground-truth selection. eval.py sends screenshot 1 + prompt to "
        "a vision model, compares the answer, and on a mismatch continues the "
        "same session with screenshot 2 asking for an explanation.")})
    blocks.append({"type": "stats", "items": [
        {"label": "Correct", "value": f"{correct}/{len(results)}"},
        {"label": "Model", "value": results[0]["model"]},
        {"label": "Steps", "value": str(len(results))},
    ]})
    blocks.append({"type": "callout", "tone": "info", "title": "Changes since batch 1", "text": (
        "Two harness/model fixes landed since batch 1. (1) reflowCoordinates "
        "no longer emits negative child sizes: integer division + padding "
        "subtraction produced h = -2 in crowded containers, which wrapped in "
        "the uint16 fields to 65534 and painted giant red rectangles covering "
        "the left third of the 'before' screenshots. Dimensions are now "
        "clamped to >= padding*2 + 4, and the generator rejects any layout "
        "with a selectable whose internal width or height is under 4px. "
        "(2) The eval driver now runs opencode with --pure: the "
        "harness-observer plugin made kimi-k2.6 tool-loop and return empty "
        "answers (0/10 runs); with plugins disabled it answers cleanly in "
        "~10s. Batch 1's 8/10 was a plugin-free fluke of that interference.")})

    blocks.append({"type": "heading", "level": 2, "text": "Results table"})
    blocks.append({"type": "table", "columns": [
        {"key": "step", "label": "Step"},
        {"key": "sel", "label": "Selected"},
        {"key": "key", "label": "Key"},
        {"key": "exp", "label": "Expected"},
        {"key": "ans", "label": "Kimi"},
        {"key": "verdict", "label": "Verdict"},
    ], "rows": [
        [str(r["step"]), r["selected_before"], r["key"], r["expected"],
         r["answer"] or "-", "OK" if r["correct"] else "MISMATCH"]
        for r in results
    ]})

    blocks.append({"type": "heading", "level": 2, "text": "Per-step detail"})
    for r in results:
        prompt = read_prompt(corpus, r["s1"])
        blocks.append({"type": "heading", "level": 3, "text": (
            f"Step {r['step']} — {r['selected_before']} selected, press "
            f"{r['key']}")})
        blocks.append({"type": "image",
                       "src": make_strip(corpus, r["s1"], r["s2"], r["key"]),
                       "alt": "before/after", "width": 1000,
                       "caption": "before (left) → after (right)"})
        blocks.append({"type": "paragraph", "text": f"Prompt: {prompt}"})
        if r["correct"]:
            blocks.append({"type": "kv", "items": [
                {"key": "Expected", "value": r["expected"]},
                {"key": "Kimi answered", "value": r["answer"]},
                {"key": "Verdict", "value": "correct"},
            ]})
        else:
            blocks.append({"type": "callout", "tone": "warn",
                           "title": f"Mismatch — expected {r['expected']}, "
                                    f"model said {r['answer']}",
                           "text": r["explanation"] or "(no explanation)"})
    return blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--host", default="http://localhost:8903")
    ap.add_argument("--batch", default="2", help="batch label for the title")
    args = ap.parse_args()

    results = load_results(args.results)
    if not results:
        sys.exit("no results")

    article = {
        "title": f"Spectrax nav-graph vision eval — batch {args.batch} "
                 f"({sum(1 for r in results if r['correct'])}/"
                 f"{len(results)} correct)",
        "author": "krang",
        "agent": "opencode",
        "summary": ("Vision-model eval of the graph navigation harness: "
                    f"Kimi K2.6 on {len(results)} steps, "
                    f"{sum(1 for r in results if r['correct'])} correct. "
                    "Screenshots, prompts, and follow-up explanations."),
        "tags": ["spectrax", "nav-harness", "vision-eval", "graph-gui"],
        "blocks": build_blocks(args.corpus, results),
    }

    req = urllib.request.Request(
        args.host + "/api/articles",
        data=json.dumps(article).encode(),
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            print("status:", resp.status)
            print("location:", resp.headers.get("Location"))
    except urllib.error.HTTPError as e:
        print("HTTP", e.code)
        print(e.read().decode()[:2000])


if __name__ == "__main__":
    main()
