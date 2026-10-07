#!/usr/bin/env python3
"""landing_audit.py [--root <repo>] [--out <tsv>] [--quiet] -- every statically known teleport landing in
the osrs239 server scripts, classified against the static map collision.

A script that teleports the player onto a tile the collision map calls blocked, or into a sealed pocket,
strands the player (b70/b71 found six: ^dod_cave_enter, ^eyeglo_cave_inside_coord, the soc ladder,
^qot_manhole, ^god_entry1_coord, ^ga_cave_out_coord, ^olafq_secondarea_coord). This finds all of them.

Landings collected:
  * p_teleport(...) / p_telejump(...) whose argument is a coord literal (L_MX_MZ_LX_LZ), a ^constant
    (resolved through every *.constant, chains followed), or a $local assigned one of those in the same
    script block;
  * the call sites of every proc/label that teleports to one of its own coord parameters (transitively:
    ~climb_ladder_to(coord $dest, ...) and anything that forwards into it), with a literal/^constant arg;
  * every maplink.dbrow / maplink_agility.dbrow `dest`.
Anything else (movecoord, loc_coord, map_findsquare, instance coords, procs returning a coord, $vars
assigned a computed value) is listed as UNRESOLVED with its expression.

Classes (the loader is reach.py's Area: jm2 floor flags + jl2 locs, its doors, op locs, zone triggers):
  SOLID   the landing tile itself is blocked (reach.Area.full).
  POCKET  the region a 4-way flood reaches from it with doors shut and op/trigger tiles blocked is under
          25 tiles, and nothing gives a way out: no door on its edge, no op tile or crossing next to it,
          no loc with an op on or beside any of its tiles, no maplink source on or beside any of its tiles.
  OK      otherwise (a small region that has a way out is OK; its size and the way out are in the note).
NPCs, script-spawned locs and inventory teleports are invisible to it: a POCKET whose way out is a
talk-to or a spawned loc is a false positive the note cannot rule out.

Report: build/orchestrator/landing_audit.tsv (source, symbol, coord, level, class, pocket_size, kind,
quests, note). The quests column names the QUEUE.tsv quest dirs a hit touches: the script lives under
quests/<quest_dir>/ (or the ^constant is defined there), or the quest's test/quests/<test_id>.lua names
a tile within 2 of the landing. Exit status 0 always (an audit, not yet a gate).
"""
import argparse
import bisect
import os
import re
import sys
import time
from collections import deque, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
# tools/quest_gate/queue.py would shadow the stdlib `queue` that multiprocessing imports
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
REPO = os.path.dirname(os.path.dirname(HERE))
SAMPLE_TOOLS = os.path.join(REPO, "test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools")
sys.path.insert(0, SAMPLE_TOOLS)
import reach  # noqa: E402  (the collision loader the quest loop already trusts)

POCKET_LIMIT = 25
COORD_RE = re.compile(r"^([0-3])_(\d+)_(\d+)_(\d+)_(\d+)$")
HEADER_RE = re.compile(r"^\[([a-z_0-9]+),([^\]]+)\](?:\((.*)\))?")
TELE_RE = re.compile(r"\bp_(teleport|telejump)\(")
CALL_RE = re.compile(r"([~@])([a-z_0-9]+)\(")


def parse_coord(text):
    m = COORD_RE.match(text.strip())
    if not m:
        return None
    lv, mx, mz, lx, lz = map(int, m.groups())
    return lv, mx * 64 + lx, mz * 64 + lz


def strip_comment(line):
    k = line.find("//")
    while k >= 0:
        if line[:k].count('"') % 2 == 0:
            return line[:k]
        k = line.find("//", k + 2)
    return line


def balanced(text, start):
    """text[start] is just past '(' : (inner text, index past the matching ')') or None."""
    depth, i, q = 1, start, False
    while i < len(text):
        c = text[i]
        if c == '"':
            q = not q
        elif not q:
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return text[start:i], i + 1
        i += 1
    return None


def split_args(inner):
    out, depth, cur, q = [], 0, [], False
    for c in inner:
        if c == '"':
            q = not q
        if not q and c in "([":
            depth += 1
        elif not q and c in ")]":
            depth -= 1
        if c == "," and depth == 0 and not q:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if cur or out:
        out.append("".join(cur).strip())
    return out


# ---------------------------------------------------------------- constants

