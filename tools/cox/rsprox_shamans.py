#!/usr/bin/env python3
"""Lizardman shamans out of RSProx transcripts: every shaman's swings, their
projectiles against the target's distance, the acid's landing and who it hit,
the jump, the spawns, the attack choice against reach, the cadence, the
sounds -- for the Chambers of Xeric shamans (7573/7574) and the surface ones
that share their attack set (molch_lizardshaman 8565 and its kin).

    python3 tools/cox/rsprox_shamans.py --section attacks FILES

Every player in the transcript is followed, not only the recorder: in a
public area (3276, the Molch temple) the other raiders' hits are most of the
data. Transcribe with PLAYER_INFO_LOCAL_PLAYER_ONLY=false (README.md).

Coordinates are printed tiles. Outside an instance that is the true tile; in
a CoX room it is the template tile, with the turned-room caveats of
rsprox_room.py -- so the room's geometry is not read here, only timing and
relative distance inside one frame (the projectile's source and target share
it).
"""

import argparse
import os
import re
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
NPC = re.compile(r"npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)")
PLAYER = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
NAMEID = re.compile(r"id=([a-z0-9_]+) \((\d+)\)")
LOCAL = re.compile(r"local player index: (\d+)")

SIZES = {"molch_lizardshaman_1": 2, "zeah_lizardshaman_spawn": 1}   # the CoX shamans are 3x3
SPAWN = "zeah_lizardshaman_spawn"
SEQS = {
    "shay_lizard_warrior_attack_ranged": "throw",     # ranged and acid share it
    "shay_lizard_warrior_attack_melee": "melee",
    "shayzien_lizard_boss_jump": "jump",
    "shayzien_lizard_boss_land": "land",
    "shayzien_lizard_boss_minion_summon": "summon",
    "shayzien_lizard_boss_tail_swipe": "tail",
    "shay_lizard_warrior_death": "death",
    "shay_lizard_warrior_defend": "defend",
}
DAMAGE_HITS = {16, 17, 5, 65, 43, 44, 1}   # everything but the blocks (12, 13) and heals (6)


def num(text, key, default=None):
    m = re.search(r"\b%s=(-?\d+)" % key, text)
    return int(m.group(1)) if m else default


def size_of(n):
    return SIZES.get(n.ids[0][2], 3)


def is_shaman(name):
    return "lizardshaman" in name and "spawn" not in name and "blocker" not in name


class Actor:
    def __init__(self, index, name):
        self.index = index
        self.name = name
        self.ids = []                  # (tick, id, name)
        self.tiles = {}                # tick -> tile at the end of the tick
        self.ev = defaultdict(list)    # tick -> [(kind, detail)]
        self.added = None
        self.deleted = None

    def tile_at(self, t):
        best = None
        for tt in sorted(self.tiles):
            if tt > t:
                break
            best = self.tiles[tt]
        return best

    def name_at(self, t):
        best = self.ids[0][2] if self.ids else self.name
        for row in self.ids:
            if row[0] <= t:
                best = row[2]
        return best


class Session:
    def __init__(self, path):
        self.path = os.path.basename(path)
        self.local = None
        self.npcs = []                 # every npc life
        self.players = {}              # index -> Actor
        self.top = defaultdict(list)   # tick -> [(kind, text, subs)]


