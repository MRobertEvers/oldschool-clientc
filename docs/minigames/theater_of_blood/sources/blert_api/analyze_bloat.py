#!/usr/bin/env python3
"""analyze_bloat -- reduce blert's per-raid Bloat event streams (stage 11) to the numbers the bloat spec table cites.

    python3 analyze_bloat.py <raw dir with <uuid>_11.json and list_m<mode>_s<scale>.json> > bloat_stats.txt

Fetched 2026-10-02 with build/spec_state/<pass>/fetch_blert_bloat.py (GET /api/v1/challenges?type=1&mode=&scale=&status=eq1,
then GET /api/v1/raids/tob/<uuid>/events?stage=11; one request every 3 s, User-Agent
`3draster-tob-research/1.0 (mrobertevers@gmail.com)`). Raw streams are not committed (~160 KB each); bloat_rooms.csv is the
per-room reduction and bloat_stats.txt is this script's output.

Event types (verify_tob_timings.py EVENT table): 7 NPC_SPAWN, 8 NPC_UPDATE, 10 NPC_ATTACK, 110 BLOAT_DOWN, 111 BLOAT_UP,
112 BLOAT_HANDS_DROP (the shadows appear), 113 BLOAT_HANDS_SPLAT. An NPC event's `npc.hitpoints` is (current << 16 | max).
Blert tick 0 is the room's first tick; blert's first walk is `down tick + 1` (BloatDataTracker lastUpTick starts at -1).
"""
import collections
import glob
import json
import os
import sys

raw = sys.argv[1]
BLOAT_IDS = {8359, 10812, 10813}
modes = {}
for f in glob.glob(os.path.join(raw, "list_m*_s*.json")):
    mode = int(os.path.basename(f).split("_")[1][1:])
    for c in json.load(open(f)):
        modes[c["uuid"]] = (mode, c.get("scale"))

C = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
rooms_csv = ["uuid8,mode,scale,max_hp,downs,ups,first_moves_after_down,reversals,hand_drops"]
first_drop_hp = collections.defaultdict(list)
hurry = []          # (hp%, gap)
spacing = collections.defaultdict(list)
elig = collections.defaultdict(lambda: [0, 0])  # mode -> [turns, eligible walking ticks]
hazard = collections.defaultdict(lambda: [0, 0])  # mode -> [turns, at-risk walking ticks] (restated 2026-10-02)
walkfirst = collections.defaultdict(collections.Counter)
walklater = collections.defaultdict(collections.Counter)
lockout = collections.defaultdict(collections.Counter)

