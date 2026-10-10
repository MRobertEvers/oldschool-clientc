#!/usr/bin/env python3
"""XARPUS, Blert against ours: what the content decides, as distributions over
every room, each with a chi-square test; what the players decide, reported.

  tools/blert_xarpus_cal.py build/blert/xarpus <our runs dir> [<our runs dir> ...]

Blert: Normal trio rooms (tools/blert_fetch_tob_rooms.py), room tick 0 its
start. Ours: the leader's ticklog.tsv of every seed under a runs dir
(test/raids/tob_xarpus.lua), room tick 0 the tick before his first HUD bar row
(the rule tools/blert_sotetseg_cal.py uses).

Blert's events: 141 an exhumed as it closes (spawnTick, healAmount, healTicks),
140 a phase (1 the stand-up, 2 the screech), 10 his attacks (10 a spit, 11 a
turn), 142 a splat (source 1 a spit, 2 a chain), 8 his hitpoints.
Ours: loc_set 32743 rows (rise and close), npc_heal rows, npc_retype to 8340,
npc_say "Screeeech!", npc_anim 8059 (a spit), npc_face in P3 (a turn).

  tested (content)                     reported (players)
  first exhumed (room tick)            heal orbs a room
  exhumed gap                          P2 length, spits in P2
  exhumeds a room                      P3 length, room length
  exhumed life (rise to close)         chained splats a spit
  heal a orb                           screech hp (Blert's npc hp lags)
  first heal after the rise
  stand-up after the last exhumed closes
  first spit after the stand-up
  spit gap
  first turn after the screech
  turn gap

A test passes at p >= 0.01 OR a total-variation distance under 0.05.
"""
import collections
import glob
import json
import os
import sys

from scipy.stats import chi2_contingency

FEED, COMBAT, DEAD = 8339, 8340, 8341
EXHUMED = 32743
ORB = 1550          # tob_xarpus_exhumed_energyorb, from the exhumed's tile
SPIT_SEQ = 8059


def blert(d):
    rooms = []
    for f in sorted(glob.glob(d + "/*-*.json")):
        ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
        R = dict(exh=[], stand=None, screech=None, spits=[], turns=[], chains=0, spit_splats=0, hp={}, end=None,
                 heals=[])
        late = min([e["tick"] for e in ev if e["type"] == 4] or [0])
        for e in ev:
            t = e["tick"] - late
            if e["type"] == 141:
                x = e["xarpusExhumed"]
                R["exh"].append(dict(rise=x["spawnTick"] - late, close=t, heals=[h - late for h in x.get("healTicks") or []],
                                     amount=x.get("healAmount")))
            if e["type"] == 140:
                if e["xarpusPhase"] == 1 and R["stand"] is None:
                    R["stand"] = t
                if e["xarpusPhase"] == 2 and R["screech"] is None:
                    R["screech"] = t
            if e["type"] == 10 and (e.get("npc") or {}).get("id") == COMBAT:
                a = e["npcAttack"]["attack"]
                if a == 10:
                    R["spits"].append(t)
                if a == 11:
                    R["turns"].append(t)
            if e["type"] == 142:
                if e["xarpusSplat"].get("source") == 2:
                    R["chains"] += 1
                else:
                    R["spit_splats"] += 1
            if e["type"] == 8 and (e.get("npc") or {}).get("id") in (FEED, COMBAT):
                h = e["npc"]["hitpoints"]
                R["hp"][t] = (h >> 16, h & 0xFFFF)
            if e["type"] == 9 and (e.get("npc") or {}).get("id") in (COMBAT, DEAD) and R["end"] is None:
                R["end"] = t
        rooms.append(R)
    return rooms


def ours(dirs):
    rooms = []
    for d in dirs:
        for sess in sorted(glob.glob(d + "/*/")):
            path = sess + "ticklog.tsv"
            if not os.path.exists(path):
                continue
            rows = [l.rstrip("\n").split("\t") for l in open(path) if not l.startswith("ticklog")]
            R = dict(exh=[], stand=None, screech=None, spits=[], turns=[], chains=0, spit_splats=0, hp={}, end=None,
                     heals=[])
            start, slot = None, None
            open_at = {}
            for r in rows:
                t, k = int(r[1]), r[2]
                if k == "hudbar" and r[4] in (str(FEED), str(COMBAT)) and start is None:
                    start = t - 1
                    slot = r[3]
                if k == "loc_set" and r[4] == str(EXHUMED):
                    open_at[r[3]] = dict(rise=t, close=None, heals=[], amount=None, coord=r[3])
                    R["exh"].append(open_at[r[3]])
                if k == "loc_set" and r[4] == "-1" and r[3] in open_at:
                    open_at.pop(r[3])["close"] = t
                # an orb leaves its exhumed's tile on the check tick (Blert's
                # healTicks): the tile names the exhumed
                if k == "projectile" and r[6] == str(ORB):
                    src = r[3]
                    for X in R["exh"]:
                        if X["coord"] == src and X["close"] is None:
                            X["heals"].append(t)
                if k == "npc_heal" and r[4] == str(FEED):
                    R["heals"].append((t, int(r[5])))
                if k == "npc_retype" and r[3] == slot and r[5] == str(COMBAT) and R["stand"] is None:
                    R["stand"] = t
                if k == "npc_retype" and r[3] == slot and r[5] == str(DEAD) and R["end"] is None:
                    R["end"] = t
                if k == "npc_say" and r[3] == slot and "Screeeech" in r[9] and R["screech"] is None:
                    R["screech"] = t
                if k == "npc_anim" and r[3] == slot and r[5] == str(SPIT_SEQ):
                    R["spits"].append(t)
                if k == "npc_face" and r[3] == slot and R["screech"] is not None and t > R["screech"]:
                    R["turns"].append(t)
                if k == "hudbar" and r[3] == slot:
                    R["hp"][t] = (int(r[8]), int(r[10])) if r[10].lstrip("-").isdigit() else None
            if start is None:
                continue
            amounts = [h[1] for h in R["heals"]]
            for X in R["exh"]:
                if X["heals"] and amounts:
                    X["amount"] = amounts[0]
            R["exh"] = [X for X in R["exh"] if X["close"] is not None]
            sh = lambda v: None if v is None else v - start
            R["stand"], R["screech"], R["end"] = sh(R["stand"]), sh(R["screech"]), sh(R["end"])
            R["spits"] = [t - start for t in R["spits"]]
            R["turns"] = [t - start for t in R["turns"]]
            R["exh"] = [dict(rise=x["rise"] - start, close=x["close"] - start, heals=[h - start for h in x["heals"]],
                             amount=x["amount"]) for x in R["exh"]]
            R["hp"] = {t - start: v for t, v in R["hp"].items() if v}
            rooms.append(R)
    return rooms