def load(path):
    S = Session(path)
    live = {}
    tick = 0
    block = None
    actor = None
    top = None
    with open(path, encoding="utf-8", errors="replace") as h:
        for raw in h:
            line = raw.rstrip("\n")
            if S.local is None:
                m = LOCAL.search(line)
                if m:
                    S.local = m.group(1)
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                block = actor = top = None
                continue
            m = TOP.match(line)
            if m:
                block = m.group(1)
                actor = None
                top = (m.group(1), m.group(2), [])
                S.top[tick].append(top)
                continue
            m = SUB8.match(line)
            if m:
                kind, text = m.group(1), m.group(2)
                if block and block.startswith("npc_info"):
                    nm = NPC.search(text)
                    actor = None
                    if nm:
                        idx = nm.group(1)
                        a = live.get(idx)
                        if a is None or kind == "add":
                            a = Actor(idx, nm.group(2))
                            a.added = tick
                            live[idx] = a
                            S.npcs.append(a)
                        if not a.ids:
                            a.ids.append((tick, int(nm.group(3)), nm.group(2)))
                        p = (int(nm.group(4)), int(nm.group(5)))
                        nc = NEWCOORD.search(text)
                        end = (int(nc.group(1)), int(nc.group(2))) if nc else p
                        a.tiles.setdefault(tick - 1, p)
                        a.tiles[tick] = end
                        if kind in ("walk", "run", "teleport", "crawl"):
                            a.ev[tick].append((kind, "%s->%s" % (p, end)))
                        elif kind == "del":
                            a.deleted = tick
                            a.ev[tick].append(("del", str(p)))
                            del live[idx]
                        elif kind == "add":
                            a.ev[tick].append(("add", str(p)))
                        actor = a
                    continue
                if block and block.startswith("player_info"):
                    pm = PLAYER.search(text)
                    actor = None
                    if pm:
                        idx = pm.group(1)
                        a = S.players.get(idx)
                        if a is None:
                            a = Actor(idx, pm.group(2))
                            S.players[idx] = a
                        nc = NEWCOORD.search(text)
                        p = (int(pm.group(3)), int(pm.group(4)))
                        a.tiles.setdefault(tick - 1, p)
                        a.tiles[tick] = (int(nc.group(1)), int(nc.group(2))) if nc else p
                        if kind == "del":
                            a.ev[tick].append(("del", ""))
                        actor = a
                    continue
                if top is not None:
                    top[2].append(line.strip())
                continue
            m = SUB12.match(line)
            if m and actor is not None:
                kind, text = m.group(1), m.group(2)
                if kind == "hit":
                    actor.ev[tick].append(("hit", (num(text, "id"), num(text, "value"))))
                elif kind == "sequence":
                    n = NAMEID.search(text)
                    actor.ev[tick].append(("seq", n.group(1) if n else text))
                elif kind == "spotanim":
                    n = NAMEID.search(text)
                    actor.ev[tick].append(("spotanim", n.group(1) if n else text))
                elif kind == "transformation":
                    n = NAMEID.search(text)
                    if n:
                        actor.ids.append((tick, int(n.group(2)), n.group(1)))
                elif kind == "headbar":
                    actor.ev[tick].append(("headbar", num(text, "fill")))
                elif kind in ("face_pathingentity", "face_angle"):
                    actor.ev[tick].append((kind, text))
    return S


# ---------------------------------------------------------------- geometry

def gap(sw, size, p):
    """Chebyshev gap from a size x size footprint at sw to tile p (0 under it)."""
    dx = max(sw[0] - p[0], 0, p[0] - (sw[0] + size - 1))
    dz = max(sw[1] - p[1], 0, p[1] - (sw[1] + size - 1))
    return max(dx, dz)


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def shamans(S):
    return [n for n in S.npcs if n.ids and is_shaman(n.ids[0][2])]


def spawns(S):
    return [n for n in S.npcs if n.ids and n.ids[0][2] == SPAWN]


def projectiles(S, t):
    out = []
    for kind, text, subs in S.top.get(t, []):
        if not kind.startswith("map_projanim"):
            continue
        n = NAMEID.search(text)
        src = dst = None
        target = None
        for s in subs:
            c = COORD.search(s)
            if s.startswith("[source]") and c:
                src = (int(c.group(1)), int(c.group(2)))
            if s.startswith("[target]") and c:
                dst = (int(c.group(1)), int(c.group(2)))
                pm = re.search(r"player=\(index=(\d+)", s)
                nm = re.search(r"npc=\(index=(\d+)", s)
                target = ("player", pm.group(1)) if pm else (("npc", nm.group(1)) if nm else None)
        out.append({"name": n.group(1) if n else "?", "start": num(text, "starttime"), "end": num(text, "endtime"),
                    "angle": num(text, "angle"), "progress": num(text, "progress"),
                    "h0": num(text, "startheight"), "h1": num(text, "endheight"),
                    "src": src, "dst": dst, "target": target})
    return out


