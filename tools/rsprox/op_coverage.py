#!/usr/bin/env python3
"""Ops the cache offers that our scripts never answer, by cache and by recording.

Woodcutting (2026-11-16) had 65 tree locs offering "Chop down" that did
nothing; mining (2026-11-17) had 94 rock types offering "Mine" the same way,
among them every Prifddinas rock and every cave-wall ore vein. A green skill
test never sees this: it clicks the records someone already bound.

    python3 tools/rsprox/op_coverage.py static Mine "Chop down" Pickpocket
        every loc/npc whose op label is one of these: placed in the world
        (maps/*.jl2 + .spawn), bound or UNBOUND
    python3 tools/rsprox/op_coverage.py recorded [--archive DIR]
        every loc/npc op clicked in the cached RSProx transcripts, with the
        number of sessions, and whether a trigger answers it
    python3 tools/rsprox/op_coverage.py inventory [--archive DIR]
        recorded inventory ops (opheldN), item-on-item, item-on-loc and
        item-on-npc: UNBOUND (no trigger on either side) or SUSPECT (a
        trigger exists but never names the other item -- often a category
        dispatch, so read it before calling it a gap)

"Bound" = a header `[op|ap<kind><N>,<name>]`, `[..,_<category>]` (overlay
categories included) or the global `[..,_]`, on the record or on its
multiloc/multinpc parent or child. A bound script may still decline: this
finds what has no script at all.
"""
import argparse
import collections
import glob
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ROOT = os.path.join(REPO, "OSRS-Content", "osrs-content")
SCR = os.path.join(ROOT, "server", "scripts")
ARCHIVE = os.path.expanduser("~/.rsprox-index-cache/home-default/.rsprox/binary/archive")
HDR = re.compile(r"^\[([a-z_]+?)([0-9]|u|t)?,([^\]]+)\]", re.M)


