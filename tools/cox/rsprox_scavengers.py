#!/usr/bin/env python3
"""The Chambers of Xeric scavenger rooms as Jagex's server sent them, from
RSProx transcripts in INSTANCE tiles (transcribe with
TRANSLATE_INSTANCED_COORDS=false and PLAYER_INFO_LOCAL_PLAYER_ONLY=false;
docs/rsprox-analysis/cox/README.md, "Instance tiles"). Translated input is
refused.

A room is the beasts (raids_scavenger_beast_a 7548, _b 7549) first seen within
40 ticks and 32 tiles of each other. Sections (--section, default all):
  room      each room: its beasts, when and where first seen and removed, the
            raiders near
  attacks   each beast swing (skavid_attack): its target (the face before it),
            the gap, the sounds on the tick, the hit on the target and its tick
  cadence   the ticks between one beast's swings
  wake      each beast: first seen, first hit taken, first face, first swing,
            and the nearest raider's distance then
  movement  each beast's steps, before and after its first swing, and how far
            it got from where it was first seen
  death     each death seq (D): the hit, the sounds, the removal, what lands
            under it and when, and the next beast added in the room

    python3 tools/cox/rsprox_scavengers.py <raw>/archive/{3282,3285,843,3389,820}.txt
"""
import argparse
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_mystics as rm  # noqa: E402

BEAST = {7548: "a", 7549: "b"}
SIZE = 2
rm.MYSTIC = set(BEAST)        # the translated check reads it


class Room:
    def __init__(self, idx, t, at):
        self.beasts = [idx]
        self.added = t
        self.anchor = at


def rooms_of(rec):
    out = []
    for t, i in sorted((rec.npc_add[i][0], i) for i in rec.npc_id if rec.npc_id[i] in BEAST and i in rec.npc_add):
        at = rec.npc_add[i][1]
        for r in out:
            if rm.cheb(r.anchor, at) <= 32 and t - max(rec.npc_add[b][0] for b in r.beasts) <= 400:
                r.beasts.append(i)
                break
        else:
            out.append(Room(i, t, at))
    for r in out:
        r.lo = r.added
        r.hi = max(rec.npc_del.get(b, max(rec.npc_tile[b])) for b in r.beasts)
    return out


def name(rec, idx):
    return "%s%d" % (BEAST[rec.npc_id[idx]], idx % 1000)


def seq_ticks(rec, idx, word):
    return [t for t, tx in rm.npc_events(rec, idx, "sequence") if word in tx]


def face_target(rec, idx, t):
    """The player the beast last faced at or before t."""
    best = None
    for tt, tx in rm.npc_events(rec, idx, "face_pathingentity"):
        if tt > t:
            break
        p = rm.PLAYER.search(tx)
        best = p.group(2) if p else None
    return best


def player_hits(rec, name_, lo, hi):
    return [(t, rm.ident(tx)[1], rm.num(tx, "value")) for t, k, tx in rec.pl.get(name_, [])
            if k == "hit" and lo <= t <= hi]


def section_room(recs):
    print("== room")
    for rec in recs:
        for r in rooms_of(rec):
            near = rec.raiders_at(r.lo, r.anchor, 32)
            print("%s room at %s (tick %d..%d): %d beasts; raiders near %s" % (
                rec.path, r.anchor, r.lo, r.hi, len(r.beasts), sorted(near)))
            for b in r.beasts:
                t, c = rec.npc_add[b]
                print("   %-6s added %d at %s, removed %s" % (name(rec, b), t, c, rec.npc_del.get(b)))


def section_attacks(recs):
    print("== attacks: C = a skavid_attack seq")
    out = Counter()
    rows = []
    for rec in recs:
        for r in rooms_of(rec):
            for b in r.beasts:
                for c in seq_ticks(rec, b, "skavid_attack"):
                    tgt = face_target(rec, b, c)
                    sw = rec.tile_at(rec.npc_tile[b], c)
                    pt = rec.tile_at(rec.pl_tile.get(tgt, {}), c) if tgt else None
                    gap = rm.foot(sw, pt, SIZE) if pt else None
                    snd = [(rm.sound_name(sid), k, d) for k, sid, d, rng, lp, at in rm.sounds(rec, c)
                           if sid in (469, 472, 471)]
                    hs = player_hits(rec, tgt, c, c + 2) if tgt else []
                    rows.append((rec.path, name(rec, b), c, tgt, gap, snd, [(t - c, h, v) for t, h, v in hs]))
                    out[("gap", gap)] += 1
                    out[("sounds on C", tuple(sorted(set(snd))))] += 1
                    out[("first hit on target, ticks after C", hs[0][0] - c if hs else None)] += 1
    for row in rows:
        print("   %s %-6s C=%d target %s gap %s sounds %s hits %s" % row)
    for k, n in sorted(out.items(), key=str):
        print("   %4d  %s" % (n, k))


