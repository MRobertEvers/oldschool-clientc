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
#   8. spawn-edit / spawn-move / cfg-set through the daemon: a move rewrites
#      one line of a hand-written .spawn in place (its comment kept), a kind
#      change is a delete + add, undo + save puts the file back byte for
#      byte, a row the file does not hold is refused; cfg-set edits a record
#      block of configs/all.<type> and of a server overlay
#      ("<type>:<name>@<overlay>") and save writes exactly that.
#   9. The named areas (editor/areas.txt): area-set marks an area (rectangles,
#      level, quoted name with a space), a second area-set of the same name
#      replaces it keeping its colour and note, `rename` renames it, a taken
#      name is refused, collection-set lists squares (a bad key is refused),
#      `areas` prints the records, area-remove + undo restores the row, save
#      writes the file with its header, and removing everything then saving
#      removes the file.
#  10. The catalog palettes (editor/palettes.txt): palette-set 0 fills the
#      first free slot (with a pose), a held slot is replaced, slot 11 is
#      refused, `palettes` lists, rename moves every line, palette-remove of a
#      slot + undo, save writes the file with its header, palette-remove with
#      no slot deletes the palette and the emptied file goes on save.
#  11. Accounts (torirs_maped_accounts.h) through a SECOND daemon bound to a
#      lane (--repo-root + --live-dir beside the scratch tree; no binary is
#      run): `accounts` lists none, account-new makes a save from a fixture
#      (a bad password refused), account-set writes the position, a stat, a
#      varp and an item into the save and queues a quest cheat, account-place
#      moves the save, account-view reads it back, account-use makes it the
#      play account (remembered in accounts.ini), account-dup / account-delete.
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

# A hand-written edited spawn file, an npc record and a server overlay for 8.
mkdir -p "$SCRATCH/server/scripts/areas/edited/configs" "$SCRATCH/server/scripts/areas/t/configs"
printf '// hand-written\n==== NPC ====\ngoblin   3205  3210 0   // keep me\nrat                                           3206  3211 0\n' \
    > "$SCRATCH/server/scripts/areas/edited/configs/m50_50.spawn"
cp "$SCRATCH/server/scripts/areas/edited/configs/m50_50.spawn" "$SCRATCH/m50_50.spawn.before"
printf '[guard]\nname=Guard\n' > "$SCRATCH/configs/all.npc"
printf '0=guard\n' > "$SCRATCH/configs/all.npc.compack"
printf '// authored\n[guard]\nwanderrange=2\n\n[other]\nwanderrange=1\n' > "$SCRATCH/server/scripts/areas/t/configs/t.npc"
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

# 8. spawn-edit / spawn-move / cfg-set.
SPAWN="$SCRATCH/server/scripts/areas/edited/configs/m50_50.spawn"
"$CTL" --port "$PORT" spawn-move npc goblin 3205 3210 0 to 3207 3212 > "$SCRATCH/smove.log" 2>&1 || true
contains "$SCRATCH/smove.log" "moved npc goblin 3205 3210 0 -> goblin 3207 3212 0 (1 command, one undo step)" \
    "spawn-move is one SPAWN_EDIT"
"$CTL" --port "$PORT" save > "$SCRATCH/smove_save.log" 2>&1 || true
printf '// hand-written\n==== NPC ====\ngoblin                                        3207  3212 0   // keep me\nrat                                           3206  3211 0\n' \
    > "$SCRATCH/m50_50.spawn.moved"
if cmp -s "$SCRATCH/m50_50.spawn.moved" "$SPAWN"; then
    check 1 "the save rewrites exactly the moved line, its comment kept"
else
    check 0 "the save rewrites exactly the moved line, its comment kept"
    diff "$SCRATCH/m50_50.spawn.moved" "$SPAWN" | head -10
