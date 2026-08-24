#!/usr/bin/env python3
"""
eval.py — vision-model evaluation driver for the nav_harness artifact set.

For each step in the harness manifest:
  1. send screenshot #1 + the recorded prompt to a vision model
  2. compare the model's answer against the recorded expected selection
  3. on a wrong answer (optional), continue the SAME session with
     screenshot #2 and ask the model to explain the discrepancy

Backends:
  --backend kimi        (default) direct call to the Kimi For Coding API
                        (https://api.kimi.com/coding/v1) using the key from
                        opencode's auth.json (provider "kimi-for-coding").
                        Model "kimi-for-coding" = K2.7 Coding, thinking-only,
                        needs a large max_tokens budget (>= 12000).
  --backend opencode    `opencode run --format json --model ... --file <png>`
                        for providers opencode does the image wrapping for
                        (e.g. opencode-go/kimi-k2.6).

Usage:
  eval.py --out <dir>                # dir containing manifest.jsonl
         [--model kimi-for-coding]
         [--limit N]                 # first N steps only
         [--no-followup]             # skip the screenshot-2 explanation pass
         [--results results.jsonl]
"""

import argparse
import base64
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

SINGLE_LETTER = re.compile(r"\b[A-Z]\b")
KIMI_API_URL = "https://api.kimi.com/coding/v1/chat/completions"
KIMI_MAX_TOKENS = 30000

def kimi_api_key():
    env = os.environ.get("KIMI_FOR_CODING_API_KEY")
    if env:
        return env
    auth = os.path.expanduser("~/.local/share/opencode/auth.json")
    if os.path.exists(auth):
        try:
            entry = json.load(open(auth)).get("kimi-for-coding")
            if entry and entry.get("key"):
                return entry["key"]
        except json.JSONDecodeError:
            pass
    sys.exit("error: no Kimi For Coding API key (auth.json or "
             "KIMI_FOR_CODING_API_KEY)")


def image_data_url(path):
    with open(path, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def kimi_chat(model: str, messages):
    """POST a chat completion to the Kimi For Coding API. Returns content."""
    body = {
        "model": model,
        "messages": messages,
        "max_tokens": KIMI_MAX_TOKENS,
    }
    req = urllib.request.Request(
        KIMI_API_URL,
        data=json.dumps(body).encode(),
        headers={
            "Authorization": "Bearer " + kimi_api_key(),
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            data = json.loads(resp.read())
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"kimi api HTTP {e.code}: {e.read().decode()[:400]}")
    if "error" in data:
        raise RuntimeError(f"kimi api error: {data['error']}")
    return data["choices"][0]["message"]["content"] or ""

def extract_answer(text: str):
    """Normalize the model's reply to a single uppercase letter."""
    if not text:
        return None
    letters = SINGLE_LETTER.findall(text)
    if letters:
        return letters[0]
    for ch in text.strip():
        if ch.isascii() and ch.isalpha():
            return ch.upper()
    return None


def build_messages(prompt: str, image: str, history=None):
    messages = list(history or [])
    messages.append({
        "role": "user",
        "content": [
            {"type": "image_url", "image_url": {"url": image_data_url(image)}},
            {"type": "text", "text": prompt},
        ],
    })
    return messages


def run_model(model: str, prompt: str, image: str, session: str | None,
              backend: str):
    """Returns (answer, session_id). session is only meaningful for the
    opencode backend; the kimi backend keeps its own history."""
    if backend == "kimi":
        history = json.loads(session) if session else None
        messages = build_messages(prompt, image, history)
        answer = kimi_chat(model, messages)
        messages.append({"role": "assistant", "content": answer})
        return answer, json.dumps(messages)
    cmd = ["opencode", "run", "--format", "json", "--model", model, "--pure"]
    if session:
        cmd += ["--session", session]
    cmd += ["--file", image, "--title", "nav-harness-eval"]
    proc = subprocess.run(cmd, input=prompt.encode(), capture_output=True,
                          text=False, timeout=600)
    answer = None
    sid = None
    for line in proc.stdout.decode(errors="replace").splitlines():
        try:
            ev = json.loads(line)
        except json.JSONDecodeError:
            continue
        if ev.get("type") == "text":
            answer = ev["part"].get("text", "")
        elif ev.get("type") == "session.id":
            sid = ev.get("sessionID")
        elif ev.get("type") == "error":
            print(f"  model error: {ev.get('error', {}).get('data', {}).get('message', ev['error'])}",
                  file=sys.stderr)
    return answer, sid


def run_step(args, s):
    prompt_file = os.path.join(args.out, s["s1"].rsplit("/", 1)[0], "prompt.txt")
    with open(prompt_file) as f:
        prompt = f.read().strip()
    prompt += ("\nAnswer with the single letter only. Do not use any tools; "
               "look at the attached image directly.")

    s1 = os.path.join(args.out, s["s1"])
    s2 = os.path.join(args.out, s["s2"])

    answer = None
    got = None
    sid = None
    for attempt in range(2):
        answer, sid = run_model(args.model, prompt, s1, None, args.backend)
        got = extract_answer(answer) if answer else None
        if got is not None:
            break
    ok = got == s["expected"]

    explanation = None
    if not ok and not args.no_followup:
        follow = (
            f"Your previous answer was {got}. The actual correct selection "
            f"after pressing {s['key']} was {s['expected']}, and the "
            f"highlighted item in this screenshot should be {s['expected']}. "
            f"Does the highlighted element in this screenshot match your "
            f"expectation? Explain what made your previous answer incorrect."
        )
        fb, _ = run_model(args.model, follow, s2, sid, args.backend)
        explanation = (fb or "").strip()

    result = dict(s)
    result["model"] = args.model
    result["backend"] = args.backend
    result["answer"] = got
    result["raw_answer"] = answer
    result["correct"] = ok
    result["explanation"] = explanation
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="harness output dir with manifest.jsonl")
    ap.add_argument("--model", default="kimi-for-coding")
    ap.add_argument("--backend", choices=["kimi", "opencode"],
                    default="kimi")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--workers", type=int, default=1,
                    help="parallel API requests (kimi tolerates concurrency; "
                         "results are written in manifest order)")
    ap.add_argument("--no-followup", action="store_true")
    ap.add_argument("--results", default="results.jsonl")
    args = ap.parse_args()

    manifest = os.path.join(args.out, "manifest.jsonl")
    steps = [json.loads(l) for l in open(manifest) if l.strip()]
    if args.limit:
        steps = steps[: args.limit]

    results = open(args.results, "w")
    correct = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futures = {i: ex.submit(run_step, args, s) for i, s in enumerate(steps)}
        for i, _ in enumerate(steps):
            r = futures[i].result()
            if r["correct"]:
                correct += 1
            results.write(json.dumps(r) + "\n")
            results.flush()
            detail = ("" if r["correct"]
                      else f" (model: {r['raw_answer']!r})")
            if not r["correct"] and r["explanation"]:
                detail += f" | follow-up: {r['explanation'][:120]}"
            print(f"[{i + 1}/{len(steps)}] layout {r['layout']} step {r['step']}: "
                  f"from {r['selected_before']} press {r['key']} -> expected "
                  f"{r['expected']} : {r['answer']} "
                  f"{'OK' if r['correct'] else 'MISMATCH'}{detail}", flush=True)
    results.close()
    print(f"\n{correct}/{len(steps)} correct "
          f"({100.0 * correct / len(steps):.1f}%) -> {args.results}")


if __name__ == "__main__":
    main()
