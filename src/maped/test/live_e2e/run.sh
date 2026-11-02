#!/bin/sh
# The live lane, end to end (docs/MAPED_STATUS.md "the launched live lane").
#
#   src/maped/test/live_e2e/run.sh            (make -C src maped-live-e2e)
#
# Everything is written under src/<obj>/tmp/live/; OSRS-Content is only read.
#
#   content/  a writable mirror of OSRS-Content/osrs-content (mirror.py): the
#             editor server saves into it and the pack build allocates in it
#   lane/     the live dir: scripts/, scripts_lanes/, servpack/ (copies of the
#             tree's three packs), cache/ (a copy of cache.osrs241), the
#             scriptrun's logs
#
# 1. torirsmaped serves the mirror with --live-dir lane/, its launch settings
#    from TORIRSMAPED_LIVE_* (the game server is server.sh: a scriptrun bot on
#    the lane's packs, control link on the game port; js5_server is real).
# 2. torirsmapedctl live-attach launch: the bot logs in and reads Hans's line.
# 3. (a) text-replace Hans's first line through the authority, RUN: the bot,
#    still in the same session, reads the new line on a later Talk-to.
# 4. (b) tile-height on m50_50 (the bot stands in it), RUN: the bot's wire log
#    shows exactly one REBUILD after the reload.
# 5. (c) `cfg-set loc:blueflower op3 Search` and an [oploc3,blueflower] script,
#    RUN: the editor diffs the server pack's manifest and names the loc group
#    (255, 70) in the reload; the server replaces its loc records; the bot,
#    never relogged, presses the new op3 and reads the script's message.
# 6. (d) the same for an npc: `op4=Wave` on Hans and [opnpc4,hans].
# 7. live-detach stops the launched processes; the tree is checked untouched.
#
# The lane has no play client (TORIRSMAPED_LIVE_PLAY_BIN=): the scriptrun bot
# is its player, and a scriptrun server refuses a real client. run_play.sh
# (make -C src maped-live-e2e-play) is the lane with a real server and the
# real play client.
set -u
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
obj=${LIVE_E2E_OBJ:-build_opt}
dir="$repo/src/$obj/tmp/live"
content_src="$repo/OSRS-Content/osrs-content"
maped_port=${LIVE_E2E_MAPED_PORT:-43629}
game_port=${LIVE_E2E_GAME_PORT:-43630}
js5_port=${LIVE_E2E_JS5_PORT:-43631}
ctl="$repo/src/$obj/torirsmapedctl --port $maped_port"
fails=0

say() { printf 'live-e2e: %s\n' "$*"; }
check() {
    # check <name> <0|1 pass> <detail>
    if [ "$2" = 1 ]; then printf 'live-e2e: %-58s ok   %s\n' "$1" "$3"
    else printf 'live-e2e: %-58s FAILED   %s\n' "$1" "$3"; fails=$((fails + 1)); fi
}
wait_for() {
    # wait_for <file> <pattern> <seconds>
    n=0
    while [ "$n" -lt "$3" ]; do
        grep -q -- "$2" "$1" 2>/dev/null && return 0
        sleep 1; n=$((n + 1))
    done
    return 1
}

for bin in torirsmaped torirsmapedctl torirsserver js5_server sscompile; do
    [ -x "$repo/src/$obj/$bin" ] || { say "missing src/$obj/$bin (make -C src torirsmaped torirsmapedctl torirsserver js5-server sscompile)"; exit 2; }
done