def map_anims(S, t, name=None):
    out = []
    for kind, text, subs in S.top.get(t, []):
        if kind == "map_anim":
            n = NAMEID.search(text)
            c = COORD.search(text)
            if n and (name is None or n.group(1) == name) and c:
                out.append((n.group(1), (int(c.group(1)), int(c.group(2))), num(text, "delay", 0), num(text, "height", 0)))
    return out


def sounds(S, t):
    out = []
    for kind, text, subs in S.top.get(t, []):
        if kind == "sound_area":
            c = COORD.search(text)
            out.append(("area", num(text, "id"), num(text, "delay", 0), num(text, "range"),
                        (int(c.group(1)), int(c.group(2))) if c else None))
        elif kind == "synth_sound":
            out.append(("synth", num(text, "id"), num(text, "delay", 0), None, None))
    return out


def seqs(a, t):
    return [d for k, d in a.ev.get(t, []) if k == "seq"]


def hits(a, t):
    return [d for k, d in a.ev.get(t, []) if k == "hit"]


def from_shaman(n, t, p):
    """Is projectile p thrown by shaman n on tick t (source inside its footprint)?"""
    sw = n.tile_at(t)
    return p["src"] is not None and sw is not None and gap(sw, size_of(n), p["src"]) <= 1


def swings(S):
    """Every shaman swing: [{n, t, kind, proj, dst, gap, src}]. A thrown
    projectile goes to the throwing shaman nearest its source (gap <= 1);
    a tie between two throwers is left unattributed (kind "throw")."""
    rows = []
    by_tick = defaultdict(list)
    for n in shamans(S):
        for t in sorted(n.ev):
            for s in seqs(n, t):
                kind = SEQS.get(s)
                if kind is None or kind in ("defend", "death"):
                    continue
                row = {"n": n, "t": t, "kind": kind, "seq": s, "proj": None, "dst": None, "gap": None, "src": None}
                rows.append(row)
                if kind == "throw":
                    by_tick[t].append(row)
    for t, throws in by_tick.items():
        for p in projectiles(S, t):
            if p["src"] is None or p["name"] not in ("lizardman_spit", "lizardshaman_spit_acid"):
                continue
            cand = sorted(((gap(r["n"].tile_at(t), size_of(r["n"]), p["src"]), i) for i, r in enumerate(throws)
                           if r["proj"] is None and r["n"].tile_at(t)))
            if not cand or cand[0][0] > 1 or (len(cand) > 1 and cand[1][0] == cand[0][0]):
                continue
            r = throws[cand[0][1]]
            r["proj"] = p
            r["kind"] = "acid" if p["name"] == "lizardshaman_spit_acid" else "ranged"
            r["dst"] = p["dst"]
            r["src"] = p["src"]
            r["gap"] = gap(r["n"].tile_at(t), size_of(r["n"]), p["dst"])
    rows.sort(key=lambda r: r["t"])
    return rows


def face_target(S, n, t):
    """The player the shaman faces at or before t (its face_pathingentity), if any."""
    last = None
    for tt in sorted(n.ev):
        if tt > t:
            break
        for k, d in n.ev[tt]:
            if k == "face_pathingentity" and "player" in d:
                m = re.search(r"index=(\d+)", d)
                last = m.group(1) if m else last
    return last


# ---------------------------------------------------------------- sections

