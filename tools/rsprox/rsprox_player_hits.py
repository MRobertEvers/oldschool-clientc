#!/usr/bin/env python3
"""Every splat on a player, with what caused it and the prayer it met.

A splat on a player rides in that player's player-info block (`[hit] id=.., value=..` under
`[player_info]` -> `[walk|run|idle|...] player=(index=N, ...)`). Its type is the VIEWER's: on the
recorder 16 hitsplat_damage_me, 12 hitsplat_block_me (a zero: a miss, a 0 roll or a fully
prayed hit), 43 hitsplat_damage_max_me, 65 hitsplat_poison_me, 18 hitsplat_shield_me.

Each splat is attributed, in this order:
  proj   a projectile aimed at the player (`[target] ... player=(index=N`) whose landing tick,
         throw + (endtime + 15) // 30, is the splat's tick or one either side (exact preferred)
  swing  an npc attack sequence (a name with "attack" in it; any sequence on a bare-id
         transcript) on the splat's tick or the one before, by an npc whose last face was the
         player; tried after a projectile landing on the splat's own tick and before one
         landing a tick either side
  tile   a map spotanim within 2 tiles of the player that tick or the one before, nearest
         first, with its distance: "tile <name> d1" (falling crystals, bombs, pools)
  dot    a poison / venom / acid splat (2, 65, 66, 68): damage over time, claims nothing
  none   nothing above: recoil, typeless damage, or a cause this cannot see
A source claims one splat. Protection prayers come from varbit 4101 prayer_allactive, which
every revision sends: bit 12 Protect from Magic, 13 Missiles, 14 Melee.

Usage:
  rsprox_player_hits.py <txt>...                    one row per splat on the recorder
  rsprox_player_hits.py <txt>... --summary          per source x prayer: n, zeros, max, mean
  rsprox_player_hits.py <txt> --player NAME         another player (needs a --all-players transcript)
  rsprox_player_hits.py <txt> --from T --to T       a window
  rsprox_player_hits.py --ours <ticklog.tsv> [--summary]   our tick log, the same rows
"""
import argparse
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TICK = re.compile(r"\[(\d+)\]\s*$")
ME = re.compile(r"local player index: (\d+)")
NAMED = r"(?:(\S+) \()?(-?\d+)\)?"
HIT = re.compile(r"\[hit\] id=" + NAMED + r", value=(-?\d+)")
PROJ = re.compile(r"\[(?:map_)?proj\w*\] id=" + NAMED + r", starttime=(-?\d+), endtime=(-?\d+)")
SEQ = re.compile(r"\[sequence\] id=" + NAMED)
NPC = re.compile(r"npc=\(index=(\d+), id=" + NAMED + r", coord=\((\d+), (\d+), (\d+)\)\)")
PLAYER = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
VB4101 = re.compile(r"\[varbit\] id=(?:\S+ \()?4101\)?, old=-?\d+, new=(-?\d+)")
HPSTAT = re.compile(r"\[update_stat\w*\] stat=(?:hitpoints|3), level=(\d+)")
MAPANIM = re.compile(r"\[map_anim\] id=" + NAMED + r".*coord=\((\d+), (\d+), (\d+)\)")
SPLATS = {11: "11", 12: "block", 13: "block_other", 16: "dmg", 17: "dmg_other", 18: "shield",
          43: "max", 53: "53", 65: "poison", 66: "poison", 68: "poison", 2: "poison", 28: "dmg", 1: "block"}
# Damage over time (poison, venom, acid): its own cause; it claims no projectile, swing or tile.
DOT = {2, 65, 66, 68}


def prayers(bits):
    out = ""
    if bits >> 12 & 1:
        out += "Mag"
    if bits >> 13 & 1:
        out += "Rng"
    if bits >> 14 & 1:
        out += "Mel"
    return out or "-"


def label(m, i=1):
    return m.group(i) or m.group(i + 1)


def attribute(t, projs, swings):
    """A projectile landing on the splat's tick, else a swing on it or the tick before, else a
    projectile landing a tick either side. A source claims one splat."""
    for p in projs:
        if not p[3] and p[0] == t:
            p[3] = True
            return "proj %s" % p[2]
    for sw in swings:
        if not sw[2] and t - 1 <= sw[0] <= t:
            sw[2] = True
            return "swing %s" % sw[1]
    for want in (-1, 1):
        for p in projs:
            if not p[3] and p[0] == t + want:
                p[3] = True
                return "proj %s" % p[2]
    return None


