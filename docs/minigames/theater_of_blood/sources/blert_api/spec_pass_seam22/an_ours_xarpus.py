# seam22 (2): our server's chain, from a party run's world tick log (p1/ticklog.tsv): per spit
# (acidspit 1555 whose source is not a landed tile), the orbs thrown from its landing tile within
# 12 ticks, and whether each lands on a tile a raider stands on at its throw tick or the tick before ("raider") or not.
import sys, collections
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1]) if l[:1].isdigit()]
def xz(c): c = int(c); return ((c >> 14) & 0x3fff, c & 0x3fff)
pos = collections.defaultdict(dict)
for r in rows:
    if r[2] == "player_tile": pos[int(r[1])][int(r[3])] = (int(r[4]), int(r[5]))
def tiles_at(t):
    out = {}
    for k in sorted(pos):
        if k > t: break
        out.update(pos[k])
    return set(out.values())
spits, chains, landed = [], [], set()
for r in rows:
    if r[2] == "projectile" and r[6] == "1555":
        s, d, t = xz(r[3]), xz(r[4]), int(r[1])
        (chains if s in landed else spits).append((t, s, d))
        landed.add(d)
per = collections.Counter(); kind = collections.Counter(); seq = []
for t, s, d in spits:
    kids = [c for c in chains if c[1] == d and t < c[0] <= t + 12]
    per[len(kids)] += 1; seq.append(len(kids))
    for c in kids: kind["raider" if c[2] in (tiles_at(c[0]) | tiles_at(c[0] - 1)) else "random"] += 1
print(f"spits {len(spits)}, chained {len(chains)}; per spit {dict(sorted(per.items()))}; sequence {seq}; chained to {dict(kind)}")
