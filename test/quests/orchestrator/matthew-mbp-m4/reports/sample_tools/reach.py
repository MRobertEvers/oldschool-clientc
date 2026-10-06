#!/usr/bin/env python3
"""reach.py sx sz tx tz [level] [margin] [--allow-op-locs] [--root <repo>] -- 4-dir flood over static map collision
(jm2 floor flags + jl2 locs). Answers the first of:

  REACH closed-doors len=N           a walk with every door closed and every OP LOC avoided
  NEEDS-DOOR len=N via d@x,z,...     only through a door/gate (op locs still avoided)
  NEEDS-OP len=N via l@x,z,... [doors d@x,z,...]
                                     only by stepping onto an op loc (the fewest of them, then the
                                     fewest doors): the walk must CLICK it -- a goto there is a cheat
  UNREACHABLE (margin M)

An OP LOC is a loc on a tile the map leaves walkable (blockwalk=0; a ground decoration that is not
blockwalk=1 and active) that has any op1-5: Regicide's pitfalls (Jump), tripwires (Step-over) and
woodsprings (Pass), a stepping stone, a log balance, a spring trap. A loc whose every op is in
PASS_THROUGH_OPS (an open door's leaf: Close; a crop: Pick) is not one, and neither is a wall
decoration (shapes 4-8: clicked from the tile beside it). A ground decoration that BLOCKS and has an
op (Troll Stronghold's climbing rocks) stays solid for REACH / NEEDS-DOOR; NEEDS-OP may cross it. So
does a blocking OBJECT (shapes 9-21) with a crossing op (CROSSING_OPS: Cross, Go-through, Squeeze,
Jump...; never a ladder or stair): the Wilderness Ditch (ditch_wilderness_cover, Cross, 3106,3521),
the Shantay Pass (shantay_pass_henge_doorway, Go-through, 3302,3116) -- before seam
matthew-mbp-m4-b64-seam1 those read UNREACHABLE. An op-less, nameless wall (an inviswall) on the
edge of such a crossing tile is part of the crossing.
A ZONE-TRIGGER tile (zone_triggers.tsv beside this file: the tiles a [zone,...] timer hurts you on,
with the .rs2 file:line) is blocked the same way and named <timer>@x,z.
--allow-op-locs restores the old flood: op locs and trigger tiles are floor.

Ignores script-spawned locs, npcs, diagonal-only gaps, and maplinks (stairs, agility shortcuts).
Importable: comp.py and the fixture test use Area / answer().

--root names the repo checkout whose OSRS-Content maps and configs are read (and whose build/ holds
the loc cache); it defaults to the checkout this file lives in, so a worktree reads its own maps."""
import heapq
import os
import pickle
import re
import sys
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))


def repo_of(path):
    """The checkout `path` lives in: the nearest directory above it holding OSRS-Content."""
    path = os.path.abspath(path)
    while True:
        if os.path.isdir(os.path.join(path, "OSRS-Content")):
            return path
        parent = os.path.dirname(path)
        assert parent != path, "no OSRS-Content above %s: pass --root <repo>" % HERE
        path = parent


TRIGGERS = HERE + "/zone_triggers.tsv"


def set_root(root):
    """Read the maps and configs of the checkout at `root` (and cache the loc table under its build/)."""
    global REPO, BASE, CACHE
    REPO = os.path.abspath(root)
    BASE = REPO + "/OSRS-Content/osrs239-content"
    # The loc table (all.loc's collision and op fields) is cached under build/, never committed, and
    # rebuilt when all.loc or its compack changes (a stale table is how the old pickle went wrong).
    CACHE = REPO + "/build/orchestrator/sample_tools/locinfo.pickle"


set_root(repo_of(HERE))
FIELDS = ("name", "op1", "op2", "op3", "op4", "op5", "blockwalk", "width", "length", "active")
DX = {0: (-1, 0), 1: (0, 1), 2: (1, 0), 3: (0, -1)}   # side: 0=W 1=N 2=E 3=S


def load_locs():
    srcs = [BASE + "/configs/all.loc", BASE + "/configs/all.loc.compack"]
    stamp = tuple((os.path.getmtime(p), os.path.getsize(p)) for p in srcs)
    if os.path.exists(CACHE):
        with open(CACHE, "rb") as fh:
            got = pickle.load(fh)
        if got[0] == stamp:
            return got[1], got[2]
    ids = {}
    for line in open(srcs[1], errors="replace"):
        a, _, b = line.strip().partition("=")
        if a.isdigit() and b:
            ids[int(a)] = b
    info, cur = {}, None
    for line in open(srcs[0], errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"^\[(.+)\]$", line)
        if m:
            cur = m.group(1)
            info[cur] = {}
            continue
        if cur and "=" in line:
            k, v = line.split("=", 1)
            if k in FIELDS and k not in info[cur]:
                info[cur][k] = v
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    tmp = CACHE + ".%d" % os.getpid()
    with open(tmp, "wb") as fh:
        pickle.dump((stamp, ids, info), fh)
    os.replace(tmp, CACHE)
    return ids, info


