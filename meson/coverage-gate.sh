#!/usr/bin/env bash
# meson/coverage-gate.sh [builddir] — fail if a gated module's branch coverage
# is below its floor in meson/coverage-min.txt.
#
# Reads <builddir>/coverage-src.info (produced by meson/coverage.sh) and
# compares per-file BRH/BRF to the thresholds. The floor list is a ratchet:
# once a module's suite closes its branches, its floor moves to 100.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build-cov}"
INFO="$BUILD/coverage-src.info"
MIN="$ROOT/meson/coverage-min.txt"

if [ ! -f "$INFO" ]; then
  echo "coverage-gate: $INFO not found; run meson/coverage.sh first" >&2
  exit 2
fi

awk -v root="${ROOT}/" -v minfile="$MIN" '
  BEGIN {
    while((getline line < minfile) > 0) {
      sub(/#.*/, "", line)
      n = split(line, a, /[ \t]+/)
      if(n >= 2) min[a[1]] = a[2] + 0
    }
    nf = 0; fail = 0
  }
  /^SF:/ { file = substr($0, 4); sub(root, "", file) }
  /^BRF:/ { tot = substr($0, 5) + 0 }
  /^BRH:/ { hit = substr($0, 5) + 0 }
  /^end_of_record/ {
    if(file in min) {
      pct = (tot > 0) ? 100.0 * hit / tot : 100.0
      ok = (pct + 1e-6 >= min[file])
      if(!ok) fail = 1
      printf "  %-4s %6.1f%%  (floor %3d%%)  %s\n", ok ? "ok" : "FAIL", pct, min[file], file
      nf++
    }
    file = ""
  }
  END {
    if(nf == 0) { print "coverage-gate: no gated files matched " minfile > "/dev/stderr"; exit 2 }
    if(fail) { print "coverage-gate: FAILED (see floors above)" > "/dev/stderr"; exit 1 }
    print "coverage-gate: all gated modules meet their floor"
  }
' "$INFO"
