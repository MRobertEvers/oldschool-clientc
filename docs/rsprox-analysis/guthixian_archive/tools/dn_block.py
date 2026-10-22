#!/usr/bin/env python3
"""Dreadnoth: does a raider play their block (defend) seq when each of the
room's mechanics hits them? Per mechanic, of the ticks it splatted a raider,
how many carry a defend seq on that raider that tick.

    python3 dn_block.py <all-players txt>...
"""
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import dn_phase as P

DEF = ("block", "defend", "defence", "parry", "_def")


def main():
    tot = Counter()
    blk = Counter()
    seqs = defaultdict(Counter)
    for path in sys.argv[1:]:
        s = P.parse(path)
        pos = P.positions(s)
        slam1, shots, land, land_shot = set(), set(), set(), set()
        for t in s.ticks:
            if t in s.boss and "dreadnoth_jump01" in s.boss[t][2]:
                slam1.add(t + 1)
                shots.update({t + 4, t + 5, t + 6})
            if t in s.boss and "dreadnoth_jump02" in s.boss[t][2]:
                land.add(t + 3)
                land_shot.add(t + 6)
        face = {}
        added = set()
        prevb = set()
        for t in s.ticks:
            if t in s.boss_adds:
                added, prevb = set(), set()
            added |= s.blood_add.get(t, set())
            now = {x for x in added if not (x in s.blood_gone and s.blood_gone[x] <= t)}
            jat = defaultdict(int)
            for idx, v in s.jel.get(t, {}).items():
                if v[3]:
                    face[idx] = v[3]
                if "jelly_attack" in v[2] and idx in face:
                    jat[face[idx]] += 1
            melee = t in s.boss and "dreadnoth_melee01" in s.boss[t][2]
            sw = P.boss_at(s, t)
            for name, (a, b) in pos[t].items():
                if not (a[0] // 64 == 65 and a[1] // 64 == 88):
                    continue
                v = s.pl.get(t, {}).get(name)
                if not v or not v[3]:
                    continue
                kinds = []
                if jat[name]:
                    kinds.append("jelly")
                if melee and sw and P.box_dist(sw, 5, a) <= 1:
                    kinds.append("melee")
                if t in slam1:
                    kinds.append("slam")
                if t in shots:
                    kinds.append("slam shot")
                if t in land:
                    kinds.append("landing")
                if t in land_shot:
                    kinds.append("landing shot")
                if a in prevb and any(x in (7, 8) for _, x in v[3]):
                    kinds.append("blood")
                key = "+".join(kinds) if kinds else "other"
                d = [q for q in v[4] if any(k in q for k in DEF)]
                tot[key] += 1
                blk[key] += bool(d)
                for q in v[4]:
                    seqs[key][q] += 1
            prevb = now
    for k in sorted(tot, key=lambda k: -tot[k]):
        print("%-28s %4d/%-4d defend seq   seqs: %s" % (k, blk[k], tot[k], seqs[k].most_common(4)))


if __name__ == "__main__":
    main()
