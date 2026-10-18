#!/usr/bin/env python3
"""One Chambers of Xeric room out of an RSProx transcript, in TEMPLATE room-local
tiles: every npc's life (types, tiles, seqs, hits, faces), the room's top-level
packets (locs, projectiles, map graphics, sounds, messages), and the local
raider (tile, hitsplats, stats, prayer varbits).

COORDINATES. The transcriber maps every instance tile back to the template tile
it was copied from, chunk by chunk (8x8). Two things go wrong in a turned room:

  1. In a room turned an ODD number of quarter turns, it rotates each tile's
     offset inside its chunk the wrong way, which MIRRORS the offset on both
     axes (l -> 7 - l): two adjacent tiles across a chunk edge print 15 apart,
     a 2-tile run prints as 14. `flip` undoes it (detected from the local
     raider's runs: any printed step of 7+ tiles inside the room).
  2. A size-N npc's printed tile is its INSTANCE south-west tile mapped back,
     which is another corner of its template footprint; `corner` says which
     (read off a fixed npc whose template tile is known, see the callers).

Even rooms (180 degrees, Tekton's in 820/3285) only have the second.
Instance directions (step labels, face angles) are not mapped at all.

    from rsprox_room import load_room
    R = load_room(path, npc_ids={7530, ...})
"""

import os
import re
from collections import defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
NPC = re.compile(r"npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\), angle=(-?\d+)\)")
PLAYER = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
NAMEID = re.compile(r"id=([a-z0-9_]+) \((\d+)\)")
LOCAL = re.compile(r"local player index: (\d+)")
STAT = re.compile(r"\[update_stat_v2\] stat=(\w+), level=(\d+)")


def num(text, key, default=None):
    m = re.search(r"\b" + key + r"=(-?\d+)", text)
    return int(m.group(1)) if m else default


def name_of(text):
    m = NAMEID.search(text)
    return m.group(1) if m else ""


def id_of(text):
    m = NAMEID.search(text)
    return int(m.group(2)) if m else None


def coord_of(text):
    m = COORD.search(text)
    return (int(m.group(1)), int(m.group(2))) if m else None


class Npc:
    def __init__(self, index, tick):
        self.index = index
        self.added = tick
        self.deleted = None
        self.types = []          # (tick, id, name)
        self.tiles = {}          # tick -> printed (x, z) at the end of the tick
        self.ev = defaultdict(list)   # tick -> [(kind, detail)]

    def type_at(self, t):
        best = self.types[0] if self.types else None
        for row in self.types:
            if row[0] <= t:
                best = row
        return best

    def tile_at(self, t):
        best = None
        for tt in sorted(self.tiles):
            if tt <= t:
                best = self.tiles[tt]
        return best


class Room:
    def __init__(self, path):
        self.path = os.path.basename(path)
        self.local = None
        self.first = None
        self.last = None
        self.npcs = {}           # index -> Npc (the live one)
        self.lives = []          # every Npc, in add order
        self.top = defaultdict(list)     # tick -> [(kind, text, subs)]
        self.me = {}             # tick -> printed tile at the end of tick
        self.my_ev = defaultdict(list)   # tick -> [(kind, detail)]
        self.stats = defaultdict(list)   # tick -> [(stat, level)]
        self.flip = False
        self.origin = None       # the room cell's printed origin (multiple of 32)
        self.turn = None         # M: an instance direction (dx, dz) -> its template direction

    # ---------------------------------------------------------------- tiles
    def fix(self, xz):
        """A printed tile -> its true template tile (absolute)."""
        if xz is None:
            return None
        x, z = xz
        if not self.flip:
            return (x, z)
        return ((x & ~7) + 7 - (x & 7), (z & ~7) + 7 - (z & 7))

    def local_of(self, xz):
        """A printed tile -> template room-local (lx, lz)."""
        t = self.fix(xz)
        if t is None:
            return None
        return (t[0] - self.origin[0], t[1] - self.origin[1])

    def dir_of(self, d):
        """An instance direction (dx, dz) -> the template's."""
        a, b, c, e = self.turn
        return (a * d[0] + b * d[1], c * d[0] + e * d[1])

    def sw_of(self, xz, size):
        """A size-N npc's printed tile -> its template room-local south-west tile."""
        lx, lz = self.local_of(xz)
        cx, cz = self.dir_of((-1, -1))          # the template corner its instance SW is
        return (lx - (size - 1 if cx > 0 else 0), lz - (size - 1 if cz > 0 else 0))

    def face_of(self, angle):
        """An instance face angle (0 S, 512 W, 1024 N, 1536 E) -> the template's."""
        import math
        a = angle * 2 * math.pi / 2048
        d = (-math.sin(a), -math.cos(a))        # S = (0,-1), W = (-1,0)
        tx, tz = self.dir_of(d)
        return int(round(math.atan2(-tx, -tz) * 2048 / (2 * math.pi))) % 2048

    def me_at(self, t):
        best = None
        for tt in sorted(self.me):
            if tt <= t:
                best = self.me[tt]
        return best


