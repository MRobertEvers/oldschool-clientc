#!/usr/bin/env python3
"""npc_reach_audit.py [--root <repo>] [--out <tsv>] [--quiet] [--gate] -- every statically known npc
spawn whose type has an [opnpcN,...] handler, classified by whether a player can stand where the op
reaches it.

The pattern this hunts (four quests so far: the Rising Sun barmaid, the Bone Voyage sawmill operator,
Kennith, Swan Song's Herman): an npc spawns behind a counter, bar or desk, so no walkable tile beside it
is joined to the map, or the tile beside it is cut off by a wall edge (a shape-0 counter). Its handlers
are only [opnpcN,<npc>], so Talk-to answers "I can't reach that!". The fix was an [apnpcN,<npc>] twin
with p_aprange(2).

Spawns collected:
  * every NPC row of every *.spawn under server/scripts (`name x z level`);
  * npc_add(<coord>, <npc>, ...) whose coord is a coord literal, a ^constant (chains followed), a $local
    assigned one in the same block, or movecoord(<one of those>, dx, dy, dz), and whose npc is a literal,
    a ^constant naming one or a $local assigned one. Anything else (relative to the player, instance
    coords, map_findsquare) is counted as unresolved and not classified.
Only npc types with an [opnpcN,<type>] or [opnpcN,_<category>] handler are classified (the attack
default [opnpc2,_] has its [apnpc2,_] twin and is left out). Npc configs: configs/all.npc, overlaid by
every *.npc under server/scripts (npc_movement.generated.npc states wanderrange/moverestrict);
categories by name from pack/category.pack. The engine default wanderrange is 5.

The reach model is the engine's npc approach (LostCity reachRectangle): the player stands on a tile
4-adjacent to the npc's size x size footprint, and the edge between that tile and the footprint carries
no wall. The loader is reach.py's Area (jm2 floor flags + jl2 locs at their effective level, its doors,
op locs and zone triggers). A tile is JOINED when a 4-way flood from it (doors open, op tiles blocked)
reaches FLOOD_CAP tiles or the box edge, or, being smaller, has a way out: a maplink source, a walkable
op loc or crossing on/next to it, or a loc with a travel op (climb, enter, go-through, ...) on/next to it.

Classes (for the spawn tile; a non-OK spawn is re-tried at every tile the npc can wander to):
  OK           a joined, standable tile 4-adjacent to the footprint with no wall on the edge.
  WANDER       not from the spawn tile, but from a tile within the npc's wanderrange it can walk to
               (doors shut); a test that talks to it right after spawn may still fail.
  WALL_EDGE    every joined tile beside the footprint is behind a wall edge (the counter case).
  POCKET       the inverse: the only clear tiles beside it are in a sealed pocket (no wall-edge tile
               either) -- a player has to be inside a pocket nothing joins to the map.
  UNREACHABLE  no standable tile beside it at all (boxed in by tables, crates, water).
A row is a HIT when its class is WALL_EDGE, POCKET or UNREACHABLE and some op it has an [opnpcN] handler
for has no [apnpcN,<type>] / [apnpcN,_<category>] twin (column uncovered_ops).

Gate (--gate): exit 1 on a HIT that the baseline (--baseline, default
tools/quest_gate/npc_reach_audit_baseline.tsv; absent = empty) does not list. A baseline row is keyed by
the spawn FILE (no line), the npc, the tile and the class, with a reason. A row no hit matches is STALE
(printed, does not fail). --write-baseline <path> seeds one from the current hits. Not wired into make.

Report: build/orchestrator/npc_reach_audit.tsv. Exit status 0 without --gate.
"""
import argparse
import os
import re
import sys
import time
from collections import deque, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
# tools/quest_gate/queue.py would shadow the stdlib `queue` that multiprocessing imports
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.insert(0, HERE)
import landing_audit as la  # noqa: E402  (its parsers, constants, maplinks and the reach import)
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
reach = la.reach
REPO = la.REPO

FLOOD_CAP = 300     # a flood this big is the wider map
MARGIN = 48         # box around a map square: wander (<=20) + a flood that leaves it is not a pocket
# the first word of a loc op (split on '-' and space, as reach.is_crossing reads it: Climb-over -> climb)
TRAVEL_WORDS = {"climb", "enter", "exit", "leave", "go", "cross", "squeeze", "crawl", "jump", "pass", "walk",
                "swing", "board", "travel", "descend", "ascend", "teleport", "step", "push", "slide", "vault",
                "tunnel", "dive", "use", "ride", "sail", "row", "balance", "grab", "shortcut"}
