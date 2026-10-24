#!/usr/bin/env python3
"""Every npc type a piece of content spawns: does it move the way the recordings say?

The generated npc blocks (_explicit/configs/server_fields.generated.npc) give EVERY npc
defaultmode=wander, wanderrange=5. A type the content never overrides wanders, on top of
whatever its script does with it. The Colosseum shipped that way (2026-10-24): the solar
flares' scripted 5x5 square came out as one-tick diagonal steps off the square, because
the pathing audit looked at the monsters and never at the modifier npcs or the gibs.

    # 1. every type the content spawns, its effective movement params; WANDERS flagged
    python3 tools/rsprox/npc_movement_audit.py defaults <content dir>

    # 2. per type: steps per life and the gaps between steps, recordings against ours
    python3 tools/rsprox/npc_movement_audit.py paths --rec <transcript|@list>... \\
        --ours <ticklog.tsv>... [--match <regex on the npc name>]

<content dir> is a directory under OSRS-Content/osrs-content/server/scripts (e.g.
minigames/minigame_colosseum). "spawned": a .rs2 under it names the type in npc_add /
npc_changetype, or a .spawn lists it; "named": mentioned anywhere else in the content (a
type spawned through a variable); "block only": the content's .npc has a block for it.

`paths` prints, per npc type (recordings by name, ours by id through all.npc.compack):
lives, steps per life, the step-gap histogram (ticks between consecutive steps: 1 = walk
or run every tick, 2 = a crawl or a two-tick script), the recorded move kinds (walk /
run / crawl / teleport), and for ours the share of diagonal steps and of steps that undo
the previous one (a wanderer's signature). Every type with steps on one side and none on
the other, or a different gap histogram, is a finding.
"""
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPTS = os.path.join(ROOT, "OSRS-Content", "osrs-content", "server", "scripts")
CONFIGS = os.path.join(ROOT, "OSRS-Content", "osrs-content", "configs")
FIELDS = ("defaultmode", "wanderrange", "moverestrict", "huntmode", "nomove")


def npc_blocks(text):
    out = {}
    for m in re.finditer(r"^\[([a-z0-9_]+)\]\n(.*?)(?=^\[|\Z)", text, re.M | re.S):
        out[m.group(1)] = dict(re.findall(r"^(%s)=(\S+)" % "|".join(FIELDS), m.group(2), re.M))
    return out


def cmd_defaults(content):
    base = os.path.join(SCRIPTS, content)
    assert os.path.isdir(base), base
    spawned = Counter()
    own = {}
    for dirpath, _, files in os.walk(base):
        for f in files:
            p = os.path.join(dirpath, f)
            text = open(p, errors="replace").read()
            if f.endswith(".rs2"):
                for m in re.finditer(r"\b(?:npc_add|npc_changetype)\s*\([^;]*?,\s*([a-z][a-z0-9_]*)\s*[,)]", text):
                    spawned[m.group(1)] += 1
            elif f.endswith(".spawn"):
                for m in re.finditer(r"^\s*([a-z][a-z0-9_]*)\s", text, re.M):
                    spawned[m.group(1)] += 1
            elif f.endswith(".npc"):
                own.update(npc_blocks(text))
    gen = npc_blocks(open(os.path.join(SCRIPTS, "_explicit", "configs", "server_fields.generated.npc"),
                          errors="replace").read())
    known = set(gen) | set(own)
    # A type spawned through a variable (npc_add($at, $type, ..)) is named somewhere else
    # in the content: a table, an enum, a constant, a comparison. Every npc name the
    # content mentions is a candidate.
    named = Counter()
    for dirpath, _, files in os.walk(base):
        for f in files:
            if f.endswith((".rs2", ".constant", ".enum", ".dbrow", ".spawn")):
                for w in re.findall(r"\b[a-z][a-z0-9_]{3,}\b", open(os.path.join(dirpath, f), errors="replace").read()):
                    if w in gen:
                        named[w] += 1
    names = sorted(n for n in set(spawned) | set(own) | set(named) if n in known)
    wanders = 0
    for n in names:
        eff = dict(gen.get(n, {}))
        eff.update(own.get(n, {}))
        mode = eff.get("defaultmode", "?")
        flag = ""
        if mode == "wander" or (eff.get("wanderrange") not in (None, "0") and mode != "none"):
            flag = "  <-- WANDERS" + ("" if n in own else " (generated default, no block of its own)")
            wanders += 1
        where = "spawned" if spawned[n] else ("named" if named[n] else "block only")
        print(f"{n:44s} {where:10s} mode={mode:7s} range={eff.get('wanderrange', '?'):3s} "
              f"moverestrict={eff.get('moverestrict', '?')}{flag}")
    print(f"{len(names)} types, {wanders} wander: confirm each against its recorded lives (paths)")


