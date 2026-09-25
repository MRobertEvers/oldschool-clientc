#!/bin/sh
# The Chambers of Xeric tick loop: run the raid under god mode and diff what it
# does against the strategy guide, tick by tick.
#
# This is the loop to iterate against. `tools/cox_verify.sh` answers "is the
# arithmetic right"; this answers "does the raid, running, do what the guide
# says it does" -- and those are different questions. Chambers of Xeric passed
# 25 arithmetic checks for weeks while every [ai_timer] in it was unarmed and
# nothing had a heartbeat at all (COX_PLAN.md S11.3 F0).
#
# Pipeline, in order, and every step is load-bearing:
#   1. ss_allocate.py      - ids for any new varp (the trace channel needs two)
#   2. stage constants     - sscompile reads a STAGED copy, not the tree
#   3. sscompile           - and its result is CHECKED; a failed compile leaves
#                            the old script.dat in place and the harness then
#                            happily reports on yesterday's raid
#   4. ToriRSServer --selftest  - with TORIRSSERVER_COX_SIM=1
#
# Usage:
#   tools/cox_sim.sh                 # 64 ticks per encounter
#   tools/cox_sim.sh 128             # longer run
#   tools/cox_sim.sh 64 --trace      # also print the per-tick table
#   tools/cox_sim.sh --selftest      # prove the harness can fail

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
C=OSRS-Content/osrs239-content
TRACE=build/cox_sim/trace.txt

TICKS=64
SHOW_TRACE=0
SELFTEST=0
for arg in "$@"; do
    case "$arg" in
        --trace) SHOW_TRACE=1 ;;
        --selftest) SELFTEST=1 ;;
        [0-9]*) TICKS=$arg ;;
    esac
done

mkdir -p build/cox_sim

[ -x src/build_opt/sscompile ] || make -C src sscompile >/dev/null
make -C src ToriRSServer >/dev/null 2>&1 || make -C src ToriRSServer

echo "--- allocate ids ---"
python3 tools/ss_allocate.py --tree $C >/dev/null

echo "--- compile ---"
src/build_opt/sscompile \
    --src $C/server/scripts \
    --out $C/server/scripts/build \
    --content-root $C 2>&1 | tee build/cox_sim/compile.log | tail -2

# An unchecked compile is how a harness comes to certify a build that does not
# exist: sscompile stops at the first error and leaves the previous script.dat
# in place, so the run that follows measures yesterday's raid.
#
# This tree is edited by several sessions at once, and sscompile stops at the
# FIRST error wherever it is -- so a green CoX package is regularly blocked by
# somebody else's half-saved quest. That must not be able to stop CoX being
# verified, so the fallback is the isolated tree cox_compile_check.sh stages:
# every other session's script edits reverted to HEAD, the CoX package kept.
ISO=""
if ! grep -q "^compiled " build/cox_sim/compile.log; then
    err=$(grep -v '^symbols:' build/cox_sim/compile.log | head -1)
    case "$err" in
        *minigame_cox*)
            echo "COMPILE FAILED in CoX - this one is yours:"
            echo "  $err"
            exit 1
            ;;
    esac
    echo "shared tree is broken by another session:"
    echo "  $err"
    echo "falling back to the isolated tree"
    ./tools/cox_compile_check.sh >/dev/null 2>&1 || true
    ISO="${TMPDIR:-/tmp}/cox-compile-check/isotree"
    [ -d "$ISO" ] || { echo "no isolated tree staged; cannot verify"; exit 1; }
    # Re-copy the package unconditionally. cox_compile_check.sh runs with its
    # output suppressed, so a partial failure could otherwise leave an EARLIER
    # run's copy in place -- and then the harness passes against code that is
    # not the code on disk.
    rm -rf "$ISO/server/scripts/minigames/minigame_cox"
    cp -R "$C/server/scripts/minigames/minigame_cox" \
          "$ISO/server/scripts/minigames/minigame_cox"
    for d in ported configs maps npc_combat npc_stats dbindex fields; do
        [ -e "$ISO/$d" ] || [ ! -e "$C/$d" ] || ln -s "$(cd $C && pwd)/$d" "$ISO/$d"
    done
    mkdir -p "$ISO/server/scripts/build"
    # Own `pack/`, shared `configs/`; lanes through the isolated tree's own
    # `ported` symlink. Same shape as tools/cox_verify.sh's fallback.
    src/build_opt/sscompile --src "$ISO/server/scripts" \
        --out "$ISO/server/scripts/build" \
        --content-root "$ISO" \
        --pack "$ISO/pack" --pack $C/configs 2>&1 | tee build/cox_sim/compile_iso.log | tail -1
    if ! grep -q "^compiled " build/cox_sim/compile_iso.log; then
        echo "ISOLATED COMPILE FAILED - refusing to report a harness result"
        exit 1
    fi
    # The isolated path has been observed serving an older pack than the one
    # just compiled, which turns a mutation test green and is worse than no
    # result at all (see tools/cox_verify.sh). So on this path the freshness
    # proof is not optional: SELFTEST is forced on, and the run only counts if
    # disarming Olm's clock in THIS tree turns the harness red.
    SELFTEST=1