before_hans=$(cksum < "$content_src/server/scripts/areas/lumbridge/scripts/hans.rs2")
before_map=$(cksum < "$content_src/maps/m50_50.jm2")
before_alloc=$(cat "$content_src"/pack/*.alloc | cksum)
before_loc=$(cksum < "$content_src/configs/all.loc")
before_npc=$(cksum < "$content_src/configs/all.npc")

rm -rf "$dir"
mkdir -p "$dir/lane"
python3 "$here/mirror.py" "$content_src" "$dir/content" || exit 2
cp -R "$content_src/server/pack" "$dir/lane/servpack"
cp -R "$content_src/server/scripts/build" "$dir/lane/scripts"
cp -R "$content_src/server/scripts/build_summoning_curses" "$dir/lane/scripts_lanes"
cp -R "$repo/cache.osrs241/" "$dir/lane/cache"
say "lane ready: $(du -sh "$dir/lane" | cut -f1)"

LIVE_E2E_REPO="$repo" LIVE_E2E_DIR="$dir/lane" LIVE_E2E_OBJ="$obj" \
TORIRSMAPED_LIVE_SERVER_BIN="$here/server.sh" TORIRSMAPED_LIVE_PLAY_BIN= \
TORIRSMAPED_LIVE_JS5_BIN="$repo/src/$obj/js5_server" \
TORIRSMAPED_LIVE_GAME_PORT="$game_port" TORIRSMAPED_LIVE_JS5_PORT="$js5_port" \
    "$repo/src/$obj/torirsmaped" "$maped_port" --content-dir "$dir/content" --repo-root "$repo" \
    --live-dir "$dir/lane" > "$dir/maped.log" 2>&1 &
maped_pid=$!
# The daemon's SIGTERM closes it, and its Close stops the launched lane.
trap 'kill $maped_pid 2>/dev/null' EXIT INT TERM
stop_early() {
    say "stopping after: $1"
    tail -40 "$2"
    $ctl live-detach > /dev/null 2>&1
    kill $maped_pid 2>/dev/null
    say "$((fails)) failed"
    exit 1
}
wait_for "$dir/maped.log" "serving" 60 || { say "torirsmaped did not come up"; cat "$dir/maped.log"; exit 1; }

$ctl live-attach launch > "$dir/attach.log" 2>&1
check "LIVE_ATTACH launch attaches to the lane's server" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/attach.log")"
pgrep -f "js5_server --cache $dir/lane/cache" > /dev/null
check "the launch started js5_server on the lane's cache" $([ $? = 0 ] && echo 1 || echo 0) ""

wait_for "$dir/lane/scriptrun.log" "live_e2e.first" 180
first=$(grep "live_e2e.first" "$dir/lane/scriptrun.log" | head -1)
check "the bot read Hans's line before the edit" $(echo "$first" | grep -q "PASS" && echo 1 || echo 0) "$first"

# (a) a dialogue line, through the authority, then RUN.
$ctl text-replace "rs2:areas/lumbridge/scripts/hans.rs2" \
    'Hello. What are you doing here?' 'Hello. What are you doing here, editor?' > "$dir/edit_a.log" 2>&1
check "TEXT_SPLICE on hans.rs2 is accepted" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_a.log")"
$ctl run 3600 > "$dir/run_a.log" 2>&1
rc=$?
check "RUN (a) finishes every step" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_a.log")"
check "RUN (a) swapped the script pack" \
    $(grep '^RUN done' "$dir/run_a.log" | grep -q 'scripts_swapped [1-9]' && echo 1 || echo 0) ""
[ $rc = 0 ] || stop_early "RUN (a)" "$dir/run_a.log"
wait_for "$dir/lane/scriptrun.log" "live_e2e.second" 180
second=$(grep "live_e2e.second" "$dir/lane/scriptrun.log" | head -1)
check "the running bot reads the new line on a later Talk-to" \
    $(echo "$second" | grep -q "PASS" && echo 1 || echo 0) "$second"
relogs=$(grep -c "LOGIN\|logged in" "$dir/lane/scriptrun.log" 2>/dev/null)
say "scriptrun login lines: $relogs"

# (b) a tile height in the square the bot stands in, then RUN.
rebuilds_before=$(grep -c "pkt	REBUILD_" "$dir/lane/wire.log")
$ctl tile-height 50 50 0 22 18 60 > "$dir/edit_b.log" 2>&1
check "TILE on m50_50 is accepted" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_b.log")"
$ctl run 3600 > "$dir/run_b.log" 2>&1
rc=$?
check "RUN (b) finishes every step" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_b.log")"
check "RUN (b) reloaded the square" \
    $(grep '^RUN done' "$dir/run_b.log" | grep -q 'squares_reloaded 1' && echo 1 || echo 0) ""
[ $rc = 0 ] || stop_early "RUN (b)" "$dir/run_b.log"
sleep 3
rebuilds_after=$(grep -c "pkt	REBUILD_" "$dir/lane/wire.log")
check "the covering bot got exactly one REBUILD" \
    $([ $((rebuilds_after - rebuilds_before)) = 1 ] && echo 1 || echo 0) \
    "$rebuilds_before -> $rebuilds_after"
grep -q "pkt	CACHE_INVALIDATE" "$dir/lane/wire.log"
check "the bot was sent CACHE_INVALIDATE for the rebuilt archives" $([ $? = 0 ] && echo 1 || echo 0) ""

login_lines() { grep -c "logged in\|LOGIN" "$dir/lane/scriptrun.log" 2>/dev/null; }
logins_before_c=$(login_lines)

# (c) a loc op: the record gains op3, a script answers it, RUN.
$ctl cfg-set loc:blueflower op3 Search > "$dir/edit_c.log" 2>&1
check "CFG_FIELD op3=Search on loc blueflower is accepted" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_c.log")"
$ctl text-replace "rs2:areas/lumbridge/scripts/hans.rs2" '[opnpc3,hans]' \
    "$(printf '[oploc3,blueflower]\nmes("editor op3");\n\n[opnpc3,hans]')" > "$dir/edit_c2.log" 2>&1
check "TEXT_SPLICE adds [oploc3,blueflower]" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_c2.log")"
started=$(date +%s)
$ctl run 3600 > "$dir/run_c.log" 2>&1
rc=$?
run_c_s=$(( $(date +%s) - started ))
say "RUN (c) took ${run_c_s}s"
check "RUN (c) finishes every step" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_c.log")"
grep -q "servpack: [0-9]* archive(s) changed in [1-9]" "$dir/run_c.log"
check "RUN (c)'s packs build named the changed server-pack groups" $([ $? = 0 ] && echo 1 || echo 0) \
    "$(grep 'servpack:' "$dir/run_c.log" | tail -1)"
grep -q "config: loc [1-9][0-9]* of [1-9][0-9]* group(s) replaced" "$dir/lane/scriptrun.log"
check "the server replaced its loc records live" $([ $? = 0 ] && echo 1 || echo 0) \
    "$(grep 'config: loc' "$dir/lane/scriptrun.log" | tail -1)"
[ $rc = 0 ] || stop_early "RUN (c)" "$dir/run_c.log"
wait_for "$dir/lane/scriptrun.log" "live_e2e.loc_op" 300
loc_row=$(grep "live_e2e.loc_op" "$dir/lane/scriptrun.log" | head -1)
check "the running bot presses the new loc op3 and reads its script" \
    $(echo "$loc_row" | grep -q "PASS" && echo 1 || echo 0) "$loc_row"

# (d) an npc op: Hans gains op4, a script answers it, RUN.
$ctl cfg-set npc:hans op4 Wave > "$dir/edit_d.log" 2>&1
check "CFG_FIELD op4=Wave on npc hans is accepted" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_d.log")"
$ctl text-replace "rs2:areas/lumbridge/scripts/hans.rs2" '[opnpc3,hans]' \
    "$(printf '[opnpc4,hans]\nmes("editor op4");\n\n[opnpc3,hans]')" > "$dir/edit_d2.log" 2>&1
check "TEXT_SPLICE adds [opnpc4,hans]" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_d2.log")"
started=$(date +%s)
$ctl run 3600 > "$dir/run_d.log" 2>&1
rc=$?
run_d_s=$(( $(date +%s) - started ))
say "RUN (d) took ${run_d_s}s"
check "RUN (d) finishes every step" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_d.log")"
grep -q "config: npc [1-9][0-9]* of [1-9][0-9]* group(s) replaced" "$dir/lane/scriptrun.log"
check "the server replaced its npc records live" $([ $? = 0 ] && echo 1 || echo 0) \
    "$(grep 'config: npc' "$dir/lane/scriptrun.log" | tail -1)"
[ $rc = 0 ] || stop_early "RUN (d)" "$dir/run_d.log"
wait_for "$dir/lane/scriptrun.log" "live_e2e.npc_op" 300
npc_row=$(grep "live_e2e.npc_op" "$dir/lane/scriptrun.log" | head -1)
check "the running bot presses the new npc op4 and reads its script" \
    $(echo "$npc_row" | grep -q "PASS" && echo 1 || echo 0) "$npc_row"
check "no login between RUN (c) and the npc op" \
    $([ "$(login_lines)" = "$logins_before_c" ] && echo 1 || echo 0) "$logins_before_c -> $(login_lines)"

$ctl live-detach > "$dir/detach.log" 2>&1
check "LIVE_DETACH stops the launched lane" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/detach.log")"

check "OSRS-Content hans.rs2 untouched" \
    $([ "$(cksum < "$content_src/server/scripts/areas/lumbridge/scripts/hans.rs2")" = "$before_hans" ] && echo 1 || echo 0) ""
check "OSRS-Content m50_50.jm2 untouched" \
    $([ "$(cksum < "$content_src/maps/m50_50.jm2")" = "$before_map" ] && echo 1 || echo 0) ""
check "OSRS-Content pack/*.alloc untouched" \
    $([ "$(cat "$content_src"/pack/*.alloc | cksum)" = "$before_alloc" ] && echo 1 || echo 0) ""
check "OSRS-Content configs/all.loc untouched" \
    $([ "$(cksum < "$content_src/configs/all.loc")" = "$before_loc" ] && echo 1 || echo 0) ""
check "OSRS-Content configs/all.npc untouched" \
    $([ "$(cksum < "$content_src/configs/all.npc")" = "$before_npc" ] && echo 1 || echo 0) ""
grep -q "^op3=Search" "$dir/content/configs/all.loc"
check "the mirror holds the saved loc record" $([ $? = 0 ] && echo 1 || echo 0) ""
grep -q "editor?" "$dir/content/server/scripts/areas/lumbridge/scripts/hans.rs2"
check "the mirror holds the saved script" $([ $? = 0 ] && echo 1 || echo 0) ""

say "$fails failed"
[ "$fails" = 0 ]
