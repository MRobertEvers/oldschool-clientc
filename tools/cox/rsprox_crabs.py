#!/usr/bin/env python3
"""The Chambers of Xeric crystal crab room (lasercrabs) as Jagex's server sent
it, from an RSProx transcript in INSTANCE tiles (transcribe with
TRANSLATE_INSTANCED_COORDS=false and PLAYER_INFO_LOCAL_PLAYER_ONLY=false;
docs/rsprox-analysis/cox/README.md, "Instance tiles"). Only 3389 (solo,
Challenge Mode) holds the room. Translated input is refused.

Ids: crabs raids_lasercrabs_crab_grey 7576, red 7577, green 7578, blue 7579;
orbs raids_lasercrabs_energy_white 7580, red 7581, green 7582, blue 7583.

Sections (--section, default all):
  room      the crabs and their tiles, the hammer, the big crystal's stages
            and the small crystals lit, the shatter
  orbs      every orb: added (tick, tile), its steps (direction, one a tick?),
            each colour change with the crab beside it, how it ended (the
            tick, the tile, the graphics and sounds), its removal; the spawn
            cadence
  paint     every crab colour change: what hit the crab that tick or the one
            before (the raider's style), and how long a colour lasted
  smash     every hammer pound: the crab's graphic, seq and sounds, and the
            crab's next swing and step
  swings    every crab swing (horror_crab_pincer_attack): its sound, the hit
            on the raider and its tick, the cadence; the blocks and their sound
  movement  every crab step, against the raider's distance

    python3 tools/cox/rsprox_crabs.py <raw>/archive/3389.txt
"""
import argparse
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_mystics as rm  # noqa: E402

CRAB = {7576: "grey", 7577: "red", 7578: "green", 7579: "blue"}
ORB = {7580: "white", 7581: "red", 7582: "green", 7583: "blue"}
rm.MYSTIC = set(CRAB) | set(ORB)      # the translated check reads it
STEP = {(0, 1): "N", (0, -1): "S", (1, 0): "E", (-1, 0): "W"}


def colour_at(rec, idx, t):
    """The npc's id at the end of tick t (adds and transformations)."""
    nid = None
    for tt, k, tx in rec.npc[idx]:
        if tt > t:
            break
        if k in ("add", "idle", "walk", "run", "teleport", "crawl", "del"):
            n = rm.NPC.search(tx)
            if n:
                nid = int(n.group(3) or n.group(4))
        if k == "transformation":
            nid = rm.ident(tx)[1]
    return nid


def colour_changes(rec, idx):
    out = []
    last = rec.npc_add[idx][1] and colour_at(rec, idx, rec.npc_add[idx][0])
    for t, k, tx in rec.npc[idx]:
        if k == "transformation":
            nid = rm.ident(tx)[1]
            if nid != last:
                out.append((t, last, nid))
                last = nid
    return out


def crabs(rec):
    return sorted(i for i in rec.npc_add if rec.npc_id.get(i) in CRAB or colour_at(rec, i, rec.npc_add[i][0]) in CRAB)


def orbs(rec):
    return sorted(i for i in rec.npc_add if colour_at(rec, i, rec.npc_add[i][0]) in ORB)


def cname(nid):
    return CRAB.get(nid) or ORB.get(nid) or str(nid)


def tops_named(rec, t, kind):
    return [(tx, subs) for k, tx, subs in rec.top.get(t, []) if k == kind]


def crab_tiles(rec, t):
    return {c: rec.tile_at(rec.npc_tile[c], t) for c in crabs(rec)
            if rec.npc_add[c][0] <= t and rec.npc_del.get(c, 10 ** 9) >= t}