def load_constants(content):
    """^name -> (value text, file, line)."""
    raw = {}
    for dp, _, fs in os.walk(content):
        for f in fs:
            if not f.endswith(".constant"):
                continue
            p = os.path.join(dp, f)
            for n, line in enumerate(open(p, errors="replace"), 1):
                m = re.match(r"^\^([A-Za-z_0-9]+)\s*=\s*(\S+)", strip_comment(line))
                if m and m[1] not in raw:
                    raw[m[1]] = (m[2], p, n)
    return raw


def resolve_constant(name, consts, depth=0):
    got = consts.get(name)
    if not got or depth > 8:
        return None
    val = got[0]
    if val.startswith("^"):
        return resolve_constant(val[1:], consts, depth + 1)
    c = parse_coord(val)
    return (c, got[1], got[2]) if c else None


# ---------------------------------------------------------------- scripts

class Block:
    def __init__(self, path, line, trigger, name, params):
        self.path, self.line, self.trigger, self.name = path, line, trigger, name
        self.params = params      # [(type, $name)]
        self.lines = []           # [(lineno, text without comment)]

    def text(self):
        return "\n".join(t for _, t in self.lines)

    def lineno_at(self, offset):
        acc = 0
        for n, t in self.lines:
            acc += len(t) + 1
            if offset < acc:
                return n
        return self.lines[-1][0] if self.lines else self.line


def load_blocks(scripts):
    blocks = []
    for dp, _, fs in os.walk(scripts):
        for f in sorted(fs):
            if not f.endswith(".rs2"):
                continue
            p = os.path.join(dp, f)
            cur = None
            for n, line in enumerate(open(p, errors="replace"), 1):
                line = strip_comment(line.rstrip("\n"))
                m = HEADER_RE.match(line)
                if m:
                    params = []
                    for part in (m[3] or "").split(","):
                        pm = re.match(r"\s*([a-z_0-9]+)\s+\$([a-z_0-9]+)", part)
                        if pm:
                            params.append((pm[1], pm[2]))
                    cur = Block(p, n, m[1], m[2], params)
                    blocks.append(cur)
                    continue
                if cur:
                    cur.lines.append((n, line))
    return blocks


def calls_in(block, regex):
    """[(match, inner args text, lineno)] for each call the regex opens."""
    text = block.text()
    out = []
    for m in regex.finditer(text):
        got = balanced(text, m.end())
        if got:
            out.append((m, got[0], block.lineno_at(m.start())))
    return out


def assignments(block, var):
    """The right-hand sides `$var = <rhs>;` in this block (def_X $var = ... or a plain store)."""
    # statement-initial only: `if ($exit = 0)` is a comparison in RuneScript, not a store
    rx = r"(?m)^\s*(?:def_[a-z]+\s+)?\$%s\s*=\s*([^;\n]+);" % re.escape(var)
    return [m[1].strip() for m in re.finditer(rx, block.text())]


class Resolver:
    def __init__(self, consts):
        self.consts = consts

    def expr(self, block, text, depth=0):
        """[(coord or None, symbol, const_file)] for one argument expression (a $var may give several)."""
        t = text.strip()
        c = parse_coord(t)
        if c:
            return [(c, t, None)]
        m = re.match(r"^\^([A-Za-z_0-9]+)$", t)
        if m:
            got = resolve_constant(m[1], self.consts)
            if got:
                return [(got[0], t, got[1])]
            return [(None, t, None)]
        m = re.match(r"^\$([a-z_0-9]+)$", t)
        if m and depth < 3:
            rhs = assignments(block, m[1])
            if rhs:
                out = []
                for r in rhs:
                    for c, sym, cf in self.expr(block, r, depth + 1):
                        out.append((c, "%s=%s" % (t, sym) if c else "%s=%s" % (t, r), cf))
                return out
        return [(None, t, None)]


MOVE_ON_RE = re.compile(r"\b(p_teleport|p_telejump|p_walk|p_exactmove|~climb[a-z_]*|~agility_[a-z_]*)\(")
CUTSCENE_RE = re.compile(r"\b(cam_moveto|cam_lookat|cam_reset|~cutscene[a-z_]*|minimap\()")


def script_triage(block, lineno, window=8):
    """Hints that the landing is not where the player is left: the script moves the player again within
    `window` lines of it (a raft ride, a maze portal's p_walk), or the block drives the camera."""
    hints = []
    for n, t in block.lines:
        if lineno < n <= lineno + window:
            m = MOVE_ON_RE.search(t)
            if m:
                hints.append("moved-on:%s@%d" % (m[1], n))
                break
    if CUTSCENE_RE.search(block.text()):
        hints.append("camera")
    return " ".join(hints)


