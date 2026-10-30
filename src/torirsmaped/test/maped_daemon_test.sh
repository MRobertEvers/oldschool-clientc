#!/bin/sh
#
# The multi-PROCESS half of ToriRSMapEd, which no in-process test can reach:
# a real daemon, several client processes, and the Client (session group)
# scoping that decides who hears what.
#
# What it proves, and why each one matters:
#
#   1. Separate processes attach to one daemon and are told their Client id.
#   2. A connection publishing state reaches the OTHER connections of its
#      Client — the viewer-click-to-controller path, across process
#      boundaries, with no shared memory anywhere.
#   3. A different Client hears none of it. This is the isolation the design
#      turns on: two people editing one world must not fight over a
#      selection.
#   4. A late joiner's STATE_SYNC replays its own Client's store and nobody
#      else's.
#   5. The authoritative document answers a square open to a client that
#      holds no document of its own.
#   7. cfg-copy (SAVE_AS on a config record) through the daemon: the copy's
#      block, its ledger id and its client membership line land byte-exact
#      on save; a second copy takes the next id; undo takes the whole copy
#      back as one step; a taken name, a missing source and a non-config
#      type are refused.
#
# Document facts (FACT_CMD/FACT_SAVED) are proven in test-torirsmaped-embed,
# which has real mirror documents to apply them to; this script covers what
# that one structurally cannot — that the same protocol works between
# processes.
#
#   maped_daemon_test.sh <daemon-binary> <ctl-binary> <scratch-dir> <sample-jm2> <sample-jl2>

set -eu

DAEMON="$1"
CTL="$2"
SCRATCH="$3"
SAMPLE_JM2="$4"
SAMPLE_JL2="$5"
PORT="${TORIRSMAPED_TEST_PORT:-43611}"

FAILURES=0
CHECKS=0

check() {
    CHECKS=$((CHECKS + 1))
    if [ "$1" = "1" ]; then
        :
    else
        echo "FAIL: $2"
        FAILURES=$((FAILURES + 1))
    fi
}

contains() {
    # contains <file> <needle> <description>
    if grep -qF -- "$2" "$1"; then check 1 "$3"; else check 0 "$3 (looked for '$2' in $1)"; fi
}

lacks() {
    if grep -qF -- "$2" "$1"; then check 0 "$3 (found '$2' in $1)"; else check 1 "$3"; fi
}

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/maps" "$SCRATCH/configs" "$SCRATCH/pack"
cp "$SAMPLE_JM2" "$SCRATCH/maps/"
cp "$SAMPLE_JL2" "$SCRATCH/maps/"

# A loc type of two records for cfg-copy (7): a repeated key, a comment and a
# blank line inside the block, an empty value and a value past what one
# CFG_FIELD carries (both go across as splices).
LONG_VALUE=$(printf 'x%.0s' $(seq 1 240))
{
    printf '// two locs for cfg-copy\n\n'
    printf '[src_loc]\nname=Crate\nmodels=15402\n// a comment the copy drops\nparam=a,1\n'
    printf 'desc=\nparam=b,2\nop1=Search\nlongfield=%s\n\n' "$LONG_VALUE"
    printf '[other_loc]\nname=Other\n'
} > "$SCRATCH/configs/all.loc"
printf '0=src_loc\n1=other_loc\n' > "$SCRATCH/configs/all.loc.compack"
printf '// Which loc entities have a client half.\n' > "$SCRATCH/pack/loc.client"