def near_tile(t, tiles, here, reach=2):
    """A map spotanim within `reach` tiles (Chebyshev) of the player's tile, on the splat's tick
    or the one before, nearest first: "tile <name> d<distance>"."""
    if here is None:
        return None
    best = None
    for tl in tiles:
        if tl[4] or not (t - 1 <= tl[0] <= t):
            continue
        d = max(abs(tl[2] - here[0]), abs(tl[3] - here[1]))
        if d <= reach and (best is None or d < best[0]):
            best = (d, tl)
    if best is None:
        return None
    best[1][4] = True
    return "tile %s d%d" % (best[1][1], best[0])


def read_transcript(path, who):
    """Rows: (tick, splat, value, prayers, hp_after, source)."""
    me = None
    tick = 0
    sect = None
    actor = None          # ("me"|"npc"|None, npc index, npc name, seqs this tick)
    pray = 0
    hp = None
    mytile = None
    npc_face = {}         # npc index -> True while its last face is the player
    npc_name = {}
    projs = []            # [land, throw, name, claimed]
    swings = []           # [tick, "npc seq", claimed]
    tiles = []            # [tick, name, x, z, claimed]
    hits = []             # [tick, splat, value, prayers]
    hp_at = {}
    tile_hist = {}
    pending = None
    for line in open(path, errors="replace"):
        if me is None and who is None:
            m = ME.match(line)
            if m:
                me = m.group(1)
            continue
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        s = line.strip()
        depth = len(line) - len(line.lstrip(" "))
        if depth == 4:
            sect = s.split("]")[0] + "]"
            pending = None
            m = VB4101.search(s)
            if m:
                pray = int(m.group(1))
            m = HPSTAT.search(s)
            if m:
                hp = int(m.group(1))
                hp_at[tick] = hp
            m = PROJ.search(s)
            if m:
                pending = (tick, label(m), int(m.group(4)))
            m = MAPANIM.search(s)
            if m:
                tiles.append([tick, label(m), int(m.group(3)), int(m.group(4)), False])
            continue
        if depth == 8 and pending and s.startswith("[target]"):
            m = PLAYER.search(s)
            if m and (m.group(1) == me or m.group(2) == who):
                t0, name, end = pending
                projs.append([t0 + (end + 15) // 30, t0, name, False])
            pending = None
            continue
        if depth == 8:
            actor = None
            if sect == "[player_info]":
                m = PLAYER.search(s)
                if m and (m.group(1) == me or m.group(2) == who):
                    if who and me is None:
                        me = m.group(1)
                    actor = ("me",)
                    n = NEWCOORD.search(s)
                    mytile = (int(n.group(1)), int(n.group(2))) if n else (int(m.group(3)), int(m.group(4)))
                    tile_hist[tick] = mytile
            elif sect and sect.startswith("[npc_info"):
                m = NPC.search(s)
                if m:
                    npc_name[m.group(1)] = label(m, 2)
                    actor = ("npc", m.group(1))
            continue
        if actor is None:
            continue
        if actor[0] == "me":
            m = HIT.search(s)
            if m:
                hits.append([tick, int(m.group(2)), int(m.group(3)), prayers(pray)])
        else:
            if s.startswith("[facing]"):
                npc_face[actor[1]] = bool(PLAYER.search(s)) and (f"index={me}," in s)
            m = SEQ.search(s)
            if m:
                name = label(m)
                if (m.group(1) is None or "attack" in name) and npc_face.get(actor[1]):
                    swings.append([tick, "%s %s" % (npc_name.get(actor[1]), name), False])
    def tile_at(t):
        return next((tile_hist[k] for k in range(t, t - 50, -1) if k in tile_hist), None)

    rows = []
    for t, splat, value, pr in hits:
        if splat in DOT:
            hpa = next((hp_at[k] for k in range(t, t + 2) if k in hp_at), None)
            rows.append((t, SPLATS[splat], value, pr, hpa, "dot"))
            continue
        src = attribute(t, projs, swings)
        if not src:
            src = near_tile(t, tiles, tile_at(t))
        hpa = next((hp_at[k] for k in range(t, t + 2) if k in hp_at), None)
        rows.append((t, SPLATS.get(splat, str(splat)), value, pr, hpa, src or "none"))
    return rows


def compack(name):
    out = {}
    path = os.path.join(ROOT, "OSRS-Content/osrs-content/configs", name)
    if os.path.exists(path):
        for line in open(path, errors="replace"):
            k, _, v = line.rstrip("\n").partition("=")
            if k.isdigit():
                out[int(k)] = v
    return out


def read_ours(path, pid=0):
    """Our tick log's hit_player rows for player `pid`, attributed the same way. A projectile
    row's target is the wire's: -(pid + 1) a player, slot + 1 an npc, 0 a tile (kept when it
    lands on the player's tile). Coords are packed level << 28 | x << 14 | z."""
    spot = compack("all.spotanim.compack")
    npcs = compack("all.npc.compack")
    seqs = compack("all.seq.compack")
    pray = {}
    tile = {}
    shots, projs, swings, hits, tiles = [], [], [], [], []
    for line in open(path, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 9 or not f[1].isdigit():
            continue
        tick, kind, a = int(f[1]), f[2], f[3:]
        if kind == "raider" and int(a[0]) == pid:
            pray[tick] = int(a[3])
        elif kind == "player_tile" and int(a[0]) == pid:
            tile[tick] = (int(a[1]), int(a[2]))
        elif kind == "map_spotanim":
            c = int(a[0])
            tiles.append([tick, spot.get(int(a[1]), a[1]), c >> 14 & 0x3fff, c & 0x3fff, False])
        elif kind == "projectile":
            target, dst = int(a[2]), int(a[1])
            land = tick + (int(a[5]) + 15) // 30
            if target == -(pid + 1):
                projs.append([land, tick, spot.get(int(a[3]), a[3]), False])
            elif target == 0:
                shots.append((land, tick, spot.get(int(a[3]), a[3]), (dst >> 14 & 0x3fff, dst & 0x3fff)))
        elif kind == "npc_anim":
            name = seqs.get(int(a[2]), a[2])
            if "attack" in name:
                swings.append([tick, "%s %s" % (npcs.get(int(a[1]), a[1]), name), False])
        elif kind == "hit_player" and int(a[0]) == pid:
            hits.append([tick, int(a[3]), int(a[2]), int(a[1]), int(a[5]) if len(a) > 5 and a[5] else -1])
    for land, t0, name, at in shots:
        here = next((tile[k] for k in range(land, land - 3, -1) if k in tile), None)
        if here == at:
            projs.append([land, t0, name, False])
    rows = []
    for t, splat, value, slot, ntype in hits:
        if splat in DOT:
            rows.append((t, "poison", value, prayers(next((pray[k] for k in range(t, t - 3, -1) if k in pray), 0)), None, "dot"))
            continue
        bits = next((pray[k] for k in range(t, t - 3, -1) if k in pray), 0)
        src = "swing %s" % npcs.get(ntype, ntype) if slot >= 0 else attribute(t, projs, swings)
        if not src:
            src = near_tile(t, tiles, next((tile[k] for k in range(t, t - 50, -1) if k in tile), None))
        rows.append((t, "dmg" if value else "block", value, prayers(bits), None, src or "none"))
    return rows


def summary(rows):
    g = defaultdict(list)
    for t, splat, value, pr, hp, src in rows:
        if splat not in ("dmg", "block", "max"):
            src = "%s splat %s" % (src, splat)
        g[(src, pr)].append(value)
    for (src, pr), vals in sorted(g.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        z = sum(1 for v in vals if v == 0)
        nz = [v for v in vals if v]
        print("%-60s %-9s n=%4d zeros=%4d (%3d%%) max=%4d mean_nonzero=%6.1f" % (
            src[:60], pr, len(vals), z, 100 * z // len(vals), max(vals),
            sum(nz) / len(nz) if nz else 0.0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("txt", nargs="*")
    ap.add_argument("--ours")
    ap.add_argument("--player")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--from", dest="t0", type=int, default=0)
    ap.add_argument("--to", dest="t1", type=int, default=1 << 30)
    args = ap.parse_args()
    sets = [(args.ours, read_ours(args.ours))] if args.ours else [(p, read_transcript(p, args.player)) for p in args.txt]
    if not sets:
        ap.error("a transcript or --ours")
    allrows = []
    for path, rows in sets:
        rows = [r for r in rows if args.t0 <= r[0] <= args.t1]
        allrows += rows
        if not args.summary:
            print("# %s: %d splats" % (os.path.basename(path), len(rows)))
            for t, splat, value, pr, hp, src in rows:
                print("[%d] %-6s %3d  pray %-9s hp %-4s %s" % (t, splat, value, pr, "-" if hp is None else hp, src))
    if args.summary:
        summary(allrows)


if __name__ == "__main__":
    sys.exit(main())
