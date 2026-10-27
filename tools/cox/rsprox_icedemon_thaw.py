#!/usr/bin/env python3
"""The Ice Demon's thaw and his icefiends, fitted from RSProx transcripts.

Sections (--section, default both):
  thaw    every recorded room's bar (the frozen demon's healthbar 7, 100 wide) per
          tick against the thaw model in cox_icedemon.rs2, with its deposits and
          fiend actions; prints, per room, the summed |fill error| and the ticks
          that differ. The single-brazier window of index 4392 [1015]-[1084] must
          match exactly.
  fiends  every recorded fiend: its brazier (in Jagex's own order, the pop's
          smoke order) and its side of that brazier, in instance tiles.

The model (docs/rsprox-analysis/cox/icedemon.md, "The thaw"):
  pool 600 a raider; each tick h = min(pool, h - sum(ceil(k/8) + 1) + 1) over the
  lit braziers; each lit brazier burns one kindling every 6 ticks, the first 4
  ticks after it catches; a fiend's action takes one kindling 1 time in 6 (the
  successes are invisible, so a guarded room is scored over --runs draws and
  the best and the mean are printed). The bar Jagex sends: 1 + h x 99 / pool,
  0 only when empty.

Reads RAW transcripts (instance tiles), from the index cache by default:
    python3 tools/cox/rsprox_icedemon_thaw.py
    python3 tools/cox/rsprox_icedemon_thaw.py --section fiends
"""
import argparse
import math
import os
import random
import re
from collections import defaultdict

RAW = os.path.expanduser('~/.rsprox-index-cache/home-raw/.rsprox/binary/archive/')
TICK = re.compile(r'^\[(\d+)\]\s*$')
NPC = re.compile(r'\] npc=\(index=\d+, id=[a-z0-9_]+ \((\d+)\), coord=\((\d+), (\d+), (\d+)\)\)')
COORD = re.compile(r'coord=\((\d+), (\d+), (\d+)\)')

# index id: (window, {brazier instance tile: label}, guarded tile or None, deposits
# (tick, label, kindling) read off the points varbit: 12 a kindling then 24 + 32)
ROOMS = {
    '4392': ('idx4392', 1004, 1141, {(12488, 2795): 'A', (12486, 2792): 'B'}, None,
             [(1014, 'A', 28), (1085, 'B', 27), (1127, 'A', 15)]),
    '3268a': ('idx3268', 15110, 15426, {(12727, 14868): 'A', (12724, 14868): 'B'}, (12724, 14868),
              [(15118, 'A', 28), (15238, 'B', 22), (15381, 'A', 10), (15419, 'B', 8)]),
    '3268b': ('idx3268', 16050, 16142, {(12651, 14900): 'A', (12651, 14903): 'B'}, (12651, 14900),
              [(16055, 'A', 28), (16060, 'B', 28)]),
    '842': ('idx842', 1090, 1186, {(12660, 9325): 'A', (12657, 9325): 'B'}, (12660, 9325),
            [(1095, 'A', 28), (1100, 'B', 28)]),
    '3265': ('idx3265', 6705, 6800, {(10737, 2797): 'A', (10740, 2797): 'B'}, (10740, 2797),
             [(6710, 'A', 28), (6716, 'B', 28)]),
    '3371': ('idx3371', 3760, 3853, {(10357, 7218): 'A', (10357, 7215): 'B'}, (10357, 7218),
             [(3767, 'A', 28), (3772, 'B', 28)]),
}
NAMES = {'4392': 'index 4392', '3268a': 'rsprox.net 3285 (1st)', '3268b': 'rsprox.net 3285 (2nd)',
         '842': 'rsprox.net 843', '3265': 'rsprox.net 3282', '3371': 'rsprox.net 3389'}

POOL, LEVEL, BURN, TIMER0, ODDS = 600, 8, 6, 2, 6


def load(name):
    f, lo, hi, braz, guarded, deps = ROOMS[name]
    tick, cur, fill, fiend = 0, None, {}, []
    for line in open(RAW + f + '.txt', errors='replace'):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        if tick < lo or tick > hi:
            continue
        n = NPC.search(line)
        if n:
            cur = int(n.group(1))
        h = re.search(r'\[headbar\] id=7, fill=(\d+)', line)
        if h and cur == 7584:
            fill[tick] = int(h.group(1))
        if 'pyrefiend_attack' in line and cur == 7586:
            fiend.append(tick)
    return dict(fill=fill, fiend=set(fiend), deps=deps, guarded=braz.get(guarded) if guarded else None,
                lo=lo, hi=hi)


