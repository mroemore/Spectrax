#!/usr/bin/env bash
# meson/coverage-gate.sh [builddir] [extra-floors-file] — fail if a gated
# module's branch coverage is below its floor in meson/coverage-min.txt.
#
# Reads <builddir>/coverage-src.info (produced by meson/coverage.sh) and
# compares per-file BRH/BRF to the thresholds. The floor list is a ratchet:
# once a module's suite closes its branches, its floor moves to 100.
#
# Exit statuses:
#   0  every gated module that was measured meets its floor
#   1  the gate ran and failed
#   2  the script could not run (no report, no floors file)
#
# A module the report does not contain is a FAILURE, not a pass. A floor on
# a module that stopped being built, or whose suite stopped referencing it so
# its object never left the static archive, would otherwise stop being
# enforced with no message anywhere. An unmeasured module is not a passing
# module.
#
# A module with no branches has no branch coverage to measure. Scoring it
# 100 would make its floor unfailable AND make it look like proof, so it is
# reported as skipped, with the reason.
#
# The optional second argument names one more floors file, for a module that
# exists only in a non-default configuration. Both files are read into the
# SAME floor table: a duplicate across the two warns exactly as a duplicate
# within one does, and a floor from either file whose module is absent from
# the report fails exactly as one from the first file does. ${2:-} rather
# than $2: the argument is optional, and an unset $2 under `set -u` is an
# unbound-variable error that would crash an ordinary one-file run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build-cov}"
INFO="$BUILD/coverage-src.info"
MIN="$ROOT/meson/coverage-min.txt"
MIN2="${2:-}"

if [ ! -f "$INFO" ]; then
  echo "coverage-gate: $INFO not found; run meson/coverage.sh first" >&2
  exit 2
fi

# A floors file that was named but is not on disk is an operator error, not a
# state with nothing to enforce. Reading no floors out of it would silently
# unenforce every module it lists — renaming or deleting the file turns off
# its floors with no message anywhere, which is the same hole this whole
# design exists to avoid, reached from the other side. So it exits 2, the
# script's "could not run" status: not 0 (a pass over floors that were never
# read) and not 1 (a gate that ran and failed).
if [ ! -f "$MIN" ]; then
  echo "coverage-gate: $MIN not found; the primary floors file is missing, so no floor could be enforced" >&2
  exit 2
fi
if [ -n "$MIN2" ] && [ ! -f "$MIN2" ]; then
  echo "coverage-gate: $MIN2 not found; the extra floors file was named but is missing, so its floors could not be enforced" >&2
  exit 2
fi

awk -v root="${ROOT}/" -v minfile="$MIN" -v minfile2="$MIN2" '
  # One reader for both files, called twice, so the two can never drift.
  #
  # On a duplicate the HIGHER floor wins, not the later one. This is a
  # ratchet: a floor only ever goes up. Last-wins would let a duplicated
  # entry with a lower value silently lower the floor, which is the one
  # thing a ratchet must never do.
  function readfloors(fn,    line, n, a) {
    while((getline line < fn) > 0) {
      sub(/#.*/, "", line)
      n = split(line, a, /[ \t]+/)
      if(n >= 2) {
        if(a[1] in min) {
          if(a[2] + 0 > min[a[1]])
            printf "coverage-gate: duplicate floor for %s (%d%% ignored, %d%% wins)\n",
                   a[1], min[a[1]], a[2] + 0 > "/dev/stderr"
          else
            printf "coverage-gate: duplicate floor for %s (%d%% wins, %d%% ignored)\n",
                   a[1], min[a[1]], a[2] + 0 > "/dev/stderr"
          if(a[2] + 0 > min[a[1]]) min[a[1]] = a[2] + 0
        } else {
          min[a[1]] = a[2] + 0
        }
      }
    }
    close(fn)
  }
  BEGIN {
    readfloors(minfile)
    if(minfile2 != "") readfloors(minfile2)
    nf = 0; ns = 0; fail = 0
    for (k in min) declared++
  }
  /^SF:/ { file = substr($0, 4); sub(root, "", file); tot = 0; hit = 0; seen[file] = 1 }
  /^BRF:/ { tot = substr($0, 5) + 0 }
  /^BRH:/ { hit = substr($0, 5) + 0 }
  /^end_of_record/ {
    if(file in min) {
      if(tot == 0) {
        printf "  %-4s %6s    (floor %3d%%)  %s  [no branches]\n", "skip", "-", min[file], file
        ns++
      } else {
        pct = 100.0 * hit / tot
        ok = (pct + 1e-6 >= min[file])
        if(!ok) fail = 1
        printf "  %-4s %6.1f%%  (floor %3d%%)  %s\n", ok ? "ok" : "FAIL", pct, min[file], file
        nf++
      }
    }
    file = ""
  }
  END {
    # A project that declares no floors has nothing to enforce; that is a
    # bootstrap state, not a gate failure. Every other path below is strict.
    if(declared == 0) {
      print "coverage-gate: no floors declared in " minfile " - nothing to enforce"
      exit 0
    }
    # A floor on a file that is not in the report at all was being ignored in
    # silence - counting only the files that matched means a gated module that
    # stopped being built simply stopped being checked.
    for(f in min)
      if(!(f in seen)) {
        printf "coverage-gate: gated file ABSENT from the report: %s\n", f > "/dev/stderr"
        fail = 1
      }
    if(fail) { print "coverage-gate: FAILED (see floors above)" > "/dev/stderr"; exit 1 }
    if(ns > 0)
      printf "coverage-gate: %d gated module(s) skipped (no branches)\n", ns > "/dev/stderr"
    print "coverage-gate: all gated modules meet their floor"
  }
' "$INFO"
