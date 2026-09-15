#!/bin/sh
# Reproduce "calling a familiar drags another entity's model around".
#
# Starts OUTSIDE the QBD lair (the manifest logs in at Lumbridge), summons a
# familiar, then calls it. Calling is the interesting half: it despawns the
# familiar npc and respawns it beside the player, which frees a scene element
# and hands the id straight back out. If any other entity is still holding that
# id, the two fight over one element -- same model, same animation, same
# position -- and the symptom is the other creature wearing the familiar's
# movement.
#
# Three detectors run together, because the failure shows up in a different one
# depending on which consumer of the element id loses the race:
#
#   TORIRS_ELEMENT_ALIAS_CHECK   two live world entities on one element
#   world_builder: element ... claimed by  the rebuild repairing such a pair
#   TORIRS_NPC_TRACE             slot -> world -> element for the traced npcs
#   TORIRS_DRAW_TRACE            per-frame cull/sort for any model >= N verts
#
# Usage:  tools/qbd_familiar_alias_harness.sh [frames] [familiar_type]
# Exits non-zero if any detector fires, so it can gate a change.
set -eu

cd "$(dirname "$0")/.."

FRAMES="${1:-2600}"
FAMILIAR="${2:-3}"   # 3 = spirit terrorbird; see summoning.constant
OUT="${QBD_HARNESS_OUT:-/tmp/qbd_familiar_alias}"

rm -rf "$OUT"
mkdir -p "$OUT/saves"

[ -x src/torirs ] || make -C src EMBED_SERVER=1 torirs

# TORIRS_NET_CHEAT REPLACES the manifest's own `cheat=` line rather than adding
# to it, so the provisioning that line normally does has to be repeated here --
# without `summoning_unlock` every summoning debugproc returns immediately on
# `~summoning_account_enabled = false` and the run looks like a clean pass while
# nothing was ever summoned. One cheat fires per interval, in order.
#
# The second call is deliberate: it re-runs the despawn/respawn against an
# element set that is no longer pristine, which is when a stale claim is most
# likely to be exposed.
# Order matters and is the whole point: get a familiar FIRST, outside the lair,
# then enter the arena, and only then call. Calling despawns and respawns the
# familiar npc, freeing a scene element and handing the id straight back out --
# and the Queen is the other big dynamic element in that scene, so if anything
# is holding a stale id this is where the two collide.
CHEATS="summoning_unlock;setlevel summoning 99;summoning_summon $FAMILIAR;rs2012qbdmanifest;summoning_call;summoning_call"
SDL_VIDEODRIVER=dummy \
TORIRSSERVER_SAVES="$OUT/saves" \
TORIRS_ELEMENT_ALIAS_CHECK=1 \
TORIRS_DRAW_TRACE=5000 \
TORIRS_NPC_TRACE=25000,25001,25002,25003 \
TORIRSSERVER_NPC_TRACE=25000,25001,25002,25003 \
TORIRS_NET_CHEAT="$CHEATS" \
TORIRS_NET_CHEAT_ROTATE=1 \
TORIRS_NET_CHEAT_EVERY=500 \
TORIRS_MAX_FRAMES="$FRAMES" \
  ./src/torirs --manifest manifests/manifest_osrs239_torirs.ini --user testc --pass test --soft3d \
  > "$OUT/run.log" 2>&1 || true

echo "log: $OUT/run.log"
grep -c "summoning" "$OUT/run.log" >/dev/null 2>&1 || true
echo "--- familiar lifecycle ---"
grep -iE "familiar|summon" "$OUT/run.log" | head -8 || true

fail=0
for pat in "element_alias" "claimed by entities" "referenced dead element"; do
    n=$(grep -c "$pat" "$OUT/run.log" || true)
    printf '%-28s %s\n' "$pat" "$n"
    [ "$n" = "0" ] || fail=1
done

echo "--- draw outcome for the big model ---"
grep -o "cull=[0-9-]*" "$OUT/run.log" | sort | uniq -c || true
if grep -q "sorted=0$" "$OUT/run.log"; then
    echo "SORTED ZERO: model projected but rasterized nothing (clickable, invisible)"
    fail=1
fi

[ "$fail" = "0" ] && echo "PASS: no element aliasing detected" || echo "FAIL: see $OUT/run.log"
exit "$fail"
