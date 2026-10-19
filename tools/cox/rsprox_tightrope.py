#!/usr/bin/env python3
"""The Chambers of Xeric tightrope room as Jagex's server sent it, from RSProx
transcripts in INSTANCE tiles (transcribe with TRANSLATE_INSTANCED_COORDS=false
and PLAYER_INFO_LOCAL_PLAYER_ONLY=false; docs/rsprox-analysis/cox/README.md,
"Instance tiles"). The guards are 1x1, so the distances need no template frame,
but a translated quarter-turned room mirrors them: translated input is refused.

A room is the guards added together (raids_tightrope_ranger 7559,
raids_tightrope_mage 7560). Sections (--section, default all):
  room      each room: its guards, their first tiles, the raiders near, the
            rope ends and keystone the local raider clicked, the barrier
  attacks   each guard attack (a guard projectile from its tile): the seq,
            graphic and sounds on its tick C, the projectile against the gap
            (Chebyshev guard -> target), the target against the raider's tile
            at the end of C-1 and C, the impact sent on C, the hit on the
            target (the local raider only) and its tick
  cadence   the ticks between one guard's attacks
  wake      each guard: added, first hit, first face, first attack; the
            crossing it answers (a crossing = the local raider's rope walk)
  crossing  the local raider's crossings: the click, the walk tick by tick,
            the rope's sounds, every guard attack in flight and every hit on
            the raider until 6 ticks after the walk
  blocks    the guards' block seqs against the attacker's projectile, and the
            synth sounds on a tick one kind of guard was hit
  movement  every guard step
  death     each guard's death seq (D) and what follows, in ticks from D
  barrier   the barrier, keystone and guards around the local raider's
            barrier click

    python3 tools/cox/rsprox_tightrope.py <raw>/archive/{843,3282,3285,3389,820}.txt
"""
import argparse
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_mystics as rm  # noqa: E402

GUARD = {7559: "ranger", 7560: "mage"}
ATTACK = {"rune_arrow_travel": "ranger", "fireblast_travel": "mage"}
ROPE_SOUND = 2495
rm.MYSTIC = set(GUARD)        # the translated check reads it


class Room:
    def __init__(self, guards, added):
        self.guards = guards                   # [idx]
        self.added = added                     # tick


def rooms_of(rec):
    """Guards added within 20 ticks of each other are one room."""
    gs = sorted((rec.npc_add[i][0], i) for i in rec.npc_id if rec.npc_id[i] in GUARD and i in rec.npc_add)
    out = []
    for t, i in gs:
        if out and t - out[-1].added <= 20:
            out[-1].guards.append(i)
        else:
            out.append(Room([i], t))
    for r in out:
        r.lo = r.added
        r.hi = max(rec.npc_del.get(i, max(rec.npc_tile[i])) for i in r.guards)
        r.anchor = rec.npc_add[r.guards[0]][1]
    return out


def gname(rec, idx):
    return "%s%d" % (GUARD[rec.npc_id[idx]][0], idx % 1000)


def tops_named(rec, t, kind):
    return [(tx, subs) for k, tx, subs in rec.top.get(t, []) if k == kind]


def guard_attacks(rec, room):
    """(C, guard, projectile) for every guard attack in the room."""
    out = []
    for t in range(room.lo, room.hi + 1):
        for p in rm.projectiles(rec, t):
            if p["name"] not in ATTACK:
                continue
            for g in room.guards:
                sw = rec.tile_at(rec.npc_tile[g], t)
                prev = rec.tile_at(rec.npc_tile[g], t - 1)
                if p["src"] in (sw, prev) and GUARD[rec.npc_id[g]] == ATTACK[p["name"]]:
                    out.append((t, g, p))
                    break
    return out


def local_hits(rec, name, lo, hi):
    out = []
    for t, k, tx in rec.pl.get(name, []):
        if k == "hit" and lo <= t <= hi:
            out.append((t, rm.ident(tx)[1], rm.num(tx, "value")))
    return out


def crossings(rec, name, lo, hi):
    """Each rope walk of the local raider: (click, first move, last move,
    [(tick, loops)] rope sounds)."""
    snd = []
    for t in range(lo, hi + 200):
        for k, sid, delay, rng, loops, c in rm.sounds(rec, t):
            if k == "synth_sound" and sid == ROPE_SOUND:
                snd.append((t, loops))
    groups = []
    for t, loops in snd:
        if groups and t - groups[-1][-1][0] <= 2:
            groups[-1].append((t, loops))
        else:
            groups.append([(t, loops)])
    out = []
    for g in groups:
        first = g[0][0]
        click = None
        for t in range(first, first - 20, -1):
            for tx, subs in tops_named(rec, t, "oploc1"):
                if "raids_tightrope_end" in tx:
                    click = t
            if click is not None:
                break
        tiles = rec.pl_tile.get(name, {})
        move = [t for t in range((click or first) + 1, g[-1][0] + 4)
                if t in tiles and rec.tile_at(tiles, t - 1) != tiles[t]]
        out.append(dict(click=click, first_move=move[0] if move else None,
                        last_move=move[-1] if move else None, sounds=g,
                        steps=[(t, tiles[t]) for t in move]))
    return out