def section_inventory(S):
    """What each shaman, spawn and the area send."""
    sq = Counter()
    for n in shamans(S) + spawns(S):
        for t, evs in n.ev.items():
            for k, d in evs:
                if k == "seq":
                    sq[(n.ids[0][2], d)] += 1
    for k, v in sorted(sq.items()):
        print("  seq %-26s %-40s x%d" % (k + (v,)))
    pj = Counter()
    for t in S.top:
        for p in projectiles(S, t):
            if "lizard" in p["name"]:
                pj[(p["name"], p["start"], p["angle"], p["progress"], p["h0"], p["h1"])] += 1
    for k, v in sorted(pj.items(), key=lambda kv: -kv[1]):
        print("  projectile %-24s start %s angle %s progress %s height %s->%s  x%d" % (k + (v,)))
    ma = Counter()
    for t in S.top:
        for nm, c, d, hgt in map_anims(S, t):
            if "lizard" in nm:
                ma[(nm, d, hgt)] += 1
    for k, v in sorted(ma.items()):
        print("  map_anim %-28s delay %s height %s  x%d" % (k + (v,)))
    print("  shamans %d, spawns %d, players seen %d" % (len(shamans(S)), len(spawns(S)), len(S.players)))


def section_attacks(S):
    """Each swing, with its projectile against the target's gap."""
    rows = swings(S)
    kinds = Counter(r["kind"] for r in rows)
    print("  swings:", dict(kinds))
    tab = defaultdict(Counter)
    for r in rows:
        p = r["proj"]
        if p is None or r["gap"] is None:
            continue
        tab[(r["kind"], r["gap"])][(p["start"], p["end"])] += 1
    for (kind, g), c in sorted(tab.items()):
        print("  %-6s gap %2d  %s" % (kind, g, "  ".join("%s->%s x%d" % (a, b, v) for (a, b), v in sorted(c.items()))))
    # source tile against the footprint
    rel = Counter()
    for r in rows:
        if r["src"] is not None:
            sw = r["n"].tile_at(r["t"])
            rel[(r["src"][0] - sw[0], r["src"][1] - sw[1])] += 1
    print("  source - sw:", dict(rel))


def section_cadence(S):
    """Ticks between one shaman's swings, by the swing before."""
    tab = defaultdict(Counter)
    for n in shamans(S):
        rows = [r for r in swings(S) if r["n"] is n and r["kind"] not in ("land",)]
        for a, b in zip(rows, rows[1:]):
            tab[a["kind"]][b["t"] - a["t"]] += 1
    for k, c in sorted(tab.items()):
        print("  after %-7s %s" % (k, "  ".join("+%d x%d" % kv for kv in sorted(c.items()))))


def section_acid(S):
    """Each acid spit: its flight, the splash tick and tile, and every player's
    hits on the splash tick against their distance from the target tile."""
    tab = defaultdict(Counter)
    near = defaultdict(list)
    for r in swings(S):
        if r["kind"] != "acid":
            continue
        p = r["proj"]
        t = r["t"]
        land = None
        for dt in range(0, 8):
            if any(c == p["dst"] for nm, c, d, h in map_anims(S, t + dt, "lizardshaman_acid_splash")):
                land = dt
                break
        tab[(r["gap"], p["end"])][land] += 1
        if land is None:
            continue
        for a in S.players.values():
            tile = a.tile_at(t + land - 1)
            if tile is None:
                continue
            d = cheb(tile, p["dst"])
            if d > 4:
                continue
            hs = [h for h in hits(a, t + land) if h[0] in DAMAGE_HITS or h[0] in (12, 13)]
            near[d].append((a.index == S.local, hs, tile, t))
    print("  gap  endtime  landing (ticks after the swing)")
    for (g, e), c in sorted(tab.items(), key=lambda kv: (kv[0][0] or 0, kv[0][1] or 0)):
        print("  %3s  %6s   %s" % (g, e, "  ".join("+%s x%d" % kv for kv in sorted(c.items(), key=lambda kv: (kv[0] is None, kv[0] or 0)))))
    print("  players by distance from the target tile (read at the end of the tick before the splash)")
    for d in sorted(near):
        rows = near[d]
        got = [r for r in rows if r[1]]
        vals = sorted(v for r in got for i, v in r[1])
        ids = Counter(i for r in got for i, v in r[1])
        print("  d %d: %d players, %d hit; values %s ids %s" % (d, len(rows), len(got), vals[:30], dict(ids)))


