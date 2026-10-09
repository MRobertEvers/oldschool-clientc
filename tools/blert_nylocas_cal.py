#!/usr/bin/env python3
"""THE NYLOCAS, Blert against ours: every measure the content decides, as
distributions over every room, each with a chi-square test.

  tools/blert_nylocas_cal.py build/blert/nylocas <our runs dir> [<our runs dir> ...]

Blert: Normal trio rooms from tools/blert_fetch_tob_rooms.py (stage 12). Ours:
the leader's ticklog.tsv of every seed under a runs dir (one session dir per
seed): a probe run (test/raids/tob_nylocas_probe.lua, nobody kills) feeds the
measures that need no kills, a solver run (test/raids/tob_nylocas.lua) feeds
all of them. Tiles are local to the room's 64-aligned square, ages are ticks
from a nylo's spawn.

  spawns      every wave's (wave, lane tile, big, style) cells: the table
  lane        the tile at ages 1..8 on the spawn's lane (no npc moves at age 0)
  flicker     the age of a flicker's first switch and how long it holds
  swap        the age an aggro turns into its fighting form, by lane and size
  lifetime    the age a nylo nobody killed despawns at, small and big
  killed      ticks from the killing hit to the despawn, small and big
  splits      a big's children: how many, their offset from its SW tile, their
              style, the tick after the parent's despawn
  supports    which support a walker settles at, by its spawn lane
  waves       the room tick of every wave against the uncapped schedule
              (player-driven: reported, not tested)
  boss        cleanup end to Vasilias' landing; landing to melee form; ticks
              between her colour changes; the colour she changes to; her
              attacks per form and their ticks after the change

A test passes at p >= 0.01 OR a total-variation distance under 0.05 (with
tens of thousands of Blert samples a trivial difference has a tiny p).
"""
import collections
import glob
import json
import os
import sys

from scipy.stats import chi2_contingency

NYLO = set(range(8342, 8354))
BIG = {8345, 8346, 8347, 8351, 8352, 8353}
FIGHT = set(range(8348, 8354))
STYLE = {}
for i, s in enumerate(("mel", "rng", "mag")):
    for base in (8342, 8345, 8348, 8351):
        STYLE[base + i] = s
SUPPORT = 8358
# the Learner kit's weapons (obj ids): Blert's kills by the same weapon only
KIT_WEAPONS = {12926: "blowpipe", 12006: "tentacle", 12899: "trident"}
BOSS = {8354: "spawning", 8355: "mel", 8356: "mag", 8357: "rng"}
SUPPORTS = {"SW": (25, 18), "NW": (25, 29), "SE": (36, 18), "NE": (36, 29)}
LANE_TILES = {(17, 24): "W", (17, 25): "W", (31, 9): "S", (32, 9): "S", (46, 24): "E", (46, 25): "E",
              (45, 24): "E"}
# the uncapped schedule: wave n's room tick (solver_specs/nylocas.md 3.1)
STALLS = [4, 4, 4, 4, 16, 4, 12, 4, 12, 8, 8, 8, 8, 8, 8, 4, 12, 8, 12, 16, 8, 12, 8, 8, 8, 4, 8, 4, 4, 4, 0]
NATURAL = [4]
for st in STALLS[:-1]:
    NATURAL.append(NATURAL[-1] + st)


def new_life(t, tile, nid, parent=None):
    return {"spawn": t, "tile0": tile, "pos": {t: tile}, "ids": [(t, nid)], "death": None, "parent": parent,
            "hp0": None, "natural": None, "wave": None, "children": [], "first_hit": None, "post_collapse": False,
            "weapons": [], "killer": None}