def section_room(recs):
    print("== room")
    for rec in recs:
        local = rm.first_raider(rec)
        for room in rooms_of(rec):
            print("%s room at %d (tick %d..%d), local %s" % (rec.path, room.anchor[0], room.lo, room.hi, local))
            for g in room.guards:
                t, c = rec.npc_add[g]
                print("   %-10s added %d at %s, removed %s" % (gname(rec, g), t, c, rec.npc_del.get(g)))
            near = rec.raiders_at(room.lo + 1, room.anchor, 40)
            print("   raiders near on add: %s" % sorted(near))
            for t in range(room.lo, room.hi + 300):
                for k in ("oploc1", "oplocu", "loc_add_change_v2", "loc_del", "loc_add_change"):
                    for tx, subs in tops_named(rec, t, k):
                        if "tightrope" in tx or "invisible" in tx:
                            print("   %d %s %s" % (t, k, tx[:150]))


def section_attacks(recs):
    print("== attacks: C = the attack's tick (its projectile), gap = Chebyshev guard -> target tile")
    shape = Counter()
    flight = defaultdict(Counter)
    on_c = Counter()
    aim = Counter()
    land = Counter()
    for rec in recs:
        local = rm.first_raider(rec)
        for room in rooms_of(rec):
            for c, g, p in guard_attacks(rec, room):
                kind = GUARD[rec.npc_id[g]]
                sw = rec.tile_at(rec.npc_tile[g], c)
                gap = rm.cheb(sw, p["tgt"])
                shape[(kind, p["name"], p["start"], p["angle"], p["progress"], p["sh"], p["eh"])] += 1
                flight[kind][(gap, p["end"])] += 1
                seqs = [rm.ident(tx)[0] for _, tx in rm.npc_events(rec, g, "sequence", c)]
                gfx = [(rm.ident(tx)[0], rm.num(tx, "height"), rm.num(tx, "delay", 0)) for _, tx in rm.npc_events(rec, g, "spotanim", c)]
                on_c[(kind, "seq", tuple(seqs))] += 1
                on_c[(kind, "gfx", tuple(gfx))] += 1
                for k, sid, delay, rng, loops, at in rm.sounds(rec, c):
                    if k == "sound_area" and at == sw:
                        on_c[(kind, "sound at guard", rm.sound_name(sid), delay, rng)] += 1
                    elif k == "sound_area" and at == p["tgt"]:
                        on_c[(kind, "sound at target", rm.sound_name(sid), "delay=end" if delay == p["end"] else delay, rng)] += 1
                tgt = p["tgt_player"]
                if tgt:
                    tiles = rec.pl_tile.get(tgt, {})
                    aim[(kind, "C-1" if rec.tile_at(tiles, c - 1) == p["tgt"] else "-",
                         "C" if rec.tile_at(tiles, c) == p["tgt"] else "-")] += 1
                if tgt == local:
                    gfx_pl = [(rm.ident(tx)[0], rm.num(tx, "height"), "delay=end" if rm.num(tx, "delay", 0) == p["end"] else rm.num(tx, "delay", 0))
                              for _, tx in rm.pl_events(rec, local, "spotanim", c)]
                    on_c[(kind, "raider gfx", tuple(g_ for g_ in gfx_pl if "impact" in g_[0]))] += 1
                    L = c + p["end"] // 30
                    hs = [(t - c, h, v) for t, h, v in local_hits(rec, local, c + 1, L + 12)]
                    land[(kind, "end//30=%d" % (p["end"] // 30), tuple(hs[:1]))] += 1
    print(" projectile shapes:")
    for k, n in shape.most_common():
        print("   %4d  %s" % (n, k))
    for kind in flight:
        print(" %s flight end by gap: %s" % (kind, sorted(flight[kind].items())))
    print(" on C:")
    for k, n in sorted(on_c.items(), key=lambda x: (x[0][0], -x[1])):
        print("   %4d  %s" % (n, k))
    print(" aimed at the raider's tile at the end of C-1 / C:")
    for k, n in aim.most_common():
        print("   %4d  %s" % (n, k))
    print(" first hit on the local raider after C (ticks from C, hitsplat, value):")
    for k, n in land.most_common():
        print("   %4d  %s" % (n, k))


def section_cadence(recs):
    print("== cadence: ticks between one guard's attacks")
    gaps = Counter()
    for rec in recs:
        for room in rooms_of(rec):
            by = defaultdict(list)
            for c, g, p in guard_attacks(rec, room):
                by[g].append(c)
            for g, cs in by.items():
                for a, b in zip(cs, cs[1:]):
                    gaps[(GUARD[rec.npc_id[g]], b - a)] += 1
    for k, n in sorted(gaps.items()):
        print("   %4d  %s" % (n, k))


def attacks_on(rec, room, idx):
    """Raider projectiles at guard idx: (launch tick, landing tick)."""
    out = []
    for t in range(room.lo, room.hi + 1):
        for p in rm.projectiles(rec, t):
            if p["tgt_npc"] == idx:
                out.append((t, t + p["end"] // 30))
    return out


def section_wake(recs):
    print("== wake: ticks are absolute; X = the local raider's crossings (click/first move)")
    for rec in recs:
        local = rm.first_raider(rec)
        for room in rooms_of(rec):
            xs = crossings(rec, local, room.lo, room.hi)
            print("%s room %d: crossings %s" % (rec.path, room.lo, [(x["click"], x["first_move"], x["last_move"]) for x in xs]))
            atk = defaultdict(list)
            for c, g, p in guard_attacks(rec, room):
                atk[g].append((c, p["tgt_player"]))
            for g in room.guards:
                hits = [t for t, _ in rm.npc_events(rec, g, "hit")]
                faces = [(t, tx[:40]) for t, tx in rm.npc_events(rec, g, "face_pathingentity")]
                says = [(t, tx) for t, tx in rm.npc_events(rec, g, "say")]
                on = attacks_on(rec, room, g)
                print("   %-10s added %d; first attacked %s; first hit %s; faces %s; says %s; attacks %s" % (
                    gname(rec, g), rec.npc_add[g][0], on[:1], hits[:1], faces[:3], says, atk[g][:4]))


def section_crossing(recs):
    print("== crossing (local raider)")
    for rec in recs:
        local = rm.first_raider(rec)
        for room in rooms_of(rec):
            xs = crossings(rec, local, room.lo, room.hi)
            ats = guard_attacks(rec, room)
            for x in xs:
                k = x["click"] or x["first_move"]
                print("%s room %d: click %s, moves %s..%s (%d steps), sounds %s" % (
                    rec.path, room.lo, x["click"], x["first_move"], x["last_move"], len(x["steps"]),
                    [(t - x["first_move"], l) for t, l in x["sounds"]]))
                print("     steps (tick from first move, tile): %s" % [(t - x["first_move"], c) for t, c in x["steps"]])
                for t in range(k - 1, x["first_move"] + 1):
                    face = [tx[:60] for _, tx in rm.pl_events(rec, local, "face_angle", t)]
                    seq = [tx[:40] for _, tx in rm.pl_events(rec, local, "sequence", t)]
                    app = [1 for _, tx in rm.pl_events(rec, local, "appearance", t)]
                    print("     %+d tile %s face %s seq %s appearance %s" % (
                        t - x["first_move"], rec.tile_at(rec.pl_tile[local], t), face, seq, bool(app)))
                end = x["last_move"]
                app = [t - x["first_move"] for t in range(x["first_move"], end + 3)
                       if rm.pl_events(rec, local, "appearance", t)]
                print("     appearance updates at %s" % app)
                for c, g, p in ats:
                    L = c + p["end"] // 30
                    if p["tgt_player"] == local and x["first_move"] - 8 <= c <= end:
                        print("     attack %s C=%+d L=%+d" % (gname(rec, g), c - x["first_move"], L - x["first_move"]))
                for t, h, v in local_hits(rec, local, x["first_move"] - 4, end + 6):
                    print("     hit on raider %+d id=%s value=%s" % (t - x["first_move"], h, v))
                print("     last move %+d" % (end - x["first_move"]))


def section_blocks(recs):
    print("== blocks: guard seq delay against the raider projectile that lands on it")
    blk = Counter()
    snd = Counter()
    for rec in recs:
        for room in rooms_of(rec):
            projs = defaultdict(list)
            for t in range(room.lo, room.hi + 1):
                for p in rm.projectiles(rec, t):
                    if p["tgt_npc"] in room.guards:
                        projs[(p["tgt_npc"], t)].append(p["end"])
            for g in room.guards:
                kind = GUARD[rec.npc_id[g]]
                for t, tx in rm.npc_events(rec, g, "sequence"):
                    name = rm.ident(tx)[0]
                    if "block" in name or "_def" in name:
                        d = rm.num(tx, "delay", 0)
                        ends = projs.get((g, t), [])
                        blk[(kind, name, "delay=end" if d in ends else ("delay=%d ends=%s" % (d, ends)))] += 1
            for t in range(room.lo, room.hi + 1):
                hit = set(GUARD[rec.npc_id[g]] for g in room.guards if rm.npc_events(rec, g, "hit", t))
                died = set(GUARD[rec.npc_id[g]] for g in room.guards
                           if any("human_death" in tx for _, tx in rm.npc_events(rec, g, "sequence", t)))
                if len(hit) == 1:
                    kind = hit.pop()
                    s = sorted(rm.sound_name(sid) for k, sid, d, r, l, c in rm.sounds(rec, t)
                               if k == "synth_sound" and sid in (29, 3878, 512, 3891, 511))
                    snd[(kind, "dies" if died else "hit", tuple(s))] += 1
    for k, n in sorted(blk.items()):
        print("   %4d  %s" % (n, k))
    print(" synth sounds (hit/death family only) on a tick only one kind of guard was hit:")
    for k, n in sorted(snd.items()):
        print("   %4d  %s" % (n, k))


def section_movement(recs):
    print("== movement: guard steps")
    for rec in recs:
        for room in rooms_of(rec):
            for g in room.guards:
                steps = [(t, k) for t, k, tx in rec.npc[g] if k in ("walk", "run", "crawl", "teleport")]
                first_atk = next((c for c, gg, p in guard_attacks(rec, room) if gg == g), None)
                print("   %s %-10s %d steps %s; first attack %s" % (rec.path, gname(rec, g), len(steps), steps[:12], first_atk))


def section_death(recs):
    print("== death: D = the guard's human_death seq")
    out = Counter()
    for rec in recs:
        for room in rooms_of(rec):
            for g in room.guards:
                kind = GUARD[rec.npc_id[g]]
                ds = [t for t, tx in rm.npc_events(rec, g, "sequence") if "human_death" in tx]
                if not ds:
                    out[(kind, "no death seq")] += 1
                    continue
                d = ds[0]
                killing = [rm.num(tx, "value") for _, tx in rm.npc_events(rec, g, "hit", d)]
                out[(kind, "hit on D", bool(killing))] += 1
                out[(kind, "removed at D+", rec.npc_del.get(g, -10**6) - d)] += 1
                tile = rec.tile_at(rec.npc_tile[g], d)
                for t in range(d, d + 8):
                    for tx, subs in tops_named(rec, t, "obj_add_specific"):
                        c = rm.COORD.search(tx)
                        if c and (int(c.group(1)), int(c.group(2))) == tile:
                            out[(kind, "drop at D+%d" % (t - d), tx.split(", count")[0], "despawn=%s" % rm.num(tx, "despawn"))] += 1
    for k, n in sorted(out.items(), key=str):
        print("   %4d  %s" % (n, k))


def section_barrier(recs):
    print("== barrier: around each barrier click and keystone dispel")
    for rec in recs:
        local = rm.first_raider(rec)
        for room in rooms_of(rec):
            clicks = [t for t in range(room.lo, room.hi + 400)
                      for tx, s in tops_named(rec, t, "oploc1") + tops_named(rec, t, "oplocu")
                      if "raids_tightrope_barrier" in tx]
            alive = [g for g in room.guards if not any("human_death" in tx for _, tx in rm.npc_events(rec, g, "sequence"))]
            print("%s room %d: barrier clicks %s; guards never killed %s" % (rec.path, room.lo, clicks,
                                                                          [(gname(rec, g), rec.npc_del.get(g)) for g in alive]))
            if not clicks:
                continue
            for t in range(clicks[0], clicks[-1] + 6):
                for k, tx, subs in rec.top.get(t, []):
                    if k in ("message_game", "loc_add_change_v2", "loc_del", "update_inv_partial", "obj_add_specific",
                             "synth_sound", "sound_area", "loc_anim_specific", "map_anim_specific"):
                        extra = " ".join(st for sk, st in subs)[:120]
                        print("   %+d %s %s %s" % (t - clicks[0], k, tx[:110], extra))
                for _, tx in rm.pl_events(rec, local, "sequence", t):
                    print("   %+d raider seq %s" % (t - clicks[0], tx[:60]))
                for g in room.guards:
                    if rec.npc_del.get(g) == t:
                        print("   %+d %s removed" % (t - clicks[0], gname(rec, g)))


SECTIONS = dict(room=section_room, attacks=section_attacks, cadence=section_cadence, wake=section_wake,
                crossing=section_crossing, blocks=section_blocks, movement=section_movement,
                death=section_death, barrier=section_barrier)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS), action="append")
    a = ap.parse_args()
    recs = []
    for path in a.transcripts:
        rec = rm.Rec(path)
        if rec.translated:
            sys.exit("%s: translated coordinates (a guard below x %d); transcribe untranslated" % (path, rm.INSTANCE_X))
        recs.append(rec)
    for name in a.section or SECTIONS:
        SECTIONS[name](recs)


if __name__ == "__main__":
    main()