fi
"$CTL" --port "$PORT" spawn-edit npc rat 3206 3211 0 to obj rat 3206 3211 0 5 > "$SCRATCH/skind.log" 2>&1 || true
contains "$SCRATCH/skind.log" "(2 commands, one undo step)" "npc -> obj is a delete and an add"
"$CTL" --port "$PORT" undo > "$SCRATCH/skind_undo.log" 2>&1 || true
contains "$SCRATCH/skind_undo.log" "undo 2 command(s)" "one undo takes both back"
"$CTL" --port "$PORT" undo > "$SCRATCH/smove_undo.log" 2>&1 || true
contains "$SCRATCH/smove_undo.log" "undo 1 command(s)" "undo the move"
"$CTL" --port "$PORT" save > "$SCRATCH/smove_save2.log" 2>&1 || true
if cmp -s "$SCRATCH/m50_50.spawn.before" "$SPAWN"; then
    check 1 "after undo + save the hand-spaced spawn file is byte-identical"
else
    check 0 "after undo + save the hand-spaced spawn file is byte-identical"
    diff "$SCRATCH/m50_50.spawn.before" "$SPAWN" | head -10
fi
if "$CTL" --port "$PORT" spawn-move npc nobody 3205 3210 0 to 3200 3200 > "$SCRATCH/snone.log" 2>&1; then
    check 0 "moving a row the file does not hold fails"
else
    check 1 "moving a row the file does not hold fails"
fi
contains "$SCRATCH/snone.log" "was refused" "and says the authority refused it"
"$CTL" --port "$PORT" cfg-set npc:guard@areas/t/configs/t.npc wanderrange 6 > "$SCRATCH/cfgset.log" 2>&1 || true
contains "$SCRATCH/cfgset.log" "wanderrange: '2' -> '6'" "cfg-set edits the overlay block"
"$CTL" --port "$PORT" cfg-set npc:guard op1 Talk-to > "$SCRATCH/cfgset2.log" 2>&1 || true
contains "$SCRATCH/cfgset2.log" "op1: '' -> 'Talk-to'" "cfg-set adds a field to the all.npc block"
"$CTL" --port "$PORT" save > "$SCRATCH/cfgset_save.log" 2>&1 || true
contains "$SCRATCH/cfgset_save.log" "npc:guard@areas/t/configs/t.npc" "save names the overlay key"
check "$( [ "$(cat "$SCRATCH/server/scripts/areas/t/configs/t.npc")" = "$(printf '// authored\n[guard]\nwanderrange=6\n\n[other]\nwanderrange=1')" ] && echo 1 )" \
    "the overlay file changes in [guard] only"
check "$( [ "$(cat "$SCRATCH/configs/all.npc")" = "$(printf '[guard]\nname=Guard\nop1=Talk-to')" ] && echo 1 )" \
    "configs/all.npc gains op1"
if "$CTL" --port "$PORT" cfg-set 'npc:guard@../t.npc' wanderrange 1 > "$SCRATCH/cfgbad.log" 2>&1; then
    check 0 "an overlay key with .. is refused"
else
    check 1 "an overlay key with .. is refused"
fi

# 9. The named areas and square collections.
AREAS_FILE="$SCRATCH/editor/areas.txt"
"$CTL" --port "$PORT" area-set "Lumbridge castle" 0 3205 3208 3225 3226 colour 2 note "the castle" > "$SCRATCH/aset.log" 2>&1 || true
contains "$SCRATCH/aset.log" "Mark area Lumbridge castle" "area-set marks a named area"
"$CTL" --port "$PORT" area-set "Lumbridge castle" 0 3205 3208 3225 3226 3226 3210 3230 3214 > "$SCRATCH/aset2.log" 2>&1 || true
contains "$SCRATCH/aset2.log" "Add rect to area Lumbridge castle" "a second area-set replaces it (one more rect)"
"$CTL" --port "$PORT" area-set Courtyard all 3210 3215 3220 3222 > "$SCRATCH/aset3.log" 2>&1 || true
contains "$SCRATCH/aset3.log" "Mark area Courtyard" "an area on every level"
if "$CTL" --port "$PORT" area-set Courtyard all 3210 3215 3220 3222 rename "Lumbridge castle" > "$SCRATCH/ataken.log" 2>&1; then
    check 0 "renaming onto a taken name fails"
else
    check 1 "renaming onto a taken name fails"
