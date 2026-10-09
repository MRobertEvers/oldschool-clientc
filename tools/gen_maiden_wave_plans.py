#!/usr/bin/env python3
"""gen_maiden_wave_plans -- the Maiden solver's wave table, one handler per
crab spawn pattern (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 4.1.1).

    tools/gen_maiden_wave_plans.py build/blert/maiden [--write]

A Normal trio wave is 6 of the 10 spawn points, so there are 210 patterns.
Each gets an entry: the FREEZER's barrage slots (cast tick after the spawn,
the points it aims at, primary first) and the DPS kill order. The rules come
from the room's geometry (a crab is saved by a cast on or before +5 for a 1,
+8 for a 2, +12 for a 3, +16 for a 4; the 3s stand adjacent at +11 and the
4s share one tile at +16), and a pattern Blert's freezers handled the same
way at least twice, consistently with those deadlines, takes Blert's slots.
Every entry carries how Blert's waves of its pattern compare.

Writes script/plugins/quest_driver/tob_maiden_waves.lua. Re-run;
do not hand-edit.
"""
import collections
import glob
import itertools
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import blert_maiden_roles as roles  # noqa: E402

OUT = os.path.join(REPO, "script", "plugins", "quest_driver", "tob_maiden_waves.lua")
ORDER = ["N1", "N2", "N3", "N4i", "N4o", "S1", "S2", "S3", "S4i", "S4o"]
# the slots' cast ticks after the spawn, and the latest cast that still saves
# each point (it reaches the leak tile at heal - 1: heals +7 / +10 / +14 / +18)
SLOTS = [1, 6, 11, 16]
LAST_SAVE = {"N1": 5, "S1": 5, "N2": 8, "S2": 8, "N3": 12, "S3": 12,
             "N4i": 16, "N4o": 16, "S4i": 16, "S4o": 16}
THREES = ["S3", "N3"]
FOURS = ["N4o", "S4i", "S4o", "N4i"]


def rule_slots(pattern):
    """The geometry's slots: [(at, [aims])], empty slots left out."""
    taken, slots = set(), []
    a = next((p for p in ("S1", "S2") if p in pattern), None)
    if a is None and "N2" in pattern and "N1" in pattern:
        a = "N2"
    if a:
        slots.append((1, [a]))
        taken.add(a)
    b = next((p for p in ("S2", "N2") if p in pattern and p not in taken), None)
    if b:
        slots.append((6, [b]))
        taken.add(b)
    c = [p for p in THREES if p in pattern]
    if c:
        slots.append((11, c))
    d = [p for p in FOURS if p in pattern]
    if d:
        slots.append((16, d))
    return slots


def companions(at, primary, pattern):
    """The crabs one barrage on `primary` at `at` also takes (its 3x3)."""
    if at == 11 and primary in THREES:
        return [primary] + [p for p in THREES if p in pattern and p != primary]
    if at == 16 and primary in FOURS:
        return [primary] + [p for p in FOURS if p in pattern and p != primary]
    return [primary]


def blert_slots(waves):
    """Each one-freezer wave's slot assignment: {at: primary}."""
    out = []
    for w in waves:
        seq = []
        for t, p, who in w["casts"]:
            if not seq or t - seq[-1][0] > 1:
                seq.append((t, p))
        got = {}
        for t, p in seq:
            for at in SLOTS:
                if at <= t < at + 5 and at not in got:
                    got[at] = p
        out.append(got)
    return out


def legal(at, primary):
    return at <= LAST_SAVE[primary]


def dps_order(pattern, slots):
    order = []
    frozen = [p for at, aims in slots for p in aims]
    if "N1" in pattern and "N1" not in frozen:
        order.append("N1")
    if "N2" in pattern and "N2" not in frozen:
        order.append("N2")
    for p in frozen:
        if p not in order:
            order.append(p)
    for p in ORDER:
        if p in pattern and p not in order:
            order.append(p)
    return order


def main():
    by = collections.defaultdict(list)
    for f in sorted(glob.glob(sys.argv[1] + "/*-*.json")):
        for w in roles.room(f):
            if len(w["freezers"]) == 1:
                by[w["pattern"]].append(w)
    entries, overrides, scored = [], 0, collections.Counter()
    for combo in itertools.combinations(ORDER, 6):
        pattern = frozenset(combo)
        slots = rule_slots(pattern)
        bs = blert_slots(by.get(pattern, []))
        note = "n=%d" % len(bs)
        if len(bs) >= 2:
            modal, count = collections.Counter(tuple(sorted(b.items())) for b in bs).most_common(1)[0]
            if count * 2 > len(bs) and modal and all(legal(at, p) for at, p in modal) \
                    and dict(modal) != {at: aims[0] for at, aims in slots}:
                slots = [(at, companions(at, p, pattern)) for at, p in modal]
                overrides += 1
                note += " blert-slots %d/%d" % (count, len(bs))
        if bs:
            mine = {at: aims for at, aims in slots}
            agree = sum(1 for b in bs for at, p in b.items() if at in mine and p in mine[at])
            total = sum(len(b) for b in bs)
            scored["agree"] += agree
            scored["total"] += total
            note += " agree %d/%d" % (agree, total)
        entries.append((" ".join(p for p in ORDER if p in pattern), slots, dps_order(pattern, slots), note))

    lines = [
        "-- GENERATED by tools/gen_maiden_wave_plans.py from Blert's Normal trio",
        "-- Maiden rooms (build/blert/maiden). Re-run; do not hand-edit.",
        "--",
        "-- The Maiden solver's wave table: one handler per crab spawn pattern",
        "-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 4.1.1). The key is the",
        "-- wave's six points in the order N1 N2 N3 N4i N4o S1 S2 S3 S4i S4o.",
        "--   freeze  the freezer's barrage slots: `at` is the cast tick after the",
        "--           spawn tick, `last` the latest cast tick that still saves",
        "--           its crabs, `aim` the points that cast takes, primary first",
        "--           (the 3s stand adjacent at +11, the 4s on one tile at +16)",
        "--   dps     both DPS's kill order",
        "--   blert   Blert's one-freezer waves of this pattern: how many, and how",
        "--           many of their slot casts this entry's slots agree with",
        "QD.MAIDEN_WAVES = {",
    ]
    for key, slots, dps, note in entries:
        fz = ", ".join("{ at = %d, last = %d, aim = { %s } }" % (at, min(LAST_SAVE[p] for p in aims),
                                                                ", ".join('"%s"' % p for p in aims)) for at, aims in slots)
        lines.append('    ["%s"] = { freeze = { %s },' % (key, fz))
        lines.append('        dps = { %s }, blert = "%s" },' % (", ".join('"%s"' % p for p in dps), note))
    lines.append("}")
    text = "\n".join(lines) + "\n"
    print("patterns %d, Blert-seen %d, overridden by Blert's slots %d, slot agreement %d/%d (%.2f)"
          % (len(entries), sum(1 for p in by if len(p) == 6), overrides, scored["agree"], scored["total"],
             scored["agree"] / max(1, scored["total"])))
    if "--write" in sys.argv:
        with open(OUT, "w") as fh:
            fh.write(text)
        print("wrote", os.path.relpath(OUT, REPO))


if __name__ == "__main__":
    main()
