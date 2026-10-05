#!/usr/bin/env python3
"""reward_pool_and_cash_out: offline checks over the pinned chest page and posts (no network).
Run: python3 reward_pool_and_cash_out.queries.py  (output kept in reward_pool_and_cash_out.queries.out)"""
import os
import re
from fractions import Fraction as F

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources")
UNIQ = ("Echo", "Sunfire fanatic", "Tonalztics")


def tables():
    cur, out = None, {}
    for ln in open(os.path.join(SRC, "wiki/wiki_Rewards_Chest_Fortis_Colosseum.wikitext"), encoding="utf-8"):
        m = re.match(r"===Wave (\d+)===", ln)
        if m:
            cur = int(m.group(1))
            out[cur] = []
            continue
        m = re.match(r"\{\{DropsLineReward\|name=(.*?)\|quantity=(.*?)\|rarity=([^|}]*)", ln)
        if m and cur:
            n, q, r = m.groups()
            out[cur].append((n, q, F(r) if "/" in r else r))
    return out


def main():
    t = tables()
    print("== per reward wave: line count, sum of fractional lines, unique share, split e/a/t")
    for w, rows in t.items():
        num = [x for x in rows if isinstance(x[2], F)]
        tot = sum(x[2] for x in num)
        un = sum(x[2] for x in num if x[0].startswith(UNIQ))
        e = sum(x[2] for x in num if x[0].startswith("Echo"))
        a = sum(x[2] for x in num if x[0].startswith("Sunfire fanatic"))
        tn = sum(x[2] for x in num if x[0].startswith("Tonal"))
        print(w, "lines", len(rows), "sum", tot, "unique", un, "split", (e / un, a / un, tn / un) if un else "-")
    print("== denominators: old 220-16R (post waves 6-11 = R 7-12), new 180-14R (post waves 3-11 = R 4-12)")
    print("old", [220 - 16 * r for r in range(7, 13)], "new", [180 - 14 * r for r in range(4, 13)])
    print("== wave 4-6 renormalised split (post 9/16 armour, 6/16 echo, tonalztics 0): armour", F(9, 15), "echo", F(6, 15))
    print("== wave-3 normal: 7 x 9/70 + 7 x 1/70 =", 7 * F(9, 70) + 7 * F(1, 70), "; wave 2: 7 x 1/7 =", 7 * F(1, 7))
    print("== wave 4 tier weights of the normal part", F(7 * 5535, 43400 - 350), F(7 * 615, 43400 - 350))


main()

def expected_splinters():
    t = tables()
    tot = F(0)
    for w, rows in t.items():
        for n, q, r in rows:
            if n == "Sunfire splinters":
                tot += F(q) * (1 if r == "Always" else r)
    return tot


print("== expected sunfire splinters per full completion from the transcribed tables:", float(expected_splinters()), "(chest page :117 says 2014.6)")