fi
"$CTL" --port "$PORT" area-set Courtyard all 3210 3215 3220 3222 rename Yard > "$SCRATCH/arename.log" 2>&1 || true
contains "$SCRATCH/arename.log" "Rename area Courtyard to Yard" "rename gives the area a new name"
"$CTL" --port "$PORT" collection-set Lumbridge m50_50 m50_51 > "$SCRATCH/cset.log" 2>&1 || true
contains "$SCRATCH/cset.log" "New collection Lumbridge" "collection-set names a collection of squares"
if "$CTL" --port "$PORT" collection-set Bad 50_50 > "$SCRATCH/cbad.log" 2>&1; then
    check 0 "a key that is not a square is refused"
else
    check 1 "a key that is not a square is refused"
fi
"$CTL" --port "$PORT" areas > "$SCRATCH/areas.log" 2>&1 || true
contains "$SCRATCH/areas.log" "2 area(s), 1 collection(s)" "areas counts the records"
contains "$SCRATCH/areas.log" "area Lumbridge castle | level 0 | rects 3205 3208 3225 3226, 3226 3210 3230 3214 | colour 2 | note the castle" \
    "the replaced area kept its colour and note"
contains "$SCRATCH/areas.log" "collection Lumbridge | squares m50_50 m50_51" "areas lists the collection"
"$CTL" --port "$PORT" area-remove Yard > "$SCRATCH/arm.log" 2>&1 || true
contains "$SCRATCH/arm.log" "Delete area Yard" "area-remove deletes it"
"$CTL" --port "$PORT" undo > "$SCRATCH/aundo.log" 2>&1 || true
contains "$SCRATCH/aundo.log" "undo 1 command(s)" "undo takes the delete back"
"$CTL" --port "$PORT" save > "$SCRATCH/asave.log" 2>&1 || true
contains "$SCRATCH/asave.log" "areas" "save names the areas document"
check "$( [ "$(head -n 1 "$AREAS_FILE")" = "# ToriRSMapEd named areas and square collections (editor/areas.txt)." ] && echo 1 )" \
    "editor/areas.txt starts with its header"
check "$( [ "$(grep -cE '^(area|collection) ' "$AREAS_FILE")" = 3 ] && echo 1 )" \
    "editor/areas.txt holds the three records"
contains "$AREAS_FILE" "area Yard | level all | rects 3210 3215 3220 3222" "the undone delete is saved in place"
"$CTL" --port "$PORT" area-remove "Lumbridge castle" > /dev/null 2>&1 || true
"$CTL" --port "$PORT" area-remove Yard > /dev/null 2>&1 || true
"$CTL" --port "$PORT" collection-remove Lumbridge > "$SCRATCH/crm.log" 2>&1 || true
contains "$SCRATCH/crm.log" "Delete collection Lumbridge" "collection-remove deletes it"
"$CTL" --port "$PORT" save > /dev/null 2>&1 || true
check "$( [ ! -e "$AREAS_FILE" ] && echo 1 )" "removing every record and saving removes the file"

# 10. The catalog palettes.
PALETTES_FILE="$SCRATCH/editor/palettes.txt"
"$CTL" --port "$PORT" palette-set Lumbridge 0 loc:tree > "$SCRATCH/pset.log" 2>&1 || true
contains "$SCRATCH/pset.log" "Add tree to palette Lumbridge slot 1" "palette-set 0 fills the first free slot"
"$CTL" --port "$PORT" palette-set Lumbridge 0 loc:oaktree shape 10 rot 2 > "$SCRATCH/pset2.log" 2>&1 || true
contains "$SCRATCH/pset2.log" "Add oaktree to palette Lumbridge slot 2" "then the next one, with a pose"
"$CTL" --port "$PORT" palette-set Lumbridge 2 npc:hans > "$SCRATCH/pset3.log" 2>&1 || true
contains "$SCRATCH/pset3.log" "Replace oaktree with hans in palette Lumbridge slot 2" "a held slot is replaced"
if "$CTL" --port "$PORT" palette-set Lumbridge 11 loc:tree > "$SCRATCH/pbad.log" 2>&1; then
    check 0 "slot 11 is refused"
else
    check 1 "slot 11 is refused"