def blert(d):
    rooms = []
    for f in sorted(glob.glob(d + "/*-*.json")):
        ev = sorted(json.load(open(f)), key=lambda e: e["tick"])
        base, live, out = None, {}, []
        room = {"nylos": out, "waves": {}, "cleanup": None, "boss_spawn": None, "boss": {}, "boss_attacks": []}
        for e in ev:
            n = e.get("npc") or {}
            nid = n.get("id")
            t = e["tick"]
            if e["type"] == 120:
                room["waves"].setdefault(e["nyloWave"]["wave"], t)
            if e["type"] == 122:
                room["cleanup"] = t
            if e["type"] == 123:
                room["boss_spawn"] = t
            if nid in BOSS and e["type"] in (7, 8):
                room["boss"][t] = nid
            if nid in BOSS and e["type"] == 10:
                room["boss_attacks"].append(t)
            if e["type"] == 5:
                tg = ((e.get("attack") or {}).get("target") or {}).get("roomId")
                if tg in live and live[tg]["first_hit"] is None:
                    live[tg]["first_hit"] = t
                if tg in live:
                    live[tg]["weapons"].append((t, ((e.get("attack") or {}).get("weapon") or {}).get("id")))
            if nid not in NYLO:
                continue
            if base is None:
                base = (e["xCoord"] - e["xCoord"] % 64, e["yCoord"] - e["yCoord"] % 64)
            tile = (e["xCoord"] - base[0], e["yCoord"] - base[1])
            rid = n["roomId"]
            hp, mx = n.get("hitpoints", 0) >> 16, n.get("hitpoints", 0) & 0xFFFF
            if e["type"] == 7:
                ny = n.get("nylo") or {}
                L = new_life(t, tile, nid, ny.get("parentRoomId") or None)
                L["wave"] = ny.get("wave")
                L["rid"] = rid
                L["max"] = mx
                live[rid] = L
                out.append(L)
            elif rid in live:
                L = live[rid]
                if e["type"] == 8:
                    L["pos"][t] = tile
                    if L["ids"][-1][1] != nid:
                        L["ids"].append((t, nid))
                    if hp == 0 and L["hp0"] is None:
                        L["hp0"] = t
                elif e["type"] == 9:
                    L["death"] = t
                    L["natural"] = L["hp0"] is None
                    if L["hp0"] is not None:
                        before = [w for tw, w in L["weapons"] if tw <= L["hp0"]]
                        L["killer"] = before[-1] if before else None
        by_rid = {L["rid"]: L for L in out}
        for L in out:
            if L["parent"] in by_rid:
                by_rid[L["parent"]]["children"].append(L)
        rooms.append(room)
    return rooms


