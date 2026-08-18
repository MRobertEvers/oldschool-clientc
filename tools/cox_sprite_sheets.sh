#!/bin/sh
# Capture every Chambers of Xeric combat encounter headlessly and compose one
# sprite sheet per encounter.
#
# This is the visual half of the CoX loop. `tools/cox_sim.sh` proves the timings
# against the strategy guide; this shows what the encounter actually looks like
# while those timings run, which is the half no assertion covers -- an npc can
# be on a perfect 3-tick clock and be invisible, mis-animated, or standing
# inside a wall.
#
# How it works: the real 239 embed client under `SDL_VIDEODRIVER=dummy`, driven
# by `TORIRS_NET_CHEAT` (which sends `::` commands over the wire exactly as a
# player would type them), writing a numbered BMP series that
# tools/cox_sprite_sheets.py crops and lays out.
#
# Usage:
#   tools/cox_sprite_sheets.sh            # every encounter
#   tools/cox_sprite_sheets.sh olm        # just one
#
# Output: build/cox_sprites/<encounter>.png, plus all.png

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

OUT=build/cox_sprites
WORK=build/cox_sprites/frames
# The EMBEDDED manifest, not the TCP one. `manifest_osrs239_net.ini` expects a
# separately-launched mock230 on port 43596 and fails with "Connection refused"
# -- and the client then renders a perfectly good empty world, so the sheets come
# out looking like a raid with no monsters in it rather than like an error.
# `transport=embed` runs the server in-process, which is what makes this one
# command instead of two.
MANIFEST=${COX_MANIFEST:-manifest_osrs239.ini}

# Frames per encounter, and how far apart. A tick is ~15 render frames under the
# dummy driver, so 30 apart is one capture every two ticks -- enough to catch an
# attack animation without shooting the same pose twice.
START=${COX_SHOT_START:-420}
STEP=${COX_SHOT_STEP:-30}
COUNT=${COX_SHOT_COUNT:-8}
MAXFRAMES=$((START + STEP * COUNT + 60))

mkdir -p "$OUT"
[ -x src/torirs ] || make -C src torirs >/dev/null

# Each entry is `name|cheats`. The cheats are sent in order, so every one of
# them enters the raid first and then spawns its own encounter underfoot.
# `god 1` is not optional: several of these kill a fresh character in two ticks,
# and a dead player's camera is a death screen rather than an encounter.
ENCOUNTERS="
tekton|god 1;cox;coxtekton
olm|god 1;cox;coxolm
vasa|god 1;cox;coxvasa
vanguards|god 1;cox;coxvanguards
muttadiles|god 1;cox;coxmutta
shamans|god 1;cox;coxshamans
mystics|god 1;cox;coxmystics
guardians|god 1;cox;coxroom 1
vespula|god 1;cox;coxroom 2
icedemon|god 1;cox;coxroom 3
crabs|god 1;cox;coxroom 5
"

want="$1"
sheets=""

# `while read`, NOT `for entry in $ENCOUNTERS`. Word splitting breaks on the
# space in "god 1": the list splits into `tekton|god` and `1;cox;coxtekton`, and
# the sweep cheerfully produces a sheet called `1;cox;coxtekton.png` from a run
# whose cheats were the single word "god". It looks like it worked.
echo "$ENCOUNTERS" | while IFS='|' read -r name cheats; do
    [ -z "$name" ] && continue
    [ -n "$want" ] && [ "$want" != "$name" ] && continue

    echo "--- $name ---"
    rm -rf "$WORK/$name"
    mkdir -p "$WORK/$name"

    # `|| true`: the client exits on TORIRS_MAX_FRAMES, and a non-zero status
    # from that path must not abort the whole sweep -- the frames it already
    # wrote are the point.
    SDL_VIDEODRIVER=dummy \
    MOCK230_SAVES="$WORK/saves" \
    TORIRS_NET_CHEAT="$cheats" \
    TORIRS_MAX_FRAMES=$MAXFRAMES \
    TORIRS_BMP_SERIES="$WORK/$name,$START,$STEP,$COUNT" \
        ./src/torirs --manifest "$MANIFEST" >"$WORK/$name.log" 2>&1 || true

    shot_count=$(ls "$WORK/$name"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
    if [ "$shot_count" = "0" ]; then
        echo "  no frames captured - see $WORK/$name.log"
        tail -3 "$WORK/$name.log" 2>/dev/null | sed 's/^/  /'
        continue
    fi
    echo "  $shot_count frames"
    python3 tools/cox_sprite_sheets.py "$WORK/$name" "$OUT/$name.png" --label "Chambers of Xeric - $name"
done

# The loop above runs in a subshell (it is the right-hand side of a pipe), so
# anything it accumulated in a variable is gone by here. Glob the directory
# instead of trying to carry a list across that boundary.
if [ -z "$want" ]; then
    python3 tools/cox_sprite_sheets.py --contact "$OUT"/*.png -o "$OUT/all.png"
fi
echo "done: $OUT"