# Ops that never gate a walk: an open door's leaf (Close: the doorway beside it is open) and a crop
# (Pick: wheat, cabbage, potato, flax -- a player walks through the field; X Marks the Spot's dig in
# the Draynor wheat read NEEDS-OP until Pick was listed here).
PASS_THROUGH_OPS = {"close", "pick"}
# The ops of a BLOCKING object a walk crosses by clicking it (helper_coverage MapWalls.CROSSING_OPS):
# the first word of the op, lowercased (`Go-through` -> go).
CROSSING_OPS = {"cross", "go", "walk", "squeeze", "crawl", "jump", "pass", "swing"}
CLIMB_NAME_RE = re.compile(r"\b(ladder|stairs|staircase|stairway|trapdoor|trap door|steps)\b")


def is_crossing(i):
    """Does this blocking object carry an op that crosses it (and is it no climb)?"""
    words = [re.split(r"[-\s]", o.lower())[0] for o in ops_of(i)]
    if "climb" in words or CLIMB_NAME_RE.search(i.get("name", "").lower()):
        return False
    return any(w in CROSSING_OPS for w in words)


def ops_of(i):
    return [i["op%d" % k].strip() for k in range(1, 6) if i.get("op%d" % k, "").strip()]


def is_op_loc(i, shape):
    """Does this placement leave its tile walkable while carrying an op the walk must click?"""
    if 4 <= shape <= 8:
        return False
    ops = ops_of(i)
    if not ops or all(o.lower() in PASS_THROUGH_OPS for o in ops):
        return False
    bw = i.get("blockwalk", "2")
    if shape == 22:
        active = i["active"] == "1" if "active" in i else True   # ops present: active by default
        return not (bw == "1" and active)
    return bw == "0"


def load_triggers():
    """[(timer, {loc symbols}, [(dx, dz)], {(zone x>>3, zone z>>3, level)}, source)] from zone_triggers.tsv."""
    out = []
    if not os.path.exists(TRIGGERS):
        return out
    for line in open(TRIGGERS):
        if not line.strip() or line.startswith("#"):
            continue
        timer, locs, offs, zones, source = line.rstrip("\n").split("\t")[:5]
        zs = set()
        for z in zones.split(","):
            parts = list(map(int, z.split("_")))
            if len(parts) == 3:   # a [mapzone,L_mx_mz]: all 64 zones of the map square
                lv, mx, mz = parts
                zs.update((mx * 8 + a, mz * 8 + b, lv) for a in range(8) for b in range(8))
            else:                 # a [zone,L_mx_mz_lx_lz]: one 8x8 zone
                lv, mx, mz, lx, lz = parts
                zs.add(((mx * 64 + lx) >> 3, (mz * 64 + lz) >> 3, lv))
        out.append((timer, set(locs.split(",")), [tuple(map(int, o.split(":"))) for o in offs.split(",")], zs, source))
    return out