def ours(dirs):
    rooms = []
    for d in dirs:
        for sess in sorted(glob.glob(d + "/*/")):
            path = sess + "ticklog.tsv"
            if not os.path.exists(path):
                continue
            rows = [l.rstrip("\n").split("\t") for l in open(path) if not l.startswith("ticklog")]
            base, live, out, t0 = None, {}, [], None
            room = {"nylos": out, "waves": {}, "cleanup": None, "boss_spawn": None, "boss": {}, "boss_attacks": []}
            boss_slot = None
            births = []
            collapse = None
            weapon_of = {}
            for r in rows:
                t, k = int(r[1]), r[2]
                if k == "npc_spawn" and int(r[4]) == SUPPORT and t0 is None:
                    t0 = t
                if k == "npc_spawn" and int(r[4]) in BOSS:
                    room["boss_spawn"] = t
                    boss_slot = r[3]
                    room["boss"][t] = int(r[4])
                if k == "npc_retype" and r[3] == boss_slot:
                    room["boss"][t] = int(r[5])
                if k == "npc_anim" and r[3] == boss_slot and room["boss"] and \
                        BOSS.get(room["boss"][max(room["boss"])]) != "spawning":
                    room["boss_attacks"].append(t)
                if k == "npc_spawn" and int(r[4]) in NYLO:
                    v = int(r[5])
                    x, z = (v >> 14) & 0x3FFF, v & 0x3FFF
                    if base is None:
                        base = (x - x % 64, z - z % 64)
                    tile = (x - base[0], z - base[1])
                    L = new_life(t, tile, int(r[4]))
                    L["max"] = 16 if int(r[4]) in BIG else 8
                    L["dealt"] = 0
                    if tile in LANE_TILES:
                        births.append(t)
                    live[r[3]] = L
                    out.append(L)
                elif k == "npc_tile" and r[3] in live:
                    live[r[3]]["pos"][t] = (int(r[4]) - base[0], int(r[5]) - base[1])
                elif k == "npc_retype" and r[3] in live:
                    live[r[3]]["ids"].append((t, int(r[5])))
                elif k == "npc_free" and int(r[4]) == SUPPORT and room["boss_spawn"] is None and collapse is None:
                    collapse = t
                elif k == "raider":
                    weapon_of[(t, r[3])] = int(r[7])
                elif k == "hit_npc" and r[3] in live:
                    L = live[r[3]]
                    if L["first_hit"] is None:
                        L["first_hit"] = t
                    L["dealt"] += int(r[5])
                    if L["dealt"] >= L["max"] and L["hp0"] is None:
                        L["hp0"] = t
                        # the killer's weapon: the dealer's worn weapon the tick before
                        L["killer"] = weapon_of.get((t - 1, r[8]))
                elif k == "npc_free" and r[3] in live:
                    L = live.pop(r[3])
                    L["death"] = t
                    L["natural"] = L["hp0"] is None
            if base is None or t0 is None:
                continue
            # A SPLIT: a box spawn on the tick a big was freed, on its SW tile
            # or SW+(1,1) (the rows of one tick come in any order)
            bigs = [L for L in out if L["ids"][0][1] in BIG and L["death"] is not None]
            for L in out:
                if L["tile0"] in LANE_TILES:
                    continue
                for P in bigs:
                    last = P["pos"][max(P["pos"])]
                    if P["death"] == L["spawn"] and (L["tile0"] == last or L["tile0"] == (last[0] + 1, last[1] + 1)) \
                            and len(P["children"]) < 2:
                        L["parent"] = id(P)
                        P["children"].append(L)
                        break
            # waves: one a lane birth tick, in order
            wave_of = {bt: w for w, bt in enumerate(sorted(set(births)), 1)}
            for L in out:
                if L["tile0"] in LANE_TILES:
                    L["wave"] = wave_of.get(L["spawn"])
                if collapse is not None and (L["death"] is None or L["death"] > collapse):
                    L["post_collapse"] = True
            # room ticks from the supports' first sight, as Blert's from the start
            shift = t0
            for L in out:
                L["spawn"] -= shift
                L["pos"] = {t - shift: p for t, p in L["pos"].items()}
                L["ids"] = [(t - shift, i) for t, i in L["ids"]]
                for key in ("death", "hp0", "first_hit"):
                    if L[key] is not None:
                        L[key] -= shift
            for w, bt in enumerate(sorted(set(births)), 1):
                room["waves"][w] = bt - shift
            lanes = [L for L in out]
            if lanes:
                room["cleanup"] = max(L["death"] for L in out if L["death"] is not None) if all(
                    L["death"] is not None for L in out) else None
            if room["boss_spawn"] is not None:
                room["boss_spawn"] -= shift
                room["boss"] = {t - shift: v for t, v in room["boss"].items()}
                room["boss_attacks"] = [t - shift for t in room["boss_attacks"]]
            rooms.append(room)
    return rooms


# ---------------------------------------------------------------- measures

def lane_of(L):
    return LANE_TILES.get(L["tile0"])


