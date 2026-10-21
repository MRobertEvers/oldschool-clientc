#!/usr/bin/env python3
"""Which tell each splat on a raider follows, room by room, in the Archive's
--all-players transcripts (the party runs are spectated: the recorder takes
no hits, every raider's splats are other players').

For each room (the window its boss family is in view) every tell -- an npc's
seq or spotanim (by npc type), a map anim, a projectile (by id) -- is lined up
with the splats on every raider at tell + 0..MAXDT ticks. A tell that a
mechanic's hit follows shows a tick where nearly every occurrence carries
splats; its values are that mechanic's damage.

    python3 tfa_damage.py <all-players txt>... [--room dreadnoth] [--min 4]
    python3 tfa_damage.py <txt>... --tell 'jump01' --room dreadnoth   one tell, every dt
    python3 tfa_damage.py <txt>... --splats --room xarth              every splat, its tells

Splat types (the viewer's): 17 another's damage, 13 another's block, 67 and
74 the Archive's burns, 65 poison. Prayers are not in a spectated transcript.
"""
import argparse
import re
from collections import Counter, defaultdict

ROOMS = {
    "guardian": r"tfa_guardian",
    "ezekiel": r"tfa_ezekiel",
    "bork": r"tfa_bork|tfa_smork",
    "dreadnoth": r"tfa_jelly_boss",
    "xarth": r"tfa_xarth$",
    "ocellus": r"tfa_ocellus_(p1|p2|p3)",
}
TICK = re.compile(r"^\[(\d+)\]")
NPCHDR = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+)")
PLHDR = re.compile(r"^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+)")
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+)")
SPOT = re.compile(r"\[spotanim\](?: slot=\d+,)? id=([a-z0-9_]+)")
HIT = re.compile(r"\[hit\] id=(\d+), value=(-?\d+)")
MAPANIM = re.compile(r"^ {4}\[map_anim\] id=([a-z0-9_]+)")
PROJ = re.compile(r"^ {4}\[(?:map_)?proj\w*\] id=([a-z0-9_]+)")
# the raiders' own kit, not the room's
KIT = re.compile(r"thrall|glaive|sanguinesti|xbows_|ice_barrage|failedspell|firewave|"
                 r"smoke_barrage|blood_barrage|shadow_barrage|dragon_arrow|dragonbolt|"
                 r"tonalztics|scythe|zaryte|human_|^-1$|^null$")


def parse(path):
    tells = []          # (tick, key)
    splats = []         # (tick, player, type, value)
    present = defaultdict(set)   # tick -> npc types seen
    tick = 0
    cur = None          # ("npc", type) or ("pl", name)
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        m = NPCHDR.match(line)
        if m:
            cur = ("npc", m.group(3))
            present[tick].add(m.group(3))
            continue
        m = PLHDR.match(line)
        if m:
            cur = ("pl", m.group(3))
            continue
        if line.startswith("    [") and not line.startswith("        "):
            cur = None
            m = MAPANIM.match(line)
            if m and not KIT.search(m.group(1)):
                tells.append((tick, "map " + m.group(1)))
            m = PROJ.match(line)
            if m and not KIT.search(m.group(1)):
                tells.append((tick, "proj " + m.group(1)))
            continue
        if not cur or not line.startswith("            "):
            continue
        s = line.strip()
        if cur[0] == "npc":
            m = SEQ.match(s)
            if m and not KIT.search(m.group(1)):
                tells.append((tick, f"{cur[1]} seq {m.group(1)}"))
            m = SPOT.match(s)
            if m and not KIT.search(m.group(1)):
                tells.append((tick, f"{cur[1]} spot {m.group(1)}"))
        else:
            m = HIT.match(s)
            if m:
                splats.append((tick, cur[1], int(m.group(1)), int(m.group(2))))
    return tells, splats, present


def windows(present, pattern):
    rx = re.compile(pattern)
    ticks = sorted(t for t, types in present.items() if any(rx.match(x) for x in types))
    out = []
    for t in ticks:
        if out and t - out[-1][1] <= 20:
            out[-1][1] = t
        else:
            out.append([t, t])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("txt", nargs="+")
    ap.add_argument("--room")
    ap.add_argument("--min", type=int, default=4)
    ap.add_argument("--maxdt", type=int, default=8)
    ap.add_argument("--tell")
    ap.add_argument("--splats", action="store_true")
    a = ap.parse_args()
    rooms = [a.room] if a.room else list(ROOMS)
    for room in rooms:
        occ = Counter()                       # tell -> occurrences
        hitat = defaultdict(lambda: defaultdict(list))   # tell -> dt -> [(value, type)]
        lines = []
        for path in a.txt:
            tells, splats, present = parse(path)
            for w0, w1 in windows(present, ROOMS[room]):
                tw = [(t, k) for t, k in tells if w0 <= t <= w1 + 4]
                sw = [x for x in splats if w0 <= x[0] <= w1 + 4]
                by_tick = defaultdict(list)
                for x in sw:
                    if x[2] in (13, 17, 67, 74, 65, 16, 12):
                        by_tick[x[0]].append(x)
                for t, k in tw:
                    occ[k] += 1
                    for dt in range(a.maxdt + 1):
                        for x in by_tick.get(t + dt, ()):
                            hitat[k][dt].append((x[3], x[2]))
                if a.splats:
                    for x in sw:
                        near = sorted({f"{k}@-{x[0] - t}" for t, k in tw if 0 <= x[0] - t <= 4})
                        lines.append(f"{path.rsplit('/', 1)[-1]} [{x[0]}] {x[1]:14s} t{x[2]} {x[3]:3d}  " + ", ".join(near[:6]))
        print(f"== {room}")
        if a.splats:
            print("\n".join(lines))
            continue
        for k, n in occ.most_common():
            if n < a.min or (a.tell and not re.search(a.tell, k)):
                continue
            best = []
            for dt, vals in hitat[k].items():
                ticks_with = len(vals)
                best.append((ticks_with / n, dt, vals))
            best.sort(reverse=True)
            if not best:
                continue
            if a.tell:
                for frac, dt, vals in sorted(best, key=lambda b: b[1]):
                    v = sorted(x[0] for x in vals)
                    nz = [x for x in v if x > 0]
                    print(f"   {k:44s} n={n:4d} dt={dt} per tell={frac:5.2f} zeros={len(v) - len(nz):4d} "
                          f"types={dict(Counter(x[1] for x in vals))} nonzero={nz[-30:]}")
                continue
            frac, dt, vals = best[0]
            v = [x[0] for x in vals]
            nz = [x for x in v if x > 0]
            print(f"   {k:60s} n={n:4d} best dt={dt} splats/tell={frac:5.2f} zero={len(v) - len(nz):3d}/{len(v):3d} max={max(v) if v else 0:3d} "
                  f"types={dict(Counter(x[1] for x in vals))}")


main()
