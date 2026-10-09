#!/usr/bin/env python3
"""THE PESTILENT BLOAT, Blert against ours: his walk, his turns, his downs,
his stomp and his hand volleys, as distributions over every room.

  tools/blert_bloat_cal.py build/blert/bloat <our runs dir> [<our runs dir> ...]

Blert: Normal (mode 11) trio rooms from tools/blert_fetch_tob_rooms.py (stage
11); room tick 0 is the room's start. Ours: the leader's ticklog.tsv of each
seed under a runs dir (one session dir per seed); the room's start is the first
`hudbar` row (the HUD bar opens with the fight). Tiles are local to the room's
64-aligned square.

  walk     Bloat's SW tile per tick: the set of lap tiles, steps per tick by
           hit-point band (>= 60%, 40..60%, < 40%), the room tick of the first
           reversal and the walking ticks between reversals
  downs    each down's room tick, its walk length (ticks since the start or
           the last rise), its length (down to first step), down to down
  stomp    ticks from the down to the stomp
  hands    ticks between volleys by band, tiles per volley, drop to landing
"""
import collections
import glob
import json
import os
import sys

BLOAT = 8359
SLEEP = 8082
FLESH = {1570, 1571, 1572, 1573}
SPLAT = 1576


def band(cur, mx):
    if cur * 100 >= 60 * mx:
        return ">=60"
    if cur * 100 >= 40 * mx:
        return "40-60"
    return "<40"


