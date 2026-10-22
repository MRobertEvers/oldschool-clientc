#!/usr/bin/env python3
"""Dreadnoth: for every hazard judged on a raider's tile, whether a raider who
moved on its tick counts on the tile they left (start) or the one they reached
(end). The Ezekiel sword bands answered "after a roll, before a walk"; this
asks the same of each of Dreadnoth's.

    python3 dn_phase.py <all-players txt>... [--rows]

Per mechanic it prints a table keyed (start in?, end in?) -> hit/total, and
with --rows every case. Tiles are the transcript's (template) tiles.
"""
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
C = r"\((\d+), (\d+), (\d+)\)"
PL = re.compile(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]*), coord=" + C + r"\)(?:, newcoord=" + C + r")?")
NPC = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \(\d+\)(?:, multinpc=[^)]*\))?, coord=" + C + r"\)(?:, newcoord=" + C + r")?")
SEQ = re.compile(r"^ {12}\[sequence\] id=([a-z0-9_]+)")
HIT = re.compile(r"^ {12}\[hit\] id=(\d+), value=(-?\d+)")
FACEPL = re.compile(r"^ {12}\[facing\] player=\(index=\d+, name=([^,]*),")
LOCADD = re.compile(r"^ {4}\[loc_add_change\w*\] id=([a-z0-9_]+) \(\d+\), coord=" + C)
LOCDEL = re.compile(r"^ {4}\[loc_del\w*\] .*?coord=" + C)
MAPANIM = re.compile(r"^ {4}\[map_anim\] id=([a-z0-9_]+) \(\d+\), coord=" + C + r"(?:, delay=(\d+))?")
MAPPROJ = re.compile(r"^ {4}\[(?:map_)?proj\w*\] id=([a-z0-9_]+) \(\d+\)")
TARGET = re.compile(r"^ {8}\[target\] coord=" + C)


class S:
    def __init__(self, path):
        self.path = path
        self.ticks = []
        self.pl = defaultdict(dict)      # tick -> name -> [start, end, kind, hits, seqs]
        self.boss = {}                   # tick -> [start, end, seqs, kind]
        self.jel = defaultdict(dict)     # tick -> idx -> [start, end, seqs, face]
        self.blood_add = defaultdict(set)  # tick -> tiles
        self.blood_gone = {}             # tile -> tick
        self.anims = defaultdict(list)   # tick -> (id, tile, delay)
        self.proj2 = defaultdict(list)   # tick -> target tile
        self.where = {}                  # name -> last tile
        self.boss_adds = set()           # ticks Dreadnoth is added (a new room: its blood is new)


def parse(path):
    s = S(path)
    tick = None
    cur = None
    lastproj = None
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            s.ticks.append(tick)
            cur = None
            continue
        if tick is None:
            continue
        m = PL.match(line)
        if m:
            a = (int(m.group(3)), int(m.group(4)))
            b = (int(m.group(6)), int(m.group(7))) if m.group(6) else a
            s.pl[tick][m.group(2)] = [a, b, m.group(1), [], []]
            s.where[m.group(2)] = b
            cur = ("pl", m.group(2))
            continue
        m = NPC.match(line)
        if m:
            a = (int(m.group(4)), int(m.group(5)))
            b = (int(m.group(7)), int(m.group(8))) if m.group(7) else a
            kind, idx, typ = m.group(1), m.group(2), m.group(3)
            if typ == "tfa_jelly_boss":
                s.boss[tick] = [a, b, [], kind]
                if kind == "add":
                    s.boss_adds.add(tick)
                cur = ("boss",)
            elif typ == "tfa_jelly_minion":
                s.jel[tick][idx] = [a, b, [], None, kind]
                cur = ("jel", idx)
            else:
                cur = None
            continue
        if line.startswith("    [") and not line.startswith("        "):
            cur = None
        m = SEQ.match(line)
        if m and cur:
            if cur[0] == "pl":
                s.pl[tick][cur[1]][4].append(m.group(1))
            elif cur[0] == "boss":
                s.boss[tick][2].append(m.group(1))
            else:
                s.jel[tick][cur[1]][2].append(m.group(1))
            continue
        m = HIT.match(line)
        if m and cur and cur[0] == "pl":
            s.pl[tick][cur[1]][3].append((int(m.group(1)), int(m.group(2))))
            continue
        m = FACEPL.match(line)
        if m and cur and cur[0] == "jel":
            s.jel[tick][cur[1]][3] = m.group(1)
            continue
        m = LOCADD.match(line)
        if m:
            t = (int(m.group(2)), int(m.group(3)))
            if m.group(1) == "tfa_jelly_boss_damage_tile":
                s.blood_add[tick].add(t)
                s.blood_gone.pop(t, None)
            elif t not in s.blood_gone:
                s.blood_gone[t] = tick
            continue
        m = LOCDEL.match(line)
        if m:
            t = (int(m.group(1)), int(m.group(2)))
            s.blood_gone.setdefault(t, tick)
            continue
        m = MAPANIM.match(line)
        if m:
            s.anims[tick].append((m.group(1), (int(m.group(2)), int(m.group(3))), int(m.group(5) or 0)))
            continue
        m = MAPPROJ.match(line)
        if m:
            lastproj = m.group(1)
            continue
        m = TARGET.match(line)
        if m and lastproj == "vfx_dreadnoth_ranged_proj02":
            s.proj2[tick].append((int(m.group(1)), int(m.group(2))))
            lastproj = None
    return s