def section_room(recs):
    print("== room")
    for rec in recs:
        cs = crabs(rec)
        print("%s: %d crabs, %d orbs" % (rec.path, len(cs), len(orbs(rec))))
        for c in cs:
            print("   crab %d added %d at %s as %s, removed %s" % (
                c % 1000, rec.npc_add[c][0], rec.npc_add[c][1], cname(colour_at(rec, c, rec.npc_add[c][0])),
                rec.npc_del.get(c)))
        seen = {}
        lo = min(rec.npc_add[c][0] for c in cs) - 30 if cs else 0
        hi = max(rec.npc_del.get(c, 0) for c in cs) + 30 if cs else 0
        for t in range(lo, hi):
            for k, tx, subs in rec.top.get(t, []):
                if k in ("loc_add_change_v2", "loc_del") and "lasercrabs" in tx:
                    key = (rm.ident(tx)[0], rm.COORD.search(tx).group(0))
                    if key not in seen:
                        seen[key] = t
                        print("   %d %s %s" % (t, k, tx[:120]))
                if k == "map_anim" and ("shatter" in tx or "lasercrabs" in tx):
                    print("   %d map_anim %s" % (t, tx[:120]))
        big = [t for t in range(lo, hi) for k, tx, subs in rec.top.get(t, [])
               if k == "loc_add_change_v2" and "bigcrystal_" in tx]
        print("   the big crystal sent %d times; gaps %s" % (len(big), sorted(Counter(b_ - a_ for a_, b_ in zip(big, big[1:])).items())))


def section_orbs(recs):
    print("== orbs: tick from add; steps as directions")
    adds = []
    for rec in recs:
        for o in orbs(rec):
            t0, at = rec.npc_add[o]
            adds.append(t0)
            ticks = sorted(rec.npc_tile[o])
            path = []
            prev = at
            for t in ticks:
                c = rec.npc_tile[o][t]
                d = (c[0] - prev[0], c[1] - prev[1])
                if d != (0, 0):
                    path.append((t - t0, STEP.get(d, str(d))))
                prev = c
            changes = []
            for t, a, b in colour_changes(rec, o):
                near = [(cname(colour_at(rec, c, t)), rm.foot(ct, rec.tile_at(rec.npc_tile[o], t), 1))
                        for c, ct in crab_tiles(rec, t).items() if ct and rm.foot(ct, rec.tile_at(rec.npc_tile[o], t), 1) <= 1]
                changes.append((t - t0, cname(a), cname(b), near))
            end = rec.npc_del.get(o)
            last = rec.tile_at(rec.npc_tile[o], end or ticks[-1])
            tells = []
            for t in range((end or ticks[-1]) - 2, (end or ticks[-1]) + 1):
                for k, tx, subs in rec.top.get(t, []):
                    if k == "map_anim" or (k == "sound_area" and rm.COORD.search(tx)
                                           and rm.cheb((int(rm.COORD.search(tx).group(1)), int(rm.COORD.search(tx).group(2))), last) <= 1):
                        tells.append((t - t0, k, tx[:70]))
            steps = Counter(d for _, d in path)
            print("   orb %d +%d at %s: %d steps %s; colours %s; removed +%s at %s; end tells %s" % (
                o % 1000, t0, at, len(path), dict(steps), changes, (end - t0) if end else None, last, tells[:4]))
    adds.sort()
    print("   spawn gaps:", Counter(b - a for a, b in zip(adds, adds[1:])))


def section_paint(recs):
    print("== paint: a crab's colour changes; what hit it")
    lasted = Counter()
    for rec in recs:
        local = rm.first_raider(rec)
        for c in crabs(rec):
            ch = colour_changes(rec, c)
            for i, (t, a, b) in enumerate(ch):
                hits = [(tt - t, rm.ident(tx)[1], rm.num(tx, "value")) for tt, tx in rm.npc_events(rec, c, "hit") if t - 3 <= tt <= t]
                pseq = [(tt - t, rm.ident(tx)[0]) for tt, k, tx in rec.pl.get(local, []) if k == "sequence" and t - 6 <= tt <= t]
                projs = [(tt - t, p["name"], p["end"]) for tt in range(t - 6, t + 1) for p in rm.projectiles(rec, tt) if p["tgt_npc"] == c]
                orb_near = [cname(colour_at(rec, o, t)) for o in orbs(rec)
                            if rec.npc_add[o][0] <= t <= rec.npc_del.get(o, 0) and rec.tile_at(rec.npc_tile[o], t)
                            and rm.foot(rec.tile_at(rec.npc_tile[c], t), rec.tile_at(rec.npc_tile[o], t), 1) <= 1]
                nxt = ch[i + 1][0] if i + 1 < len(ch) else None
                if nxt:
                    lasted[(cname(b), nxt - t)] += 1
                print("   crab %d t%d %s -> %s; hits %s; raider seqs %s; shots %s; orb beside %s; next change +%s" % (
                    c % 1000, t, cname(a), cname(b), hits, pseq[-3:], projs[-2:], orb_near, (nxt - t) if nxt else None))
    print("   how long a colour lasted:", sorted(lasted.items()))