def landing_params(blocks):
    """{(kind '~'|'@', name): {param index}} for procs/labels that teleport to one of their own params,
    directly or by forwarding it into another such proc/label (fixpoint)."""
    callable_blocks = {}
    for b in blocks:
        if b.trigger in ("proc", "label"):
            callable_blocks[("~" if b.trigger == "proc" else "@", b.name)] = b
    lp = defaultdict(set)
    for key, b in callable_blocks.items():
        names = [p[1] for p in b.params]
        for _, inner, _ in calls_in(b, TELE_RE):
            a = split_args(inner)
            if a and a[0].startswith("$") and a[0][1:] in names:
                lp[key].add(names.index(a[0][1:]))
    changed = True
    while changed:
        changed = False
        for key, b in callable_blocks.items():
            names = [p[1] for p in b.params]
            for m, inner, _ in calls_in(b, CALL_RE):
                tgt = (m[1], m[2])
                if tgt not in lp:
                    continue
                args = split_args(inner)
                for idx in lp[tgt]:
                    if idx < len(args) and args[idx].startswith("$") and args[idx][1:] in names:
                        k = names.index(args[idx][1:])
                        if k not in lp[key]:
                            lp[key].add(k)
                            changed = True
    return dict(lp)


def collect(content, consts):
    """[dict(source, symbol, coord, kind, const_file)] -- coord None when unresolved."""
    scripts = os.path.join(content, "server/scripts")
    blocks = load_blocks(scripts)
    res = Resolver(consts)
    lp = landing_params(blocks)
    out = []

    def emit(block, lineno, kind, arg, via=""):
        params = [p[1] for p in block.params]
        if arg.startswith("$") and arg[1:] in params and (
                ("~" if block.trigger == "proc" else "@", block.name) in lp):
            return   # the callers' args are the landings
        triage = script_triage(block, lineno)
        for c, sym, cf in res.expr(block, arg):
            out.append(dict(source="%s:%d" % (os.path.relpath(block.path, REPO), lineno),
                            symbol=sym + via, coord=c, kind=kind, const_file=cf, block=block,
                            triage=triage))

    for b in blocks:
        for m, inner, n in calls_in(b, TELE_RE):
            a = split_args(inner)
            if a:
                emit(b, n, "p_" + m[1], a[0])
        for m, inner, n in calls_in(b, CALL_RE):
            tgt = (m[1], m[2])
            if tgt in lp:
                args = split_args(inner)
                for idx in sorted(lp[tgt]):
                    if idx < len(args):
                        emit(b, n, "call", args[idx], " via %s%s" % tgt)
    for rel in ("ladders_stairs/configs/maplink.dbrow", "skill_agility/configs/maplink_agility.dbrow"):
        p = os.path.join(scripts, rel)
        row, loc = None, None
        pending = None
        for n, line in enumerate(open(p, errors="replace"), 1):
            line = line.strip()
            m = re.match(r"^\[(.+)\]$", line)
            if m:
                if pending:
                    out.append(pending)
                row, loc, pending = m[1], None, None
                continue
            if line.startswith("data=dest,"):
                c = parse_coord(line.split(",", 1)[1])
                pending = dict(source="%s:%d" % (os.path.relpath(p, REPO), n), symbol=row,
                               coord=c, kind="maplink", const_file=None, block=None)
            elif line.startswith("data=loc,") and pending:
                pending["symbol"] = "%s (%s)" % (row, line.split(",", 1)[1])
        if pending:
            out.append(pending)
    return out


def maplink_sources(content):
    srcs = set()
    for rel in ("ladders_stairs/configs/maplink.dbrow", "skill_agility/configs/maplink_agility.dbrow"):
        for line in open(os.path.join(content, "server/scripts", rel), errors="replace"):
            if line.startswith("data=src,"):
                c = parse_coord(line.strip().split(",", 1)[1])
                if c:
                    srcs.add(c)
    return srcs


# ---------------------------------------------------------------- collision