def section_ranged(S):
    """Each ranged spit: its flight against the gap, and the target's hitsplat."""
    tab = defaultdict(Counter)
    vals = []
    for r in swings(S):
        if r["kind"] != "ranged":
            continue
        p = r["proj"]
        tgt = p["target"]
        if not tgt or tgt[0] != "player":
            continue
        a = S.players.get(tgt[1])
        if a is None:
            continue
        land = None
        for dt in range(0, 7):
            if hits(a, r["t"] + dt):
                land = dt
                break
        tab[(r["gap"], p["end"])][land] += 1
        if land is not None:
            vals.extend(hits(a, r["t"] + land))
    print("  gap  endtime  hitsplat (ticks after the swing)")
    for (g, e), c in sorted(tab.items(), key=lambda kv: (kv[0][0] or 0, kv[0][1] or 0)):
        print("  %3s  %6s   %s" % (g, e, "  ".join("+%s x%d" % kv for kv in sorted(c.items(), key=lambda kv: (kv[0] is None, kv[0] or 0)))))
    print("  splats:", Counter(i for i, v in vals), "max", max([v for i, v in vals] or [0]))


def section_choice(S):
    """The attack picked against the target's gap at the swing (end of the tick before)."""
    tab = defaultdict(Counter)
    for r in swings(S):
        n, t = r["n"], r["t"]
        if r["kind"] in ("land", "tail", "summon"):
            continue
        tgt = face_target(S, n, t)
        g = None
        if r["dst"] is not None:
            g = r["gap"]
        elif tgt and tgt in S.players:
            tile = S.players[tgt].tile_at(t - 1)
            sw = n.tile_at(t - 1)
            if tile and sw:
                g = gap(sw, size_of(n), tile)
        tab["adjacent" if g is not None and g <= 1 else ("apart" if g is not None else "unknown")][r["kind"]] += 1
    for k, c in sorted(tab.items()):
        tot = sum(c.values())
        print("  %-9s n=%-4d %s" % (k, tot, "  ".join("%s %d (%.2f)" % (kk, v, v / tot) for kk, v in sorted(c.items()))))


def section_jump(S):
    """Each jump: the tiles, the land, the teleport, and every player's hits."""
    for r in swings(S):
        if r["kind"] != "jump":
            continue
        n, t = r["n"], r["t"]
        line = []
        for dt in range(0, 8):
            evs = [("%s" % SEQS.get(d, d)) for d in seqs(n, t + dt)]
            mv = [d for k, d in n.ev.get(t + dt, []) if k in ("walk", "run", "teleport", "crawl")]
            if evs or mv:
                line.append("+%d %s%s" % (dt, ",".join(evs), (" " + ",".join(mv)) if mv else ""))
        sw0 = n.tile_at(t - 1)
        land_t = None
        for dt in range(1, 8):
            if "shayzien_lizard_boss_land" in seqs(n, t + dt):
                land_t = t + dt
                break
        sw1 = n.tile_at(land_t) if land_t else None
        tgt = face_target(S, n, t)
        tile = S.players[tgt].tile_at(t - 1) if tgt in S.players else None
        hit_rows = []
        if land_t:
            for a in S.players.values():
                for dt in range(-1, 3):
                    for h in hits(a, land_t + dt):
                        tile_a = a.tile_at(land_t - 1)
                        hit_rows.append("%s%+d %s d%s" % (a.name[:8], dt, h, gap(sw1, size_of(n), tile_a) if tile_a and sw1 else "?"))
        print("  t%-6d %s  from %s to %s; target tile %s (centre %s)  %s | %s" % (
            t, n.index, sw0, sw1, tile, (sw1[0] + 1, sw1[1] + 1) if sw1 else None, "; ".join(line), "; ".join(hit_rows)))