def measures(rooms):
    M = collections.defaultdict(collections.Counter)
    for room in rooms:
        for L in room["nylos"]:
            nid0 = L["ids"][0][1]
            big = "big" if nid0 in BIG else "small"
            lane = lane_of(L)
            if lane and L["wave"] is not None:
                M["spawns"][(L["wave"], L["tile0"], big, STYLE[nid0])] += 1
            unhit_lane = L["first_hit"] is None or L["first_hit"] - L["spawn"] > 9
            if lane and unhit_lane and not L["post_collapse"]:
                for a in range(1, 9):
                    p = L["pos"].get(L["spawn"] + a)
                    if p is not None:
                        M["lane %s %s age %d" % (L["tile0"], big, a)][p] += 1
            changes = [(t - L["spawn"], i) for t, i in L["ids"][1:]]
            flick = [(a, i) for a, i in changes if i not in FIGHT]
            if flick:
                M["flicker first switch age"][flick[0][0]] += 1
                if len(flick) >= 2:
                    M["flicker hold"][flick[1][0] - flick[0][0]] += 1
            swap = [a for a, i in changes if i in FIGHT]
            # the MOUTH swap (an aggro's); a late one is a pillar's collapse
            # retargeting its chewers, counted apart
            if swap and lane and swap[0] < 16 and not L["post_collapse"]:
                M["swap age %s %s" % (lane, big)][swap[0]] += 1
            if L["death"] is not None and L["natural"]:
                M["lifetime (natural despawn age) %s" % big][L["death"] - L["spawn"]] += 1
            # BY THE KILLING WEAPON: Blert's mixture is its teams' weapons
            # (a multi-hit melee weapon reads hp 0 a tick late, a projectile
            # in time), so a kit is compared with the same weapon's kills
            if L["death"] is not None and L["hp0"] is not None and L["killer"] in KIT_WEAPONS:
                M["killed: hp 0 to despawn %s, %s" % (big, KIT_WEAPONS[L["killer"]])][L["death"] - L["hp0"]] += 1
            if nid0 in BIG and L["death"] is not None:
                M["split children per big"][len(L["children"])] += 1
                last = L["pos"][max(L["pos"])]
                # from a parent that stood still its last 3 ticks: a walking
                # one's last row can be a tick stale on Blert's side
                td = max(L["pos"])
                still = all(L["pos"].get(td - i, last) == last for i in range(1, 4))
                for c in L["children"]:
                    if still:
                        M["split offset from parent SW (still)"][(c["tile0"][0] - last[0], c["tile0"][1] - last[1])] += 1
                    M["split spawn - parent despawn"][c["spawn"] - L["death"]] += 1
            if L["parent"] is not None:
                M["split style"][STYLE[nid0]] += 1
            # the support a walker settles at (it rests beside a support's 3x3)
            if lane and not swap and L["death"] is not None and L["death"] - L["spawn"] >= 30 and not L["post_collapse"]:
                n = 2 if nid0 in BIG else 1
                last = L["pos"][max(L["pos"])]
                for name, (ax, az) in SUPPORTS.items():
                    gx = max(ax - (last[0] + n - 1), last[0] - (ax + 2), 0)
                    gz = max(az - (last[1] + n - 1), last[1] - (az + 2), 0)
                    if max(gx, gz) <= 1:
                        M["walker support, %s lane" % lane][name] += 1
                        M["cell support"][(L["wave"], L["tile0"], big, name)] += 1
                        break
        # room-level
        for w, t in room["waves"].items():
            if 1 <= w <= 31:
                M["wave late (vs uncapped) w%02d" % w][t - NATURAL[w - 1]] += 1
        if room["cleanup"] is not None and room["boss_spawn"] is not None:
            M["boss: cleanup end to landing"][room["boss_spawn"] - room["cleanup"]] += 1
        forms = sorted(room["boss"].items())
        seq = []
        for t, nid in forms:
            if not seq or seq[-1][1] != nid:
                seq.append((t, nid))
        if seq and BOSS.get(seq[0][1]) == "spawning" and len(seq) >= 2:
            M["boss: landing to melee form"][seq[1][0] - seq[0][0]] += 1
        coloured = [(t, BOSS[n]) for t, n in seq if BOSS.get(n) != "spawning"]
        for (ta, ca), (tb, cb) in zip(coloured, coloured[1:]):
            M["boss: ticks between colour changes"][tb - ta] += 1
            M["boss: change from->to"][(ca, cb)] += 1
        for i, (ta, ca) in enumerate(coloured):
            tb = coloured[i + 1][0] if i + 1 < len(coloured) else None
            if tb is None:
                continue
            att = sorted({t for t in room["boss_attacks"] if ta <= t < tb})
            M["boss: attacks per form"][len(att)] += 1
            if i > 0:
                M["boss: first attack after a change"][att[0] - ta if att else None] += 1
    return M