def load_names():
    names = {}
    p = os.path.join(CONFIGS, "all.npc.compack")
    for m in re.finditer(r"^(\d+)=(\S+)", open(p, errors="replace").read(), re.M):
        names[int(m.group(1))] = m.group(2)
    return names


def summarise(lives):
    """lives: [[(tick, x, z, kind)], ...] per life, add first."""
    steps = 0
    gaps = Counter()
    kinds = Counter()
    diag = 0
    undo = 0
    for life in lives:
        prev_t = None
        prev_d = None
        for i in range(1, len(life)):
            t, x, z, k = life[i]
            _, px, pz, _ = life[i - 1]
            dx, dz = x - px, z - pz
            if dx == 0 and dz == 0:
                continue
            steps += 1
            kinds[k] += 1
            if prev_t is not None:
                gaps[min(t - prev_t, 9)] += 1
            if dx != 0 and dz != 0:
                diag += 1
            if prev_d is not None and (dx, dz) == (-prev_d[0], -prev_d[1]):
                undo += 1
            prev_t = t
            prev_d = (dx, dz)
    return steps, gaps, kinds, diag, undo


def cmd_paths(args):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from rsprox_colosseum import load, expand  # the generic transcript model
    recs, ours, match = [], [], None
    i = 0
    while i < len(args):
        if args[i] == "--rec":
            i += 1
            while i < len(args) and not args[i].startswith("--"):
                recs.append(args[i])
                i += 1
            continue
        if args[i] == "--ours":
            i += 1
            while i < len(args) and not args[i].startswith("--"):
                ours.append(args[i])
                i += 1
            continue
        if args[i] == "--match":
            match = re.compile(args[i + 1])
            i += 2
            continue
        raise SystemExit("unknown argument " + args[i])
    names = load_names()
    R = defaultdict(list)
    for p in expand(recs):
        d = load(p)
        cur = {}
        for e in d["events"]:
            if e["k"] != "npc":
                continue
            n = e.get("name") or names.get(e.get("nid"), str(e.get("nid")))
            if match and not match.search(str(n)):
                continue
            c = e.get("nc") or e.get("c")
            if not c:
                continue
            if e["mv"] == "add":
                cur[e["idx"]] = [(e["t"], c[0], c[1], "add")]
                R[n].append(cur[e["idx"]])
            elif e["idx"] in cur and e["mv"] in ("walk", "run", "crawl", "teleport", "telejump"):
                cur[e["idx"]].append((e["t"], c[0], c[1], e["mv"]))
    O = defaultdict(list)
    for p in ours:
        cur = {}
        for line in open(p, errors="replace"):
            f = line.rstrip("\n").split("\t")
            if len(f) < 8 or not f[1].isdigit():
                continue
            t, kind = int(f[1]), f[2]
            if kind == "npc_spawn":
                n = names.get(int(f[4]), f[4])
                if match and not match.search(n):
                    continue
                c = int(f[5])
                cur[f[3]] = [(t, (c >> 14) & 0x3FFF, c & 0x3FFF, "add")]
                O[n].append(cur[f[3]])
            elif kind == "npc_free":
                cur.pop(f[3], None)
            elif kind == "npc_tile" and f[3] in cur:
                cur[f[3]].append((t, int(f[4]), int(f[5]), "step"))
    for n in sorted(set(R) | set(O)):
        line = [f"== {n}"]
        for side, lives in (("rec", R.get(n, [])), ("ours", O.get(n, []))):
            if not lives:
                line.append(f"   {side}: no lives")
                continue
            steps, gaps, kinds, diag, undo = summarise(lives)
            g = " ".join(f"{k}:{v}" for k, v in sorted(gaps.items()))
            extra = (" kinds " + " ".join(f"{k}={v}" for k, v in kinds.most_common())) if side == "rec" else ""
            line.append(f"   {side}: {len(lives)} lives, {steps / len(lives):.1f} steps/life, gaps {g or '-'}, "
                        f"diagonal {100 * diag / max(steps, 1):.0f}%, undo {100 * undo / max(steps, 1):.0f}%{extra}")
        print("\n".join(line))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    if sys.argv[1] == "defaults":
        cmd_defaults(sys.argv[2])
    elif sys.argv[1] == "paths":
        cmd_paths(sys.argv[2:])
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