TRAVEL_NAME_RE = re.compile(r"\b(ladder|stairs|staircase|stairway|trapdoor|trap door|hatch|portal|tunnel|"
                            r"cave|entrance|exit|rope|steps|door|gate|passage|crevice|gap|hole)\b")
OP_HDR_RE = re.compile(r"^\[(op|ap)npc([1-5]),([^\]]+)\]")
NPC_FIELDS = ("name", "op1", "op2", "op3", "op4", "op5", "size", "category", "wanderrange", "moverestrict",
              "nomove")


# ---------------------------------------------------------------- npc configs

def load_npc_configs(content):
    """{type: {field: value}} from configs/all.npc, overlaid by every *.npc under server/scripts."""
    import config_text  # tools/config_text.py, put on the path by reach
    info = {}

    def read(path, overlay):
        cur = None
        for line in config_text.read_lines(path, errors="replace"):
            line = la.strip_comment(line).strip()
            m = re.match(r"^\[([A-Za-z_0-9]+)\]$", line)
            if m:
                cur = info.setdefault(m[1], {})
                cur.setdefault("_seen", set())
                continue
            if cur is None or "=" not in line:
                continue
            k, v = line.split("=", 1)
            if k not in NPC_FIELDS:
                continue
            if overlay or k not in cur["_seen"]:
                cur[k] = v
                cur["_seen"].add(k)

    read(os.path.join(content, "configs/all.npc"), False)
    for dp, ds, fs in os.walk(os.path.join(content, "server/scripts")):
        ds.sort()
        for f in sorted(fs):
            if f.endswith(".npc"):
                read(os.path.join(dp, f), True)
    return info


def load_categories(content):
    out = {}
    p = os.path.join(content, "pack/category.pack")
    if os.path.exists(p):
        for line in open(p, errors="replace"):
            m = re.match(r"^(\d+)=([a-z_0-9]+)", line.strip())
            if m:
                out[m[1]] = m[2]
    return out


def npc_category(cfg, cats):
    c = cfg.get("category", "")
    return cats.get(c, c) if c else ""


def wander_of(cfg):
    if cfg.get("nomove") in ("yes", "true", "1") or cfg.get("moverestrict") == "nomove":
        return 0
    try:
        return int(cfg.get("wanderrange", 5))
    except ValueError:
        return 5


def size_of(cfg):
    try:
        return max(1, int(cfg.get("size", 1)))
    except ValueError:
        return 1


# ---------------------------------------------------------------- handlers

def load_handlers(scripts):
    """{('op'|'ap', subject): {op: [file:line]}} -- subject an npc type or '_category' or '_'."""
    out = defaultdict(lambda: defaultdict(list))
    for dp, ds, fs in os.walk(scripts):
        ds.sort()
        for f in sorted(fs):
            if not f.endswith(".rs2"):
                continue
            p = os.path.join(dp, f)
            for n, line in enumerate(open(p, errors="replace"), 1):
                if not line.startswith("[") or "npc" not in line[:8]:
                    continue
                m = OP_HDR_RE.match(line)
                if m:
                    out[(m[1], m[3].strip())][int(m[2])].append("%s:%d" % (os.path.relpath(p, REPO), n))
    return out


def handler_ops(handlers, npc, category):
    """(op handler {op: [sites]}, ap handler {op: [sites]}) for one npc type (name + category)."""
    op, ap = defaultdict(list), defaultdict(list)
    for subj in [npc] + (["_" + category] if category else []):
        for k, v in handlers.get(("op", subj), {}).items():
            op[k].extend(v)
        for k, v in handlers.get(("ap", subj), {}).items():
            ap[k].extend(v)
    return op, ap


# ---------------------------------------------------------------- spawns

def load_spawn_files(scripts):
    """[dict(source, npc, coord)] for every NPC row of every *.spawn."""
    out = []
    for dp, ds, fs in os.walk(scripts):
        ds.sort()
        for f in sorted(fs):
            if not f.endswith(".spawn"):
                continue
            p = os.path.join(dp, f)
            sec = None
            for n, line in enumerate(open(p, errors="replace"), 1):
                s = la.strip_comment(line).strip()
                m = re.match(r"^====\s*([A-Z]+)\s*====", s)
                if m:
                    sec = m[1]
                    continue
                if sec != "NPC" or not s:
                    continue
                parts = s.split()
                if len(parts) >= 4 and parts[1].isdigit() and parts[2].isdigit() and parts[3].isdigit():
                    out.append(dict(source="%s:%d" % (os.path.relpath(p, REPO), n), npc=parts[0],
                                    coord=(int(parts[3]), int(parts[1]), int(parts[2])), kind="spawn"))
    return out


