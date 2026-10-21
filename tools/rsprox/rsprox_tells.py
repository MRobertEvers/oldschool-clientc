#!/usr/bin/env python3
"""Everything Jagex's server sent in a window of an RSProx transcript: per npc
type its sequences, graphics, hits and lifecycle (add/del/transform), every
projectile with its full parameters and who threw it, map graphics, area
sounds (with what they pair with), loc changes and loc animations, game
messages, and the local player's sequences and graphics.

    python3 tools/rsprox/rsprox_tells.py <transcript> [--from T] [--to T]
        [--npc SUBSTR ...] [--names <named transcript>] [--timeline]

--npc keeps npcs whose name holds any SUBSTR (default: all). A rev whose
transcript has bare ids (no names, e.g. rev 227) gets them per type from our
configs (OSRS-Content/osrs-content/configs/all.<type>.compack, `id=name`). --timeline prints the ordered event stream
instead of the inventory, for timing work.
"""
import argparse
import os
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
PLAYER = re.compile(r"player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), (\d+)\)")
# rev 234+: id=name (n) or id=n; rev 227: <display name> (id=n)
NPC = re.compile(r"npc=\(index=(\d+), (?:id=(?:([a-z0-9_]+) \((\d+)\)|(\d+))|[^,]*?\(id=(\d+)\)), coord=\((\d+), (\d+), \d+\)")
INFO_KIND = re.compile(r"^\s+\[(add|del|idle|walk|run|teleport|crawl)\] npc=")
SEQ = re.compile(r"\[sequence\] id=(?:([a-z0-9_]+) \((\d+)\)|(\d+))(.*)$")
SPOT = re.compile(r"\[spotanim\] (?:slot=\d+, )?id=(?:([a-z0-9_]+) \((\d+)\)|(\d+))(.*)$")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
TRANSFORM = re.compile(r"\[transformation\] id=(?:([a-z0-9_]+) \((\d+)\)|(\d+))")
FACE = re.compile(r"\[(face_angle|face_coord|face_pathingentity)\]")
PROJ = re.compile(r"\[map_projanim_v[12]\] id=(?:([a-z0-9_]+) \((\d+)\)|(\d+)), (.*)$")
SRC = re.compile(r"\[source\] coord=\((\d+), (\d+), \d+\)(?:, npc=\(index=\d+, (?:id=(?:([a-z0-9_]+) \(\d+\)|(\d+))|[^,]*?\(id=(\d+)\))|, player=)?")
DST = re.compile(r"\[target\] coord=\((\d+), (\d+), \d+\)(?:, (npc|player)=)?")
MAPANIM = re.compile(r"\[map_anim\] id=(?:([a-z0-9_]+) \((\d+)\)|(\d+))(.*?)coord=\((\d+), (\d+)")
SOUND = re.compile(r"\[sound_area\] id=(\d+), (.*?)coord=\((\d+), (\d+)")
SYNTH = re.compile(r"\[synth_sound\] id=(\d+)(.*)$")
LOC = re.compile(r"\[(loc_add_change_v[12]|loc_del|loc_anim)\] (?:id=(?:([a-z0-9_]+) \((\d+)\)|(\d+)), )?coord=\((\d+), (\d+), \d+\)(.*)$")
MESSAGE = re.compile(r'\[message_game\] type=([a-z]+), message="(.*)"$')
SAY = re.compile(r'\[say\] text="(.*)"')


CONFIGS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content", "osrs-content", "configs")