class Area:
    """The static collision of one box on one level. full: blocked tiles; wall: (x,z) -> blocked sides;
    doorw: (x,z,side) -> door label; optile: (x,z) -> op loc / trigger label on a walkable tile;
    opgate: (x,z) -> a blocking ground decoration with an op (climbing rocks), in full too, that only
    the NEEDS-OP rung may cross. optile and opgate are empty with allow_op_locs."""

    def __init__(self, x0, x1, z0, z1, level, allow_op_locs=False):
        self.x0, self.x1, self.z0, self.z1, self.level = x0, x1, z0, z1, level
        ids, info = load_locs()
        flags, locs = {}, []
        for mx in range(x0 >> 6, (x1 >> 6) + 1):
            for mz in range(z0 >> 6, (z1 >> 6) + 1):
                p = "%s/maps/m%d_%d" % (BASE, mx, mz)
                if os.path.exists(p + ".jm2"):
                    for line in open(p + ".jm2"):
                        m = re.match(r"^(\d) (\d+) (\d+):(.*)$", line)
                        if m:
                            f = re.search(r"\bf(\d+)", m[4])
                            if f:
                                flags[(int(m[1]), mx * 64 + int(m[2]), mz * 64 + int(m[3]))] = int(f[1])
                if os.path.exists(p + ".jl2"):
                    for line in open(p + ".jl2"):
                        m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", line)
                        if m:
                            locs.append((int(m[1]), mx * 64 + int(m[2]), mz * 64 + int(m[3]),
                                         int(m[4]), int(m[5]), int(m[6] or 0)))

        def eff(L, x, z):
            return L - 1 if flags.get((1, x, z), 0) & 2 else L

        self.full, self.wall, self.doorw, self.optile, self.opgate = set(), {}, {}, {}, {}
        self.hardwall = {}   # (x,z) -> sides walled by a loc with a name or an op (never part of a crossing)
        self.trigger_sources = {}
        for (L, x, z), f in flags.items():
            if f & 1 and eff(L, x, z) == level:
                self.full.add((x, z))
        triggers = [] if allow_op_locs else load_triggers()
        for (L, x, z, lid, shape, rot) in locs:
            if eff(L, x, z) != level:
                continue
            nm = ids.get(lid, str(lid))
            i = info.get(nm, {})
            for timer, syms, offs, zones, source in triggers:
                if nm in syms:
                    for dx, dz in offs:
                        if ((x + dx) >> 3, (z + dz) >> 3, L) in zones:
                            self.optile.setdefault((x + dx, z + dz), "%s@%d,%d" % (timer, x + dx, z + dz))
                            self.trigger_sources[timer] = source
            w, ln = int(i.get("width", 1)), int(i.get("length", 1))
            if rot & 1:
                w, ln = ln, w
            foot = [(x + a, z + b) for a in range(w) for b in range(ln)] if 9 <= shape <= 21 else [(x, z)]
            if is_op_loc(i, shape):
                if not allow_op_locs:
                    for t in foot:
                        self.optile[t] = "%s@%d,%d" % (nm, x, z)
                continue
            if i.get("blockwalk") == "0":
                continue
            op = (i.get("op1", "") + i.get("op2", "")).lower()
            nml = i.get("name", "").lower()
            isdoor = ("open" in op or "door" in nml or "gate" in nml) and shape in (0, 2)
            door = ("%s@%d,%d" % (nm, x, z)) if isdoor else None
            soft = not ops_of(i) and not i.get("name")   # an inviswall: no name, no op
            if shape == 0:
                self.addwall(x, z, rot, door, soft)
            elif shape == 2:
                self.addwall(x, z, rot, door, soft)
                self.addwall(x, z, (rot + 1) % 4, door, soft)
            elif 9 <= shape <= 21:
                self.full.update(foot)
                if not allow_op_locs and is_crossing(i):
                    # a blocking object a click crosses (the Wilderness Ditch, the Shantay Pass): only
                    # the NEEDS-OP rung may step onto it, and names it
                    for t in foot:
                        self.opgate[t] = "%s@%d,%d" % (nm, x, z)
            elif shape == 22 and i.get("blockwalk") == "1" and (i.get("active") == "1" or
                                                                 ("active" not in i and ops_of(i))):
                self.full.add((x, z))   # the server's floor-decoration stamp (torirs_server_scene.c)
                if not allow_op_locs and [o for o in ops_of(i) if o.lower() not in PASS_THROUGH_OPS]:
                    # a BLOCKING ground decoration with an op (Troll Stronghold's climbing rocks): a
                    # click crosses it, so the NEEDS-OP rung may step onto it and names it
                    self.opgate[(x, z)] = "%s@%d,%d" % (nm, x, z)

    def addwall(self, x, z, side, door, soft=False):
        dx, dz = DX[side]
        for (a, b, s) in ((x, z, side), (x + dx, z + dz, (side + 2) % 4)):
            if door:
                self.doorw[(a, b, s)] = door
            else:
                self.wall.setdefault((a, b), set()).add(s)
                if not soft:
                    self.hardwall.setdefault((a, b), set()).add(s)

    def steps(self, c, ends, open_doors, allow_ops):
        """(neighbour, crossed door label or None, entered op label or None) for each legal 4-way step."""
        x, z = c
        for side, (dx, dz) in DX.items():
            n = (x + dx, z + dz)
            if not (self.x0 <= n[0] <= self.x1 and self.z0 <= n[1] <= self.z1):
                continue
            if side in self.wall.get(c, ()):
                # an inviswall on a crossing tile's edge is part of the crossing (NEEDS-OP only)
                if not (allow_ops and side not in self.hardwall.get(c, ()) and
                        (c in self.opgate or n in self.opgate)):
                    continue
            door = self.doorw.get((x, z, side))
            if door and not open_doors:
                continue
            op = self.optile.get(n) if n not in ends else None
            if n in self.full and n not in ends:
                op = self.opgate.get(n)
                if not op or not allow_ops:
                    continue
            if op and not allow_ops:
                continue
            yield n, door, op

    def bfs(self, s, t, open_doors):
        """Shortest path s->t with op tiles blocked, or None."""
        prev = {s: None}
        q = deque([s])
        ends = {s, t}
        while q:
            c = q.popleft()
            if c == t:
                path = []
                while c:
                    path.append(c)
                    c = prev[c]
                return path[::-1]
            for n, _, _ in self.steps(c, ends, open_doors, False):
                if n not in prev:
                    prev[n] = c
                    q.append(n)
        return None

    def cheapest(self, s, t):
        """Doors open, op tiles allowed: the path entering the fewest op tiles, then crossing the fewest
        doors, then shortest. (path, [op labels], [door labels]) or None."""
        ends = {s, t}
        best = {s: (0, 0, 0)}
        prev = {s: None}
        heap = [((0, 0, 0), s)]
        while heap:
            cost, c = heapq.heappop(heap)
            if best.get(c) != cost:
                continue
            if c == t:
                break
            for n, door, op in self.steps(c, ends, True, True):
                nc = (cost[0] + (1 if op else 0), cost[1] + (1 if door else 0), cost[2] + 1)
                if n not in best or nc < best[n]:
                    best[n] = nc
                    prev[n] = c
                    heapq.heappush(heap, (nc, n))
        if t not in best:
            return None
        path, c = [], t
        while c:
            path.append(c)
            c = prev[c]
        path = path[::-1]
        return path, self.ops_on(path), self.doors_on(path)

    def doors_on(self, path):
        out = []
        for a, b in zip(path, path[1:]):
            for side, (dx, dz) in DX.items():
                if (a[0] + dx, a[1] + dz) == b and (a[0], a[1], side) in self.doorw:
                    out.append(self.doorw[(a[0], a[1], side)])
        return list(dict.fromkeys(out))

    def ops_on(self, path):
        return list(dict.fromkeys(self.optile.get(c) or self.opgate[c] for c in path[1:-1]
                                  if c in self.optile or c in self.opgate))


