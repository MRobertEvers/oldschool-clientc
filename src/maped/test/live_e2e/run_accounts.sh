#!/bin/sh
# Accounts against a REAL lane: the real torirsserver and js5_server on the
# lane's packs and cache, the real play client logged in as an account made
# here (docs/MAPED_STATUS.md "Accounts"; make -C src maped-live-e2e-accounts).
#
#   src/maped/test/live_e2e/run_accounts.sh
#
# 1. torirsmaped serves a writable mirror (mirror.py) with --live-dir lane/.
# 2. account-new makes `acctb` from fresh_lumbridge.ini; account-use makes it
#    the play account; an OFFLINE account-set writes attack 40 into its save.
# 3. live-attach launch starts the lane's servers; `play` logs the client in
#    as acctb (its first login claims the password) -- the server reads the
#    edited save: attack 40 is on the character.
# 4. ONLINE: account-set cooking 60 is ::setlevel; the daemon's ::checkpoint
#    lands and account-view shows the snapshot with cooking 60 and attack 40;
#    account-place is ::tele and the server's player list moves; a quest mark
#    is ::questmark and the snapshot's varp says complete.
# 5. play-stop; OFFLINE: strength 33 into the save, a quest mark QUEUED; `play`
#    again: the queue flushes after the login, the snapshot shows both.
# 6. live-detach stops everything; OSRS-Content is checked untouched.
#
# Writes only under src/<obj>/tmp/live_accounts/ (removed when it passes
# unless LIVE_E2E_KEEP=1). LIVE_E2E_OBJ names the objdir of torirsmaped and
# torirsmapedctl; LIVE_E2E_SERVER_OBJ (default build_opt) the one with
# torirsserver, js5_server and sscompile; LIVE_E2E_PLAY_BIN the client.
set -u
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
obj=${LIVE_E2E_OBJ:-build_opt}
sobj=${LIVE_E2E_SERVER_OBJ:-build_opt}
dir="$repo/src/$obj/tmp/live_accounts"
content_src="$repo/OSRS-Content/osrs-content"
maped_port=${LIVE_E2E_MAPED_PORT:-43642}
game_port=${LIVE_E2E_GAME_PORT:-43643}
js5_port=${LIVE_E2E_JS5_PORT:-43644}
play_bin=${LIVE_E2E_PLAY_BIN:-$repo/src/torirs}
user=acctb
ctl="$repo/src/$obj/torirsmapedctl --port $maped_port"
fails=0

say() { printf 'live-e2e-accounts: %s\n' "$*"; }
check() {
    if [ "$2" = 1 ]; then printf 'live-e2e-accounts: %-64s ok   %s\n' "$1" "$3"
    else printf 'live-e2e-accounts: %-64s FAILED   %s\n' "$1" "$3"; fails=$((fails + 1)); fi
}
has() { grep -qF -- "$2" "$1"; }

for bin in torirsmaped torirsmapedctl; do
    [ -x "$repo/src/$obj/$bin" ] || { say "missing src/$obj/$bin"; exit 2; }
done
for bin in torirsserver js5_server sscompile; do
    [ -x "$repo/src/$sobj/$bin" ] || { say "missing src/$sobj/$bin"; exit 2; }
done
[ -x "$play_bin" ] || { say "missing the play client $play_bin (LIVE_E2E_PLAY_BIN)"; exit 2; }