def load_room(path, npc_ids, pad=40, gap=1500, anchor=None):
    """The first window of the transcript holding any of `npc_ids` (+-pad ticks).

    anchor = (npc_id, size, {cell_origin_x: (lx, lz)}): a fixed npc and its
    template south-west tile per room cell, to read the turn from when no npc
    in the room takes a labelled step.
    """
    # pass 1: the window
    first = last = None
    tick = 0
    with open(path, encoding="utf-8", errors="replace") as h:
        for line in h:
            m = TICK.match(line.rstrip("\n"))
            if m:
                tick = int(m.group(1))
                continue
            if "npc=(index=" in line:
                n = NPC.search(line)
                if n and int(n.group(3)) in npc_ids:
                    if first is None:
                        first = tick
                    elif last is not None and tick > last + gap:
                        break
                    last = tick
    assert first is not None, "%s: no npc of %s" % (path, sorted(npc_ids))
    R = Room(path)
    R.first, R.last = first, last
    lo, hi = first - pad, last + pad
    # pass 2: the window's packets
    tick = 0
    block = None
    actor = None
    top = None
    me_tile = None
    with open(path, encoding="utf-8", errors="replace") as h:
        for raw in h:
            line = raw.rstrip("\n")
            if R.local is None:
                m = LOCAL.search(line)
                if m:
                    R.local = m.group(1)
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                top = actor = None
                if lo <= tick <= hi and me_tile is not None:
                    R.me.setdefault(tick, me_tile)
                if tick > hi:
                    break
                continue
            if tick < lo:
                if "player=(index=" in line and R.local:
                    pm = PLAYER.search(line)
                    if pm and pm.group(1) == R.local:
                        nc = NEWCOORD.search(line)
                        me_tile = (int(nc.group(1)), int(nc.group(2))) if nc else (int(pm.group(3)), int(pm.group(4)))
                continue
            m = TOP.match(line)
            if m:
                block = m.group(1)
                actor = None
                top = (m.group(1), m.group(2), [])
                R.top[tick].append(top)
                sm = STAT.search(line)
                if sm:
                    R.stats[tick].append((sm.group(1), int(sm.group(2))))
                continue
            m = SUB8.match(line)
            if m:
                kind, text = m.group(1), m.group(2)
                if block and block.startswith("npc_info"):
                    nm = NPC.search(text)
                    if nm:
                        index = nm.group(1)
                        npc = R.npcs.get(index)
                        if npc is None or kind == "add":
                            npc = Npc(index, tick)
                            R.npcs[index] = npc
                            R.lives.append(npc)
                        if not npc.types:
                            npc.types.append((tick, int(nm.group(3)), nm.group(2)))
                        p = (int(nm.group(4)), int(nm.group(5)))
                        nc = NEWCOORD.search(text)
                        end = (int(nc.group(1)), int(nc.group(2))) if nc else p
                        npc.tiles.setdefault(tick - 1, p)
                        npc.tiles[tick] = end
                        st = re.search(r"step=([a-z-]+)", text)
                        npc.ev[tick].append((kind, "%s->%s%s" % (p, end, (" " + st.group(1)) if st else "")))
                        if kind == "del":
                            npc.deleted = tick
                        actor = ("npc", npc)
                    else:
                        actor = None
                    continue
                if block and block.startswith("player_info"):
                    pm = PLAYER.search(text)
                    if pm:
                        nc = NEWCOORD.search(text)
                        tile = (int(nc.group(1)), int(nc.group(2))) if nc else (int(pm.group(3)), int(pm.group(4)))
                        if pm.group(1) == R.local:
                            me_tile = tile
                            R.me[tick] = tile
                            actor = ("me", None)
                            if kind != "idle":
                                R.my_ev[tick].append((kind, str(tile)))
                        else:
                            actor = ("player", pm.group(1))
                    continue
                if top is not None:
                    top[2].append(line.strip())
                continue
            m = SUB12.match(line)
            if m and actor is not None:
                kind, text = m.group(1), m.group(2)
                if actor[0] == "npc":
                    npc = actor[1]
                    if kind == "transformation":
                        npc.types.append((tick, id_of(text), name_of(text)))
                        npc.ev[tick].append(("retype", name_of(text)))
                    elif kind == "sequence":
                        npc.ev[tick].append(("seq", name_of(text) + ("" if num(text, "delay") is None else " d%d" % num(text, "delay"))))
                    elif kind == "face_angle":
                        npc.ev[tick].append(("face", str(num(text, "angle"))))
                    elif kind == "hit":
                        npc.ev[tick].append(("hit", "%s %s" % (num(text, "id"), num(text, "value"))))
                    elif kind == "headbar":
                        npc.ev[tick].append(("headbar", "%s" % num(text, "fill")))
                    else:
                        npc.ev[tick].append((kind, text))
                elif actor[0] == "me":
                    if kind == "hit":
                        R.my_ev[tick].append(("hit", "%s %s" % (num(text, "id"), num(text, "value"))))
                    elif kind in ("sequence", "spotanim"):
                        R.my_ev[tick].append((kind, name_of(text)))
    # the room cell, and whether its chunk offsets print mirrored
    # (the cell is the same printed or fixed: the mirror stays inside a chunk)
    pts = [n.tiles[min(n.tiles)] for n in R.lives if n.types and n.types[0][1] in npc_ids]
    xs = sorted(p[0] for p in pts)
    zs = sorted(p[1] for p in pts)
    R.origin = (xs[len(xs) // 2] & ~31, zs[len(zs) // 2] & ~31)
    R.flip = detect_flip(R)
    R.turn = detect_turn(R, npc_ids, anchor)
    return R


def detect_flip(R):
    """Mirrored chunk offsets show as the local raider stepping 7+ tiles in one
    tick between two tiles of the same 32x32 cell."""
    jumps = steps = 0
    prev = None
    cell = (R.origin[0] >> 5, R.origin[1] >> 5)
    for t in sorted(R.me):
        p = R.me[t]
        if not (R.first <= t <= R.last) or (p[0] >> 5, p[1] >> 5) != cell:
            prev = None
            continue
        if prev is not None and p != prev:
            d = max(abs(p[0] - prev[0]), abs(p[1] - prev[1]))
            steps += 1
            if d >= 7:
                jumps += 1
        prev = p
    return jumps > 0


STEP = {"north": (0, 1), "south": (0, -1), "east": (1, 0), "west": (-1, 0),
        "north-east": (1, 1), "north-west": (-1, 1), "south-east": (1, -1), "south-west": (-1, -1)}
TURNS = [(1, 0, 0, 1), (0, 1, -1, 0), (-1, 0, 0, -1), (0, -1, 1, 0)]   # quarter turns of (dx, dz)


def detect_turn(R, npc_ids, anchor=None):
    """The matrix taking an instance step (its label) to its template step (the
    fixed printed tiles), by vote over every labelled npc step in the room."""
    votes = {}
    for n in R.lives:
        if not n.types or n.types[0][1] not in npc_ids:
            continue
        for t, rows in n.ev.items():
            for k, d in rows:
                m = re.match(r"\((\d+), (\d+)\)->\((\d+), (\d+)\) ([a-z-]+)$", d)
                if not m or m.group(5) not in STEP:
                    continue
                a = R.fix((int(m.group(1)), int(m.group(2))))
                b = R.fix((int(m.group(3)), int(m.group(4))))
                dt = (b[0] - a[0], b[1] - a[1])
                lab = STEP[m.group(5)]
                for M in TURNS:
                    if (M[0] * lab[0] + M[1] * lab[1], M[2] * lab[0] + M[3] * lab[1]) == dt:
                        votes[M] = votes.get(M, 0) + 1
    if votes:
        return max(votes, key=votes.get)
    assert anchor is not None, "%s: no labelled npc step to read the room's turn from, and no anchor" % R.path
    npc_id, size, table = anchor
    want = table.get(R.origin[0])
    assert want is not None, "%s: no anchor tile for the cell at x %d" % (R.path, R.origin[0])
    for n in R.lives:
        if n.types and n.types[0][1] == npc_id:
            fits = []
            for M in TURNS:
                R.turn = M
                if R.sw_of(n.tiles[min(n.tiles)], size) == want:
                    fits.append(M)
            # the flip says odd or even
            fits = [M for M in fits if (M[0] == 0) == R.flip]
            assert len(fits) == 1, "%s: the anchor fits %s" % (R.path, fits)
            return fits[0]
    raise AssertionError("%s: the anchor npc %d is not in the room" % (R.path, npc_id))