def answer(sx, sz, tx, tz, level=0, margin=40, allow_op_locs=False):
    """The one-line verdict reach.py prints."""
    x0, x1 = min(sx, tx) - margin, max(sx, tx) + margin
    z0, z1 = min(sz, tz) - margin, max(sz, tz) + margin
    a = Area(x0, x1, z0, z1, level, allow_op_locs)
    s, t = (sx, sz), (tx, tz)
    note = []
    if s in a.full:
        note.append("start tile solid")
    if t in a.full:
        note.append("target tile solid")
    if s in a.optile:
        note.append("start on %s" % a.optile[s])
    if t in a.optile:
        note.append("target on %s" % a.optile[t])
    tail = (" " + " ".join(note)) if note else ""
    p = a.bfs(s, t, False)
    if p:
        return "REACH closed-doors len=%d%s" % (len(p) - 1, tail)
    p = a.bfs(s, t, True)
    if p:
        return "NEEDS-DOOR len=%d via %s%s" % (len(p) - 1, ",".join(a.doors_on(p)), tail)
    got = None if allow_op_locs else a.cheapest(s, t)
    if got:
        path, ops, doors = got
        shown = ",".join(ops[:8]) + (",+%d more" % (len(ops) - 8) if len(ops) > 8 else "")
        srcs = sorted({a.trigger_sources[o.split("@")[0]] for o in ops if o.split("@")[0] in a.trigger_sources})
        return "NEEDS-OP len=%d via %s%s%s%s" % (
            len(path) - 1, shown, (" doors " + ",".join(doors)) if doors else "",
            (" [zone triggers: %s]" % "; ".join(srcs)) if srcs else "", tail)
    return "UNREACHABLE (margin %d)%s" % (margin, tail)


def take_root(argv):
    """argv without `--root <repo>`, applying it (set_root) when given."""
    if "--root" in argv:
        k = argv.index("--root")
        set_root(argv[k + 1])
        argv = argv[:k] + argv[k + 2:]
    return argv


def main(argv):
    argv = take_root(argv)
    allow = "--allow-op-locs" in argv
    args = [a for a in argv if a != "--allow-op-locs"]
    if len(args) < 4:
        sys.exit(__doc__)
    sx, sz, tx, tz = map(int, args[:4])
    level = int(args[4]) if len(args) > 4 else 0
    margin = int(args[5]) if len(args) > 5 else 40
    print(answer(sx, sz, tx, tz, level, margin, allow))


if __name__ == "__main__":
    main(sys.argv[1:])
