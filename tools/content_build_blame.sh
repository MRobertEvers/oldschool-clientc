#!/bin/sh
# Build the content scripts and say WHOSE error stopped it.
#
# This tree has more than one session writing to it at once, and
# `torirsserver-scripts` is a whole-tree compile: a sibling's half-finished edit —
# a deleted constant, a duplicate trigger name, a type subject that is not a
# type yet — stops the build for everybody. The question that costs the most
# time in that state is not "what is the error", it is "is the error mine".
#
# So: run the build, and if it fails, sort the reported files into ones matching
# the paths you name and ones that do not. Nothing is skipped, moved, renamed or
# parked — the tree is read-only to this script, which is the point. Parking a
# sibling's file to green your own compile is what PORTING_GUIDE §7 forbids and
# what `.cursor/rules/no-park-sibling-content.mdc` exists to catch.
#
#   tools/content_build_blame.sh <path fragment>...
#
# Exit 0 when the build is green, 1 when it failed on one of YOUR paths, and 2
# when it failed on somebody else's — so a loop can wait out 2 and must stop
# on 1.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOG=$(mktemp -t content_build_blame)
trap 'rm -f "$LOG"' EXIT

if make -C "$ROOT/src" torirsserver-scripts >"$LOG" 2>&1; then
  grep -E '^compiled ' "$LOG" || true
  exit 0
fi

FAILED=$(grep -oE '[^ ]+\.rs2:[0-9]+:.*' "$LOG" || true)
if [ -z "$FAILED" ]; then
  echo "content_build_blame: the build failed without naming a script:" >&2
  tail -20 "$LOG" >&2
  exit 2
fi

MINE=0
echo "$FAILED" | while IFS= read -r line; do
  printf '  %s\n' "$line"
done
for frag in "$@"; do
  if echo "$FAILED" | grep -q -- "$frag"; then
    MINE=1
    echo "content_build_blame: ^ matches your path fragment '$frag'" >&2
  fi
done
[ "$MINE" = 1 ] && exit 1
echo "content_build_blame: no failure matches $* — this is another lane's" >&2
exit 2