def measures(rooms):
    M = collections.defaultdict(collections.Counter)
    for R in rooms:
        ex = sorted(R["exh"], key=lambda x: x["rise"])
        if not ex:
            continue
        M["first exhumed (room tick)"][ex[0]["rise"]] += 1
        for a, b in zip(ex, ex[1:]):
            M["exhumed gap"][b["rise"] - a["rise"]] += 1
        M["exhumeds a room"][len(ex)] += 1
        heals = 0
        for x in ex:
            M["exhumed life (rise to close)"][x["close"] - x["rise"]] += 1
            heals += len(x["heals"])
            if x["heals"]:
                M["first heal after the rise"][min(x["heals"]) - x["rise"]] += 1
                if x["amount"] is not None:
                    M["heal a orb"][x["amount"]] += 1
        M["heal orbs a room (report)"][min(heals, 30)] += 1
        M["exhumeds that healed a room (report)"][sum(1 for x in ex if x["heals"])] += 1
        if R["stand"] is not None:
            M["stand-up after the last exhumed closes"][R["stand"] - max(x["close"] for x in ex)] += 1
            p2 = [t for t in R["spits"] if t > R["stand"] and (R["screech"] is None or t < R["screech"])]
            if p2:
                M["first spit after the stand-up"][p2[0] - R["stand"]] += 1
                for a, b in zip(p2, p2[1:]):
                    M["spit gap"][b - a] += 1
            if R["screech"] is not None:
                M["P2 length (report, 10s)"][((R["screech"] - R["stand"]) // 10) * 10] += 1
                M["spits in P2 (report, 5s)"][(len(p2) // 5) * 5] += 1
                if p2:
                    M["chained splats a spit (report, x10)"][round(10 * R["chains"] / len(p2))] += 1
        if R["screech"] is not None:
            tr = [t for t in R["turns"] if t > R["screech"]]
            if tr:
                M["first turn after the screech"][tr[0] - R["screech"]] += 1
                for a, b in zip(tr, tr[1:]):
                    M["turn gap"][b - a] += 1
            if R["end"] is not None:
                M["P3 length (report, 10s)"][((R["end"] - R["screech"]) // 10) * 10] += 1
            hs = [v for t, v in sorted(R["hp"].items()) if t < R["screech"] and v]
            if hs and hs[-1][1]:
                M["screech hp permille (report, 10s)"][(1000 * hs[-1][0] // hs[-1][1]) // 10 * 10] += 1
        if R["end"] is not None:
            M["room length (report, 20s)"][(R["end"] // 20) * 20] += 1
    return M


def compare(name, a, b, test=True):
    keys = sorted(set(a) | set(b), key=lambda k: (str(type(k)), k))
    na, nb = sum(a.values()), sum(b.values())
    if na == 0 or nb == 0:
        print("  %-46s blert n=%d ours n=%d  (no data on one side)" % (name, na, nb))
        return None
    tvd = 0.5 * sum(abs(a[k] / na - b[k] / nb) for k in keys)
    p = None
    if test and len(keys) > 1:
        try:
            p = chi2_contingency([[a[k] for k in keys], [b[k] for k in keys]])[1]
        except ValueError:
            p = None
    ok = (p is not None and p >= 0.01) or tvd < 0.05
    top = lambda c, n: " ".join("%s:%d%%" % (k, round(100 * v / n)) for k, v in c.most_common(7))
    print("  %-46s %-6s tvd %.3f p %s  (blert n=%d ours n=%d)" % (
        name, ("MATCH" if ok else "DIFFER") if test else "report", tvd, "%.3g" % p if p is not None else "-", na, nb))
    print("      blert %s" % top(a, na))
    print("      ours  %s" % top(b, nb))
    return ok


if __name__ == "__main__":
    B = measures(blert(sys.argv[1]))
    O = measures(ours(sys.argv[2:]))
    results = {}
    for name in sorted(set(B) | set(O)):
        test = "(report" not in name
        r = compare(name, B[name], O[name], test=test)
        if r is not None and test:
            results[name] = r
    bad = [k for k, v in results.items() if not v]
    print("== %d measures tested, %d MATCH, %d DIFFER" % (len(results), len(results) - len(bad), len(bad)))
    for k in bad:
        print("   DIFFER:", k)