def section_cadence(recs):
    print("== cadence")
    gaps = Counter()
    for rec in recs:
        for r in rooms_of(rec):
            for b in r.beasts:
                cs = seq_ticks(rec, b, "skavid_attack")
                for a, c in zip(cs, cs[1:]):
                    gaps[c - a] += 1
    print("   ", sorted(gaps.items()))


def nearest(rec, at, t):
    """(gap from the 2x2 at `at`, name) of the nearest raider at the end of t."""
    best = None
    for n, tab in rec.pl_tile.items():
        c = rec.tile_at(tab, t)
        if c:
            d = rm.foot(at, c, SIZE)
            if best is None or d < best[0]:
                best = (d, n)
    return best


def section_wake(recs):
    print("== wake: ticks absolute; near = the nearest raider (distance, name) at that tick")
    for rec in recs:
        for r in rooms_of(rec):
            for b in r.beasts:
                t0, at = rec.npc_add[b]
                hits = [t for t, tx in rm.npc_events(rec, b, "hit")]
                faces = [(t, (rm.PLAYER.search(tx).group(2) if rm.PLAYER.search(tx) else "-"))
                         for t, tx in rm.npc_events(rec, b, "face_pathingentity")]
                cs = seq_ticks(rec, b, "skavid_attack")
                f1 = next((t for t, n in faces if n != "-"), None)
                trail = [(t - (f1 or 0), nearest(rec, rec.tile_at(rec.npc_tile[b], t), t)[0])
                         for t in range(max(t0, (f1 or t0) - 8), (f1 or t0))] if f1 else []
                print("   %s %-6s added %d; first hit taken %s; first face %s %s; first swing %s; near at add %s, at face-1 %s" % (
                    rec.path, name(rec, b), t0, hits[:1], f1, faces[:2], cs[:1], nearest(rec, at, t0),
                    nearest(rec, rec.tile_at(rec.npc_tile[b], f1 - 1), f1 - 1) if f1 else None))
                if trail:
                    print("          nearest raider's gap the 8 ticks before the face: %s" % trail)


def section_movement(recs):
    print("== movement")
    for rec in recs:
        for r in rooms_of(rec):
            for b in r.beasts:
                t0, at = rec.npc_add[b]
                cs = seq_ticks(rec, b, "skavid_attack")
                steps = [(t, k) for t, k, tx in rec.npc[b] if k in ("walk", "run", "crawl", "teleport")]
                far = max(rm.cheb(at, c) for c in rec.npc_tile[b].values())
                before = [t for t, k in steps if not cs or t < cs[0]]
                after = [t for t, k in steps if cs and t >= cs[0]]
                print("   %s %-6s %d ticks seen, %d steps before its first swing, %d after; furthest %d from first tile; %s" % (
                    rec.path, name(rec, b), max(rec.npc_tile[b]) - t0, len(before), len(after), far, steps[:10]))


def section_death(recs):
    print("== death: D = skavid_death")
    for rec in recs:
        for r in rooms_of(rec):
            for b in r.beasts:
                for d in seq_ticks(rec, b, "skavid_death"):
                    at = rec.tile_at(rec.npc_tile[b], d)
                    hit = [(t - d, rm.num(tx, "value")) for t, tx in rm.npc_events(rec, b, "hit") if d - 2 <= t <= d]
                    snd = [(t - d, rm.sound_name(sid), k) for t in range(d - 1, d + 3)
                           for k, sid, dl, rng, lp, c in rm.sounds(rec, t) if sid in (471, 472)]
                    drops = []
                    for t in range(d, d + 8):
                        for tx, subs in [(tx, s) for k, tx, s in rec.top.get(t, []) if k == "obj_add_specific"]:
                            c = rm.COORD.search(tx)
                            if c and (int(c.group(1)), int(c.group(2))) == at:
                                drops.append((t - d, tx.split(", neverturnpublic")[0]))
                    nxt = [(rec.npc_add[o][0] - d, rec.npc_add[o][1]) for o in rec.npc_id
                           if rec.npc_id[o] in BEAST and o in rec.npc_add and d < rec.npc_add[o][0] <= d + 50
                           and rm.cheb(rec.npc_add[o][1], at) <= 32]
                    print("   %s %-6s D=%d at %s: hits %s, sounds %s, removed D+%s" % (
                        rec.path, name(rec, b), d, at, hit, snd,
                        (rec.npc_del[b] - d) if b in rec.npc_del else None))
                    for dr in drops:
                        print("        drop D+%d %s" % dr)
                    print("        beasts added within 50 ticks: %s" % nxt)


SECTIONS = dict(room=section_room, attacks=section_attacks, cadence=section_cadence, wake=section_wake,
                movement=section_movement, death=section_death)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS), action="append")
    a = ap.parse_args()
    recs = []
    for path in a.transcripts:
        rec = rm.Rec(path)
        if rec.translated:
            sys.exit("%s: translated coordinates (a beast below x %d); transcribe untranslated" % (path, rm.INSTANCE_X))
        recs.append(rec)
    for n in a.section or SECTIONS:
        SECTIONS[n](recs)


if __name__ == "__main__":
    main()
