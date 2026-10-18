#!/usr/bin/env python3
"""Vespula's room as Jagex's server sent it, read out of RSProx transcripts, in
TEMPLATE room-local tiles (tools/cox/rsprox_room.py: the turned-room fixes).

    python3 tools/cox/rsprox_vespula.py ~/.rsprox/binary/archive/{820,843,3389}.txt
    python3 tools/cox/rsprox_vespula.py --section timeline FILES...

Sections: timeline, layout, vespula, grubs, enrage, drain, soldiers, roots, me, clear.
Ticks are the transcript's. "me at T" is the local raider's tile after tick T.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_room import load_room, name_of, coord_of, num  # noqa: E402

VESPULA = {7530: "flying", 7531: "enraged", 7532: "walking"}
PORTAL = 7533
GRUBS = {7534: "healthy", 7535: "sickly", 7536: "infected", 7537: "dead"}
SOLDIERS = {7538: "flying", 7539: "walking"}
IDS = set(VESPULA) | {PORTAL} | set(GRUBS) | set(SOLDIERS)
SIZE = {7530: 5, 7531: 5, 7532: 5, 7533: 4, 7534: 3, 7535: 3, 7536: 3, 7537: 3, 7538: 3, 7539: 3}
ROOM_LOCS = ("raids_vespula", "raids_floor_abyss", "medivaemia")
# The portal's template south-west tile per room cell (cox_vespula.rs2; the
# cells: m51_82 (0,1) ccw, (1,1) thru, m52_82 (0,1) cw): the turn's anchor when
# no npc in the room takes a labelled step.
ANCHOR = (PORTAL, 4, {3264: (17, 20), 3296: (22, 11), 3328: (11, 20)})


def kind_of(npc):
    t = npc.types[0][1]
    if t in VESPULA:
        return "vespula"
    if t == PORTAL:
        return "portal"
    if t in GRUBS:
        return "grub"
    if t in SOLDIERS:
        return "soldier"
    return None


def short(npc, t=None):
    row = npc.type_at(t) if t is not None else npc.types[-1]
    return "%s#%s" % (row[2].replace("raids_vespula_", "").replace("caterpillar_", "grub_").replace("vespine_", "sold_"), npc.index[-3:])


def room_npcs(R, kind=None):
    return [n for n in R.lives if kind_of(n) and (kind is None or kind_of(n) == kind)]


# ------------------------------------------------------------------ sections

def timeline(R):
    print("== %s timeline (flip %s, turn %s, origin %s, ticks %d..%d)" % (R.path, R.flip, R.turn, R.origin, R.first, R.last))
    for t in range(R.first - 3, R.last + 6):
        row = []
        for n in room_npcs(R):
            for k, d in n.ev.get(t, []):
                if k in ("idle", "headbar"):
                    continue
                if k in ("walk", "teleport", "add", "del", "run", "crawl"):
                    p = R.sw_of(n.tiles.get(t), SIZE.get(n.type_at(t)[1], 1))
                    row.append("%s %s %s" % (short(n, t), k, p))
                elif k == "face":
                    row.append("%s face=%s" % (short(n, t), R.face_of(int(d))))
                elif k == "seq" and d.startswith(("vespine_walk", "luxgrub_dead")):
                    continue
                else:
                    row.append("%s %s=%s" % (short(n, t), k, d))
        for kind, text, subs in R.top.get(t, []):
            if kind.startswith("loc_") and any(s in text for s in ROOM_LOCS + ("coord=",)) and ("raids_vespula" in text or "herb" in text or kind == "loc_anim" or kind == "loc_del"):
                row.append("%s %s %s r%s" % (kind.replace("_change_v2", ""), name_of(text) or "", R.local_of(coord_of(text)), num(text, "rotation")))
            elif kind.startswith("map_projanim"):
                nm = name_of(text)
                if nm.startswith(("raids_vespula", "ii_dragon", "league")) and not nm.startswith(("ii_dragon", "league")):
                    src = [R.local_of(coord_of(s)) for s in subs if s.startswith("[source]")]
                    tgt = [R.local_of(coord_of(s)) for s in subs if s.startswith("[target]")]
                    row.append("proj %s %s->%s st%s et%s a%s p%s h%s-%s" % (nm.replace("raids_vespula_", ""), src[0] if src else None, tgt[0] if tgt else None,
                               num(text, "starttime"), num(text, "endtime"), num(text, "angle"), num(text, "progress"), num(text, "startheight"), num(text, "endheight")))
            elif kind == "map_anim":
                nm = name_of(text)
                if "vespula" in nm or "chompy" in nm or "vespine" in nm:
                    row.append("mapanim %s %s d%s" % (nm, R.local_of(coord_of(text)), num(text, "delay", 0)))
            elif kind.startswith("message"):
                row.append("msg %s" % text[:140])
            elif kind == "sound_area":
                row.append("snd %s d%s r%s %s" % (num(text, "id"), num(text, "delay", 0), num(text, "range"), R.local_of(coord_of(text))))
        for k, d in R.my_ev.get(t, []):
            if k in ("hit", "walk", "run", "spotanim") or (k == "sequence" and d in ("human_pickuptable", "human_eat")):
                row.append("ME %s %s" % (k, d if k not in ("walk", "run") else R.local_of(R.me.get(t))))
        for st, lv in R.stats.get(t, []):
            if st in ("prayer", "hitpoints"):
                row.append("ME %s=%d" % (st, lv))
        if row:
            print("  %5d  me%s  %s" % (t, R.local_of(R.me_at(t)), " | ".join(row)))


def main():
    secs = ["timeline"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append")
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    for p in args.transcripts:
        R = load_room(p, IDS, anchor=ANCHOR)
        for s in args.section or secs:
            globals()[s](R)


if __name__ == "__main__":
    main()