class Collision:
    """reach.Area per (level, map square), cached; a flood that touches a square's edge is re-run in a box
    centred on the landing."""

    def __init__(self):
        self.areas = {}
        self.loc_cache = {}
        self.ids, self.info = reach.load_locs()
        self.loads = 0

    def square(self, level, x, z):
        key = (level, x >> 6, z >> 6)
        if key not in self.areas:
            mx, mz = x >> 6, z >> 6
            self.areas[key] = reach.Area(mx * 64, mx * 64 + 63, mz * 64, mz * 64 + 63, level)
            self.loads += 1
        return self.areas[key]

    def centred(self, level, x, z):
        m = POCKET_LIMIT + 2
        self.loads += 1
        return reach.Area(x - m, x + m, z - m, z + m, level)

    def op_locs(self, mx, mz):
        """[(level, footprint tiles, label)] for every loc with an op in one map square's jl2."""
        key = (mx, mz)
        if key in self.loc_cache:
            return self.loc_cache[key]
        out = []
        p = "%s/maps/m%d_%d.jl2" % (reach.BASE, mx, mz)
        if os.path.exists(p):
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
                w, ln = int(i.get("width", 1)), int(i.get("length", 1))
                if rot & 1:
                    w, ln = ln, w
                foot = [(x + a, z + b) for a in range(w) for b in range(ln)] if 9 <= shape <= 21 else [(x, z)]
                out.append((L, foot, "%s@%d,%d[%s]" % (nm, x, z, ops[0])))
        self.loc_cache[key] = out
        return out

    def classify(self, level, x, z, mlsrc):
        """(class, pocket size or '', note)."""
        a = self.square(level, x, z)
        s = (x, z)
        if s in a.full:
            why = a.opgate.get(s, "")
            off = self.flood(a, s)
            if off is None:
                off = self.flood(self.centred(level, x, z), s)
            off = ("step-off ok" if len(off) >= POCKET_LIMIT else
                   "no step-off" if len(off) == 1 else "step-off region %d" % len(off))
            return "SOLID", "", ("blocked" + (" by " + why if why else "") +
                                 self.blocker_note(level, x, z) + "; " + off)
        region = self.flood(a, s)
        if region is None:
            a = self.centred(level, x, z)
            region = self.flood(a, s)
        if len(region) >= POCKET_LIMIT:
            return "OK", "", ""
        outs = self.ways_out(a, region, level, mlsrc)
        if outs:
            return "OK", len(region), "small region, out via " + ",".join(outs[:3])
        return "POCKET", len(region), "sealed: " + " ".join("%d,%d" % t for t in sorted(region)[:6])

    def flood(self, a, s):
        """The tiles reachable from s (doors shut, op tiles blocked), stopping at POCKET_LIMIT; None when
        a small flood touched the Area's edge (the box may be what closed it)."""
        seen = {s}
        q = deque([s])
        while q and len(seen) < POCKET_LIMIT:
            c = q.popleft()
            for n, _, _ in a.steps(c, {s}, False, False):
                if n not in seen:
                    seen.add(n)
                    q.append(n)
        if len(seen) < POCKET_LIMIT:
            for (tx, tz) in seen:
                if tx in (a.x0, a.x1) or tz in (a.z0, a.z1):
                    return None
        return seen

    def ways_out(self, a, region, level, mlsrc):
        outs = []
        near = set()
        for (x, z) in region:
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    near.add((x + dx, z + dz))
        for (x, z) in region:
            for side, (dx, dz) in reach.DX.items():
                d = a.doorw.get((x, z, side))
                if d:
                    outs.append("door " + d)
                n = (x + dx, z + dz)
                if n not in region and (n in a.optile or n in a.opgate):
                    outs.append("op " + (a.optile.get(n) or a.opgate[n]))
        for t in near:
            if (level, t[0], t[1]) in mlsrc:
                outs.append("maplink src %d,%d" % t)
        squares = {(t[0] >> 6, t[1] >> 6) for t in near}
        for mx, mz in squares:
            for L, foot, label in self.op_locs(mx, mz):
                if L == level and any(f in near for f in foot):
                    outs.append("loc " + label)
        return list(dict.fromkeys(outs))

    def blocker_note(self, level, x, z):
        names = []
        for mx, mz in {((x + dx) >> 6, (z + dz) >> 6) for dx in (-4, 0) for dz in (-4, 0)}:
            p = "%s/maps/m%d_%d.jl2" % (reach.BASE, mx, mz)
            if not os.path.exists(p):
                continue
            for line in open(p):
                m = re.match(r"^(\d+) (\d+) (\d+): (\d+) (\d+)(?: (\d+))?", line)
                if not m:
                    continue
                L, lx, lz, lid, shape, rot = (int(m[1]), mx * 64 + int(m[2]), mz * 64 + int(m[3]),
                                              int(m[4]), int(m[5]), int(m[6] or 0))
                if L != level or not (9 <= shape <= 22):
                    continue
                nm = self.ids.get(lid, str(lid))
                i = self.info.get(nm, {})
                w, ln = int(i.get("width", 1)), int(i.get("length", 1))
                if rot & 1:
                    w, ln = ln, w
                if shape == 22:
                    # reach.Area's floor-decoration stamp: blockwalk=1 and active (ops imply active)
                    w = ln = 1
                    if not (i.get("blockwalk") == "1" and
                            (i.get("active") == "1" or ("active" not in i and reach.ops_of(i)))):
                        continue
                if lx <= x < lx + w and lz <= z < lz + ln and i.get("blockwalk") != "0":
                    names.append("%s@%d,%d" % (nm, lx, lz))
        return (" (" + ",".join(dict.fromkeys(names)) + ")") if names else " (floor flag)"