fi
"$CTL" --port "$PORT" palettes > "$SCRATCH/palettes.log" 2>&1 || true
contains "$SCRATCH/palettes.log" "1 palette(s)" "palettes counts them"
contains "$SCRATCH/palettes.log" "palette Lumbridge | slot 2 | npc:hans" "palettes lists the slots"
"$CTL" --port "$PORT" palette-set Lumbridge rename Castle > "$SCRATCH/prename.log" 2>&1 || true
contains "$SCRATCH/prename.log" "Rename palette Lumbridge to Castle" "rename moves the palette"
"$CTL" --port "$PORT" palette-remove Castle 1 > "$SCRATCH/prm.log" 2>&1 || true
contains "$SCRATCH/prm.log" "Remove tree from palette Castle slot 1" "palette-remove empties a slot"
"$CTL" --port "$PORT" undo > "$SCRATCH/pundo.log" 2>&1 || true
contains "$SCRATCH/pundo.log" "undo 1 command(s)" "undo puts the slot back"
"$CTL" --port "$PORT" save > "$SCRATCH/psave.log" 2>&1 || true
contains "$SCRATCH/psave.log" "palettes" "save names the palettes document"
check "$( [ "$(head -n 1 "$PALETTES_FILE")" = "# ToriRSMapEd catalog palettes (editor/palettes.txt)." ] && echo 1 )" \
    "editor/palettes.txt starts with its header"
contains "$PALETTES_FILE" "palette Castle | slot 1 | loc:tree" "the undone remove is saved"
contains "$PALETTES_FILE" "palette Castle | slot 2 | npc:hans" "the renamed slot is saved"
"$CTL" --port "$PORT" palette-remove Castle > "$SCRATCH/pdel.log" 2>&1 || true
contains "$SCRATCH/pdel.log" "Remove hans from palette Castle slot 2" "palette-remove with no slot takes every line"
"$CTL" --port "$PORT" save > /dev/null 2>&1 || true
check "$( [ ! -e "$PALETTES_FILE" ] && echo 1 )" "deleting the last palette and saving removes the file"

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

# 11. Accounts: a second daemon with a lane (the first has no repo root, so
# no lane). The lane's live dir sits beside the scratch tree; nothing in it
# is launched.
ACCT_PORT=$((PORT + 1))
ACCT_LIVE="$SCRATCH.live"
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
rm -rf "$ACCT_LIVE"
mkdir -p "$ACCT_LIVE"
printf '; a fixture\n[player]\nversion = 1\nname = fixture\nx = 3206\nz = 3233\nlevel = 0\n\n[varps]\n281 = 1000\n\n[inv]\n0 = 1277 1\n' \
    > "$SCRATCH/fixture.ini"
TORIRSMAPED_LIVE_SERVER_BIN=/nonexistent/torirsserver TORIRSMAPED_LIVE_PLAY_BIN=/nonexistent/torirs \
    "$DAEMON" "$ACCT_PORT" --content-dir "$SCRATCH" --repo-root "$REPO_ROOT" --live-dir "$ACCT_LIVE" \
    > "$SCRATCH/daemon_acct.log" 2>&1 &
ACCT_DAEMON_PID=$!
cleanup_acct() { kill "$ACCT_DAEMON_PID" 2>/dev/null || true; cleanup; }
trap cleanup_acct EXIT INT TERM
sleep 1
ACTL() { "$CTL" --port "$ACCT_PORT" "$@"; }
# The first daemon has no lane: the verb says so.
"$CTL" --port "$PORT" accounts > "$SCRATCH/acct_nolane.log" 2>&1 || true
contains "$SCRATCH/acct_nolane.log" "no lane is bound" "accounts on a daemon without a lane says so"
ACTL accounts > "$SCRATCH/acct0.log" 2>&1 || true
contains "$SCRATCH/acct0.log" "0 account(s) in " "accounts lists the lane's empty saves dir"
contains "$SCRATCH/acct0.log" "/saves (play account" "naming the lane's saves dir"
contains "$SCRATCH/acct0.log" "play account maped43620" "and the lane's default play account"
if ACTL account-new tester short > "$SCRATCH/acct_bad.log" 2>&1; then
    check 0 "account-new refuses a short password"