def records(path):
    cur, rec = None, {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("[") and line.endswith("]"):
                if cur:
                    yield cur, rec
                cur, rec = line[1:-1], {}
            elif "=" in line and cur:
                k, v = line.split("=", 1)
                if v in ("default", "empty"):
                    continue
                rec[k] = rec[k] + "|" + v if k in rec else v
    if cur:
        yield cur, rec


def load(kind):
    recs = {n: r for n, r in records(os.path.join(ROOT, "configs", "all.%s" % kind))}
    ids = {}
    for line in open(os.path.join(ROOT, "configs", "all.%s.compack" % kind)):
        if "=" in line:
            i, n = line.strip().split("=", 1)
            ids[n] = int(i)
    for p in glob.glob(os.path.join(SCR, "**", "configs", "*.%s" % kind), recursive=True):
        for n, r in records(p):
            recs.setdefault(n, {}).update(r)
    return recs, ids


class Tree:
    def __init__(self):
        self.cats = {}
        for line in open(os.path.join(ROOT, "pack", "category.pack")):
            line = line.strip()
            if line and not line.startswith("//") and "=" in line:
                i, n = line.split("=", 1)
                self.cats[i.strip()] = n.strip()
        self.files = collections.defaultdict(set)  # (trigger, subject) -> files
        self.text = {}
        for p in glob.glob(os.path.join(SCR, "**", "*.rs2"), recursive=True):
            t = open(p, encoding="utf-8", errors="replace").read()
            self.text[p] = t
            for m in HDR.finditer(t):
                self.files[(m.group(1) + (m.group(2) or ""), m.group(3))].add(p)
        self.rec = {k: load(k) for k in ("loc", "npc", "obj")}
        self.multi = {"loc": collections.defaultdict(list), "npc": collections.defaultdict(list)}
        for kind, key in (("loc", "multiloc"), ("npc", "multinpc")):
            for n, r in self.rec[kind][0].items():
                for k, v in r.items():
                    if re.fullmatch(key + r"\d*", k):
                        for c in v.split("|"):
                            if c != "-1":
                                self.multi[kind][c].append(n)
                                self.multi[kind][n].append(c)

    def cat(self, kind, name):
        c = self.rec[kind][0].get(name, {}).get("category")
        if not c:
            return None
        c = c.split("|")[-1]
        return self.cats.get(c, c)

    def subjects(self, kind, name):
        c = self.cat(kind, name)
        return [name] + (["_" + c] if c else [])

    def bound_files(self, trig, kind, name, glob_ok=True, multi=True):
        fs = set()
        names = [name] + (self.multi[kind].get(name, []) if multi and kind in self.multi else [])
        for nm in names:
            for s in self.subjects(kind, nm) + (["_"] if glob_ok else []):
                fs |= self.files.get((trig, s), set())
        return fs

    def op_bound(self, kind, name, opn):
        return self.bound_files("op%s%d" % (kind, opn), kind, name) | \
            self.bound_files("ap%s%d" % (kind, opn), kind, name)

    def names_obj(self, files, obj):
        c = self.cat("obj", obj)
        pat = re.compile(r"\b(%s%s)\b" % (re.escape(obj), "|_?" + re.escape(c) if c else ""))
        return any(pat.search(self.text[f]) for f in files)


def placements(tree):
    idname = {k: {i: n for n, i in tree.rec[k][1].items()} for k in ("loc", "npc")}
    placed = {"loc": collections.Counter(), "npc": collections.Counter()}
    for p in glob.glob(os.path.join(ROOT, "maps", "*.jl2")):
        sec = None
        for line in open(p):
            if line.startswith("===="):
                sec = line.strip()
            elif sec == "==== LOC ====" and ":" in line:
                try:
                    placed["loc"][idname["loc"].get(int(line.split(":")[1].split()[0]), "?")] += 1
                except ValueError:
                    pass
    for p in glob.glob(os.path.join(SCR, "**", "*.spawn"), recursive=True):
        sec = None
        for line in open(p, encoding="utf-8", errors="replace"):
            if line.startswith("===="):
                sec = line.split()[1]
                continue
            s = line.split()
            if s and not line.startswith("//") and sec in ("NPC", "LOC"):
                placed[sec.lower()][s[0]] += 1
    return placed


def scan(archive, specs):
    """One pass over the archive's transcripts. specs = [(pattern, "$1 $2")];
    returns, per spec, a list of (file, [groups]). grep -E (rg if installed)
    finds candidate lines once; Python's re pulls the groups per spec."""
    rxs = [(re.compile(p), [int(g) for g in re.findall(r"\$(\d)", r)]) for p, r in specs]
    eres = [p.replace(r"\d", "[0-9]").replace("(?:", "(") for p, _ in specs]
    if shutil.which("rg"):
        cmd = ["rg", "--no-line-number", "-H", "-g", "*.txt"]
        for p, _ in specs:
            cmd += ["-e", p]
    else:
        cmd = ["grep", "-r", "-H", "-E", "--include=*.txt"]
        for e in eres:
            cmd += ["-e", e]
    out = [[] for _ in specs]
    proc = subprocess.Popen(cmd + [archive], stdout=subprocess.PIPE, text=True, errors="replace")
    for line in proc.stdout:
        f, rest = line.rstrip("\n").split(":", 1)
        for i, (rx, order) in enumerate(rxs):
            m = rx.match(rest)
            if m:
                out[i].append((os.path.basename(f), [m.group(g) or "" for g in order]))
                break
    proc.wait()
    return out


def cmd_static(tree, verbs):
    verbs = {v.lower() for v in verbs}
    placed = placements(tree)
    rows = []
    for kind in ("loc", "npc"):
        for n, r in tree.rec[kind][0].items():
            for o in range(1, 6):
                op = r.get("op%d" % o)
                if not op or op.lower() not in verbs:
                    continue
                pl = placed[kind][n] + sum(placed[kind][c] for c in tree.multi[kind].get(n, []))
                rows.append((op, not tree.op_bound(kind, n, o), -pl, kind, n, r.get("name", ""), o, tree.cat(kind, n)))
    for op, unbound, pl, kind, n, disp, o, c in sorted(rows):
        print("%s\t%s\top%d\t%s\t%s\tplaced=%d\tcat=%s\t%s" % (kind, op, o, n, disp, -pl, c, "UNBOUND" if unbound else "bound"))


def cmd_recorded(tree, archive):
    sess, clicks = collections.defaultdict(set), collections.Counter()
    pat = r"^    \[(oploc[1-5]|opnpc[1-5])(?:_v\d)?\] (?:id=|npc=\(index=\d+, id=)([a-z0-9_]+) \(\d+\)(?:, multinpc=([a-z0-9_]+))?"
    for f, p in scan(archive, [(pat, "$1 $2 $3")])[0]:
        key = (p[0][:-1][2:], int(p[0][-1]), p[1], p[2])
        sess[key].add(f)
        clicks[key] += 1
    rows = []
    for (kind, o, name, multi), fs in sess.items():
        recs = tree.rec[kind][0]
        if name not in recs:
            continue
        lab = (recs.get(multi, {}) if multi else {}).get("op%d" % o) or recs[name].get("op%d" % o) or "-"
        ok = tree.op_bound(kind, name, o) or (multi and tree.op_bound(kind, multi, o))
        rows.append((len(fs), clicks[(kind, o, name, multi)], kind, o, name, multi, lab, "bound" if ok else "UNBOUND"))
    for r in sorted(rows, reverse=True):
        print("\t".join(map(str, r)))


def cmd_inventory(tree, archive):
    HELD = r"^    \[if_buttonx\] com=inventory:items \(149:0\), sub=\d+, obj=([a-z0-9_]+) \(\d+\), op=(\d+)"
    HELDU = r"^    \[if_buttont\] selectcom=inventory:items \(149:0\), selectedsub=\d+, selectedobj=([a-z0-9_]+) \(\d+\), targetcom=inventory:items \(149:0\), targetsub=\d+, targetobj=([a-z0-9_]+)"
    LOCU = r"^    \[oploct(?:_v\d)?\] id=([a-z0-9_]+) \(\d+\), coord=\([^)]*\), com=inventory:items \(149:0\), sub=\d+, obj=([a-z0-9_]+)"
    NPCU = r"^    \[opnpct(?:_v\d)?\] npc=\(index=\d+, id=([a-z0-9_]+) \(\d+\)[^)]*\)\), com=inventory:items \(149:0\), sub=\d+, obj=([a-z0-9_]+)"
    found = scan(archive, [(HELD, "$2 $1"), (HELDU, "$1 $2"), (LOCU, "$1 $2"), (NPCU, "$1 $2")])

    def agg(i):
        d, n = collections.defaultdict(set), collections.Counter()
        for f, p in found[i]:
            d[tuple(p)].add(f)
            n[tuple(p)] += 1
        return d, n
    objs = tree.rec["obj"][0]
    out = []
    d, n = agg(0)
    for (op, obj), fs in d.items():
        op = int(op)
        if not 2 <= op <= 6 or obj not in objs:
            continue
        lab = objs[obj].get("ifop%d" % (op - 1), "-")
        if lab in ("Wield", "Wear", "Drop", "Destroy", "-"):
            continue
        ok = tree.bound_files("opheld%d" % (op - 1), "obj", obj, multi=False)
        out.append(("held", len(fs), n[(str(op), obj)], obj, "ifop%d=%s" % (op - 1, lab), "bound" if ok else "UNBOUND"))
    d, n = agg(1)
    for (a, b), fs in d.items():
        if a not in objs or b not in objs:
            continue
        fa = tree.bound_files("opheldu", "obj", a, False, False)
        fb = tree.bound_files("opheldu", "obj", b, False, False)
        v = "UNBOUND" if not fa and not fb else "bound" if (fa and tree.names_obj(fa, b)) or (fb and tree.names_obj(fb, a)) else "SUSPECT"
        out.append(("heldu", len(fs), n[(a, b)], "%s -> %s" % (a, b), "", v))
    for kind, trig, idx in (("loc", "oplocu", 2), ("npc", "opnpcu", 3)):
        d, n = agg(idx)
        for (t, obj), fs in d.items():
            if t not in tree.rec[kind][0]:
                continue
            bfs = tree.bound_files(trig, kind, t)
            v = "UNBOUND" if not bfs else "bound" if tree.names_obj(bfs, obj) else "SUSPECT"
            out.append((kind + "u", len(fs), n[(t, obj)], "%s -> %s" % (obj, t), "", v))
    for r in sorted(out, key=lambda x: (x[0], -x[1])):
        print("\t".join(map(str, r)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=("static", "recorded", "inventory"))
    ap.add_argument("verbs", nargs="*", help="static: op labels to audit")
    ap.add_argument("--archive", default=ARCHIVE)
    a = ap.parse_args()
    tree = Tree()
    if a.mode == "static":
        if not a.verbs:
            sys.exit("static needs at least one op label (e.g. Mine)")
        cmd_static(tree, a.verbs)
    elif a.mode == "recorded":
        cmd_recorded(tree, a.archive)
    else:
        cmd_inventory(tree, a.archive)


if __name__ == "__main__":
    main()
