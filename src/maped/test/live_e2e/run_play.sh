#!/bin/sh
# The live lane with a REAL game server and the REAL play client, end to end
# (docs/MAPED_STATUS.md "The live lane end to end", the play client).
#
#   src/maped/test/live_e2e/run_play.sh      (make -C src maped-live-e2e-play)
#
# run.sh's lane plays a scriptrun bot, which refuses a real client; this one
# is the lane as the Live panel launches it for a person: the real
# torirsserver and js5_server on the lane's packs and cache, and the play
# client (src/torirs, LIVE_E2E_PLAY_BIN) logged in through the lane's JS5 as
# the lane's dev account, with the fresh_lumbridge save so it does not start
# on Tutorial Island. Everything is written under src/<obj>/tmp/live_play/;
# OSRS-Content is only read.
#
# 1. torirsmaped serves a writable mirror (mirror.py) with --live-dir lane/.
# 2. tile-height spike A, `run` with the lane DOWN: the RUN saves, builds both
#    packs and the client cache, finds no server, launches the lane on what it
#    just built and starts the play client; `play` waits until the server lists
#    the account. The main torirsserver builds its world (and first answers the
#    control link) at that login, so the link attaches then.
# 3. tile-height spike B, `run`: attached, the server reloads the square, the
#    still-logged-in client takes CACHE_INVALIDATE and the REBUILD -- "reloaded
#    in the running client", and the server's login count stays at 1.
# 4. the play client is killed; `run`: "restarted the play client (it had
#    exited)"; it logs in again (login count 2).
# 5. live-detach stops the server, js5_server and the play client; the tree is
#    checked untouched.
#
# LIVE_E2E_SHOTS=<dir> gives the play client a film strip there
# (TORIRS_BMP_SERIES, every LIVE_E2E_SHOT_STEP frames) for photographs;
# LIVE_E2E_KEEP=1 leaves tmp/live_play/ behind (it is removed otherwise).
set -u
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
obj=${LIVE_E2E_OBJ:-build_opt}
dir="$repo/src/$obj/tmp/live_play"
content_src="$repo/OSRS-Content/osrs-content"
maped_port=${LIVE_E2E_MAPED_PORT:-43632}
game_port=${LIVE_E2E_GAME_PORT:-43633}
js5_port=${LIVE_E2E_JS5_PORT:-43634}
play_bin=${LIVE_E2E_PLAY_BIN:-$repo/src/torirs}
user="maped$game_port"
ctl="$repo/src/$obj/torirsmapedctl --port $maped_port"
fails=0

say() { printf 'live-e2e-play: %s\n' "$*"; }
check() {
    # check <name> <0|1 pass> <detail>
    if [ "$2" = 1 ]; then printf 'live-e2e-play: %-60s ok   %s\n' "$1" "$3"
    else printf 'live-e2e-play: %-60s FAILED   %s\n' "$1" "$3"; fails=$((fails + 1)); fi
}
logins() { grep -c " logged in (pid" "$dir/lane/logs/server.log" 2>/dev/null; }
play_pid() { sed -n 's/^play: state .* pid \([0-9-]*\) exit .*/\1/p' "$1" | tail -1; }

for bin in torirsmaped torirsmapedctl torirsserver js5_server sscompile; do
    [ -x "$repo/src/$obj/$bin" ] || { say "missing src/$obj/$bin (make -C src torirsmaped torirsmapedctl torirsserver js5-server sscompile)"; exit 2; }
done
[ -x "$play_bin" ] || { say "missing the play client $play_bin (LIVE_E2E_PLAY_BIN)"; exit 2; }