else
    check 1 "account-new refuses a short password"
fi
contains "$SCRATCH/acct_bad.log" "8 to 20 characters" "and says the rule"
ACTL account-new tester Secret123x "$SCRATCH/fixture.ini" > "$SCRATCH/acct_new.log" 2>&1 || true
contains "$SCRATCH/acct_new.log" "created tester from $SCRATCH/fixture.ini" "account-new makes the save from the fixture"
check "$( [ -f "$ACCT_LIVE/saves/tester.ini" ] && echo 1 )" "the save file exists"
contains "$ACCT_LIVE/saves/tester.ini" "name = tester" "with the account's name"
contains "$ACCT_LIVE/accounts.ini" "tester = Secret123x" "the registry remembers the password"
ACTL account-set tester player.pos 3222 3218 1 > "$SCRATCH/acct_pos.log" 2>&1 || true
contains "$SCRATCH/acct_pos.log" "offline: wrote saves/tester.ini (position 3222,3218 level 1)" "account-set player.pos writes the save (offline)"
contains "$ACCT_LIVE/saves/tester.ini" "x = 3222" "x is in the file"
ACTL account-set tester stats.cooking 50 > "$SCRATCH/acct_stat.log" 2>&1 || true
contains "$ACCT_LIVE/saves/tester.ini" "7 = 50 1013330" "stats.cooking 50 is boosted + xp_tenths"
ACTL account-set tester varps.281 0 > "$SCRATCH/acct_varp.log" 2>&1 || true
lacks "$ACCT_LIVE/saves/tester.ini" "281 = 1000" "varps.281 0 removes the line"
ACTL account-set tester inv.3 995 100 > "$SCRATCH/acct_inv.log" 2>&1 || true
contains "$ACCT_LIVE/saves/tester.ini" "3 = 995 100" "inv.3 995 100 lands in [inv]"
if ACTL account-set tester stats.cooking 100 > "$SCRATCH/acct_refuse.log" 2>&1; then
    check 0 "a level of 100 is refused (exit 1)"
else
    check 1 "a level of 100 is refused (exit 1)"
fi
contains "$SCRATCH/acct_refuse.log" "1..99" "with the range"
ACTL account-set tester cheat ::give coins 5 > "$SCRATCH/acct_cheat.log" 2>&1 || true
contains "$SCRATCH/acct_cheat.log" "queued for tester's next login" "an offline cheat is queued"
contains "$ACCT_LIVE/accounts.ini" "tester.0 = give coins 5" "in the registry"
ACTL account-place tester 3100 3300 2 > "$SCRATCH/acct_place.log" 2>&1 || true
contains "$SCRATCH/acct_place.log" "offline: wrote saves/tester.ini (position 3100,3300 level 2)" "account-place writes the save"
ACTL account-view tester > "$SCRATCH/acct_view.log" 2>&1 || true
contains "$SCRATCH/acct_view.log" "position 3100,3300 level 2" "account-view reads the position back"
contains "$SCRATCH/acct_view.log" "cooking=50/101333" "and the stat"
contains "$SCRATCH/acct_view.log" "3=995x100" "and the item"
contains "$SCRATCH/acct_view.log" "::give coins 5" "and the queue"
ACTL account-use tester > "$SCRATCH/acct_use.log" 2>&1 || true
contains "$SCRATCH/acct_use.log" "RUN and Play log in as tester" "account-use sets the play account"
contains "$ACCT_LIVE/accounts.ini" "play_user = tester" "remembered in accounts.ini"
ACTL accounts > "$SCRATCH/acct1.log" 2>&1 || true
contains "$SCRATCH/acct1.log" "play account tester" "accounts shows the new play account"
contains "$SCRATCH/acct1.log" "tester         offline 3100,3300,2 play password queued 1" "and the row with its flags"
ACTL account-dup tester second Other9pass > "$SCRATCH/acct_dup.log" 2>&1 || true
contains "$SCRATCH/acct_dup.log" "duplicated as second" "account-dup copies it"
contains "$ACCT_LIVE/saves/second.ini" "name = second" "under its own name"
ACTL account-delete second > "$SCRATCH/acct_del.log" 2>&1 || true
contains "$SCRATCH/acct_del.log" "deleted second" "account-delete removes it"
check "$( [ ! -e "$ACCT_LIVE/saves/second.ini" ] && echo 1 )" "the save is gone"
ACTL quests > "$SCRATCH/acct_quests.log" 2>&1 || true
contains "$SCRATCH/acct_quests.log" "quest row(s)" "quests answers (the scratch tree has none)"
kill "$ACCT_DAEMON_PID" 2>/dev/null || true
rm -rf "$ACCT_LIVE"

