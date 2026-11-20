#!/bin/bash
# rebuild_park_probe.sh <embedded-server client> <out dir> [seconds]
#
# A parallel task parked on a LIVE background rebuild, shown by a run rather
# than a test. The frame contract (app.h) says a parallel task that reaches
# its apply while its view is being rebuilt parks on the view's built trigger
# (PT_APP_AWAIT_VIEW_REBUILT), is woken by the commit, re-checks and applies
# to the NEW scene or exits because its target is gone -- and never applies
# to the scene being replaced. Every live run before this one printed
# "0 parked on it" at each fire: packets behind a REBUILD are held, so only a
# task queued before it and still loading can arrive during the gather, and
# on a local disk none is still loading.
#
# So this run makes every read slow and keeps something loading:
#   - TORIRS_IO_DEFER_MS (default 800) from TORIRS_IO_DEFER_AFTER_MS
#     (default 45000, after the login and its bake): every cache read
#     answered >= that late, so a dropped obj's model read outlasts the tick
#     to the REBUILD and a rebuild's gather takes several;
#   - TORIRS_WORLD_REBUILD=background: the walk's REBUILDs are background
#     jobs, the trigger primed by the job and fired at its commit;
#   - a scripted walk (minimap clicks from Lumbridge) with a NEW obj dropped
#     under the player every other server tick (~dropobj, staff level 2), so
#     a model is in flight in the tick before a REBUILD.
# The client is the capped native loop with its in-process server (an
# EMBED_SERVER=1 binary), not a content test (whose rebuilds are blocking),
# headless (SDL dummy drivers, --soft3d), strict and traced
# (TORIRS_QUEUE_CONTRACT_STRICT=1, _TRACE=1).
#
# STATUS (lo-checks, 2026-11-19): no run has shown a park yet. Five runs of
# 240-600 s gave one background REBUILD (park10: primed by the job, gather
# 2.4 s at 800 ms a read, fired at its commit, 0 parked on it); every other
# walk REBUILD took the blocking path because its tick was already open with
# something besides varps (a drop's zone packet, a held script:
# App_WorldJobWanted), and the slow reads slow the walk itself (frames held
# while a packet waits on a required read), so few boundaries are crossed.
# A park needs, in one run: a drop in the tick BEFORE a REBUILD whose own
# tick holds varps only. Lengthen the run, or drop less often, to raise the
# odds; a deterministic version needs a server-side way to order a drop one
# tick ahead of the crossing, which this probe does not have.
#
# Run it from the main checkout (caches, OSRS-Content, packs). It writes
# <out dir>/client.log and <out dir>/verdict.txt and exits 0 when at least one
# task parked on a background rebuild, every such task woke at a fire (the
# commit) holding a newer scene than the one it was made in, and no queue
# contract breach was printed; 1 otherwise; 2 on a usage error.
set -u
if [ $# -lt 2 ]; then
    echo "usage: $0 <embedded-server client> <out dir> [seconds]" >&2
    exit 2
fi
BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT=$2
SECS=${3:-240}
REPO=$(pwd)
if [ ! -x "$BIN" ] || [ ! -f "$REPO/test/quests/fixtures/fresh_lumbridge.ini" ]; then
    echo "rebuild_park_probe: run from the repository root with an executable client" >&2
    exit 2
fi
DEFER=${TORIRS_IO_DEFER_MS:-800}
# Boot and log in at disk speed: a gameframe bake at 800 ms a read outlasts
# the session. The walk starts after this.
DEFER_AFTER=${TORIRS_IO_DEFER_AFTER_MS:-45000}
USER_NAME=parkprobe$$
rm -rf "$OUT"
mkdir -p "$OUT/saves"
sed -e "s/^name = .*/name = $USER_NAME/" "$REPO/test/quests/fixtures/fresh_lumbridge.ini" > "$OUT/saves/$USER_NAME.ini"

# The boot manifest: the pristine rev-242 cache, the in-process server.
cat > "$OUT/boot.ini" <<EOF
[cache:boot]
epoch=dat2
game=oldschool
revision=242
quirks=none
dir=$REPO/cache.osrs242
spawn=50,50
[net:boot]
rev=osrs242
transport=embed
host=localhost
port=43597
client_version=242
rsa_exp=10001
rsa_mod=c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d690f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19
[ui:boot]
logic=cs2
chrome=revconfig
interface_id=161
revconfig_ui=$REPO/revconfig/osrs242/osrs242_ui.ini
revconfig_cache=$REPO/revconfig/osrs242/osrs242_dat2_cache.ini
hidpi=0
[chrome]
[revconfig:component:gameframe]
type=rs_iface
[revconfig:component:overlay]
type=debug_overlay
[revconfig:component:entity_overlay]
type=entity_overlay
[revconfig:component:cross]
type=cross
w=16
h=16
[revconfig:component:hovertext]
type=hovertext
font=496
[revconfig:component:minimenu]
type=minimenu
font=496
[revconfig:layout:root]
c=gameframe
=
c=overlay
=
c=entity_overlay
=
c=cross
=
c=hovertext
=
c=minimenu
[features:boot]
world_rebuild=background
EOF

# The walk: a minimap click to the north-east, and a NEW
# obj dropped every 60 frames (two server ticks) from frame 400: a model
# that is not resident yet, so it loads through the deferred reads. Every
# other tick only: a drop in the REBUILD's own tick opens that tick with a
# zone packet, and such a REBUILD takes the blocking path by design
# (App_WorldJobWanted); a drop in the tick before it is the one that is
# still loading when it lands.
OBJS=""
for metal in bronze iron steel black mithril adamant rune; do
    for kind in dagger sword longsword scimitar mace axe battleaxe warhammer 2h_sword spear \
                pickaxe med_helm full_helm sq_shield kiteshield chainbody platebody platelegs; do
        OBJS="$OBJS ${metal}_$kind"
    done
done
# No teleport: a teleport's REBUILD is blocking by design, and with slow
# reads its scene load outlasts the session (the client logs out). The
# fixture save already stands at the home tile.
CMDS="150,~dropobj bronze_arrow 1"
frame=400
for obj in $OBJS; do
    [ "${PROBE_DROPS:-1}" = 1 ] && CMDS="$CMDS;$frame,~dropobj $obj 1"
    frame=$((frame + 60))
done
# A drop stops the walk (the cheat runs as an action), so the click that
# walks on follows each drop by 5 frames.
CLICKS="50,382,336;300,684,30"
MOVES="290,684,30"
for frame in $(seq 405 60 $((frame - 55))); do
    CLICKS="$CLICKS;$frame,684,30"
    MOVES="$MOVES;$((frame - 3)),684,30"
done

echo "rebuild_park_probe: bin=$BIN defer=${DEFER}ms after=${DEFER_AFTER}ms seconds=$SECS user=$USER_NAME" > "$OUT/run.txt"
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TORIRS_STDERR_UNBUFFERED=1 TORIRS_PLUGINS=0 \
    TORIRS_PREFS="$OUT/prefs.ini" TORIRS_EDITOR_LAYOUT="$OUT/layout.ini" \
    TORIRSSERVER_SAVES="$OUT/saves" TORIRSSERVER_HOME=3222,3218 TORIRSSERVER_STAFF_LEVEL=2 \
    TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
    TORIRS_QUEUE_CONTRACT_STRICT=1 TORIRS_QUEUE_CONTRACT_TRACE=1 \
    TORIRS_WORLD_REBUILD=background TORIRS_REBUILD_TIMING=1 TORIRS_IO_DEFER_MS="$DEFER" TORIRS_IO_DEFER_AFTER_MS="$DEFER_AFTER" \
    TORIRS_SIM_AFTER_READY=1 TORIRS_SIM_CMD="$CMDS" TORIRS_SIM_CLICK_AT="$CLICKS" TORIRS_SIM_MOVE_AT="$MOVES" \
    perl -e '$s=shift; $p=fork; if(!$p){exec @ARGV} $t=time;
             while(time-$t<$s){ if(waitpid($p,1)>0){exit($?>>8)} select(undef,undef,undef,0.2)}
             kill "INT",$p; $t=time;
             while(time-$t<15){ if(waitpid($p,1)>0){exit(0)} select(undef,undef,undef,0.2)}
             kill "KILL",$p; waitpid($p,0); exit 137' \
    "$SECS" "$BIN" --manifest "$OUT/boot.ini" --user "$USER_NAME" --pass test --soft3d \
    > "$OUT/client.log" 2>&1
echo "client exit $?" >> "$OUT/run.txt"

python3 - "$OUT/client.log" > "$OUT/verdict.txt" <<'PY'
import re, sys
lines = open(sys.argv[1], errors="replace").read().splitlines()
breach = [l for l in lines if l.startswith("queue_contract:") and not re.match(
    r"queue_contract: (trace|parked|loop)", l)]
jobs = [l for l in lines if "trace rebuild view" in l and "priming" in l and "background" in l]
fires = [l for l in lines if "trace rebuild view" in l and "built, firing" in l]
parked_fires = [l for l in fires if not re.search(r", 0 parked on it", l)]
parks = [l for l in lines if "parks on its scene's rebuild" in l]
wakes = [l for l in lines if "woke from its scene's rebuild" in l]
bad = []
for w in wakes:
    m = re.search(r"made in scene (\d+), the view holds scene (\d+)", w)
    if "(fired: the commit)" not in w or not m or int(m.group(2)) <= int(m.group(1)) or "still loading" in w:
        bad.append(w)
print("background rebuilds primed: %d" % len(jobs))
print("fires: %d, with a task parked: %d" % (len(fires), len(parked_fires)))
print("parks: %d, wakes: %d, wakes not at a commit onto a newer scene: %d" % (len(parks), len(wakes), len(bad)))
print("queue_contract breaches: %d" % len(breach))
for l in breach[:5]:
    print("  " + l)
print("--- the parked rebuilds, in order ---")
keep = set()
for i, l in enumerate(lines):
    if ("trace rebuild view" in l and ("priming" in l or "firing" in l or "parks on" in l or "woke from" in l)) \
            or "world_job" in l and ("start" in l or "commit" in l):
        keep.add(i)
shown = 0
for i in sorted(keep):
    l = lines[i]
    if "0 parked on it" in l and "firing" in l:
        continue
    print(l)
    shown += 1
    if shown > 80:
        break
ok = len(parked_fires) > 0 and len(wakes) > 0 and not bad and not breach
print("VERDICT %s" % ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
rc=$?
cat "$OUT/verdict.txt" | head -12
exit $rc