def section_smash(recs):
    print("== smash: every human_hammer_hit")
    for rec in recs:
        local = rm.first_raider(rec)
        for t, k, tx in rec.pl.get(local, []):
            if k != "sequence" or "human_hammer_hit" not in tx:
                continue
            seen = []
            for c in crabs(rec):
                for tt, kk, ctx in rec.npc[c]:
                    if t <= tt <= t + 2 and kk in ("spotanim", "sequence", "hit"):
                        seen.append((tt - t, c % 1000, kk, ctx[:50]))
            snd = [(tt - t, rm.sound_name(sid), k2, d) for tt in range(t, t + 2)
                   for k2, sid, d, rng, lp, at in rm.sounds(rec, tt) if sid in (556, 2727)]
            print("   t%d: %s sounds %s" % (t, seen, snd))
    print("   (stun length: see `swings`, the stunned crab's next swing)")


def section_swings(recs):
    print("== swings")
    cad = Counter()
    out = Counter()
    for rec in recs:
        local = rm.first_raider(rec)
        for c in crabs(rec):
            cs = [t for t, tx in rm.npc_events(rec, c, "sequence") if "pincer_attack" in tx]
            for a, b in zip(cs, cs[1:]):
                cad[b - a] += 1
            stuns = [t for t, tx in rm.npc_events(rec, c, "spotanim") if "stunned" in tx]
            for t in cs:
                snd = sorted(set(rm.sound_name(sid) for k, sid, d, rng, lp, at in rm.sounds(rec, t) if sid in (718, 720)))
                hs = [(tt - t, rm.ident(tx)[1], rm.num(tx, "value")) for tt, k, tx in rec.pl.get(local, [])
                      if k == "hit" and t <= tt <= t + 1]
                out[("sound", tuple(snd))] += 1
                out[("hit on the raider", tuple((d, h) for d, h, v in hs[:1]))] += 1
                if hs:
                    out[("max", max(v for d, h, v in hs))] += 0
            for s in stuns:
                nxt = next((t for t in cs if t > s), None)
                out[("stun -> next swing", (nxt - s) if nxt else None)] += 1
        vals = [rm.num(tx, "value") for t, k, tx in rec.pl.get(local, []) if k == "hit"]
        print("   %s: hits on the raider in the room window: %s" % (rec.path, sorted(Counter(vals).items())))
    print("   cadence:", sorted(cad.items()))
    for k, n in sorted(out.items(), key=str):
        if n:
            print("   %4d  %s" % (n, k))


def section_movement(recs):
    print("== movement")
    for rec in recs:
        local = rm.first_raider(rec)
        for c in crabs(rec):
            steps = [(t, rm.foot(rec.tile_at(rec.npc_tile[c], t - 1), rec.tile_at(rec.pl_tile[local], t - 1), 1))
                     for t, k, tx in rec.npc[c] if k in ("walk", "run", "crawl")]
            first = rec.npc_add[c][1]
            far = max(rm.cheb(first, x) for x in rec.npc_tile[c].values())
            print("   crab %d: %d steps, furthest %d from its first tile; (tick, raider gap the tick before) %s" % (
                c % 1000, len(steps), far, steps[:16]))


SECTIONS = dict(room=section_room, orbs=section_orbs, paint=section_paint, smash=section_smash,
                swings=section_swings, movement=section_movement)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS), action="append")
    a = ap.parse_args()
    recs = []
    for path in a.transcripts:
        rec = rm.Rec(path)
        if rec.translated:
            sys.exit("%s: translated coordinates; transcribe untranslated" % path)
        recs.append(rec)
    for n in a.section or SECTIONS:
        SECTIONS[n](recs)


if __name__ == "__main__":
    main()