def positions(s):
    """tick -> name -> (start, end): every raider every tick, carried forward."""
    out = {}
    last = {}
    for t in s.ticks:
        row = {}
        for name, v in s.pl.get(t, {}).items():
            if v[2] in ("teleport",) and name not in last:
                pass
        for name in set(last) | set(s.pl.get(t, {})):
            if name in s.pl.get(t, {}):
                a, b = s.pl[t][name][0], s.pl[t][name][1]
                row[name] = (a, b)
                last[name] = b
            else:
                row[name] = (last[name], last[name])
        out[t] = row
    return out


def box_dist(sw, size, p):
    dx = max(sw[0] - p[0], 0, p[0] - (sw[0] + size - 1))
    dz = max(sw[1] - p[1], 0, p[1] - (sw[1] + size - 1))
    return max(dx, dz)


def hits_of(s, t, name):
    return [v for _, v in s.pl.get(t, {}).get(name, [None, None, None, [], []])[3]]


def boss_at(s, t):
    """His south-west tile after tick t's move (carried forward)."""
    for k in range(t, t - 400, -1):
        if k in s.boss:
            return s.boss[k][1]
    return None


def blood_set(s, upto):
    """Blood tiles that exist after tick `upto`'s adds."""
    tiles = set()
    for t in s.ticks:
        if t > upto:
            break
        tiles |= s.blood_add.get(t, set())
    return {x for x in tiles if not (x in s.blood_gone and s.blood_gone[x] <= upto)}


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    rows = "--rows" in sys.argv
    tab = defaultdict(Counter)
    allk = defaultdict(Counter)
    lines = defaultdict(list)
    for path in paths:
        s = parse(path)
        pos = positions(s)
        tag = path[-8:-4]
        ticks = s.ticks
        tset = set(ticks)
        # blood: per tick, per raider; blood as it stands after the previous
        # tick (a tile added this tick is not yet under them), hits of 7/8.
        blood_prev = set()
        added_so_far = set()
        for t in ticks:
            if t in s.boss_adds:
                blood_prev, added_so_far = set(), set()
            prev = set(blood_prev)
            added_so_far |= s.blood_add.get(t, set())
            now = {x for x in added_so_far if not (x in s.blood_gone and s.blood_gone[x] <= t)}
            for name, (a, b) in pos[t].items():
                if a not in prev and b not in prev and a not in now and b not in now:
                    continue
                h = hits_of(s, t, name)
                key = ("start" + ("+" if a in prev else "-"), "end" + ("+" if b in now else "-"), "moved" if a != b else "still")
                got = any(v in (7, 8) for v in h)
                allk["blood"][key] += 1
                tab["blood"][key] += got
                lines["blood"].append((tag, t, name, a, b, key, h))
            blood_prev = now
        # the boss's melee: dreadnoth_melee01 ticks; his box after the tick.
        for t in ticks:
            if t not in s.boss or "dreadnoth_melee01" not in s.boss[t][2]:
                continue
            sw = s.boss[t][1]
            for name, (a, b) in pos[t].items():
                da, db = box_dist(sw, 5, a), box_dist(sw, 5, b)
                if a == b or (da > 2 and db > 2):
                    continue
                key = ("start" + ("+" if da <= 1 else "-"), "end" + ("+" if db <= 1 else "-"))
                h = hits_of(s, t, name)
                allk["melee"][key] += 1
                tab["melee"][key] += bool(h)
                lines["melee"].append((tag, t, name, a, b, key, h, sw))
        # the enrage landing: jump02 at L, landing L+3 under his new box.
        for t in ticks:
            if t not in s.boss or "dreadnoth_jump02" not in s.boss[t][2]:
                continue
            L = t + 3
            if L not in pos:
                continue
            sw = s.boss[t][1]
            for name, (a, b) in pos[L].items():
                da, db = box_dist(sw, 5, a), box_dist(sw, 5, b)
                if da > 1 and db > 1:
                    continue
                key = ("start" + ("+" if da == 0 else "-"), "end" + ("+" if db == 0 else "-"), "moved" if a != b else "still")
                h = hits_of(s, L, name)
                allk["landing"][key] += 1
                tab["landing"][key] += any(v > 0 for v in h)
                lines["landing"].append((tag, L, name, a, b, key, h, sw))
        # proj02: aimed at a tile on its tick P, lands at P+6.
        for t in ticks:
            for tile in s.proj2.get(t, []):
                for k in (5, 6, 7):
                    P = t + k
                    if P not in pos:
                        continue
                    for name, (a, b) in pos[P].items():
                        if a != tile and b != tile:
                            continue
                        key = ("+%d" % k, "start" + ("+" if a == tile else "-"), "end" + ("+" if b == tile else "-"))
                        h = hits_of(s, P, name)
                        allk["proj02"][key] += 1
                        tab["proj02"][key] += any(v > 0 for v in h)
                        lines["proj02"].append((tag, P, name, a, b, key, h, tile))
        # jelly swings: jelly_attack, its tile after the tick, the raider it faces.
        face = {}
        for t in ticks:
            for idx, v in s.jel.get(t, {}).items():
                if v[3]:
                    face[idx] = v[3]
                if "jelly_attack" not in v[2] or idx not in face:
                    continue
                me = v[1]
                name = face[idx]
                if name not in pos[t]:
                    continue
                a, b = pos[t][name]
                side = lambda p: abs(p[0] - me[0]) + abs(p[1] - me[1]) == 1
                if a == b:
                    continue
                key = ("start" + ("+" if side(a) else "-"), "end" + ("+" if side(b) else "-"))
                h = hits_of(s, t, name)
                allk["jelly"][key] += 1
                tab["jelly"][key] += bool(h)
                lines["jelly"].append((tag, t, name, a, b, key, h, me))
        # blob seeds: a jelly_spawning_idle map anim with no delay on a raider's
        # tile at J+4 (the seed); start or end tile?
        for t in ticks:
            if t not in s.boss:
                pass
            seeds = [tile for (i, tile, d) in s.anims.get(t, []) if i == "jelly_spawning_idle" and d == 0]
            splat0 = {tile for (i, tile, d) in s.anims.get(t, []) if i == "tob_bloat_blood_splat" and d == 0}
            seeds = [x for x in seeds if x in splat0 and x not in s.blood_add.get(t, set())]
            if not seeds:
                continue
            for name, (a, b) in pos[t].items():
                if a == b:
                    continue
                key = ("start" + ("+" if a in seeds else "-"), "end" + ("+" if b in seeds else "-"))
                if key == ("start-", "end-"):
                    continue
                allk["seed"][key] += 1
                tab["seed"][key] += 1
                lines["seed"].append((tag, t, name, a, b, key, seeds))
    for mech in ("blood", "melee", "landing", "proj02", "jelly", "seed"):
        print("==", mech)
        for k in sorted(allk[mech]):
            print("  %-40s %d/%d" % (" ".join(k), tab[mech][k], allk[mech][k]))
        if rows:
            for r in lines[mech]:
                print("   ", *r)


if __name__ == "__main__":
    main()