fi

echo "--- tick harness (${TICKS} ticks/encounter, god mode) ---"
run_harness() {
    if [ -n "$ISO" ]; then
        TORIRSSERVER_CONTENT="$ISO" TORIRSSERVER_SCRIPTS="$ISO/server/scripts/build" \
        TORIRSSERVER_COX_SIM=1 TORIRSSERVER_COX_SIM_TICKS="$TICKS" TORIRSSERVER_COX_SIM_TRACE="$TRACE" \
            ./src/build_opt/torirsserver --selftest 2>&1 || true
    else
        TORIRSSERVER_COX_SIM=1 TORIRSSERVER_COX_SIM_TICKS="$TICKS" TORIRSSERVER_COX_SIM_TRACE="$TRACE" \
            ./src/build_opt/torirsserver --selftest 2>&1 || true
    fi
}

out=$(run_harness)
echo "$out" | grep -E "cox tick harness|cox-sim:|^  olm:|^  tekton:" || true

if [ "$SHOW_TRACE" = "1" ] && [ -f "$TRACE" ]; then
    echo
    echo "--- trace ($TRACE) ---"
    # Collapse runs of idle ticks: a 4-tick clock is 3 blank lines out of every
    # 4, and printing them buries the actions the eye is looking for.
    awk '/^#/ { print; next }
         $2 == "-" { idle++; next }
         { if (idle) { printf "    ... %d idle tick(s)\n", idle; idle = 0 } print }
         END { if (idle) printf "    ... %d idle tick(s)\n", idle }' "$TRACE"
fi

if [ "$SELFTEST" = "1" ]; then
    # A gate that cannot fail is not a gate. Disarm Olm's timer -- the exact F0
    # regression this harness exists to catch -- and confirm it goes red.
    echo
    echo "--- selftest: with Olm's ai_timer disarmed ---"
    SRC=$C/server/scripts
    [ -n "$ISO" ] && SRC="$ISO/server/scripts"
    OLM=$SRC/minigames/minigame_cox/scripts/cox_olm.rs2
    cp "$OLM" build/cox_sim/cox_olm.bak
    # The SPAWNING form's block, not `[ai_spawn,olm_head]`. Olm is added as
    # `olm_head_spawning` and reaches `olm_head` by changetype, which does not
    # re-dispatch spawn -- so the head's own spawn block never runs and mutating
    # it proves nothing. The first version of this gate mutated exactly that
    # no-op line, compiled cleanly, and reported that the harness was worthless.
    #
    # `npc_settimer(0)` rather than deleting the line: 0 is the engine's own
    # "no timer" value, so the mutation is exactly the F0 defect and nothing
    # else. An earlier version substituted `npc_anim(null, 0)`, which did not
    # compile -- and because the rebuild below was unchecked, the harness then
    # ran against the PREVIOUS pack, passed, and reported that the gate proves
    # nothing. The gate was fine; the mutation never reached the server.
    python3 - "$OLM" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "[ai_spawn,olm_head_spawning]\nnpc_settimer(1);"
assert old in s, "mutation target not found - cox_olm.rs2 moved"
open(p, 'w').write(s.replace(old, "[ai_spawn,olm_head_spawning]\nnpc_settimer(0);", 1))
PY
    PACKDIR=$C/pack
    ROOTDIR=$C
    [ -n "$ISO" ] && PACKDIR="$ISO/pack" && ROOTDIR="$ISO"
    src/build_opt/sscompile --src $SRC --out $SRC/build \
        --content-root $ROOTDIR \
        --pack $PACKDIR --pack $C/configs > build/cox_sim/compile_mutation.log 2>&1
    # CHECKED. An unchecked rebuild here leaves the previous script.dat in place,
    # so the "broken" run is really the good build measured twice -- it passes,
    # and the gate reports itself worthless. That is exactly what happened.
    if ! grep -q "^compiled " build/cox_sim/compile_mutation.log; then
        cp build/cox_sim/cox_olm.bak "$OLM"
        echo "FAIL: the mutated tree did not compile, so nothing was proved:"
        grep -v '^symbols:' build/cox_sim/compile_mutation.log | head -1
        exit 1
    fi
    broken=$(run_harness)
    cp build/cox_sim/cox_olm.bak "$OLM"
    if echo "$broken" | grep -q "cox tick harness FAILED"; then
        echo "OK: disarming Olm's timer turns the harness red, so a green run means something"
    else
        echo "FAIL: the harness passed with Olm's clock unarmed - it proves nothing"
        exit 1
    fi
    # Put the real pack back so a later run is not measuring the broken build.
    exec "$0" "$TICKS"
fi

echo "$out" | grep -q "cox tick harness passed" && exit 0
exit 1