NPC_ADD_RE = re.compile(r"\bnpc_add\(")


def resolve_coord(res, block, text, depth=0):
    """[(coord or None, symbol)] -- landing_audit's Resolver plus movecoord(<expr>, dx, dy, dz)."""
    t = text.strip()
    m = re.match(r"^movecoord\((.*)\)$", t, re.S)
    if m and depth < 3:
        a = la.split_args(m[1])
        if len(a) == 4 and all(re.match(r"^-?\d+$", v) for v in a[1:]):
            dx, dy, dz = map(int, a[1:])
            return [((c[0] + dy, c[1] + dx, c[2] + dz) if c else None, "movecoord(%s)" % s)
                    for c, s in resolve_coord(res, block, a[0], depth + 1)]
        return [(None, t)]
    return [(c, s) for c, s, _ in res.expr(block, t)]


def resolve_npc(block, consts, text, npcs, depth=0):
    t = text.strip()
    if re.match(r"^[a-z_0-9]+$", t):
        return [t] if t in npcs else []
    m = re.match(r"^\^([A-Za-z_0-9]+)$", t)
    if m and depth < 4:
        got = consts.get(m[1])
        return resolve_npc(block, consts, got[0], npcs, depth + 1) if got else []
    m = re.match(r"^\$([a-z_0-9]+)$", t)
    if m and depth < 3:
        out = []
        for r in la.assignments(block, m[1]):
            out.extend(resolve_npc(block, consts, r, npcs, depth + 1))
        return out
    return []


def load_npc_adds(content, consts, npcs):
    """([dict(source, npc, coord)], unresolved count)."""
    blocks = la.load_blocks(os.path.join(content, "server/scripts"))
    res = la.Resolver(consts)
    out, unresolved = [], 0
    for b in blocks:
        for m, inner, n in la.calls_in(b, NPC_ADD_RE):
            a = la.split_args(inner)
            if len(a) < 2:
                continue
            types = resolve_npc(b, consts, a[1], npcs)
            coords = [c for c, _ in resolve_coord(res, b, a[0]) if c]
            if not types or not coords:
                unresolved += 1
                continue
            for t in types:
                for c in coords:
                    out.append(dict(source="%s:%d" % (os.path.relpath(b.path, REPO), n), npc=t, coord=c,
                                    kind="npc_add"))
    return out, unresolved


# ---------------------------------------------------------------- collision

