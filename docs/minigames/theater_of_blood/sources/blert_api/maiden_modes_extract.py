#!/usr/bin/env python3
"""Distil Maiden event streams (stage 10) of Hard (12) and Regular (11) raids into
maiden_modes_rooms.csv, maiden_modes_attacks.csv and maiden_modes_trails.csv.

    python3 maiden_modes_extract.py <raw dir>   # <uuid>_10.json + list_m<mode>_s<scale>.json

Raw streams were fetched 2026-10-02 at one request per 3 s (User-Agent
3draster-tob-research/1.0 (mrobertevers@gmail.com)) by
sources/blert_api/fetch_blert_maiden.py; they are not committed.
A leak is a Matomenos NPC_DEATH whose current hitpoints are above zero (it was removed by
reaching her, not by damage). Deaths after her last attack are discarded (her death despawns
every crab at full health).
"""
import csv, collections, glob, json, os, sys
RAW = sys.argv[1]
OUT = os.path.dirname(os.path.abspath(__file__))
MAIDEN = set(range(8360, 8366)) | set(range(10822, 10828))
CRAB = {8366, 10828}
SLUG = {8367, 10829}
hp = lambda v: (v >> 16, v & 0xFFFF)
lists = {}
for f in glob.glob(RAW + "/list_m*_s*.json"):
    for c in json.load(open(f)):
        lists[c["uuid"]] = c
rooms, attacks, trails = [], [], []
for f in sorted(glob.glob(RAW + "/*_10.json")):
    u = os.path.basename(f)[:36]
    c = lists.get(u)
    if not c:
        continue
    ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
    atk = sorted({(e["tick"], e["npcAttack"]["attack"]) for e in ev
                  if e["type"] == 10 and e["npc"]["id"] in MAIDEN})
    last_attack = atk[-1][0]
    leaks = sorted(e["tick"] for e in ev if e["type"] == 9 and e["npc"]["id"] in CRAB
                   and hp(e["npc"]["hitpoints"])[0] > 0 and e["tick"] <= last_attack)
    mhp = [hp(e["npc"]["hitpoints"])[1] for e in ev if e["type"] == 7 and e["npc"]["id"] in MAIDEN][:1]
    crab_by_tick = collections.defaultdict(list)
    for e in ev:
        if e["type"] == 7 and e["npc"]["id"] in CRAB:
            crab_by_tick[e["tick"]].append(hp(e["npc"]["hitpoints"])[1])
    slug = sorted({hp(e["npc"]["hitpoints"])[1] for e in ev if e["type"] == 7 and e["npc"]["id"] in SLUG})
    rooms.append([u, c["mode"], c["scale"], mhp[0] if mhp else "", atk[0][0],
                  ";".join(str(len(v)) for _, v in sorted(crab_by_tick.items())),
                  sorted({x for v in crab_by_tick.values() for x in v})[0] if crab_by_tick else "",
                  ";".join(map(str, slug)), len(leaks)])
    for i, (t, a) in enumerate(atk):
        gap = atk[i + 1][0] - t if i + 1 < len(atk) else ""
        attacks.append([u, c["mode"], c["scale"], i, t, a, gap, sum(1 for x in leaks if x <= t)])
    # trail tile runs (event 101 carries every active trail tile each time it changes)
    present = collections.defaultdict(list)
    prev = set()
    for e in ev:
        if e["type"] != 101:
            continue
        now = {(s["x"], s["y"]) for s in e["maidenBloodSplats"]}
        for tile in now - prev:
            present[tile].append([e["tick"], None])
        for tile in prev - now:
            present[tile][-1][1] = e["tick"]
        prev = now
    end = ev[-1]["tick"]
    for tile, runs in present.items():
        for a, b in runs:
            trails.append([u, c["mode"], c["scale"], tile[0], tile[1], a, b if b is not None else "", (b - a) if b is not None else "", end])
def w(name, header, rows):
    with open(os.path.join(OUT, name), "w", newline="") as fh:
        wr = csv.writer(fh); wr.writerow(header); wr.writerows(rows)
    print(name, len(rows))
w("maiden_modes_rooms.csv", ["raid", "mode", "scale", "maiden_max_hp", "first_attack_tick", "crabs_per_spawn_event", "crab_max_hp", "blood_spawn_max_hp", "leaks"], rooms)
w("maiden_modes_attacks.csv", ["raid", "mode", "scale", "attack_index", "tick", "attack_id", "gap_after", "leaks_by_tick"], attacks)
w("maiden_modes_trails.csv", ["raid", "mode", "scale", "x", "y", "first_tick", "gone_tick", "run_ticks", "last_event_tick"], trails)