def classify_batch(coords, mlsrc, root=None):
    """({coord: verdict}, areas loaded) for landings grouped by map square (one worker's share)."""
    if root:
        reach.set_root(root)   # a spawned worker re-imports reach at its default root
    col = Collision()
    return {c: col.classify(*c, mlsrc) for c in sorted(coords)}, col.loads


# ---------------------------------------------------------------- quests

def load_queue():
    rows = []
    p = os.path.join(REPO, "test/quests/QUEUE.tsv")
    lines = open(p).read().splitlines()
    hdr = lines[0].split("\t")
    for line in lines[1:]:
        if line.strip():
            rows.append(dict(zip(hdr, line.split("\t"))))
    return rows


def quest_tiles(queue):
    """{quest_dir: sorted [(x, z)]} named by each quest's test lua."""
    out = {}
    for r in queue:
        p = os.path.join(REPO, "test/quests/%s.lua" % r["test_id"])
        if not os.path.exists(p):
            continue
        tiles = {(int(m[1]), int(m[2])) for m in re.finditer(r"\b(\d{4})\s*,\s*(\d{4,5})\b", open(p).read())}
        out[r["quest_dir"]] = sorted(tiles)
    return out


def quests_for(row, queue_dirs, qtiles):
    hits = []
    for path in (row["source"], row.get("const_file") or ""):
        m = re.search(r"/quests/(quest_[a-z0-9_]+)/", path)
        if m and m[1] in queue_dirs and m[1] not in hits:
            hits.append(m[1])
    if row["coord"]:
        _, x, z = row["coord"]
        for q, tiles in qtiles.items():
            if q in hits:
                continue
            k = bisect.bisect_left(tiles, (x - 2, -1))
            while k < len(tiles) and tiles[k][0] <= x + 2:
                if abs(tiles[k][1] - z) <= 2:
                    hits.append(q + "~lua")
                    break
                k += 1
    return hits


def area_of(source):
    m = re.search(r"server/scripts/(.+?)/(?:scripts|configs)/", source)
    if m:
        return m[1]
    m = re.search(r"server/scripts/([^/]+)/", source)
    return m[1] if m else source


def print_groups(rows, status, limit):
    """SOLID/POCKET hits grouped by the quest whose script holds them (else the script area), one line
    per distinct (class, symbol, tile), with every quest the hit touches."""
    groups = defaultdict(dict)
    for r in rows:
        if r["cls"] not in ("SOLID", "POCKET"):
            continue
        direct = [q.split("(")[0] for q in r["qcol"].split(",") if q and "~lua" not in q]
        owner = direct[0] if direct else area_of(r["source"])
        sym = re.sub(r"^\$[a-z_0-9]+=", "", r["symbol"])
        lv, x, z = r["coord"]
        key = (r["cls"], sym, x, z, lv)
        g = groups[owner].setdefault(key, dict(sites=[], quests=set(), note=r["note"], size=r["size"],
                                               triage=set()))
        g["triage"].add(r.get("triage", "").split(":")[0] if r.get("triage", "").startswith("moved-on")
                        else r.get("triage", "") or "-")
        g["sites"].append(r["source"].split("server/scripts/")[-1])
        g["quests"].update(q for q in r["qcol"].split(",") if q)
    order = sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0]))
    print("\nSOLID/POCKET by quest/area (%d groups, %d distinct landings):" % (
        len(order), sum(len(v) for _, v in order)))
    for owner, hits in order[:limit or None]:
        print("%s%s  [%d]" % (owner, ("(%s)" % status[owner]) if owner in status else "", len(hits)))
        for (cls, sym, x, z, lv), g in sorted(hits.items(), key=lambda kv: (kv[0][0] != "POCKET", kv[0][1])):
            extra = sorted(q for q in g["quests"] if not q.startswith(owner + "("))
            print("  %-6s %s %d,%d,%d%s  x%d %s%s" % (
                cls, sym, x, z, lv, (" size=%s" % g["size"]) if g["size"] != "" else "", len(g["sites"]),
                g["sites"][0], ("  also " + ",".join(extra)) if extra else ""))
            print("         %s  [%s]" % (g["note"][:140], ",".join(sorted(g["triage"]))))
    if limit and len(order) > limit:
        print("... %d more groups (--groups 0 prints all)" % (len(order) - limit))