# 12. Editor lanes (torirsmaped/torirs_maped_lanes.h): a third daemon on
# --lane work, whose content is a mirror of the scratch tree under its own
# repo root ($SCRATCH.repo/build/lanes/work). An edit saved through it lands
# in the lane and never in the tree; publish-new copies it into a new lane;
# publish main writes it into the tree and re-links the lane; discard puts a
# later edit back.
LANE_PORT=$((PORT + 2))
LANE_REPO="$SCRATCH.repo"
rm -rf "$LANE_REPO"
mkdir -p "$LANE_REPO"
SCRATCH_ABS="$(cd "$SCRATCH" && pwd)"
LANE_REPO_ABS="$(cd "$LANE_REPO" && pwd)"
"$DAEMON" "$LANE_PORT" --content-dir "$SCRATCH" --repo-root "$LANE_REPO" --lane work \
    > "$SCRATCH/daemon_lane.log" 2>&1 &
LANE_DAEMON_PID=$!
cleanup_lane() { kill "$LANE_DAEMON_PID" 2>/dev/null || true; cleanup_acct; }
trap cleanup_lane EXIT INT TERM
sleep 2
LCTL() { "$CTL" --port "$LANE_PORT" "$@"; }
SQUARE_JM2="$(basename "$SAMPLE_JM2")"
SQ_X="$(echo "$SQUARE_JM2" | sed -E 's/^m([0-9]+)_([0-9]+)\.jm2$/\1/')"
SQ_Z="$(echo "$SQUARE_JM2" | sed -E 's/^m([0-9]+)_([0-9]+)\.jm2$/\2/')"
contains "$SCRATCH/daemon_lane.log" "lane 'work': $LANE_REPO_ABS/build/lanes/work/content, mirrors $SCRATCH_ABS" \
    "the daemon serves the lane's mirror"
check "$( [ -L "$LANE_REPO/build/lanes/work/content/maps/$SQUARE_JM2" ] && echo 1 )" "the mirror links the square"
"$CTL" --port "$PORT" lane > "$SCRATCH/lane_main.log" 2>&1 || true
contains "$SCRATCH/lane_main.log" "lane main (the main tree)" "a daemon without a lane is the main lane"
LCTL lane > "$SCRATCH/lane_work.log" 2>&1 || true
contains "$SCRATCH/lane_work.log" "lane work" "lane names the lane"
contains "$SCRATCH/lane_work.log" "base    $SCRATCH_ABS" "and its base"
LCTL lanes > "$SCRATCH/lanes.log" 2>&1 || true
contains "$SCRATCH/lanes.log" "2 lane(s)" "lanes: main and work"
contains "$SCRATCH/lanes.log" "* work" "the served one is marked"
LCTL changes > "$SCRATCH/changes0.log" 2>&1 || true
contains "$SCRATCH/changes0.log" "0 modified, 0 added, 0 deleted" "a fresh lane has no changes"
BASE_SUM="$(cksum < "$SCRATCH/maps/$SQUARE_JM2")"
LCTL tile-height "$SQ_X" "$SQ_Z" 0 22 23 120 > "$SCRATCH/lane_edit.log" 2>&1 || true
LCTL save > "$SCRATCH/lane_save.log" 2>&1 || true
check "$( [ -f "$LANE_REPO/build/lanes/work/content/maps/$SQUARE_JM2" ] && [ ! -L "$LANE_REPO/build/lanes/work/content/maps/$SQUARE_JM2" ] && echo 1 )" \
    "a save makes the square a real file in the lane"