before_map=$(cksum < "$content_src/maps/m50_50.jm2")
before_alloc=$(cat "$content_src"/pack/*.alloc | cksum)

rm -rf "$dir"
mkdir -p "$dir/lane"
python3 "$here/mirror.py" "$content_src" "$dir/content" || exit 2
# A hook over the mirror (LIVE_E2E_MIRROR_HOOK=<command>, run with the mirror's
# path): the mirror's files follow OSRS-Content, so another session's
# half-written record can fail the packs build; the hook can replace that one
# file in the mirror (never in OSRS-Content) for a run.
if [ -n "${LIVE_E2E_MIRROR_HOOK:-}" ]; then
    $LIVE_E2E_MIRROR_HOOK "$dir/content" || { say "the mirror hook failed"; exit 2; }
fi
cp -R "$content_src/server/pack" "$dir/lane/servpack"
cp -R "$content_src/server/scripts/build" "$dir/lane/scripts"
cp -R "$content_src/server/scripts/build_summoning_curses" "$dir/lane/scripts_lanes"
cp -R "$repo/cache.osrs242/" "$dir/lane/cache"
say "lane ready: $(du -sh "$dir/lane" | cut -f1)"

shots_env=""
if [ -n "${LIVE_E2E_SHOTS:-}" ]; then
    mkdir -p "$LIVE_E2E_SHOTS"
    shots_env="TORIRSMAPED_PLAY_ENV_TORIRS_BMP_SERIES=$LIVE_E2E_SHOTS,${LIVE_E2E_SHOT_STEP:-500},${LIVE_E2E_SHOT_STEP:-500},${LIVE_E2E_SHOT_COUNT:-400}"
fi
# The play client headless (no window, no plugins) with the invalidation
# trace on, so its log shows each RUN arriving.
env $shots_env \
TORIRSMAPED_LIVE_SERVER_BIN="$repo/src/$obj/torirsserver" \
TORIRSMAPED_LIVE_JS5_BIN="$repo/src/$obj/js5_server" \
TORIRSMAPED_LIVE_PLAY_BIN="$play_bin" \
TORIRSMAPED_LIVE_GAME_PORT="$game_port" TORIRSMAPED_LIVE_JS5_PORT="$js5_port" \
TORIRSMAPED_PLAY_ENV_SDL_VIDEODRIVER="${LIVE_E2E_VIDEO:-dummy}" \
TORIRSMAPED_PLAY_ENV_TORIRS_PLUGINS=0 \
TORIRSMAPED_PLAY_ENV_TORIRS_CACHE_INVALIDATE_DEBUG=1 \
    "$repo/src/$obj/torirsmaped" "$maped_port" --content-dir "$dir/content" --repo-root "$repo" \
    --live-dir "$dir/lane" > "$dir/maped.log" 2>&1 &
maped_pid=$!
# The daemon's SIGTERM closes it; its Close stops the lane and the play client.
trap 'kill $maped_pid 2>/dev/null' EXIT INT TERM
n=0
until grep -q "serving" "$dir/maped.log" 2>/dev/null; do
    n=$((n + 1)); [ $n -gt 60 ] && { say "torirsmaped did not come up"; tail -20 "$dir/maped.log"; exit 1; }
    sleep 1
done

# (A) a spike with the lane down: RUN launches it and starts the play client.
$ctl tile-height 50 50 0 22 23 120 > "$dir/edit_a.log" 2>&1
check "TILE A on m50_50 is accepted" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_a.log")"
started=$(date +%s)
$ctl run 3600 > "$dir/run_a.log" 2>&1
rc=$?
say "RUN (A) took $(( $(date +%s) - started ))s"
check "RUN (A) with the lane down finishes" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_a.log")"
grep '^RUN done' "$dir/run_a.log" | grep -q "launched it on the new packs and cache; play client started as $user"
check "RUN (A) launched the lane and started the play client" $([ $? = 0 ] && echo 1 || echo 0) ""
started=$(date +%s)
$ctl play 300 > "$dir/play_a.log" 2>&1
rc=$?
say "the play client logged in $(( $(date +%s) - started ))s after RUN (A)"
check "the play client logs into the lane" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^play: state' "$dir/play_a.log" | tail -1)"
[ $rc = 0 ] || { tail -30 "$dir/lane/logs/play.log"; tail -20 "$dir/lane/logs/server.log"; }
pid_a=$(play_pid "$dir/play_a.log")
check "one login on the server" $([ "$(logins)" = 1 ] && echo 1 || echo 0) "logins $(logins)"
$ctl live-cheat "$user" "tele 0_50_50_22_18" > "$dir/tele.log" 2>&1
# Let the client's JS5 background fill of its empty cache finish (about a
# minute here): a CACHE_INVALIDATE that lands while it runs drops the
# client's game session (docs/MAPED_STATUS.md open issues), and RUN (B)
# would restart the client instead of reloading it in place.
sleep "${LIVE_E2E_SETTLE:-75}"

# (B) another spike: the attached server reloads; the client takes it live.
inval_before=$(grep -c "cache invalidate" "$dir/lane/logs/play.log" 2>/dev/null)
$ctl tile-height 50 50 0 25 23 120 > "$dir/edit_b.log" 2>&1
check "TILE B on m50_50 is accepted" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/edit_b.log")"
started=$(date +%s)
$ctl run 3600 > "$dir/run_b.log" 2>&1
rc=$?
say "RUN (B) took $(( $(date +%s) - started ))s"
check "RUN (B) finishes every step" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_b.log")"
grep '^RUN done' "$dir/run_b.log" | grep -q "squares_reloaded 1"
check "RUN (B) reloaded the square" $([ $? = 0 ] && echo 1 || echo 0) ""
grep '^RUN done' "$dir/run_b.log" | grep -q "reloaded in the running client ($user at"
check "RUN (B) reads \"reloaded in the running client\"" $([ $? = 0 ] && echo 1 || echo 0) ""
sleep 3
inval_after=$(grep -c "cache invalidate" "$dir/lane/logs/play.log" 2>/dev/null)
check "the play client took CACHE_INVALIDATE live" $([ "$inval_after" -gt "$inval_before" ] && echo 1 || echo 0) \
    "$inval_before -> $inval_after trace line(s)"
grep -q "cache invalidate: index 5, 1 archive(s): 1 dropped" "$dir/lane/logs/play.log"
check "it dropped its copy of the square" $([ $? = 0 ] && echo 1 || echo 0) \
    "$(grep 'cache invalidate: index 5' "$dir/lane/logs/play.log" | tail -1)"
grep -q "cache invalidate: js5 fetched 5/" "$dir/lane/logs/play.log"
check "and fetched the rewritten square over JS5" $([ $? = 0 ] && echo 1 || echo 0) \
    "$(grep 'js5 fetched 5/' "$dir/lane/logs/play.log" | tail -1)"
check "still one login after RUN (B): no relog" $([ "$(logins)" = 1 ] && echo 1 || echo 0) "logins $(logins)"
kill -0 "$pid_a" 2>/dev/null
check "the same play client process (pid $pid_a) is alive" $([ $? = 0 ] && echo 1 || echo 0) ""
sleep 5

# (C) the client dies: the next RUN starts it again.
kill -9 "$pid_a" 2>/dev/null
sleep 2
$ctl run 3600 > "$dir/run_c.log" 2>&1
rc=$?
check "RUN (C) after the client died finishes" $([ $rc = 0 ] && echo 1 || echo 0) "$(grep '^RUN done' "$dir/run_c.log")"
grep '^RUN done' "$dir/run_c.log" | grep -q "restarted the play client (it had exited"
check "RUN (C) restarted the play client" $([ $? = 0 ] && echo 1 || echo 0) ""
$ctl play 300 > "$dir/play_c.log" 2>&1
check "the restarted client logs in again" $([ $? = 0 ] && echo 1 || echo 0) "$(grep '^play: state' "$dir/play_c.log" | tail -1)"
check "a second login on the server" $([ "$(logins)" = 2 ] && echo 1 || echo 0) "logins $(logins)"
pid_c=$(play_pid "$dir/play_c.log")

$ctl live-detach > "$dir/detach.log" 2>&1
check "LIVE_DETACH stops the launched lane" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/detach.log")"
sleep 1
kill -0 "$pid_c" 2>/dev/null
check "the play client stopped with the lane" $([ $? = 0 ] && echo 0 || echo 1) "pid $pid_c"
pgrep -f "js5_server --cache $dir/lane/cache" > /dev/null
check "js5_server stopped with the lane" $([ $? = 0 ] && echo 0 || echo 1) ""

check "OSRS-Content m50_50.jm2 untouched" \
    $([ "$(cksum < "$content_src/maps/m50_50.jm2")" = "$before_map" ] && echo 1 || echo 0) ""
check "OSRS-Content pack/*.alloc untouched" \
    $([ "$(cat "$content_src"/pack/*.alloc | cksum)" = "$before_alloc" ] && echo 1 || echo 0) ""

kill $maped_pid 2>/dev/null
wait $maped_pid 2>/dev/null
say "$fails failed"
if [ "${LIVE_E2E_KEEP:-0}" != 1 ] && [ "$fails" = 0 ]; then rm -rf "$dir"; fi
[ "$fails" = 0 ]