class Square:
    """One reach.Area per (level, map square) plus MARGIN, with memoised floods."""

    def __init__(self, level, mx, mz, mlsrc, ids, info):
        self.level = level
        self.a = reach.Area(mx * 64 - MARGIN, mx * 64 + 63 + MARGIN, mz * 64 - MARGIN, mz * 64 + 63 + MARGIN,
                            level)
        self.mlsrc, self.ids, self.info = mlsrc, ids, info
        self.region = {}        # tile -> ("J", note) joined | ("P", size) sealed pocket
        self._travel = None

    def travel_locs(self):
        """{tile: label} for every tile a loc with a travel op occupies, on this level."""
        if self._travel is not None:
            return self._travel
        out = {}
        a = self.a
        col = la.Collision.__new__(la.Collision)
        col.bridge_cache = {}
        for mx in range(a.x0 >> 6, (a.x1 >> 6) + 1):
            for mz in range(a.z0 >> 6, (a.z1 >> 6) + 1):
                p = "%s/maps/m%d_%d.jl2" % (reach.BASE, mx, mz)
                if not os.path.exists(p):
                    continue
                for line in open(p):
                    m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", line)
                    if not m:
                        continue
                    L, x, z, lid, shape, rot = (int(m[1]), mx * 64 + int(m[2]), mz * 64 + int(m[3]),
                                                int(m[4]), int(m[5]), int(m[6] or 0))
                    nm = self.ids.get(lid, str(lid))
                    i = self.info.get(nm, {})
                    ops = reach.ops_of(i)
                    if not ops:
                        continue
                    words = {re.split(r"[-\s]", o.lower())[0] for o in ops}
                    name = i.get("name", "").lower()
                    opened = "open" in words and TRAVEL_NAME_RE.search(name)
                    if not (words & TRAVEL_WORDS or opened or
                            (TRAVEL_NAME_RE.search(name) and words - {"examine"})):
                        continue
                    if col.eff_level(L, x, z) != self.level:
                        continue
                    w, ln = int(i.get("width", 1)), int(i.get("length", 1))
                    if rot & 1:
                        w, ln = ln, w
                    foot = [(x + u, z + v) for u in range(w) for v in range(ln)] if 9 <= shape <= 21 else [(x, z)]
                    for t in foot:
                        out.setdefault(t, "%s@%d,%d[%s]" % (nm, x, z, ops[0]))
        self._travel = out
        return out

    def joined(self, t):
        """("J", note) when the tile is joined to the wider map, ("P", size) when it is a sealed pocket."""
        if t in self.region:
            return self.region[t]
        a = self.a
        seen = {t}
        q = deque([t])
        edge = False
        while q and len(seen) < FLOOD_CAP:
            c = q.popleft()
            if c[0] in (a.x0, a.x1) or c[1] in (a.z0, a.z1):
                edge = True
                break
            for n, _, _ in a.steps(c, {t}, True, False):
                if n not in seen:
                    seen.add(n)
                    q.append(n)
        if edge or len(seen) >= FLOOD_CAP:
            v = ("J", "")
        else:
            outs = self.ways_out(seen)
            v = ("J", "small region %d, out via %s" % (len(seen), ",".join(outs[:2]))) if outs else \
                ("P", len(seen))
        for s in seen:
            self.region[s] = v
        return v

    def ways_out(self, region):
        a, outs = self.a, []
        near = {(x + dx, z + dz) for (x, z) in region for dx in (-1, 0, 1) for dz in (-1, 0, 1)}
        trav = self.travel_locs()
        for t in near:
            if (self.level, t[0], t[1]) in self.mlsrc:
                outs.append("maplink src %d,%d" % t)
            if t not in region and (t in a.optile or t in a.opgate):
                outs.append("op " + (a.optile.get(t) or a.opgate[t]))
            if t in trav:
                outs.append("loc " + trav[t])
        return list(dict.fromkeys(outs))

    def edges(self, x, z, size):
        """[(standable tile T, wall-on-edge label or '', door label or '')] around a footprint."""
        a = self.a
        foot = {(x + u, z + v) for u in range(size) for v in range(size)}
        out = []
        for f in sorted(foot):
            for side, (dx, dz) in reach.DX.items():
                t = (f[0] + dx, f[1] + dz)
                if t in foot or t in a.full:
                    continue
                wall = side in a.wall.get(f, ()) or (side + 2) % 4 in a.wall.get(t, ())
                door = a.doorw.get((f[0], f[1], side), "")
                out.append((t, wall, door))
        return out

    def classify_at(self, x, z, size, ok_only=False):
        """(class, note) for a footprint at (x, z): OK / WALL_EDGE / POCKET / UNREACHABLE; with ok_only,
        (None, '') for anything but OK (the wander search asks only that)."""
        es = self.edges(x, z, size)
        clear_pocket, walled_joined = [], []
        for t, wall, door in es:
            kind, note = self.joined(t)
            if not wall and kind == "J":
                extra = ("through door " + door) if door else note
                return "OK", ("from %d,%d" % t) + ((" (" + extra + ")") if extra else "")
            if not wall and kind == "P":
                clear_pocket.append((t, note))
            if wall and kind == "J":
                walled_joined.append(t)
        if ok_only:
            return None, ""
        inner = ("; inner side a sealed pocket of %d (%s)" % (
            clear_pocket[0][1], " ".join("%d,%d" % c[0] for c in clear_pocket[:3]))) if clear_pocket else ""
        if walled_joined:
            return "WALL_EDGE", "joined %s behind a wall edge%s" % (
                " ".join("%d,%d" % w for w in walled_joined[:4]), inner)
        ring = self.ring2(x, z, size)
        if ring:
            return "UNREACHABLE", "no joined tile beside it; joined %s at range 2 (across solid tiles: a counter, booth, table)%s" % (
                " ".join("%d,%d" % t for t in ring[:4]), inner)
        if clear_pocket:
            return "POCKET", "reachable only from a sealed pocket of %d tiles (%s); nothing joined within 2" % (
                clear_pocket[0][1], " ".join("%d,%d" % c[0] for c in clear_pocket[:4]))
        return "UNREACHABLE", ("no standable tile beside it" if not es else
                               "every standable tile beside it is walled off and not joined") + \
            "; nothing joined within 2"

    def ring2(self, x, z, size):
        """Standable joined tiles at Chebyshev distance 2 from the footprint (an apnpc p_aprange(2) spot)."""
        out = []
        for u in range(-2, size + 2):
            for v in range(-2, size + 2):
                if -1 <= u <= size and -1 <= v <= size:
                    continue
                t = (x + u, z + v)
                if t in self.a.full:
                    continue
                if self.joined(t)[0] == "J":
                    out.append(t)
        return out

    def npc_walk(self, x, z, radius):
        """Tiles a size-1 npc can walk to from (x, z) within `radius` (doors shut), BFS order."""
        a = self.a
        seen = {(x, z): 0}
        q = deque([(x, z)])
        while q:
            c = q.popleft()
            for side, (dx, dz) in reach.DX.items():
                n = (c[0] + dx, c[1] + dz)
                if n in seen or n in a.full or abs(n[0] - x) > radius or abs(n[1] - z) > radius:
                    continue
                if side in a.wall.get(c, ()) or (c[0], c[1], side) in a.doorw:
                    continue
                if not (a.x0 < n[0] < a.x1 and a.z0 < n[1] < a.z1):
                    continue
                seen[n] = seen[c] + 1
                q.append(n)
        return seen

    def classify(self, x, z, size, wander):
        cls, note = self.classify_at(x, z, size)
        solid = any((x + u, z + v) in self.a.full for u in range(size) for v in range(size))
        if solid:
            note = "spawn tile solid; " + note
        if cls == "OK" or wander <= 0 or size > 1:
            return cls, note
        for (wx, wz), d in sorted(self.npc_walk(x, z, wander).items(), key=lambda kv: kv[1]):
            if d == 0:
                continue
            c2, n2 = self.classify_at(wx, wz, 1, ok_only=True)
            if c2 == "OK":
                return "WANDER", "after %d step(s) to %d,%d %s; at spawn: %s %s" % (d, wx, wz, n2, cls, note)
        return cls, note + "; wanderrange %d does not help" % wander