def bar(h):
    return 0 if h <= 0 else 1 + (h * 99) // POOL


def simulate(room, rng):
    h, k, timer, pred = POOL, {}, {}, {}
    deps = defaultdict(list)
    for t, b, n in room['deps']:
        deps[t].append((b, n))
    for t in range(room['lo'], room['hi'] + 1):
        dmg = 0
        for b in [b for b in k if k[b] > 0]:
            dmg += math.ceil(k[b] / LEVEL) + 1
            timer[b] += 1
            if timer[b] >= BURN:
                timer[b] = 0
                k[b] -= 1
        h = min(POOL, h - dmg + 1)
        g = room['guarded']
        if g and t in room['fiend'] and k.get(g, 0) > 0 and rng.random() < 1 / ODDS:
            k[g] -= 1
        for b, n in deps[t]:
            if k.get(b, 0) <= 0:
                timer[b] = TIMER0
            k[b] = k.get(b, 0) + n
        pred[t] = h
        if h <= 0:
            break
    return pred


def score(room, pred, lo=None, hi=None):
    err, bad = 0, []
    for t, f in sorted(room['fill'].items()):
        if t < room['deps'][0][0] or (lo and t < lo) or (hi and t > hi):
            continue
        p = bar(pred[t]) if t in pred else 0
        if p != f:
            err += abs(p - f)
            bad.append((t, f, p))
    return err, bad


def section_thaw(runs):
    print('== thaw: summed |recorded fill - model fill| over each room (the bar is 0..100)')
    for name in ROOMS:
        room = load(name)
        n = sum(1 for t in room['fill'] if t >= room['deps'][0][0])
        draws = runs if room['guarded'] else 1
        errs = [score(room, simulate(room, random.Random(s)))[0] for s in range(draws)]
        print(f"  {NAMES[name]:24} {n:4} ticks  best {min(errs):4}  mean {sum(errs) / len(errs):7.1f}"
              f"  ({'guarded brazier lit, ' + str(draws) + ' draws' if room['guarded'] else 'no fiend acted'})")
    room = load('4392')
    err, bad = score(room, simulate(room, random.Random(0)), 1015, 1084)
    print(f"  index 4392 single brazier [1015]-[1084]: {len(bad)} ticks differ" + (f": {bad[:8]}" if bad else ' (exact)'))


def section_fiends():
    print('== fiends: brazier (Jagex order = smoke delay / 5 at the pop) and side, instance tiles')
    for f in ('idx4392', 'idx842', 'idx3268', 'idx3371', 'idx3265', 'idx819'):
        fiends, braz, tick = {}, {}, 0
        for line in open(RAW + f + '.txt', errors='replace'):
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                continue
            n = re.search(r'\[add\] npc=\(index=\d+, id=[a-z0-9_]+ \(7586\), coord=\((\d+), (\d+), \d+\)\).*spawnangle=(\w+)', line)
            if n:
                fiends.setdefault((int(n.group(1)), int(n.group(2))), (tick, n.group(3)))
            if 'smokepuff_large' in line:
                c = COORD.search(line)
                d = re.search(r'delay=(\d+)', line)
                braz.setdefault((int(c.group(1)), int(c.group(2))), int(d.group(1)) // 5 if d else 0)
        for (x, z), (t, ang) in sorted(fiends.items()):
            near = [(b, i) for b, i in braz.items() if max(abs(b[0] - x), abs(b[1] - z)) == 1]
            for b, i in near:
                print(f"  {f:8} [{t}] fiend {(x, z)} beside brazier {i}, side {(x - b[0], z - b[1])}, spawnangle {ang}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--section', choices=('thaw', 'fiends', 'all'), default='all')
    ap.add_argument('--runs', type=int, default=200, help='draws of the fiend successes per guarded room')
    a = ap.parse_args()
    if a.section in ('thaw', 'all'):
        section_thaw(a.runs)
    if a.section in ('fiends', 'all'):
        section_fiends()


if __name__ == '__main__':
    main()