# --repo-root '' disables baking: a test must never shell out to a cache build.
"$DAEMON" "$PORT" --content-dir "$SCRATCH" --repo-root '' > "$SCRATCH/daemon.log" 2>&1 &
DAEMON_PID=$!
cleanup() { kill "$DAEMON_PID" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# The daemon binds before it loads, so a short settle is enough.
sleep 1

# 1. Two processes attach and are granted distinct Clients.
"$CTL" --port "$PORT" status > "$SCRATCH/status_a.log" 2>&1
"$CTL" --port "$PORT" status > "$SCRATCH/status_b.log" 2>&1
contains "$SCRATCH/status_a.log" "as client 1" "the first connection is granted Client 1"
contains "$SCRATCH/status_b.log" "as client 2" "the second connection is granted Client 2"
contains "$SCRATCH/status_a.log" "1 square(s) in the tree" "the daemon serves its content tree"

# 2 + 3. State reaches one Client's connections and no other's.
# Explicit pids, never job specs: this runs under a non-interactive shell
# with no job control, where `wait %1` is an error and `wait` alone would
# block on the daemon, which never exits.
"$CTL" --port "$PORT" --client 1 watch 4 > "$SCRATCH/watch_c1.log" 2>&1 &
WATCH_C1=$!
"$CTL" --port "$PORT" --client 2 watch 4 > "$SCRATCH/watch_c2.log" 2>&1 &
WATCH_C2=$!
sleep 1
"$CTL" --port "$PORT" --client 1 select 3200 3200 0 > "$SCRATCH/select.log" 2>&1
"$CTL" --port "$PORT" --client 1 tool 2 > "$SCRATCH/tool.log" 2>&1
wait "$WATCH_C1" || true
wait "$WATCH_C2" || true

contains "$SCRATCH/watch_c1.log" "sel  terrain at 3200,3200 level 0" \
    "a Client's own connections receive its selection, across processes"
contains "$SCRATCH/watch_c1.log" "tool 2" "and its tool change"
lacks "$SCRATCH/watch_c2.log" "sel  terrain" \
    "another Client never hears that selection"
lacks "$SCRATCH/watch_c2.log" "tool 2" "nor the tool change"

# 4. STATE_SYNC replays this Client's store only.
"$CTL" --port "$PORT" --client 1 sync > "$SCRATCH/sync_c1.log" 2>&1
"$CTL" --port "$PORT" --client 2 sync > "$SCRATCH/sync_c2.log" 2>&1
contains "$SCRATCH/sync_c1.log" "2 state key(s)" "a late joiner replays its Client's two keys"
contains "$SCRATCH/sync_c1.log" "sel  terrain at 3200,3200" "with the stored values"
contains "$SCRATCH/sync_c2.log" "0 state key(s)" "the other Client's store is empty"

# 5. The authoritative document answers a client that holds none.
"$CTL" --port "$PORT" open 50 50 > "$SCRATCH/open.log" 2>&1
contains "$SCRATCH/open.log" "m50_50 open:" "the document answers a square open"
contains "$SCRATCH/open.log" "dirty map=0 loc=0" "a freshly opened square is clean"

# 7. cfg-copy through the daemon.
cp "$SCRATCH/configs/all.loc" "$SCRATCH/all.loc.before"
cp "$SCRATCH/pack/loc.client" "$SCRATCH/loc.client.before"
"$CTL" --port "$PORT" cfg-copy loc src_loc copy_loc > "$SCRATCH/copy.log" 2>&1 || true
contains "$SCRATCH/copy.log" "copied loc:src_loc to loc:copy_loc, id 70000" \
    "cfg-copy coins the namespace's floor id (70000, the register's loc base)"
"$CTL" --port "$PORT" save > "$SCRATCH/copy_save.log" 2>&1 || true
contains "$SCRATCH/copy_save.log" "loc:copy_loc" "save writes the copied record"
contains "$SCRATCH/copy_save.log" "pack:loc.alloc" "and the ledger"
contains "$SCRATCH/copy_save.log" "pack:loc.client" "and the client membership"
{
    cat "$SCRATCH/all.loc.before"
    printf '\n[copy_loc]\nname=Crate\nmodels=15402\nparam=a,1\ndesc=\nparam=b,2\nop1=Search\n'
    printf 'longfield=%s\n' "$LONG_VALUE"
} > "$SCRATCH/all.loc.expected"
if cmp -s "$SCRATCH/all.loc.expected" "$SCRATCH/configs/all.loc"; then
    check 1 "configs/all.loc gains exactly [copy_loc] with every field, after a separator"
else
    check 0 "configs/all.loc gains exactly [copy_loc] with every field, after a separator"
    diff "$SCRATCH/all.loc.expected" "$SCRATCH/configs/all.loc" | head -20
fi
{ cat "$SCRATCH/loc.client.before"; printf 'copy_loc\n'; } > "$SCRATCH/loc.client.expected"
if cmp -s "$SCRATCH/loc.client.expected" "$SCRATCH/pack/loc.client"; then
    check 1 "pack/loc.client gains exactly one copy_loc line"
else
    check 0 "pack/loc.client gains exactly one copy_loc line"
fi
check "$( [ "$(tail -n 1 "$SCRATCH/pack/loc.alloc")" = "70000=copy_loc" ] && echo 1 )" \
    "pack/loc.alloc ends with 70000=copy_loc"
check "$( [ "$(grep -c 'allocated below this line by tools/ss_allocate.py' "$SCRATCH/pack/loc.alloc")" = 1 ] && echo 1 )" \
    "the new ledger carries ss_allocate.py's marker once"
cp "$SCRATCH/configs/all.loc" "$SCRATCH/all.loc.saved"
cp "$SCRATCH/pack/loc.alloc" "$SCRATCH/loc.alloc.saved"
cp "$SCRATCH/pack/loc.client" "$SCRATCH/loc.client.saved"

"$CTL" --port "$PORT" cfg-copy loc src_loc copy_two > "$SCRATCH/copy2.log" 2>&1 || true
contains "$SCRATCH/copy2.log" "copied loc:src_loc to loc:copy_two, id 70001" \
    "a second copy takes the next id"
"$CTL" --port "$PORT" undo > "$SCRATCH/copy_undo.log" 2>&1 || true
contains "$SCRATCH/copy_undo.log" "undo " "undo answers"
lacks "$SCRATCH/copy_undo.log" "undo 0 command(s)" "and takes the copy back"
"$CTL" --port "$PORT" save > "$SCRATCH/copy_save2.log" 2>&1 || true
for f in configs/all.loc pack/loc.alloc pack/loc.client; do
    b=$(basename "$f")
    if cmp -s "$SCRATCH/$f" "$SCRATCH/$b.saved"; then
        check 1 "after undo + save, $f is byte-identical to before the second copy"
    else
        check 0 "after undo + save, $f is byte-identical to before the second copy"
    fi
done
"$CTL" --port "$PORT" cfg-copy loc src_loc copy_loc > "$SCRATCH/copy_taken.log" 2>&1 || true
contains "$SCRATCH/copy_taken.log" "was refused" "copying onto a taken name is refused"
"$CTL" --port "$PORT" cfg-copy loc no_such_loc copy_three > "$SCRATCH/copy_absent.log" 2>&1 || true
contains "$SCRATCH/copy_absent.log" "was refused" "copying a record that is not there is refused"
"$CTL" --port "$PORT" cfg-copy enum some_enum copy_enum > "$SCRATCH/copy_enum.log" 2>&1 || true
contains "$SCRATCH/copy_enum.log" "was refused" "a non-loc/npc/obj type is refused"

# 6. The browser's route in: the same daemon, upgraded, framed. Skipped
# rather than failed when python3 is absent — the probe needs a WebSocket
# client and hand-rolling one in sh is not a test, it is a liability.
if command -v python3 > /dev/null 2>&1; then
    if python3 "$(dirname "$0")/maped_ws_probe.py" "$PORT" > "$SCRATCH/ws.log" 2>&1; then
        check 1 "a WebSocket client completes the handshake and the protocol"
    else
        check 0 "a WebSocket client completes the handshake and the protocol"
        cat "$SCRATCH/ws.log"
    fi
    contains "$SCRATCH/ws.log" "0 failures" "every WebSocket probe check passed"
else
    echo "(python3 absent — skipping the WebSocket probe)"
fi

echo "maped_daemon_test: $CHECKS checks, $FAILURES failures"
[ "$FAILURES" -eq 0 ]