def classify_batch(items, mlsrc, root=None):
    """{(npc type key, level, x, z, size, wander): (class, note)} for spawns grouped by map square."""
    if root:
        reach.set_root(root)
    ids, info = reach.load_locs()
    squares, out = {}, {}
    for key in sorted(items):
        lv, x, z, size, wander = key
        sk = (lv, x >> 6, z >> 6)
        if sk not in squares:
            squares[sk] = Square(lv, x >> 6, z >> 6, mlsrc, ids, info)
        out[key] = squares[sk].classify(x, z, size, wander)
    return out, len(squares)


# ---------------------------------------------------------------- quests

def lua_words(queue):
    out = {}
    for r in queue:
        p = os.path.join(REPO, "test/quests/%s.lua" % r["test_id"])
        if os.path.exists(p):
            out[r["quest_dir"]] = set(re.findall(r"[a-z_][a-z_0-9]+", open(p, errors="replace").read()))
    return out


def quests_for(row, status, words):
    hits = []
    for path in [row["source"]] + row["handler_sites"]:
        m = re.search(r"/quests/(quest_[a-z0-9_]+)/", path)
        if m and m[1] in status and m[1] not in hits:
            hits.append(m[1])
    for q, w in words.items():
        if q not in hits and row["npc"] in w:
            hits.append(q + "~lua")
    return hits


def area_of(source):
    m = re.search(r"server/scripts/(.+?)/(?:scripts|configs)/", source)
    if m:
        return m[1]
    m = re.search(r"server/scripts/([^/]+)/", source)
    return m[1] if m else source


# ---------------------------------------------------------------- gate

BASELINE = os.path.join(HERE, "npc_reach_audit_baseline.tsv")
BASELINE_COLS = ("file", "npc", "tile", "class", "reason")


def hit_key(row):
    lv, x, z = row["coord"]
    return (row["source"].rsplit(":", 1)[0], row["npc"], "%d,%d,%d" % (x, z, lv), row["cls"])


