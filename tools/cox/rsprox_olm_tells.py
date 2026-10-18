#!/usr/bin/env python3
"""Inventory of what Jagex's server sends during the Great Olm fight, from RSProx
transcripts: loc animations, projectiles (with their timing/height parameters),
map/player graphics, area/synth sounds and game messages -- the things no wiki
page gives exactly. Only lines inside the Olm chamber (or, for player-attached
ones, while the raider is in it) are kept.

    python3 tools/cox/rsprox_olm_tells.py ~/.rsprox/binary/archive/{3389,3282,843,3285}.txt

Prints each distinct signature with its count, most common first.
"""
import re
import sys
from collections import Counter, defaultdict

CHAMBER = (3220, 5727, 3245, 5755)       # x0, z0, x1, z1 (level 0): the Olm room (RSProx frame)
TICK = re.compile(r"^\[(\d+)\]$")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
LOC_ANIM = re.compile(r"\[loc_anim\] coord=\(\d+, \d+, \d+\), shape=\d+, rotation=\d+, anim=([a-z0-9_]+) \((\d+)\)")
PROJ = re.compile(r"\[map_projanim_v2\] id=([a-z0-9_]+) \((\d+)\), (.*)$")
MAPANIM = re.compile(r"\[map_anim\] id=([a-z0-9_]+) \((\d+)\)(.*?)coord=")
SPOT = re.compile(r"\[spotanim\] (?:slot=\d+, )?id=([a-z0-9_]+) \((\d+)\)(.*)$")
SOUND_AREA = re.compile(r"\[sound_area\] id=(\d+), (.*?)coord=")
SYNTH = re.compile(r"\[synth_sound\] id=(\d+)(.*)$")
MESSAGE = re.compile(r'\[message_game\] type=([a-z]+), message="(.*)"$')
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+) \((\d+)\)(.*)$")
PLAYER = re.compile(r"player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), (\d+)\)")
ME = re.compile(r"local player index: (\d+)")


def inside(x, z, level):
    return level == 0 and CHAMBER[0] <= x <= CHAMBER[2] and CHAMBER[1] <= z <= CHAMBER[3]


def main(paths):
    seen = defaultdict(Counter)
    for path in paths:
        me, in_room, actor, section = None, False, None, None
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                m = ME.search(line)
                if m:
                    me = m.group(1)
                s = line.strip()
                if s.startswith("[player_info]"):
                    section = "player"
                elif s.startswith("[npc_info"):
                    section = "npc"
                m = PLAYER.search(line)
                if m and section == "player":
                    actor = "me" if m.group(1) == me else "other"
                    if actor == "me":
                        in_room = inside(int(m.group(2)), int(m.group(3)), int(m.group(4)))
                    continue
                npc = re.search(r"npc=\(index=\d+, id=([a-z0-9_]+)", line)
                if npc and section == "npc" and "[" in s[:12]:
                    actor = "npc:" + npc.group(1)
                c = COORD.search(line)
                here = inside(int(c.group(1)), int(c.group(2)), int(c.group(3))) if c else in_room
                if not here:
                    continue
                m = LOC_ANIM.search(line)
                if m:
                    seen["loc_anim"]["%s (%s)" % (m.group(1), m.group(2))] += 1
                    continue
                m = PROJ.search(line)
                if m:
                    params = re.sub(r"progress=\d+, ", "", m.group(3))
                    seen["projectile"]["%s (%s) %s" % (m.group(1), m.group(2), params)] += 1
                    continue
                m = MAPANIM.search(line)
                if m:
                    seen["map_anim"]["%s (%s)%s" % (m.group(1), m.group(2), m.group(3).rstrip(", "))] += 1
                    continue
                m = SOUND_AREA.search(line)
                if m:
                    seen["sound_area"]["%s %s" % (m.group(1), m.group(2).rstrip(", "))] += 1
                    continue
                if not in_room:
                    continue
                m = MESSAGE.search(line)
                if m:
                    seen["message"]["[%s] %s" % (m.group(1), m.group(2))] += 1
                    continue
                m = SPOT.search(line)
                if m and actor == "me" and "olm" in m.group(1):
                    seen["spotanim_on_me"]["%s (%s)%s" % (m.group(1), m.group(2), m.group(3))] += 1
                    continue
                m = SEQ.search(line)
                if m and actor == "me":
                    seen["my_sequence"]["%s (%s)" % (m.group(1), m.group(2))] += 1
                    continue
                m = SYNTH.search(line)
                if m:
                    seen["synth"][m.group(1) + m.group(2)] += 1
    for kind in ("loc_anim", "projectile", "map_anim", "spotanim_on_me", "sound_area", "synth", "message", "my_sequence"):
        print("==", kind)
        for key, n in seen[kind].most_common(60):
            print("%6d  %s" % (n, key[:170]))


if __name__ == "__main__":
    main(sys.argv[1:])