for f in sorted(glob.glob(os.path.join(raw, "*_11.json"))):
    uuid = os.path.basename(f)[:-8]
    mode, scale = modes.get(uuid, (None, None))
    ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
    pos, hp, mx = {}, {}, 0
    for e in ev:
        if e["type"] in (7, 8) and "npc" in e and e["npc"]["id"] in BLOAT_IDS:
            pos[e["tick"]] = (e["xCoord"], e["yCoord"])
            hv = e["npc"]["hitpoints"]
            hp[e["tick"]] = hv >> 16
            if mx == 0:
                mx = hv & 0xFFFF
    if not pos:
        continue
    downs = sorted(e["tick"] for e in ev if e["type"] == 110)
    dinfo = {e["tick"]: e["bloatDown"] for e in ev if e["type"] == 110}
    ups = sorted(e["tick"] for e in ev if e["type"] == 111)
    drops = sorted(set(e["tick"] for e in ev if e["type"] == 112))
    splats = sorted(set(e["tick"] for e in ev if e["type"] == 113))
    stomps = sorted(e["tick"] for e in ev if e["type"] == 10)
    c = C[mode]
    c["rooms"][0] += 1
    sign = lambda v: (v > 0) - (v < 0)

    def in_down(t, tail=32):
        return any(d <= t <= d + tail for d in downs)

    moves = []
    for d in downs:
        mv = [t for t in sorted(pos) if t > d and t - 1 in pos and pos[t] != pos[t - 1]]
        if mv:
            moves.append(mv[0] - d)
            c["first_move_after_down"][mv[0] - d] += 1
        up = [u for u in ups if u > d]
        if up:
            c["up_after_down"][up[0] - d] += 1
        st = [s for s in stomps if d < s < d + 40]
        if st:
            c["stomp_after_down"][st[0] - d] += 1
        info = dinfo[d]
        (walkfirst if info.get("downNumber") == 1 else walklater)[mode][info.get("walkTime")] += 1
    for u in ups:
        dr = [x for x in drops if x >= u]
        if dr:
            c["first_drop_after_up"][dr[0] - u] += 1
    for a in drops:
        s = [x for x in splats if x >= a]
        if s:
            c["drop_to_splat"][s[0] - a] += 1
    for a, b in zip(drops, drops[1:]):
        if b - a in (4, 6) and not in_down(a, 34) and not in_down(b, 34):
            k = max(x for x in hp if x <= a)
            hurry.append((100.0 * hp[k] / mx, b - a))
        if mode == 11 or mode == 12:
            c["gap_spans_a_down" if any(a < d <= b for d in downs) else "gap_within_walk"][b - a] += 1
    if drops and drops[0] > 10:
        k = max(x for x in hp if x <= drops[0])
        first_drop_hp[mode].append(round(100.0 * hp[k] / mx, 2))
    if drops and drops[0] <= 10:
        c["first_drop_tick_early"][drops[0]] += 1
    for a in drops:
        if any(d <= a <= d + 33 for d in downs):
            c["drops_in_down_interval"][0] += 1
    prev, pdir, revs, walking = None, None, [], []
    for t in sorted(pos):
        down_now = in_down(t)
        if t >= 1 and not down_now:
            walking.append(t)
        if prev is not None and t == prev + 1 and not down_now:
            dx, dz = pos[t][0] - pos[prev][0], pos[t][1] - pos[prev][1]
            step = max(abs(dx), abs(dz))
            h = 100.0 * hp[t] / mx
            band = ">60" if h > 60 else ("40-60" if h >= 40 else "<40")
            c["steps" + band][step] += 1
            if (dx, dz) != (0, 0):
                dn = (sign(dx), sign(dz))
                if pdir and dn == (-pdir[0], -pdir[1]):
                    revs.append(t)
                pdir = dn
        prev = t
    cd = {11: 32, 12: 16}.get(mode, 32)
    last = 0
    rs = set(revs)
    for i, t in enumerate(walking):
        if t in rs:
            elig[mode][0] += 1
            if last:
                spacing[mode].append(i - last)
            last = i
        if i - last >= cd:
            elig[mode][1] += 1
    # Per-tick hazard (restated 2026-10-02). `elig` above is turns over eligible NON-turn ticks, which
    # is odds rather than a probability, and it counts turns on blert tick 31 / 15 with no eligible tick
    # behind them. A per-tick roll `random(N) = 0` is measured as turns over at-risk ticks: a walking
    # tick is at risk from the earliest tick a turn is ever seen (31 Regular, 15 Hard = the cooldown
    # from the room's first walking tick) and then from `cd` walking ticks after each turn, the turn
    # tick itself counted. Checked on our tick log by the seam2 Bloat fixer: it reads 1/61.8 for a 1-in-64 roll.
    first_risk, nxt = {11: 31, 12: 15}.get(mode, 31), None
    for i, t in enumerate(walking):
        if nxt is None and t >= first_risk:
            nxt = i
        if nxt is not None and i >= nxt:
            hazard[mode][1] += 1
            if t in rs:
                hazard[mode][0] += 1
                nxt = i + cd
    for d in downs:
        ws = max([u for u in ups if u < d], default=0)
        r = [x for x in revs if ws <= x <= d]
        if r:
            lockout[mode][d - r[-1]] += 1
    firstrev = revs[0] if revs else None
    if firstrev is not None:
        c["first_reversal_tick"][firstrev] += 1
    rooms_csv.append("%s,%s,%s,%s,%s,%s,%s,%s,%s" % (
        uuid[:8], mode, scale, mx, ";".join(map(str, downs)), ";".join(map(str, ups)),
        ";".join(map(str, moves)), ";".join(map(str, revs)), len(drops)))

for mode in sorted(C, key=lambda m: (m is None, m)):
    print("=== mode", mode, "(10 entry, 11 regular, 12 hard) rooms", C[mode]["rooms"][0])
    for k, v in sorted(C[mode].items()):
        if k not in ("rooms",):
            print(" ", k, sorted(v.items()) if len(v) < 40 else sorted(v.items())[:40])
    if first_drop_hp[mode]:
        fh = sorted(first_drop_hp[mode])
        print("  first hands drop, rooms where it came after tick 10: hp%% min %.2f max %.2f (n=%d)" % (fh[0], fh[-1], len(fh)))
    t, e = elig[mode]
    if e:
        print("  turn rate: %d reversals over %d eligible walking ticks = %.4f (cooldown %d); min spacing %s; spacing histogram %s" % (
            t, e, t / e, {11: 32, 12: 16}.get(mode, 32), min(spacing[mode]) if spacing[mode] else None,
            sorted(collections.Counter(spacing[mode]).items())[:14]))
    print("  down minus last reversal of the same walk (min is the post-turn lockout):", sorted(lockout[mode].items())[:12])
    print("  first walk (walkTime on downNumber 1):", sorted(walkfirst[mode].items()))
    print("  later walks:", sorted(walklater[mode].items()))
four = [h for h, g in hurry if g == 4]
six = [h for h, g in hurry if g == 6]
print("hands cadence threshold (all modes): highest hp%% with a 4-tick gap %.2f, lowest hp%% with a 6-tick gap %.2f (n4=%d n6=%d)" % (max(four), min(six), len(four), len(six)))
for mode in sorted(hazard):
    t, r = hazard[mode]
    h = t / r
    se = (h * (1 - h) / r) ** 0.5
    print("turn hazard mode %s (restated 2026-10-02): %d turns over %d at-risk walking ticks = %.4f +- %.4f (1 in %.1f; 2 SE %.1f-%.1f %%)" % (
        mode, t, r, h, se, 1 / h, 100 * (h - 2 * se), 100 * (h + 2 * se)))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "bloat_rooms.csv"), "w").write("\n".join(rooms_csv) + "\n")