def load_names():
    names = {}
    for kind in ("seq", "spotanim", "npc", "loc"):
        table = {}
        with open(os.path.join(CONFIGS, "all.%s.compack" % kind), "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                key, _, value = line.rstrip("\n").partition("=")
                if key.isdigit():
                    table[key] = value
        names[kind] = table
    return names


def name_of(named, numeric, table):
    if named:
        return named
    if numeric and numeric in table:
        return table[numeric]
    return numeric or "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("transcript")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", dest="end", type=int, default=10 ** 9)
    ap.add_argument("--npc", nargs="*", default=[])
    ap.add_argument("--timeline", action="store_true")
    a = ap.parse_args()
    names = load_names()
    inv = defaultdict(Counter)
    timeline = []
    me, tick, section, actor = None, 0, None, None
    proj = None

    def keep(npc):
        return not a.npc or any(s in npc for s in a.npc)

    def note(kind, key):
        inv[kind][key] += 1
        timeline.append((tick, kind, key))

    with open(a.transcript, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = ME.search(line)
            if m:
                me = m.group(1)
            s = line.strip()
            m = TICK.match(s)
            if m and line.startswith("["):
                tick = int(m.group(1))
                continue
            if tick < a.start or tick > a.end:
                continue
            if s.startswith("[player_info]"):
                section = "player"
            elif s.startswith("[npc_info"):
                section = "npc"
            if proj is not None:
                m = SRC.search(line)
                if m:
                    proj["src"] = name_of(m.group(3), m.group(4) or m.group(5), names["npc"]) if (m.group(3) or m.group(4) or m.group(5)) else ("player" if "player=" in line else "tile")
                    continue
                m = DST.search(line)
                if m:
                    proj["dst"] = (m.group(3) or "tile")
                    who = proj.pop("src", "?")
                    if keep(who) or keep(proj["name"]):
                        note("projectile", "%s from %s to %s: %s" % (proj["name"], who, proj["dst"], proj["params"]))
                    proj = None
                    continue
            m = PROJ.search(line)
            if m:
                params = re.sub(r"progress=0, ", "", m.group(4))
                proj = {"name": name_of(m.group(1), m.group(3), names["spotanim"]), "params": params}
                continue
            m = NPC.search(line)
            if m and section == "npc":
                actor = ("npc", name_of(m.group(2), m.group(4) or m.group(5), names["npc"]))
                k = INFO_KIND.match(line)
                if k and k.group(1) in ("add", "del", "teleport") and keep(actor[1]):
                    note("npc_life", "%s %s" % (actor[1], k.group(1)))
                continue
            m = PLAYER.search(line)
            if m and section == "player":
                actor = ("me" if m.group(1) == me else "player", None)
                continue
            if actor and actor[0] == "npc" and keep(actor[1]):
                m = SEQ.search(line)
                if m:
                    note("npc_seq", "%s: %s%s" % (actor[1], name_of(m.group(1), m.group(3), names["seq"]), m.group(4)))
                    continue
                m = SPOT.search(line)
                if m:
                    note("npc_spot", "%s: %s%s" % (actor[1], name_of(m.group(1), m.group(3), names["spotanim"]), m.group(4)))
                    continue
                m = TRANSFORM.search(line)
                if m:
                    note("npc_life", "%s -> %s" % (actor[1], name_of(m.group(1), m.group(3), names["npc"])))
                    continue
                m = HIT.search(line)
                if m:
                    inv["npc_hit"]["%s splat %s" % (actor[1], m.group(1))] += 1
                    continue
            if actor and actor[0] == "me":
                m = SEQ.search(line)
                if m:
                    note("my_seq", name_of(m.group(1), m.group(3), names["seq"]) + m.group(4))
                    continue
                m = SPOT.search(line)
                if m:
                    note("my_spot", name_of(m.group(1), m.group(3), names["spotanim"]) + m.group(4))
                    continue
                m = HIT.search(line)
                if m:
                    timeline.append((tick, "my_hit", "%s splat %s" % (m.group(2), m.group(1))))
                    continue
                m = SAY.search(line)
                if m:
                    note("my_say", m.group(1))
                    continue
            m = MAPANIM.search(line)
            if m:
                note("map_anim", "%s%s" % (name_of(m.group(1), m.group(3), names["spotanim"]), m.group(4).rstrip(", ")))
                continue
            m = SOUND.search(line)
            if m:
                note("sound_area", "%s %s" % (m.group(1), m.group(2).rstrip(", ")))
                continue
            m = SYNTH.search(line)
            if m:
                note("synth", m.group(1) + m.group(2))
                continue
            m = LOC.search(line)
            if m:
                rest = m.group(7)
                anim = re.search(r"anim=(?:([a-z0-9_]+) \((\d+)\)|(\d+))", rest)
                what = name_of(m.group(2), m.group(4), names["loc"]) if (m.group(2) or m.group(4)) else ""
                if anim:
                    what = "anim " + name_of(anim.group(1), anim.group(3), names["seq"])
                note("loc", "%s %s" % (m.group(1), what))
                continue
            m = MESSAGE.search(line)
            if m:
                note("message", "[%s] %s" % (m.group(1), m.group(2)))
    if a.timeline:
        for t, kind, key in timeline:
            print("%6d  %-10s %s" % (t, kind, key[:190]))
        return
    for kind in ("npc_life", "npc_seq", "npc_spot", "npc_hit", "projectile", "map_anim", "loc", "sound_area", "synth",
                 "message", "my_seq", "my_spot", "my_say"):
        if inv[kind]:
            print("==", kind)
            for key, n in inv[kind].most_common(80):
                print("%6d  %s" % (n, key[:200]))


if __name__ == "__main__":
    main()
