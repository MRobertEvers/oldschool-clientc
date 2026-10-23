#!/usr/bin/env python3
"""The Colosseum reward tables from the pinned wiki chest page, as
[(wave, item name, qty_lo, qty_hi, numerator, denominator)].

    python3 tools/rsprox/colosseum/colo_wiki_rewards.py          # print
"""
import os
import re
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
WIKI = os.path.join(ROOT, "docs/minigames/colosseum/sources/wiki/wiki_Rewards_Chest_Fortis_Colosseum.wikitext")


def tables():
    out = []
    wave = None
    for line in open(WIKI, errors="replace"):
        m = re.match(r"^===Wave (\d+)===", line)
        if m:
            wave = int(m.group(1))
            continue
        if line.startswith("==") and not line.startswith("==="):
            wave = None if "Rewards" not in line else wave
        m = re.search(r"\{\{DropsLineReward\|name=([^|]+)\|quantity=([0-9,\-]+)\|rarity=([^|}]+)", line)
        if m and wave:
            name, q, r = m.group(1), m.group(2).replace(",", ""), m.group(3)
            lo, hi = (q.split("-") + [q])[:2] if "-" in q else (q, q)
            if r == "Always":
                fr = Fraction(1, 1)
            elif "/" in r:
                a, b = r.split("/")
                fr = Fraction(int(a.replace(",", "")), int(b.replace(",", "")))
            else:
                fr = None
            out.append((wave, name, int(lo), int(hi), fr))
    return out


if __name__ == "__main__":
    tot = {}
    for w, n, lo, hi, fr in tables():
        print(w, n, lo, hi, fr)
        if fr is not None:
            tot[w] = tot.get(w, 0) + fr
    print({w: str(v) for w, v in tot.items()})
