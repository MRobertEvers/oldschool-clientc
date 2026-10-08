#!/usr/bin/env python3
"""Sotetseg: the hitpoints a seat lost, PER CAUSE, read from a run's world tick
log (owner_damage 2026-10-07, when the room was the raid's second-worst for
damage taken and the total told nobody anything).

    python3 tools/raid_gate/sote_damage_by_cause.py build/quest_gate/*playsotet*/ticklog.tsv
    python3 tools/raid_gate/sote_damage_by_cause.py --prayer <logs>    # the ball misses
    python3 tools/raid_gate/sote_damage_by_cause.py --melee  <logs>    # the melee, by prayer

THE CAUSES, from the room's own mechanics (OSRS-Content .../tob_sotetseg.rs2):

  death   the big red ball (spotanim 1604).  Its damage is split over the 3x3
          around its target (tob_sote_ball_impact, radius 1), which is what the
          owner's ruling asks all three raiders to stand in, so a share here is
          BY DESIGN.  The Blert reference's 115 median does not carry one: 22
          of its 28 recorded landings had a single raider in the 3x3.
  ball    his magic (1606) or ranged (1607) ball, or a ricochet, landing on a
          raider.  A ball prayed against correctly deals NOTHING (wiki
          Strategies:789), so every hitpoint here is a prayer that was wrong.
  melee   any other hit of his: the swing (seq 8138) he may take at range 1.
          Prayed it is 22 at worst, unprayed 45.
  maze    a hit inside a maze window -- a wrong tile's blast or the tornado.
  self    a hit with no npc behind it (npc_slot -1).

WHAT IT FOUND, over the five survey names' 15 seats (2,105 hitpoints):
  140.3 a seat: death 29.5, ball 19, melee ~86, maze 1.5, self 4.7.
  Take out the death ball's share, which the reference does not carry, and the
  comparable figure is 110.8 against the reference's 115 median.
  626 of those hitpoints were his melee landing while a BALL's colour was up,
  and 550 of them (24 hits at 23 each, where Protect from Melee makes it 13.5)
  came TWO ticks after the ball had already struck: the colour was held a tick
  too long.  Only 2 of 285 ball landings had both colours on one tick, and on
  the 102 landings where this raider's ball and its neighbour's differed in
  colour the plan prayed its own 99 times -- the balls are blockable and the
  targeting is right; the plan just came off the colour late.
  Shortening the hold is NOT safe today: it makes the plan switch protections
  on consecutive ticks, and the press path loses the prayer when it does
  (svdplaysotet p2, missiles -> melee -> magic: no protection at all for ten
  ticks, 77 hitpoints and the raider's life).  See build/seam_state/
  sm_sotetseg/progress.md.

Columns are script/plugins/quest_driver/ticklog.lua's:
  hit_player  pid npc_slot damage hitsplat dealer_pid npc_type .. raw
  projectile  src dst target spotanim start_cycle end_cycle
  raider      pid hp prayer prayers weapon style .. spec
A ball is dated the way the server dates it, launch + end_cycle // 30 (raid
seam52: "Primary = launch + floor(dur/30)"), and a splat can show a tick late
for a raider earlier in the tick's order, so a landing covers [land, land+2].
Prayer bits are varp83_prayer0's: 4096 magic, 8192 missiles, 16384 melee.
"""

import collections
import csv
import os
import sys

PRAYER_BIT = {"magic": 4096, "missiles": 8192, "melee": 16384}
BALL_SPOTANIM = {1606: "magic", 1607: "missiles"}
DEATH_BALL_SPOTANIM = 1604
LAND_WINDOW = (0, 1, 2)


def tick_log(path):
    """(tick, kind, fields) for every row of a world tick log."""
    assert path
    rows = []
    with open(path, newline="") as handle:
        for record in csv.reader(handle, delimiter="\t"):
            if len(record) < 4 or record[0] == "ticklog-v1":
                continue
            try:
                tick = int(record[1])
            except ValueError:
                continue
            rows.append((tick, record[2], record[3:]))
    return rows


def read_room(rows):
    """The room's shape: the mark, his death, the maze windows, the prayers a
    raider had lit each tick, and every ball dated to its landing tick."""
    assert rows
    mark, death = None, None
    retypes = []
    prayers = {}
    balls = collections.defaultdict(dict)       # pid -> land -> colour
    death_balls = set()
    for tick, kind, fields in rows:
        if kind == "mark" and mark is None:
            mark = tick
        elif kind == "npc_death":
            death = tick
        elif kind == "npc_retype":
            retypes.append(tick)
        elif kind == "raider":
            prayers[(tick, int(fields[0]))] = int(fields[3])
        elif kind == "projectile":
            spotanim, target, end_cycle = int(fields[3]), int(fields[2]), int(fields[5])
            land = tick + end_cycle // 30
            if spotanim == DEATH_BALL_SPOTANIM:
                for step in LAND_WINDOW:
                    death_balls.add(land + step)
            elif spotanim in BALL_SPOTANIM and target < 0:
                balls[-target - 1][land] = BALL_SPOTANIM[spotanim]
    mazes = [(retypes[i], retypes[i + 1]) for i in range(0, len(retypes) - 1, 2)]
    return {
        "mark": mark if mark is not None else 0,
        "end": death if death is not None else 10 ** 9,
        "mazes": mazes, "prayers": prayers, "balls": balls, "death_balls": death_balls,
    }


def lit(mask):
    return [name for name, bit in PRAYER_BIT.items() if mask & bit]