before_alloc=$(cat "$content_src"/pack/*.alloc | cksum)
rm -rf "$dir"
mkdir -p "$dir/lane"
python3 "$here/mirror.py" "$content_src" "$dir/content" || exit 2
cp -R "$content_src/server/pack" "$dir/lane/servpack"
cp -R "$content_src/server/scripts/build" "$dir/lane/scripts"
cp -R "$content_src/server/scripts/build_summoning_curses" "$dir/lane/scripts_lanes"
cp -R "$repo/cache.osrs242/" "$dir/lane/cache"
say "lane ready: $(du -sh "$dir/lane" | cut -f1)"

env TORIRSMAPED_LIVE_SERVER_BIN="$repo/src/$sobj/torirsserver" \
TORIRSMAPED_LIVE_JS5_BIN="$repo/src/$sobj/js5_server" \
TORIRSMAPED_LIVE_PLAY_BIN="$play_bin" \
TORIRSMAPED_LIVE_GAME_PORT="$game_port" TORIRSMAPED_LIVE_JS5_PORT="$js5_port" \
TORIRSMAPED_PLAY_ENV_SDL_VIDEODRIVER="${LIVE_E2E_VIDEO:-dummy}" \
TORIRSMAPED_PLAY_ENV_TORIRS_PLUGINS=0 \
    "$repo/src/$obj/torirsmaped" "$maped_port" --content-dir "$dir/content" --repo-root "$repo" \
    --live-dir "$dir/lane" > "$dir/maped.log" 2>&1 &
maped_pid=$!
trap 'kill $maped_pid 2>/dev/null' EXIT INT TERM
n=0
until grep -q "serving" "$dir/maped.log" 2>/dev/null; do
    n=$((n + 1)); [ $n -gt 60 ] && { say "torirsmaped did not come up"; tail -20 "$dir/maped.log"; exit 1; }
    sleep 1
done

# 2. The account, offline.
$ctl account-new $user Secret123x "$repo/test/quests/fixtures/fresh_lumbridge.ini" > "$dir/new.log" 2>&1
check "account-new makes $user from the fixture" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/new.log")"
$ctl account-use $user > "$dir/use.log" 2>&1
check "account-use makes it the play account" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/use.log")"
$ctl account-set $user stats.attack 40 > "$dir/set_offline.log" 2>&1
check "offline: stats.attack 40 writes the save" $(has "$dir/set_offline.log" "offline: wrote saves/$user.ini" && echo 1 || echo 0) "$(tail -1 "$dir/set_offline.log")"
check "the save holds attack 40 (xp 37224)" $(has "$dir/lane/saves/$user.ini" "0 = 40 372240" && echo 1 || echo 0) ""

# 3. The lane and the client. The main torirsserver first answers the
# control link when its world is built, at the first login: the launch's
# attach is only complete once `play` has logged the client in, so the verb's
# own wait is not awaited here (it would time out without a player).
$ctl live-attach launch > "$dir/attach.log" 2>&1 &
attach_pid=$!
n=0
until [ -f "$dir/lane/logs/server.log" ] || [ $n -gt 30 ]; do n=$((n + 1)); sleep 1; done
check "live-attach launch starts the lane's server" $([ -f "$dir/lane/logs/server.log" ] && echo 1 || echo 0) "$(head -1 "$dir/lane/logs/server.log" 2>/dev/null)"
started=$(date +%s)
$ctl play 300 > "$dir/play.log" 2>&1
rc=$?
say "the play client logged in $(( $(date +%s) - started ))s after launch"
check "play logs the client in as $user" $([ $rc = 0 ] && has "$dir/play.log" "$user" && echo 1 || echo 0) "$(grep '^play: state' "$dir/play.log" | tail -1)"
wait $attach_pid 2>/dev/null
check "the launch's attach completed with the login" $(has "$dir/attach.log" "attached" && echo 1 || echo 0) "$(grep -i 'attached' "$dir/attach.log" | tail -1)"
[ $rc = 0 ] || { tail -20 "$dir/lane/logs/play.log"; tail -20 "$dir/lane/logs/server.log"; }
sleep 3
$ctl accounts > "$dir/list_online.log" 2>&1
check "accounts lists $user online" $(grep -q "^  $user *online" "$dir/list_online.log" && echo 1 || echo 0) "$(grep "^  $user" "$dir/list_online.log")"

# 4. Online edits.
$ctl account-set $user stats.cooking 60 > "$dir/set_online.log" 2>&1
check "online: stats.cooking 60 is ::setlevel" $(has "$dir/set_online.log" "online: ::setlevel cooking 60" && echo 1 || echo 0) "$(tail -1 "$dir/set_online.log")"
sleep 3
$ctl account-view $user > "$dir/view_online.log" 2>&1
check "the view is the server's checkpoint (snapshot)" $(has "$dir/view_online.log" " snapshot" && echo 1 || echo 0) "$(sed -n 1,2p "$dir/view_online.log" | tr '\n' ' ')"
check "cooking is 60 on the server" $(has "$dir/view_online.log" "cooking=60/" && echo 1 || echo 0) "$(grep -o 'cooking=[0-9/]*' "$dir/view_online.log")"
check "attack is 40 (the offline edit reached the character at login)" $(has "$dir/view_online.log" "attack=40/" && echo 1 || echo 0) "$(grep -o 'attack=[0-9/]*' "$dir/view_online.log")"
$ctl account-place $user 3222 3218 0 > "$dir/place.log" 2>&1
check "account-place online is ::tele" $(has "$dir/place.log" "online: ::tele 0_50_50_22_18" && echo 1 || echo 0) "$(tail -1 "$dir/place.log")"
sleep 4
$ctl accounts > "$dir/list_moved.log" 2>&1
check "the server lists $user at 3222,3218" $(grep -q "^  $user *online *3222,3218,0" "$dir/list_moved.log" && echo 1 || echo 0) "$(grep "^  $user" "$dir/list_moved.log")"
$ctl account-set $user quests.quest_cooksassistant mark > "$dir/quest_online.log" 2>&1
check "online: a quest mark is ::questmark" $(has "$dir/quest_online.log" "online: ::questmark quest_cooksassistant" && echo 1 || echo 0) "$(tail -1 "$dir/quest_online.log")"
sleep 3
$ctl account-view $user > "$dir/view_quest.log" 2>&1
check "the snapshot says Cook's Assistant is complete" $(has "$dir/view_quest.log" "quest complete: quest_cooksassistant" && echo 1 || echo 0) "$(grep 'quests:' "$dir/view_quest.log")"

# 5. Logged out: an offline edit and a queued cheat, then the next login.
$ctl play-stop > "$dir/stop.log" 2>&1
sleep 4
$ctl accounts > "$dir/list_offline.log" 2>&1
check "after play-stop $user is offline" $(grep -q "^  $user *offline" "$dir/list_offline.log" && echo 1 || echo 0) "$(grep "^  $user" "$dir/list_offline.log")"
$ctl account-set $user stats.strength 33 > "$dir/set_offline2.log" 2>&1
check "offline again: strength 33 writes the save" $(has "$dir/set_offline2.log" "offline: wrote" && echo 1 || echo 0) "$(tail -1 "$dir/set_offline2.log")"
$ctl account-set $user quests.quest_sheepshearer mark > "$dir/quest_queue.log" 2>&1
check "a quest mark while offline is queued" $(has "$dir/quest_queue.log" "queued for $user's next login" && echo 1 || echo 0) "$(tail -1 "$dir/quest_queue.log")"
$ctl play 300 > "$dir/play2.log" 2>&1
check "play logs the client in again" $([ $? = 0 ] && echo 1 || echo 0) "$(grep '^play: state' "$dir/play2.log" | tail -1)"
sleep 8
$ctl account-set $user refresh > "$dir/refresh.log" 2>&1
sleep 3
$ctl account-view $user > "$dir/view_final.log" 2>&1
check "strength 33 reached the character" $(has "$dir/view_final.log" "strength=33/" && echo 1 || echo 0) "$(grep -o 'strength=[0-9/]*' "$dir/view_final.log")"
check "the queued questmark ran: Sheep Shearer complete" $(has "$dir/view_final.log" "quest complete: quest_sheepshearer" && echo 1 || echo 0) "$(grep 'queued' "$dir/view_final.log" | head -1)"
check "the queue is empty" $(has "$dir/view_final.log" "queued (0):" && echo 1 || echo 0) ""
grep -c "logged in (pid" "$dir/lane/logs/server.log" > "$dir/logins.txt" 2>/dev/null
check "two logins on the server" $([ "$(cat "$dir/logins.txt")" = 2 ] && echo 1 || echo 0) "logins $(cat "$dir/logins.txt")"

$ctl live-detach > "$dir/detach.log" 2>&1
check "live-detach stops the lane" $([ $? = 0 ] && echo 1 || echo 0) "$(tail -1 "$dir/detach.log")"
check "OSRS-Content pack/*.alloc untouched" $([ "$(cat "$content_src"/pack/*.alloc | cksum)" = "$before_alloc" ] && echo 1 || echo 0) ""
kill $maped_pid 2>/dev/null
wait $maped_pid 2>/dev/null
say "$fails failed"
if [ "${LIVE_E2E_KEEP:-0}" != 1 ] && [ "$fails" = 0 ]; then rm -rf "$dir"; fi
[ "$fails" = 0 ]
