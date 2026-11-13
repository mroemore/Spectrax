#!/usr/bin/env bash
# meson/lib.sh — shared helpers for the CTOL gate scripts.
#
# Sourced by meson/check.sh, meson/contract.sh, meson/coverage.sh and
# meson/matrix.sh. It holds no state of its own: every function takes what
# it needs and returns what the caller needs.
#
# The theme of everything here is the same: a gate must never report a pass
# over work it did not do, and a tier that could not look must say so rather
# than guess.

# build_state <builddir> [option-name]
#
# The state of a build directory, as a WORD rather than a boolean.
#
#   on          the named option is on
#   off         the named option is off - the tree says so
#   unreadable  the tree could not be read - UNKNOWN, not off
#   readable    (no option named) the tree could be read
#
# "off" and "unreadable" are different problems with different fixes, and a
# caller cannot collapse them into a boolean, because there is no true/false
# here to test - which is the point of returning a word. A tier that skips
# on a tree it could not read reports a true sentence about a tree nobody
# read, and a reader believes it and stops looking.
build_state() {
  local dir="$1"
  local option="${2:-}"
  local out line
  if [ -z "$option" ]; then
    if meson configure "$dir" >/dev/null 2>&1; then
      echo readable
    else
      echo unreadable
    fi
    return 0
  fi
  out="$(meson configure "$dir" 2>/dev/null)" || {
    echo unreadable
    return 0
  }
  line="$(printf '%s\n' "$out" | awk -v o="$option" '$1 == o { print $2; exit }')"
  case "$line" in
    true) echo on ;;
    false) echo off ;;
    *) echo unreadable ;;
  esac
}

# list_tests <builddir> [suite] — list the tests meson would run.
#
# Sets _LIST_TESTS to the listing and returns:
#   0  the listing succeeded; the list may still be empty - check it
#   1  meson says the tree defines no tests
#   2  the listing failed for some other reason
#
# meson 1.12 has two notices for "nothing to run": `No tests defined.` (the
# tree has no test() call at all) and `No suitable tests defined.` (tests
# exist but the filters excluded them). Both come from get_tests() on
# STDERR, and list_tests() returns `not tests`, so on meson 1.12 BOTH exit 1.
# That is why the stderr arm, and only that arm, accepts a non-zero status:
# there the notice is meson's own statement that discovery was empty, and
# get_tests returns immediately after printing it, so no unrelated failure
# can share that stream. On stdout the notice is not a statement meson 1.12
# makes at all, so that arm requires a SUCCESSFUL command before it may
# conclude anything.
list_tests() {
  local dir="$1"
  local suite="${2:-}"
  local err rc
  err="$(mktemp "${TMPDIR:-.}/ctol-list.XXXXXX" 2>/dev/null || mktemp)"
  if [ -n "$suite" ]; then
    _LIST_TESTS="$(meson test -C "$dir" --suite "$suite" --list 2>"$err")"
    rc=$?
  else
    _LIST_TESTS="$(meson test -C "$dir" --list 2>"$err")"
    rc=$?
  fi
  if [ "$rc" -eq 0 ]; then
    rm -f "$err"
    return 0
  fi
  if grep -Eq 'No (suitable )?tests defined\.' "$err" 2>/dev/null; then
    rm -f "$err"
    _LIST_TESTS=""
    return 1
  fi
  rm -f "$err"
  _LIST_TESTS=""
  return 2
}

# require_tests <builddir> <tier-name> [suite] — the empty-suite guard.
#
# A suite that matched nothing is not a tier with nothing to run. `meson
# test --suite X` exits 0 having run no test at all when the suite is not
# registered, so a run would report PASS over zero tests. That is the one
# thing a gate must never be.
#
# Every way of having nothing here is a FAILURE, and each says which:
#   * the listing succeeded and is empty  -> the registration is gone
#   * meson says the tree defines no tests -> the tree has no tests
#   * the listing failed                   -> UNKNOWN, not empty
require_tests() {
  local dir="$1"
  local name="$2"
  local suite="${3:-}"
  local rc
  list_tests "$dir" "$suite"
  rc=$?
  case "$rc" in
    0)
      if [ -n "${_LIST_TESTS:-}" ]; then
        return 0
      fi
      echo "$name: the suite matched no tests; a run would report PASS over zero tests" >&2
      echo "$name: the suite is registered where the tests are declared; an empty one means that registration is gone" >&2
      return 1
      ;;
    1)
      echo "$name: the suite matched no tests - meson says the tree defines none" >&2
      return 1
      ;;
    *)
      echo "$name: cannot list the tests in $dir; whether the suite is registered is UNKNOWN, not empty" >&2
      echo "$name: a meson upgrade invalidates existing trees; re-run: meson setup --reconfigure $dir" >&2
      return 1
      ;;
  esac
}

# project_declares_suite <root> <suite-name>
#
# Does the project declare any test in that meson suite?
#
# This is how the race tier tells "the project has no race suite" - nothing
# to race-check, a skip - from "the project declared one and it matched
# nothing" - the registration is broken, a failure. meson cannot tell them
# apart: both look like "No suitable tests defined." to the tier.
project_declares_suite() {
  local root="$1"
  local suite="$2"
  grep -rqE "suite[[:space:]]*:[[:space:]]*\[?[^]]*['\"]${suite}['\"]" \
    --include=meson.build "$root" 2>/dev/null
}

# race_suite_complete <root> <builddir> <suite-name>
#
# Every test whose source contains a thread-creation primitive must be in
# the suite. A test that creates no thread cannot create a data race of its
# own, so the sources that contain one are a superset of the tests that need
# the race detector - and a superset rule catches the case that matters: a
# new concurrency test that nobody tagged is silently uncovered by the race
# detector otherwise.
#
# Prints the uncovered tests and returns 1 when there are any.
race_suite_complete() {
  local root="$1"
  local build="$2"
  local suite="$3"
  local listing f base missing=0
  listing="$(meson test -C "$build" --suite "$suite" --list 2>/dev/null)"
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    grep -qE 'pthread_create|thrd_create|CreateThread' "$f" || continue
    base="$(basename "$f" .c)"
    case "$listing" in
      *"$base"*) ;;
      *)
        echo "$base"
        missing=1
        ;;
    esac
  done < <(find "$root/tests" -maxdepth 2 -type f -name 'test_*.c' 2>/dev/null | sort)
  return "$missing"
}
