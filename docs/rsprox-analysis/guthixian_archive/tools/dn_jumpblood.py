#!/usr/bin/env python3
"""Blood (tfa_jelly_boss_damage_tile) dropped at Dreadnoth's jumps: per slam
(dreadnoth_jump01 at J) and per enrage leap (dreadnoth_jump02 at L), the ticks
the tiles come on, their offsets from his south-west tile, the rotation, and
the raiders in the room, with the splat and sound that come with each.

    python3 dn_jumpblood.py <all-players txt>... [--rows]
"""
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dn_phase as P

ROT = re.compile(r"^ {4}\[loc_add_change\w*\] id=tfa_jelly_boss_damage_tile \(\d+\), coord=\((\d+), (\d+), \d\), shape=(\d+), rotation=(\d+)")
SND = re.compile(r"^ {4}\[sound_area\] id=12387, loops=(\d+), range=(\d+), coord=\((\d+), (\d+), \d\)(.*)")


def extra(path):
    rot = {}
    snd = defaultdict(set)
    tick = None
    for line in open(path, errors="replace"):
        m = P.TICK.match(line.rstrip("\n"))
        if m:
            tick = int(m.group(1))
            continue
        m = ROT.match(line)
        if m:
            rot[(tick, (int(m.group(1)), int(m.group(2))))] = (int(m.group(3)), int(m.group(4)))
        m = SND.match(line)
        if m:
            snd[tick].add(((int(m.group(3)), int(m.group(4))), m.group(1), m.group(2), m.group(5).strip()))
    return rot, snd


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    rows = "--rows" in sys.argv
    stat = defaultdict(Counter)
    for path in paths:
        s = P.parse(path)
        pos = P.positions(s)
        rot, snd = extra(path)
        tag = path[-8:-4]
        tset = set(s.ticks)
        blood = set()
        owned = set()
        # every blood add, so the leftovers (jelly deaths, proj02) show too
        events = []
        for t in s.ticks:
            if t in s.boss and "dreadnoth_jump01" in s.boss[t][2]:
                events.append(("slam", t, s.boss[t][1]))
            if t in s.boss and "dreadnoth_jump02" in s.boss[t][2]:
                events.append(("leap", t, s.boss[t][1]))
        for kind, t0, sw in events:
            raiders = [n for n, (a, b) in pos[t0].items() if b[0] // 64 == 65 and b[1] // 64 == 88]
            for k in range(0, 6):
                t = t0 + k
                tiles = sorted(s.blood_add.get(t, ()))
                if not tiles:
                    continue
                # exclude a jelly's death tile on the tick (a jelly_death that tick on it)
                dead = set()
                for tt in (t, t - 1, t - 2, t - 3):
                    for idx, v in s.jel.get(tt, {}).items():
                        if "jelly_death" in v[2]:
                            dead.add(v[1])
                mine = [x for x in tiles if x not in dead]
                if not mine:
                    continue
                offs = [(x[0] - sw[0], x[1] - sw[1]) for x in mine]
                stat[kind + " tick"]["+%d" % k] += 1
                stat[kind + " count@+%d by raiders" % k][(len(raiders), len(mine))] += 1
                for o, x in zip(offs, mine):
                    stat[kind + " dx"][o[0]] += 1
                    stat[kind + " dz"][o[1]] += 1
                    r = rot.get((t, x))
                    if r:
                        stat[kind + " shape,rotation"][r] += 1
                    sn = [z for z in snd.get(t, ()) if z[0] == x]
                    stat[kind + " sound"][tuple(sn[0][1:]) if sn else None] += 1
                    sp = [a for a in s.anims.get(t, ()) if a[0] == "tob_bloat_blood_splat" and a[1] == x]
                    stat[kind + " splat delay"][sp[0][2] if sp else None] += 1
                if rows:
                    print(tag, kind, t0, "+%d" % k, "sw", sw, "raiders", len(raiders), "blood", offs, "dead jellies", sorted(dead & set(tiles)))
    for k in sorted(stat):
        print(k, sorted(stat[k].items(), key=str))


if __name__ == "__main__":
    main()