def load_baseline(path):
    out = {}
    if not os.path.exists(path):
        return out
    for n, line in enumerate(open(path), 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.rstrip("\n").split("\t")
        if f[:len(BASELINE_COLS)] == list(BASELINE_COLS):
            continue
        assert len(f) == len(BASELINE_COLS) and f[4].strip(), \
            "%s:%d: want %d tab-separated columns %s, the last a reason" % (path, n, len(BASELINE_COLS),
                                                                          "/".join(BASELINE_COLS))
        out[tuple(f[:4])] = f[4]
    return out


def gate(rows, path):
    base = load_baseline(path)
    hits = [r for r in rows if r["hit"]]
    seen, new = set(), []
    for r in hits:
        k = hit_key(r)
        if k in base:
            seen.add(k)
        else:
            new.append(r)
    stale = [k for k in base if k not in seen]
    print("npc reach gate: %d hit rows (%d baselined, %d NEW); baseline %d rows, %d stale (%s%s)" % (
        len(hits), len(hits) - len(new), len(new), len(base), len(stale), os.path.relpath(path, REPO),
        "" if os.path.exists(path) else ", absent"))
    for r in new:
        print("  NEW   %s  %s  %s  %s  ops %s  -- %s" % ((r["source"],) + hit_key(r)[1:] +
                                                         (r["uncovered"], r["note"][:110])))
    for k in stale:
        print("  STALE %s  %s  %s  %s  (no longer a hit: delete the row)" % k)
    if new:
        print("An npc a player cannot stand beside: add an [apnpcN,<npc>] twin with p_aprange (sourced), move"
              " the spawn to its sourced tile, or add its row to %s with the reason." %
              os.path.relpath(path, REPO))
    return 1 if new else 0


def write_baseline(rows, path):
    keys = list(dict.fromkeys(hit_key(r) for r in rows if r["hit"]))
    with open(path, "w") as fh:
        fh.write("# npc_reach_audit.py --gate baseline: one row per accepted hit; the reason IS the row.\n")
        fh.write("\t".join(BASELINE_COLS) + "\n")
        for k in keys:
            fh.write("\t".join(k) + "\tunreviewed: seeded by --write-baseline\n")
    print("baseline: %d rows -> %s" % (len(keys), path))


# ---------------------------------------------------------------- report

def print_groups(rows, status, limit):
    groups = defaultdict(list)
    for r in rows:
        if not r["hit"]:
            continue
        direct = [q for q in r["quests"] if "~lua" not in q]
        groups[direct[0] if direct else area_of(r["source"])].append(r)
    order = sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0]))
    print("\nHITS by quest/area (%d groups, %d rows):" % (len(order), sum(len(v) for _, v in order)))
    shown = 0
    for owner, rs in order:
        if limit and shown >= limit:
            print("... %d more rows (--top 0 prints all)" % (sum(len(v) for _, v in order) - shown))
            break
        print("%s%s  [%d]" % (owner, ("(%s)" % status[owner]) if owner in status else "", len(rs)))
        for r in rs:
            if limit and shown >= limit:
                break
            shown += 1
            lv, x, z = r["coord"]
            others = [q for q in r["quests"] if q != owner]
            print("  %-11s %s %d,%d,%d ops=%s  %s%s" % (r["cls"], r["npc"], x, z, lv, r["uncovered"],
                                                     r["source"].split("server/scripts/")[-1],
                                                     ("  also " + ",".join(others)) if others else ""))
            print("              %s" % r["note"][:150])