def hits(rows, room):
    """Every damaging hit of HIS inside the room, with what the raider had lit."""
    assert rows
    assert room
    for tick, kind, fields in rows:
        if kind != "hit_player" or tick <= room["mark"] or tick > room["end"]:
            continue
        pid, slot, damage = int(fields[0]), int(fields[1]), int(fields[2])
        if damage <= 0:
            continue
        yield tick, pid, slot, damage, lit(room["prayers"].get((tick, pid), 0))


def ball_at(room, pid, tick):
    """The colour of a ball of his aimed at `pid` that is landing on `tick`."""
    for step in LAND_WINDOW:
        colour = room["balls"][pid].get(tick - step)
        if colour is not None:
            return colour, tick - step
    return None, None


def by_cause(paths):
    total, seats, per_seat = collections.Counter(), 0, []
    for path in paths:
        rows = tick_log(path)
        room = read_room(rows)
        per = collections.defaultdict(collections.Counter)
        for tick, pid, slot, damage, up in hits(rows, room):
            if slot < 0:
                per[pid]["self"] += damage
            elif any(a <= tick <= b for a, b in room["mazes"]):
                per[pid]["maze"] += damage
            elif tick in room["death_balls"]:
                per[pid]["death"] += damage
            else:
                colour, _ = ball_at(room, pid, tick)
                # a ball prayed against correctly deals nothing, so a hit on a
                # landing tick with the right colour up is his melee, not the ball
                per[pid]["ball" if (colour is not None and colour not in up) else "melee"] += damage
        name = os.path.basename(os.path.dirname(path))
        for pid in sorted(per):
            counts = per[pid]
            took = sum(counts.values())
            seats += 1
            per_seat.append(took)
            total.update(counts)
            print("%-14s p%d  took %4d | death %4d  ball %4d  melee %4d  maze %3d  self %3d"
                  % (name, pid, took, counts["death"], counts["ball"],
                     counts["melee"], counts["maze"], counts["self"]))
    if seats == 0:
        return
    per_seat.sort()
    print("-- %d seats: median %d, third quartile %d, worst %d"
          % (seats, per_seat[len(per_seat) // 2], per_seat[(len(per_seat) * 3) // 4], per_seat[-1]))
    print("-- a seat: took %5.1f | death %5.1f  ball %5.1f  melee %5.1f  maze %4.1f  self %4.1f"
          % (sum(total.values()) / seats, total["death"] / seats, total["ball"] / seats,
             total["melee"] / seats, total["maze"] / seats, total["self"] / seats))
    print("-- without the death ball's share, which the reference does not carry: %5.1f a seat"
          % ((sum(total.values()) - total["death"]) / seats))


def prayer_misses(paths):
    """Every ball that got through, and why: the colour it was, the colour that
    was up, and whether one prayer could have blocked everything landing then."""
    verdicts, damage = collections.Counter(), collections.Counter()
    collisions, landings = 0, 0
    for path in paths:
        rows = tick_log(path)
        room = read_room(rows)
        colours_at = collections.defaultdict(set)
        for pid, byland in room["balls"].items():
            for land, colour in byland.items():
                colours_at[(pid, land)].add(colour)
        for key, colours in colours_at.items():
            landings += 1
            if len(colours) > 1:
                collisions += 1
        for tick, pid, slot, damage_taken, up in hits(rows, room):
            if slot < 0 or tick in room["death_balls"]:
                continue
            colour, land = ball_at(room, pid, tick)
            if colour is None or colour in up:
                continue
            if len(colours_at[(pid, land)]) > 1:
                verdict = "both colours landed on that tick -- one cannot be prayed"
            elif not up:
                verdict = "no protection at all (the five-tick block, or a refused press)"
            else:
                verdict = "the wrong colour was up (%s, against %s)" % ("+".join(up), colour)
            verdicts[verdict] += 1
            damage[verdict] += damage_taken
    print("%d ball landings, %d of them with both colours on one tick" % (landings, collisions))
    for verdict in sorted(damage, key=lambda key: -damage[key]):
        print("%4d hp over %2d hits  %s" % (damage[verdict], verdicts[verdict], verdict))


def melee_split(paths):
    """His melee, by the protection the raider had lit, and how far the hit was
    from the ball whose colour it was praying."""
    counts, damage = collections.Counter(), collections.Counter()
    for path in paths:
        rows = tick_log(path)
        room = read_room(rows)
        for tick, pid, slot, damage_taken, up in hits(rows, room):
            if slot < 0 or tick in room["death_balls"]:
                continue
            if any(a <= tick <= b for a, b in room["mazes"]):
                continue
            colour, _ = ball_at(room, pid, tick)
            if colour is not None and colour not in up:
                continue                      # that is the ball, not the melee
            if "melee" in up:
                key = "melee prayed (22 at worst)"
            elif not up:
                key = "no protection at all"
            else:
                offsets = [land - tick for land in room["balls"][pid] if abs(land - tick) <= 4]
                nearest = min(offsets, key=abs) if offsets else None
                key = ("melee unprayed, praying a ball that struck t%+d" % nearest
                       if nearest is not None else "melee unprayed, no ball within four ticks")
            counts[key] += 1
            damage[key] += damage_taken
    for key in sorted(damage, key=lambda k: -damage[k]):
        print("%4d hp over %2d hits (%4.1f each)  %s" % (damage[key], counts[key],
                                                         damage[key] / counts[key], key))


def main(argv):
    mode = "cause"
    if argv and argv[0].startswith("--"):
        mode, argv = argv[0][2:], argv[1:]
    assert argv, "give one or more build/quest_gate/<name>/ticklog.tsv"
    if mode == "cause":
        by_cause(argv)
    elif mode == "prayer":
        prayer_misses(argv)
    elif mode == "melee":
        melee_split(argv)
    else:
        sys.exit("sote_damage_by_cause.py: no such mode --%s (cause, prayer, melee)" % mode)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