def walk_stats(pos, hp, downs_at, ups_at):
    """pos: tick -> (x, z); hp: tick -> (cur, max). Steps per tick by band
    while walking, reversals (direction flips of the step along his path)."""
    ticks = sorted(pos)
    steps = collections.defaultdict(collections.Counter)
    path_dir = []
    for a, b in zip(ticks, ticks[1:]):
        if b != a + 1:
            continue
        dx, dz = pos[b][0] - pos[a][0], pos[b][1] - pos[a][1]
        n = max(abs(dx), abs(dz))
        down = any(d <= b < u for d, u in zip(downs_at, ups_at))
        if down:
            continue
        h = hp.get(a)
        if h:
            steps[band(*h)][n] += 1
        if n:
            path_dir.append((b, (dx // n if dx else 0, dz // n if dz else 0)))
    return steps, path_dir


def reversals(path_dir):
    """A reversal: a step opposite the previous one (a corner turns 90)."""
    out = []
    for (ta, da), (tb, db) in zip(path_dir, path_dir[1:]):
        if da[0] == -db[0] and da[1] == -db[1] and da != (0, 0):
            out.append(tb)
    return out


def blert(d):
    R = []
    for f in sorted(glob.glob(d + "/*-*.json")):
        ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
        base = None
        pos, hp, downs, ups, walks, uptk, drops, splats, stomps = {}, {}, [], [], [], [], [], [], []
        for e in ev:
            n = e.get("npc") or {}
            if n.get("id") == BLOAT and e["type"] in (7, 8):
                if base is None:
                    base = (e["xCoord"] - e["xCoord"] % 64, e["yCoord"] - e["yCoord"] % 64)
                pos[e["tick"]] = (e["xCoord"] - base[0], e["yCoord"] - base[1])
                hp[e["tick"]] = (n["hitpoints"] >> 16, n["hitpoints"] & 0xFFFF)
            if e["type"] == 110:
                downs.append(e["tick"])
                walks.append(e["bloatDown"]["walkTime"])
            if e["type"] == 111:
                ups.append(e["tick"])
            if e["type"] == 112:
                drops.append((e["tick"], len({(h["x"], h["y"]) for h in e["bloatHands"]})))
            if e["type"] == 113:
                splats.append(e["tick"])
            if e["type"] == 10 and (e.get("npcAttack") or {}).get("attack") == 3:
                stomps.append(e["tick"])
        if base is None:
            continue
        R.append(dict(pos=pos, hp=hp, downs=downs, ups=ups, walks=walks, drops=drops, splats=splats,
                      stomps=stomps, start=0, base=base))
    return R


def ours(dirs):
    R = []
    for d in dirs:
        for sess in sorted(glob.glob(d + "/*/")):
            path = sess + "ticklog.tsv"
            if not os.path.exists(path):
                continue
            rows = [l.rstrip("\n").split("\t") for l in open(path) if not l.startswith("ticklog")]
            base, start = None, None
            pos, hp, downs, ups, drops, splats, stomps = {}, {}, [], [], collections.Counter(), [], []
            cur, mx = None, None
            for r in rows:
                t, k = int(r[1]), r[2]
                if k == "hudbar" and r[4] == str(BLOAT) and start is None:
                    start = t
                if k == "npc_tile" and r[7] == str(BLOAT):
                    x, z = int(r[4]), int(r[5])
                    if base is None:
                        base = (x - x % 64, z - z % 64)
                    pos[t] = (x - base[0], z - base[1])
                if k == "hudbar" and r[4] == str(BLOAT):
                    try:
                        c, m = r[9].split()[1].split("/")
                        cur, mx = int(c), int(m)
                    except (IndexError, ValueError):
                        pass
                if k == "npc_anim" and r[4] == str(BLOAT) and int(r[5]) == SLEEP:
                    downs.append(t)
                if k == "map_spotanim" and int(r[4]) in FLESH:
                    drops[t] += 1
                if k == "map_spotanim" and int(r[4]) == SPLAT:
                    splats.append(t)
                if k == "hit_player" and r[8] == str(BLOAT):
                    for dn in downs:
                        if t - dn in (29, 30):
                            stomps.append(t)
                if cur is not None:
                    hp[t] = (cur, mx)
            if base is None or start is None:
                continue
            # the rise: his first step after each down
            ticks = sorted(pos)
            for dn in downs:
                nxt = [t for t in ticks if t > dn + 1]
                ups.append(nxt[0] if nxt else None)
            pos = {t - start: p for t, p in pos.items()}
            hp = {t - start: v for t, v in hp.items()}
            downs = [t - start for t in downs]
            ups = [u - start for u in ups if u is not None]
            walks = []
            prev = 0
            for dn, up in zip(downs, [0] + ups):
                walks.append(dn - (up if up else 0))
            dr = sorted((t - start, n) for t, n in drops.items())
            R.append(dict(pos=pos, hp=hp, downs=downs, ups=ups, walks=walks, drops=dr,
                          splats=sorted({t - start for t in splats}), stomps=sorted({t - start for t in stomps}),
                          start=0, base=base))
    return R


def hist(xs, cap=14):
    c = collections.Counter(xs)
    return " ".join("%s:%d" % kv for kv in sorted(c.items())[:cap])


def report(name, R):
    print("== %s: %d rooms" % (name, len(R)))
    lap = collections.Counter()
    steps = collections.defaultdict(collections.Counter)
    first_rev, rev_gap, walks1, walks_n, down_len, d2d, stomp, gap_by_band, tiles, land = ([] for _ in range(10))
    gap_by_band = collections.defaultdict(list)
    for r in R:
        for t, p in r["pos"].items():
            if t < 0:
                lap[p] += 1
        st, pdir = walk_stats({t: p for t, p in r["pos"].items() if t >= 0}, r["hp"], r["downs"],
                              r["ups"] + [10 ** 9] * (len(r["downs"]) - len(r["ups"])))
        for b, c in st.items():
            steps[b].update(c)
        rev = reversals(pdir)
        if rev:
            first_rev.append(rev[0])
            rev_gap += [b - a for a, b in zip(rev, rev[1:])]
        if r["walks"]:
            walks1.append(r["walks"][0])
            walks_n += r["walks"][1:]
        down_len += [u - d for d, u in zip(r["downs"], r["ups"])]
        d2d += [b - a for a, b in zip(r["downs"], r["downs"][1:])]
        stomp += [s - max([d for d in r["downs"] if d <= s], default=s) for s in r["stomps"]]
        for (ta, na), (tb, nb) in zip(r["drops"], r["drops"][1:]):
            h = r["hp"].get(ta)
            gap_by_band[band(*h) if h else "?"].append(tb - ta)
        tiles += [n for _, n in r["drops"]]
        land += [min([s - t for s in r["splats"] if s >= t], default=None) for t, _ in r["drops"]]
    print(" pre-fight lap tiles", len(lap))
    for b in (">=60", "40-60", "<40"):
        print(" steps a tick %-5s %s" % (b, hist([k for k, v in steps[b].items() for _ in range(v)])))
    print(" first reversal (room tick)", hist(first_rev))
    print(" ticks between reversals   ", hist(rev_gap, 20))
    print(" first down's walk          ", hist(walks1))
    print(" later downs' walk          ", hist(walks_n))
    print(" down length (to first step)", hist(down_len))
    print(" down to down              ", hist(d2d, 20))
    print(" stomp - down              ", hist(stomp))
    for b in (">=60", "40-60", "<40", "?"):
        if gap_by_band[b]:
            print(" volley gap %-5s %s" % (b, hist(gap_by_band[b])))
    print(" tiles a volley            ", hist(tiles))
    print(" drop to land              ", hist([x for x in land if x is not None]))
    return lap


if __name__ == "__main__":
    B = blert(sys.argv[1])
    O = ours(sys.argv[2:])
    lb = report("BLERT", B)
    lo = report("OURS", O)
    print("lap tiles only Blert's:", sorted(set(lb) - set(lo))[:12], "only ours:", sorted(set(lo) - set(lb))[:12])