check "$( [ "$(cksum < "$SCRATCH/maps/$SQUARE_JM2")" = "$BASE_SUM" ] && echo 1 )" "the tree's square is untouched"
LCTL changes > "$SCRATCH/changes1.log" 2>&1 || true
contains "$SCRATCH/changes1.log" "1 modified, 0 added, 0 deleted" "changes: the saved square"
contains "$SCRATCH/changes1.log" "M maps/$SQUARE_JM2" "by path"
LCTL publish-new review > "$SCRATCH/pub_new.log" 2>&1 || true
contains "$SCRATCH/pub_new.log" "publish done: published 1 file(s) to review" "publish-new makes the lane and writes into it"
check "$( [ -f "$LANE_REPO/build/lanes/review/content/maps/$SQUARE_JM2" ] && [ ! -L "$LANE_REPO/build/lanes/review/content/maps/$SQUARE_JM2" ] && echo 1 )" \
    "the new lane holds the square as a real file"
check "$( [ "$(cksum < "$SCRATCH/maps/$SQUARE_JM2")" = "$BASE_SUM" ] && echo 1 )" "publishing to a lane leaves the tree alone"
LCTL lanes > "$SCRATCH/lanes2.log" 2>&1 || true
contains "$SCRATCH/lanes2.log" "3 lane(s)" "lanes: main, review, work"
# The first daemon serves the tree directly and holds its lock: a publish
# into it is refused until forced.
LCTL publish main > "$SCRATCH/pub_main_held.log" 2>&1 || true
contains "$SCRATCH/pub_main_held.log" "is being edited by pid" "publish main is refused while another editor holds the tree"
LCTL publish main force > "$SCRATCH/pub_main.log" 2>&1 || true
contains "$SCRATCH/pub_main.log" "publish done: published 1 file(s) to main (the main tree); this lane is clean again" \
    "publish main force writes into the tree and resets the lane"
check "$( [ "$(cksum < "$SCRATCH/maps/$SQUARE_JM2")" != "$BASE_SUM" ] && echo 1 )" "the tree's square changed"
check "$( [ -L "$LANE_REPO/build/lanes/work/content/maps/$SQUARE_JM2" ] && echo 1 )" "the lane's square is a link again"
LCTL changes > "$SCRATCH/changes2.log" 2>&1 || true
contains "$SCRATCH/changes2.log" "0 modified, 0 added, 0 deleted" "and the lane has no changes"
NEW_SUM="$(cksum < "$SCRATCH/maps/$SQUARE_JM2")"
LCTL tile-height "$SQ_X" "$SQ_Z" 0 25 23 120 > "$SCRATCH/lane_edit2.log" 2>&1 || true
LCTL save > "$SCRATCH/lane_save2.log" 2>&1 || true
LCTL discard > "$SCRATCH/discard.log" 2>&1 || true
contains "$SCRATCH/discard.log" "discard done: discarded 1 change(s)" "discard puts the change back"
contains "$SCRATCH/discard.log" "every window reopens its square" "and tells every window to reopen"
check "$( [ -L "$LANE_REPO/build/lanes/work/content/maps/$SQUARE_JM2" ] && echo 1 )" "the square is a link after the discard"
check "$( [ "$(cksum < "$SCRATCH/maps/$SQUARE_JM2")" = "$NEW_SUM" ] && echo 1 )" "the tree kept what was published"
"$CTL" --port "$PORT" changes > "$SCRATCH/changes_main.log" 2>&1 || true
contains "$SCRATCH/changes_main.log" "0 modified, 0 added, 0 deleted" "the main lane never has changes"
"$CTL" --port "$PORT" publish review > "$SCRATCH/pub_from_main.log" 2>&1 || true
contains "$SCRATCH/pub_from_main.log" "edits the main tree directly" "publishing from main is refused"
kill "$LANE_DAEMON_PID" 2>/dev/null || true
rm -rf "$LANE_REPO"

echo "maped_daemon_test: $CHECKS checks, $FAILURES failures"
[ "$FAILURES" -eq 0 ]