# ---------------------------------------------------------------- main

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", help="checkout whose OSRS-Content is read (default: this one)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1),
                    help="worker processes, each owning whole map squares (default min(4, cpus))")
    ap.add_argument("--groups", type=int, default=40,
                    help="SOLID/POCKET groups (quest or area) to print, most hits first; 0 = all")
    args = ap.parse_args(argv)
    if args.root:
        reach.set_root(args.root)
    content = reach.BASE
    out = args.out or os.path.join(REPO, "build/orchestrator/landing_audit.tsv")
    t0 = time.time()
    consts = load_constants(content)
    rows = collect(content, consts)
    t1 = time.time()
    mlsrc = maplink_sources(content)
    by_square = defaultdict(list)
    for c in {r["coord"] for r in rows if r["coord"]}:
        by_square[(c[0], c[1] >> 6, c[2] >> 6)].append(c)
    batches = [[] for _ in range(max(1, args.jobs))]
    for k, key in enumerate(sorted(by_square)):
        batches[k % len(batches)].extend(by_square[key])
    verdict, loads = {}, 0
    if args.jobs > 1:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(args.jobs) as ex:
            for got, n in ex.map(classify_batch, batches, [mlsrc] * len(batches),
                                  [args.root] * len(batches)):
                verdict.update(got)
                loads += n
    else:
        verdict, loads = classify_batch(batches[0], mlsrc, args.root)
    t2 = time.time()
    queue = load_queue()
    status = {q["quest_dir"]: q["status"] for q in queue}
    qtiles = quest_tiles(queue)
    counts = defaultdict(int)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as fh:
        fh.write("source\tsymbol\tcoord\tlevel\tclass\tpocket_size\tkind\tquests\tnote\ttriage\n")
        for r in rows:
            if r["coord"]:
                lv, x, z = r["coord"]
                cls, size, note = verdict[r["coord"]]
                coord, level = "%d,%d" % (x, z), str(lv)
            else:
                cls, size, note, coord, level = "UNRESOLVED", "", "", "", ""
            counts[cls] += 1
            qs = quests_for(r, status, qtiles) if cls in ("SOLID", "POCKET") else []
            qcol = ",".join("%s(%s)" % (q, status.get(q.replace("~lua", ""), "?")) for q in qs)
            r.update(cls=cls, size=size, note=note, qcol=qcol)
            fh.write("\t".join(re.sub(r"\s+", " ", str(v)) for v in (
                r["source"], r["symbol"], coord, level, cls, size, r["kind"], qcol, note,
                r.get("triage", ""))) + "\n")
    t3 = time.time()
    if not args.quiet:
        uniq = defaultdict(set)
        for c, v in verdict.items():
            uniq[v[0]].add(c)
        print("landings %d (unique tiles %d), map areas loaded %d" % (len(rows), len(verdict), loads))
        for k in ("OK", "SOLID", "POCKET", "UNRESOLVED"):
            print("  %-10s %5d rows%s" % (k, counts[k], "  (%d tiles)" % len(uniq[k]) if k in uniq else ""))
        print("time: collect %.1fs classify %.1fs report %.1fs total %.1fs" % (t1 - t0, t2 - t1, t3 - t2, t3 - t0))
        print("report:", os.path.relpath(out, REPO))
        print_groups(rows, status, args.groups)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
