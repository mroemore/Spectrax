#!/bin/sh
# show-runs.sh — pretty-print a pulled benchmark run directory.
# Usage: show-runs.sh <run-dir>
set -eu

dir=${1:-}
[ -n "$dir" ] && [ -d "$dir" ] || { echo "usage: $0 <run-dir>" >&2; exit 64; }

if [ -f "$dir/summary.json" ]; then
    echo "=== summary ==="
    if command -v python3 >/dev/null 2>&1; then
        python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for section, values in d.items():
    print("[%s]" % section)
    for k, v in values.items():
        print("  %-28s %s" % (k, v))
' "$dir/summary.json"
    else
        cat "$dir/summary.json"
    fi
fi

if [ -f "$dir/timeseries.csv" ]; then
    echo
    echo "=== timeseries (head) ==="
    head -20 "$dir/timeseries.csv"
fi

if [ -f "$dir/hostinfo.txt" ]; then
    echo
    echo "=== hostinfo (head) ==="
    head -20 "$dir/hostinfo.txt"
fi
