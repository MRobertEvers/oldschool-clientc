#!/usr/bin/env python3
"""Every raider dodge of Ezekiel's fights (`tfa_ezekiel_roll` / `_strafe`) against
the rules of ezekiel_plan.md §7, over the `--all-players --bas` transcripts:

    python3 ez_dodge_audit.py [idx ...]      # default 4300 4301 4304 4314 4315

- the exact move: tiles, cycles, angle (a roll the way it goes, a strafe the
  other way and 4 more);
- the face block on the dodge's tick: an angle to what the raider faced, from
  the tile the dodge leaves;
- the window: the cage's strike ticks (V+4..V+6), or the flight tick of a sword
  whose path holds the raider; ordinary steps from standing on those ticks.
"""
import math
import os
import re
import sys
from collections import Counter

ARCHIVE = os.path.expanduser("~/.rsprox-index-cache/home-all-bas/.rsprox/binary/archive")
ENT = re.compile(r"^ {8}\[(\w+)\] (player|npc)=\(index=(\d+), (?:id=(\w+) \(\d+\)|name=([^,]+))"
                 r"(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+), \d\))?")


def parse(path):
    t = 0
    who = None
    pl, mv, flies, vol, death, ntile = {}, {}, {}, set(), set(), {}
    for line in open(path, errors="replace"):
        m = re.match(r"^\[(\d+)\]", line)
        if m:
            t = int(m.group(1))
            who = None
            continue
        m = ENT.match(line)
        if m:
            kind, ent, idx, nid, name = m.groups()[:5]
            a = (int(m.group(6)), int(m.group(7)))
            b = (int(m.group(8)), int(m.group(9))) if m.group(8) else a
            if ent == "player":
                who = ("pl", name)
                mv.setdefault(name, {})[t] = (kind, a, b)
            else:
                who = ("npc", nid)
                ntile.setdefault(nid + idx, {})[t] = b
                if nid == "tfa_ezekiel_sword" and kind == "teleport":
                    flies.setdefault(t, []).append((a, b))
            continue
        if re.match(r"^ {4}\[", line):
            who = None
            continue
        if who is None or not line.startswith("            ") or line.startswith("                "):
            continue
        s = line.strip()
        if who[0] == "pl":
            pl.setdefault(who[1], {}).setdefault(t, []).append(s)
        elif who[1] == "tfa_ezekiel_finale_vfx" and "sword_telegraph" in s and "slot=" not in s:
            vol.add(t)
        elif who[1] == "tfa_ezekiel" and "finale_death01" in s:
            death.add(t)
    return pl, mv, flies, sorted(vol), death, ntile


def yaw(dx, dz):
    """Cut toward zero, as Jagex's: (2, -1) 1688, not 1687."""
    return int(math.atan2(-dx, -dz) * 1024 / math.pi) & 2047


def near(a, b):
    return a == b


def in_path(p, flies):
    for f, to in flies:
        x0, x1 = sorted((f[0], to[0]))
        z0, z1 = sorted((f[1], to[1]))
        if x0 <= p[0] <= x1 + 2 and z0 <= p[1] <= z1 + 2:
            return True
    return False


def main():
    ids = [int(a) for a in sys.argv[1:]] or [4300, 4301, 4304, 4314, 4315]
    C = Counter()
    for i in ids:
        pl, mv, flies, vol, death, ntile = parse(f"{ARCHIVE}/idx{i}.txt")
        cage = {v + k for v in vol for k in (4, 5, 6)}
        for n, ticks in pl.items():
            target = None
            for t in sorted(ticks):
                bl = ticks[t]
                seq = [b for b in bl if b.startswith("[sequence] id=tfa_ezekiel_")]
                if seq:
                    kind = "roll" if "roll" in seq[0] else "strafe"
                    _, a, b = mv[n][t]
                    dx, dz = b[0] - a[0], b[1] - a[1]
                    xm = [x for x in bl if x.startswith("[exactmove]")][0]
                    d2, ang = map(int, re.search(r"delay2=(\d+), angle=(\d+)", xm).groups())
                    want = yaw(dx, dz) if kind == "roll" else (yaw(dx, dz) + 1028) & 2047
                    C[f"{kind}: {max(abs(dx), abs(dz))} tiles, {d2} cycles"] += 1
                    C[f"{kind}: angle on the rule"] += ang == want
                    C[f"{kind}s"] += 1
                    fa = [int(x.split("angle=")[1]) for x in bl if x.startswith("[facing] angle=")]
                    if target:
                        C["dodges breaking a face"] += 1
                        tx, tz = target
                        C["  face angle to it from the start tile"] += bool(fa) and near(fa[0], yaw(tx - a[0], tz - a[1]))
                    else:
                        C["dodges with no face to break, no face block"] += not fa
                        C["dodges with no face to break"] += 1
                    win = "cage V+4..6" if t in cage else ("flight tick, in a path" if t in flies and in_path(a, flies[t]) else "outside every window")
                    C["window: " + win] += 1
                for x in bl:
                    m = re.match(r"\[facing\] (?:npc|player)=\(index=\d+, (?:id=(\w+) \(\d+\)|name=[^,]+), coord=\((\d+), (\d+)", x)
                    if m:
                        size = 3 if m.group(1) in ("tfa_ezekiel", "tfa_ezekiel_sword") else 1
                        target = (int(m.group(2)) + (size - 1) / 2, int(m.group(3)) + (size - 1) / 2)
                    elif x.startswith("[facing]"):
                        target = None
        # ordinary steps from standing on window ticks
        for n, m in mv.items():
            for t, (kind, a, b) in m.items():
                if kind not in ("walk", "run") or m.get(t - 1, ("idle",))[0] in ("walk", "run", "teleport"):
                    continue
                if any(d < t for d in death if t - d < 15):
                    continue
                if t in cage:
                    C["ordinary from standing: cage strike tick"] += 1
                elif t in flies:
                    C["ordinary from standing: flight tick, " + ("in a path" if in_path(a, flies[t]) else "outside")] += 1
    for k in sorted(C):
        print(f"{C[k]:5d}  {k}")


if __name__ == "__main__":
    main()
