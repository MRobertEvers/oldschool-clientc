#!/bin/sh
# Build the CoX content and run its selftest, failing loudly at every step.
#
# Why this exists: an earlier ad-hoc version of this pipeline redirected the
# compiler to /dev/null. When a build failed, the OLD script.dat stayed in place
# and the selftest happily reported a pass -- which made a deliberately broken
# constant look like it was still correct, and cost hours of chasing a
# non-existent bug in the debugproc dispatcher. Never suppress these commands.
#
# One staging step is mandatory and easy to forget: ss_allocate.py, which
# assigns ids to new records. There used to be a second -- a staged COPY of the
# constant tree, so that editing a .constant and forgetting to re-stage silently
# compiled the old value. sscompile reads the tree's own constants directly now
# (the one generated value, a lane's feature flag, comes from the lane
# descriptor), so that trap is gone with the step.
#
# The lane list is gone from the command line for the same reason: which content
# lanes go into a pack comes from `ported/<lane>/lane.ini` (--content-root), not
# from seven --pack flags that every copy of this pipeline had to repeat.
#
# Usage: tools/cox_verify.sh

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
C=OSRS-Content/osrs239-content

[ -x src/build_opt/sscompile ] || make -C src sscompile >/dev/null
[ -x src/build_opt/mock230 ] || make -C src mock230 >/dev/null

echo "--- allocate ids ---"
python3 tools/ss_allocate.py --tree $C >/dev/null

echo "--- compile ---"
src/build_opt/sscompile \
    --src $C/server/scripts \
    --out $C/server/scripts/build \
    --content-root $C 2>&1 | tee /tmp/cox_verify_compile.log | tail -3
CONTENT_ROOT=""
if ! grep -q "^compiled " /tmp/cox_verify_compile.log; then
    err=$(grep -v '^symbols:' /tmp/cox_verify_compile.log | head -1)
    case "$err" in
        *minigame_cox*)
            echo "COMPILE FAILED in CoX - this one is yours:"
            echo "  $err"
            exit 1
            ;;
    esac
    # Someone else's in-flight edit is breaking the shared tree. That must not
    # be able to block CoX verification, so fall back to the isolated tree the
    # compile-check harness stages (their scripts reverted to HEAD, CoX kept).
    echo "shared tree is broken by another session:"
    echo "  $err"
    echo "falling back to the isolated tree"
    ./tools/cox_compile_check.sh >/dev/null 2>&1 || true
    ISO="${TMPDIR:-/tmp}/cox-compile-check/isotree"
    [ -d "$ISO" ] || { echo "no isolated tree staged; cannot verify"; exit 1; }
    # Re-copy the CoX package unconditionally. cox_compile_check.sh is invoked
    # with its output suppressed and `|| true`, so if it failed part-way the
    # staged tree could be from an EARLIER run -- and then a mutation test
    # "passes" because it was never compiled. That happened: mutating the
    # vanguard heal threshold produced a green result off a stale copy.
    rm -rf "$ISO/server/scripts/minigames/minigame_cox"
    cp -R "$C/server/scripts/minigames/minigame_cox" \
          "$ISO/server/scripts/minigames/minigame_cox"
    for d in ported configs maps npc_combat npc_stats dbindex fields; do
        [ -e "$ISO/$d" ] || [ ! -e "$C/$d" ] || ln -s "$(cd $C && pwd)/$d" "$ISO/$d"
    done
    mkdir -p "$ISO/server/scripts/build"
    # --pack is stated here and only here: the isolated tree has its own
    # `pack/` (its allocations) but borrows the shared tree's `configs/`, which
    # is not the two-directory default. Lanes still come from the isolated
    # tree's own --content-root, through the `ported` symlink staged above.
    src/build_opt/sscompile --src "$ISO/server/scripts" \
        --out "$ISO/server/scripts/build" \
        --content-root "$ISO" \
        --pack "$ISO/pack" --pack $C/configs 2>&1 | tee /tmp/cox_verify_iso.log | tail -1
    # Same rule as the shared-tree compile: an unchecked compile leaves the
    # previous pack in place and the selftest then reports on THAT. This hole
    # cost a spurious green on a mutation test.
    if ! grep -q "^compiled " /tmp/cox_verify_iso.log; then
        echo "ISOLATED COMPILE FAILED - refusing to report a selftest result"
        exit 1
    fi
    CONTENT_ROOT="$ISO"
fi

# The osrs239 cache is an untracked build artifact at the repo root, so a fresh
# worktree does not have one -- and without it the selftest crashes in the
# collision section long before it reaches CoX. That produced a GREEN result,
# because the only test below was that no CoX failure line appeared. Refuse to
# run rather than report on a scene that never loaded.
if [ ! -e cache.osrs239 ]; then
    echo "no cache.osrs239 in $ROOT - the selftest cannot load a scene."
    echo "  ln -s <path to a checkout with one>/cache.osrs239 cache.osrs239"
    exit 1
fi

echo "--- selftest (CoX checks only) ---"
if [ -n "$CONTENT_ROOT" ]; then
    out=$(MOCK230_CONTENT="$CONTENT_ROOT" \
          MOCK230_SCRIPTS="$CONTENT_ROOT/server/scripts/build" \
          ./src/build_opt/mock230 --selftest 2>&1 || true)
else
    out=$(./src/build_opt/mock230 --selftest 2>&1 || true)
fi
# Positive evidence that the CoX block executed. Grepping only for failures
# cannot distinguish "passed" from "never ran", and "never ran" is the common
# case when something upstream in the suite dies: a missing cache, a segfault
# in the collision section, a content tree that would not load. Every one of
# those used to be reported here as a CoX pass.
if ! echo "$out" | grep -q '^mock230 selftest: Chambers of Xeric$'; then
    echo "the selftest never reached the CoX section - reporting nothing."
    echo "  last lines of the run:"
    echo "$out" | tail -5 | sed 's/^/    /'
    exit 1
fi
COX_FAIL='Chambers of Xeric|::cox|entering CoX|leaving CoX'
cox_lines=$(echo "$out" | grep -E "$COX_FAIL" | grep -v '^mock230 selftest: Chambers of Xeric$' || true)
if [ -n "$cox_lines" ]; then
    echo "$cox_lines"
    echo "RESULT: CoX selftest FAILED"
    exit 1
fi
if [ -n "$CONTENT_ROOT" ]; then
    # A green from the fallback path is NOT trustworthy and must never be
    # reported as a pass. Proved the hard way: with the shared tree broken, a
    # source-level mutation to cox_crabs.rs2 (breaking the crystal->beam table
    # the selftest exists to pin) still came back green. Something in the
    # isolated staging serves an older pack than the one just compiled, and
    # until that is understood the only safe answer here is "unknown".
    #
    # The compile above IS meaningful -- it proves the CoX package builds. The
    # selftest result is what cannot be trusted.
    echo "RESULT: INCONCLUSIVE - CoX compiled, but the shared tree is broken so"
    echo "        the selftest ran against the isolated tree, which has been"
    echo "        observed to report false passes. Re-run when the shared tree"
    echo "        builds to get a real result."
    exit 2
fi
echo "RESULT: CoX selftest passed (::coxrun reported 0 failures)"