def compare(name, a, b, test=True):
    keys = sorted(set(a) | set(b), key=lambda k: (str(type(k)), k if k is not None else -1))
    na, nb = sum(a.values()), sum(b.values())
    if na == 0 or nb == 0:
        print("  %-44s blert n=%d ours n=%d  (no data on one side)" % (name, na, nb))
        return None
    tvd = 0.5 * sum(abs(a[k] / na - b[k] / nb) for k in keys)
    p = None
    if test and len(keys) > 1:
        try:
            p = chi2_contingency([[a[k] for k in keys], [b[k] for k in keys]])[1]
        except ValueError:
            p = None
    ok = (p is not None and p >= 0.01) or tvd < 0.05
    top = lambda c, n: " ".join("%s:%d%%" % (k if not isinstance(k, tuple) else ",".join(map(str, k)),
                                              round(100 * v / n)) for k, v in c.most_common(5))
    verdict = ("MATCH" if ok else "DIFFER") if test else "report"
    print("  %-44s %-6s tvd %.3f p %s  (blert n=%d ours n=%d)" % (
        name, verdict, tvd, "%.3g" % p if p is not None else "-", na, nb))
    print("      blert %s" % top(a, na))
    print("      ours  %s" % top(b, nb))
    return ok


if __name__ == "__main__":
    B = measures(blert(sys.argv[1]))
    O = measures(ours(sys.argv[2:]))
    # THE SUPPORT A CELL'S WALKER SETTLES AT, per (wave, tile, size): Blert's
    # modal against ours. Per cell, because Blert's survivors to +30 are the
    # ones its players did not kill (the lane totals are a kill-biased sample)
    def modal(c):
        out = {}
        for (w, tile, big, sup), n in c.items():
            out.setdefault((w, tile, big), collections.Counter())[sup] += n
        return {k: v.most_common(1)[0][0] for k, v in out.items()}
    mb, mo = modal(B["cell support"]), modal(O["cell support"])
    both = sorted(set(mb) & set(mo), key=str)
    agree = [k for k in both if mb[k] == mo[k]]
    print("== walker support by spawn cell: %d of %d cells agree" % (len(agree), len(both)))
    for k in both:
        if mb[k] != mo[k]:
            print("   cell w%s %s %s: blert %s ours %s" % (k[0], k[1], k[2], mb[k], mo[k]))
    sb, so = set(B["spawns"]), set(O["spawns"])
    print("== spawn table: %d cells both, %d Blert only, %d ours only" % (len(sb & so), len(sb - so), len(so - sb)))
    for k in sorted(sb - so)[:12]:
        print("   blert only", k)
    for k in sorted(so - sb)[:12]:
        print("   ours only ", k)
    results = {}
    for name in sorted(set(B) | set(O)):
        if name in ("spawns", "cell support"):
            continue
        # the lane totals of the supports are a kill-biased sample on Blert's
        # side; the per-cell agreement above is the test
        player_driven = name.startswith("wave late") or name.startswith("walker support")
        # a kill timing by a weapon Blert's teams barely used is no test
        thin = name.startswith("killed") and sum(B[name].values()) < 30
        r = compare(name, B[name], O[name], test=not player_driven and not thin)
        if r is not None and not player_driven and not thin:
            results[name] = r
    bad = [k for k, v in results.items() if not v]
    print("== %d measures tested, %d MATCH, %d DIFFER" % (len(results), len(results) - len(bad), len(bad)))
    for k in bad:
        print("   DIFFER:", k)
