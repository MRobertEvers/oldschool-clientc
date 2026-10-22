#!/usr/bin/env python3
"""Our Dreadnoth tick log against the recorded rules of dn_phase.py,
dn_jumpblood.py, dn_block.py and the death's cancels:

  blood    a raider starting a tick on blood is hit (7/8) that tick, then
           every second tick they still start on it
  defend   every hit on a raider plays a seq that tick (their defend seq, or
           an attack of theirs that took the slot)
  box      blood at a slam (J+1) or landing (L+3) lies -1..5 from his
           south-west tile and on the floor (spawn -5..+9 each way)
  death    no blood and no hit from a heavy shot after his death; no landing
           due after it

    python3 dn_ours_check.py <ticklog.tsv>...
"""
import sys
from collections import Counter, defaultdict

BLOOD, BOSS = 62609, 16665
JUMP01, JUMP02, DEATH01 = 14777, 14778, 14782


def unpack(c):
    return ((c >> 14) & 0x3FFF, c & 0x3FFF)


def main():
    for path in sys.argv[1:]:
        rows = defaultdict(list)
        ticks = set()
        for line in open(path):
            f = line.rstrip("\n").split("\t")
            if len(f) < 9 or not f[1].lstrip("-").isdigit():
                continue
            t = int(f[1])
            ticks.add(t)
            rows[t].append(f)
        ticks = sorted(ticks)
        tile = {}
        blood = set()
        state = {}
        res = Counter()
        bad = []
        spawn = None
        boss_anims = Counter()
        jumps = {}
        death = None
        defend = Counter()
        box = Counter()
        after_death = []
        for t in ticks:
            prev_blood = set(blood)
            start = dict(tile)
            hits = defaultdict(list)
            anims = defaultdict(list)
            adds = []
            for f in rows[t]:
                k = f[2]
                if k == "player_tile":
                    tile[int(f[3])] = (int(f[4]), int(f[5]))
                elif k == "hit_player":
                    hits[int(f[3])].append(int(f[5]))
                elif k == "player_anim":
                    anims[int(f[3])].append(int(f[4]))
                elif k == "loc_set" and int(f[4]) == BLOOD:
                    adds.append(unpack(int(f[3])))
                    blood.add(unpack(int(f[3])))
                elif k == "loc_set" and unpack(int(f[3])) in blood:
                    blood.discard(unpack(int(f[3])))
                elif k == "npc_spawn" and int(f[4]) == BOSS:
                    spawn = unpack(int(f[5]))
                    blood, state = set(), {}
                elif k == "npc_anim" and int(f[4]) == BOSS:
                    boss_anims[int(f[5])] += 1
                    if int(f[5]) in (JUMP01, JUMP02):
                        jumps[t] = int(f[5])
                    if int(f[5]) == DEATH01:
                        death = t
            # blood rule, on the tile each raider started the tick on
            for pid, a in start.items():
                on = a in prev_blood
                got = any(v in (7, 8) for v in hits.get(pid, []))
                hit = False
                if not on:
                    state.pop(pid, None)
                elif pid not in state or t >= state[pid]:
                    hit = True
                    state[pid] = t + 2
                res[(hit, got)] += 1
                if hit != got and (on or got):
                    bad.append((t, pid, a, on, hits.get(pid)))
            for pid, hs in hits.items():
                defend[bool(anims.get(pid))] += len(hs)
            # blood thrown at a slam / landing: offsets from his sw, floor
            for j, kind in jumps.items():
                due = j + (1 if kind == JUMP01 else 3)
                if t == due and adds and spawn:
                    for x in adds:
                        on_floor = spawn[0] - 5 <= x[0] <= spawn[0] + 9 and spawn[1] - 5 <= x[1] <= spawn[1] + 9
                        box["floor" if on_floor else "OFF FLOOR"] += 1
            if death is not None and t > death:
                if adds:
                    after_death.append((t, "blood", adds))
        print(path)
        print("  boss seqs", dict(boss_anims))
        print("  blood rule: predicted&hit %d, predicted&none %d, unpredicted 7/8 %d" % (res[(True, True)], res[(True, False)], res[(False, True)]))
        for b in bad[:12]:
            print("    off-rule", b)
        print("  hits with a seq on the raider that tick: %d of %d" % (defend[True], defend[True] + defend[False]))
        print("  jump blood:", dict(box))
        print("  death at", death, "; blood after it:", after_death[:6])


if __name__ == "__main__":
    main()