# ---------------------------------------------------------------- main

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", help="checkout whose OSRS-Content is read (default: this one)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--top", type=int, default=50, help="hit rows to print, grouped; 0 = all")
    ap.add_argument("--gate", action="store_true", help="exit 1 on a HIT the baseline does not list")
    ap.add_argument("--baseline", default=BASELINE, help="the gate's baseline (default %(default)s)")
    ap.add_argument("--write-baseline", metavar="PATH", help="seed a baseline from the current hits")
    args = ap.parse_args(argv)
    if args.root:
        reach.set_root(args.root)
    content = reach.BASE
    scripts = os.path.join(content, "server/scripts")
    out = args.out or os.path.join(REPO, "build/orchestrator/npc_reach_audit.tsv")
    t0 = time.time()
    npcs = load_npc_configs(content)
    cats = load_categories(content)
    handlers = load_handlers(scripts)
    consts = la.load_constants(content)
    spawns = load_spawn_files(scripts)
    adds, unresolved_adds = load_npc_adds(content, consts, npcs)
    spawns += adds
    rows = []
    for s in spawns:
        cfg = npcs.get(s["npc"], {})
        cat = npc_category(cfg, cats)
        op, apx = handler_ops(handlers, s["npc"], cat)
        if not op:
            continue
        s.update(cfg=cfg, category=cat, op=op, ap=apx, size=size_of(cfg), wander=wander_of(cfg))
        rows.append(s)
    t1 = time.time()
    mlsrc = la.maplink_sources(content)
    by_square = defaultdict(set)
    for r in rows:
        lv, x, z = r["coord"]
        by_square[(lv, x >> 6, z >> 6)].add((lv, x, z, r["size"], r["wander"]))
    batches = [set() for _ in range(max(1, args.jobs))]
    for k, key in enumerate(sorted(by_square, key=lambda k: -len(by_square[k]))):
        min(batches, key=len).update(by_square[key])
    verdict, loads = {}, 0
    if args.jobs > 1:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(args.jobs) as ex:
            for got, n in ex.map(classify_batch, batches, [mlsrc] * len(batches), [args.root] * len(batches)):
                verdict.update(got)
                loads += n
    else:
        verdict, loads = classify_batch(batches[0], mlsrc, args.root)
    t2 = time.time()
    queue = la.load_queue()
    status = {q["quest_dir"]: q["status"] for q in queue}
    words = lua_words(queue)
    counts, hits = defaultdict(int), defaultdict(int)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as fh:
        fh.write("source\tnpc\tcoord\tlevel\tsize\top_handlers\tap_handlers\tuncovered_ops\tclass\thit\t"
                 "kind\tquests\tnote\n")
        for r in rows:
            lv, x, z = r["coord"]
            cls, note = verdict[(lv, x, z, r["size"], r["wander"])]
            cfg = r["cfg"]
            opl = ",".join("op%d:%s" % (k, cfg.get("op%d" % k, "-")) for k in sorted(r["op"]))
            apl = ",".join("op%d" % k for k in sorted(r["ap"]))
            unc = [k for k in sorted(r["op"]) if k not in r["ap"]]
            hit = cls in ("WALL_EDGE", "POCKET", "UNREACHABLE") and bool(unc)
            r["handler_sites"] = [s for v in list(r["op"].values()) + list(r["ap"].values()) for s in v]
            r.update(cls=cls, note=note, hit=hit, uncovered=",".join("op%d" % k for k in unc) or "-")
            r["quests"] = quests_for(r, status, words)
            qcol = ",".join("%s(%s)" % (q, status.get(q.replace("~lua", ""), "?")) for q in r["quests"])
            counts[cls] += 1
            if hit:
                hits[cls] += 1
            fh.write("\t".join(re.sub(r"\s+", " ", str(v)) for v in (
                r["source"], r["npc"] + (" (_%s)" % r["category"] if r["category"] else ""), "%d,%d" % (x, z),
                lv, r["size"], opl, apl or "-", r["uncovered"], cls, "HIT" if hit else ("covered" if
                cls in ("WALL_EDGE", "POCKET", "UNREACHABLE") else "-"), r["kind"], qcol, note)) + "\n")
    t3 = time.time()
    if not args.quiet:
        types = {r["npc"] for r in rows}
        print("npc spawns %d (%d from *.spawn, %d npc_add resolved, %d npc_add unresolved); with an opnpc"
              " handler %d rows, %d types, %d unique placements; map areas loaded %d" % (
                  len(spawns), len(spawns) - len(adds), len(adds), unresolved_adds, len(rows), len(types),
                  len(verdict), loads))
        for k in ("OK", "WANDER", "WALL_EDGE", "POCKET", "UNREACHABLE"):
            print("  %-11s %5d rows  (%d HIT: no apnpc twin for a handled op)" % (k, counts[k], hits[k]))
        print("time: collect %.1fs classify %.1fs report %.1fs total %.1fs" % (t1 - t0, t2 - t1, t3 - t2,
                                                                              t3 - t0))
        print("report:", os.path.relpath(out, REPO))
        if not args.gate:
            print_groups(rows, status, args.top)
    if args.write_baseline:
        write_baseline(rows, args.write_baseline)
    if args.gate:
        return gate(rows, args.baseline)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