def section_spawns(S):
    """Each summon: the spawns' tiles against the target, their walk, the
    hands-to-head and burst ticks, and the hits around each burst."""
    sp = spawns(S)
    for n in shamans(S):
        for t in sorted(n.ev):
            if "shayzien_lizard_boss_minion_summon" not in seqs(n, t):
                continue
            tgt = face_target(S, n, t)
            tile = S.players[tgt].tile_at(t - 1) if tgt in S.players else None
            mine = [s for s in sp if s.added is not None and t <= s.added <= t + 2]
            print("  t%-6d shaman %s target %s at %s; %d spawns" % (t, n.index, tgt, tile, len(mine)))
            for s in mine:
                a = s.tiles.get(s.added)
                warn = next((tt for tt in sorted(s.ev) if "shayzien_lizard_boss_hands_to_head" in seqs(s, tt)), None)
                rise = next((tt for tt in sorted(s.ev) if "shayzien_lizard_boss_rise" in seqs(s, tt)), None)
                steps = sum(1 for tt in s.ev for k, d in s.ev[tt] if k in ("walk", "run"))
                last = s.tile_at(warn) if warn else None
                burst = None
                if warn:
                    for dt in range(0, 8):
                        if any(c == s.tile_at(warn + dt) or cheb(c, s.tile_at(warn + dt)) <= 1
                               for nm, c, d, h in map_anims(S, warn + dt, "lizardshaman_spawn_explode")):
                            burst = warn + dt
                            break
                print("     spawn %s add +%d at %s (d %s from target) rise %s; %d steps; warn +%s at %s; burst +%s; del +%s" % (
                    s.index, s.added - t, a, cheb(a, tile) if a and tile else "?",
                    None if rise is None else "+%d" % (rise - t), steps,
                    None if warn is None else warn - t, last, None if burst is None else burst - t,
                    None if s.deleted is None else s.deleted - t))


def section_bursts(S):
    """Damage from each spawn burst against distance (Chebyshev, Euclidean)."""
    import math
    tab = defaultdict(list)
    for s in spawns(S):
        for t in sorted(s.ev):
            if not any(c == s.tile_at(t) for nm, c, d, h in map_anims(S, t, "lizardshaman_spawn_explode")):
                continue
            at = s.tile_at(t)
            for dt in range(0, 3):
                for a in S.players.values():
                    tile = a.tile_at(t + dt - 1)
                    if tile is None or cheb(tile, at) > 3:
                        continue
                    hs = hits(a, t + dt)
                    e = math.hypot(tile[0] - at[0], tile[1] - at[1])
                    tab[(dt, cheb(tile, at))].append((round(e, 2), hs))
    for k in sorted(tab):
        rows = tab[k]
        print("  burst%+d d %d: n=%d  %s" % (k + (len(rows), " ".join("%s:%s" % (e, ",".join("%s/%s" % h for h in hs) or "-") for e, hs in rows[:24]))))


def section_sounds(S):
    """The sounds on the tick of each shaman event."""
    tab = defaultdict(Counter)
    n_ev = Counter()
    for r in swings(S):
        key = r["kind"]
        n_ev[key] += 1
        for snd in sounds(S, r["t"]):
            tab[key][snd[:4]] += 1
    for key in sorted(n_ev):
        common = [(k, v) for k, v in tab[key].most_common() if v >= max(2, n_ev[key] // 3)]
        print("  %-7s n=%-4d %s" % (key, n_ev[key], "; ".join("%s %s delay %s range %s x%d" % (k + (v,)) for k, v in common) or "-"))


SECTIONS = ["inventory", "attacks", "cadence", "choice", "ranged", "acid", "jump", "spawns", "bursts", "sounds"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--section", default=None)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    for f in args.files:
        S = load(f)
        print("== %s: %d shamans, %d spawns" % (S.path, len(shamans(S)), len(spawns(S))))
        for name in SECTIONS:
            if args.section and name != args.section:
                continue
            print("-- " + name)
            globals()["section_" + name](S)


if __name__ == "__main__":
    main()
