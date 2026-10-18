#!/usr/bin/env python3
"""Vespula's room as Jagex's server sent it, read out of RSProx transcripts, in
TEMPLATE room-local tiles (tools/cox/rsprox_room.py: the turned-room fixes).

    python3 tools/cox/rsprox_vespula.py ~/.rsprox/binary/archive/{820,843,3389}.txt
    python3 tools/cox/rsprox_vespula.py --section timeline FILES...

Sections: timeline, layout, vespula, grubs, drain, clear (default: all but timeline).
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


def start_of(R):
    """The room's start: the tick its npcs appear (they first move 2 ticks later)."""
    return min(n.added for n in room_npcs(R))


def layout(R):
    """Every npc's first template tile (south-west), and the fixed locs."""
    print("== %s layout (flip %s, turn %s, origin %s)" % (R.path, R.flip, R.turn, R.origin))
    for n in room_npcs(R):
        t0 = min(n.tiles)
        print("  %-26s sw %s" % (short(n, t0), R.sw_of(n.tiles[t0], SIZE.get(n.types[0][1], 1))))
    seen = set()
    for t in sorted(R.top):
        for kind, text, subs in R.top[t]:
            nm = name_of(text)
            if kind.startswith("loc_") and nm.startswith("raids_vespula") and (nm, coord_of(text)) not in seen:
                seen.add((nm, coord_of(text)))
                print("  loc %-34s anchor %s r%s" % (nm, R.local_of(coord_of(text)), num(text, "rotation")))


def vespula(R):
    """Her forms by tick, her moves before the first enrage, and the grub stings."""
    s0 = start_of(R)
    print("== %s vespula (start %d)" % (R.path, s0))
    for n in room_npcs(R, "vespula"):
        print("  forms:", [(t - s0, nm.split("_")[-1]) for t, i, nm in n.types])
        moves = [(t - s0, R.sw_of(n.tiles[t], 5)) for t in sorted(n.tiles) if t in n.ev and any(k in ("walk", "teleport") for k, _ in n.ev[t])]
        print("  moves (start+, sw):", moves[:12])
        stings = [t for t in sorted(n.ev) for k, d in n.ev[t] if k == "seq" and d.startswith("vespula_attack_melee_flying")]
        print("  sting seqs at start+:", [t - s0 for t in stings], " mod 3:", sorted(set((t - s0) % 3 for t in stings)))
        flips = [t for t, i, nm in n.types if nm.endswith("enraged")]
        print("  enraged at start+:", [t - s0 for t in flips], " gaps:", [b - a for a, b in zip(flips, flips[1:])])


def grubs(R):
    """Each grub's forms and bar: the sting's drop, the drain, the hatch, the return."""
    s0 = start_of(R)
    print("== %s grubs (start %d)" % (R.path, s0))
    for n in room_npcs(R, "grub"):
        bars = [(t - s0, int(d)) for t in sorted(n.ev) for k, d in n.ev[t] if k == "headbar"]
        print("  %s sw %s forms %s" % (short(n), R.sw_of(n.tiles[min(n.tiles)], 3), [(t - s0, nm.split("_")[-1]) for t, i, nm in n.types]))
        if bars:
            print("     bar (start+, /30): first %s last %s over %d ticks" % (bars[0], bars[-1], bars[-1][0] - bars[0][0]))
    for n in room_npcs(R, "soldier"):
        heals = [t - s0 for t in sorted(n.ev) for k, d in n.ev[t] if k == "spotanim" and "healportal" in d]
        print("  %s rise start+%d at sw %s; heals at start+%s" % (short(n), n.added - s0, R.sw_of(n.tiles[min(n.tiles)], 3), heals))


def drain(R):
    """The portal's drains: tick, target tile, and the local raider's tile."""
    s0 = start_of(R)
    rows = [(t, R.local_of(coord_of([x for x in subs if x.startswith("[target]")][0])))
            for t, kind, text, subs in [(t, k, x, y) for t in sorted(R.top) for k, x, y in R.top[t]]
            if kind.startswith("map_projanim") and "portal_attack" in text]
    print("== %s drains (start %d): %d" % (R.path, s0, len(rows)))
    print("  at start+:", [t - s0 for t, _ in rows][:30])
    print("  gaps:", sorted(set(b[0] - a[0] for a, b in zip(rows, rows[1:]))))
    print("  targets:", sorted(set(str(x) for _, x in rows)), " me:", sorted(set(str(R.local_of(R.me_at(t - 1))) for t, _ in rows)))


def clear(R):
    """The death schedule from the portal's last tick (its ops hidden)."""
    portal = room_npcs(R, "portal")
    if not portal:
        return
    p = portal[0]
    d = [t for t in sorted(p.ev) for k, x in p.ev[t] if k == "enabled_ops" and "0b00000" in x]
    if not d:
        return
    D = d[0]
    print("== %s clear (D %d)" % (R.path, D))
    for t in range(D, D + 8):
        out = []
        for n in room_npcs(R):
            for k, x in n.ev.get(t, []):
                if k in ("del",) or (k == "seq" and "death" in x):
                    out.append("%s %s" % (short(n, t), k if k == "del" else x))
        for kind, text, subs in R.top.get(t, []):
            if kind.startswith(("loc_", "obj_add", "message", "map_anim", "sound_area")) and ("vespula" in text or "Abyss" in text or "chompy" in text or kind.startswith(("obj_add", "sound_area"))):
                if "tendrils_entrance" in text or "boil_blocking" in text:
                    continue
                out.append("%s %s" % (kind.replace("_change_v2", "").replace("_specific", ""), (name_of(text) or text)[:60]))
        if out:
            print("  D+%d: %s" % (t - D, "; ".join(out)))


def main():
    secs = ["layout", "vespula", "grubs", "drain", "clear"]
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
