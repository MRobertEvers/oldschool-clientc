#!/usr/bin/env python3
"""Queries behind encounters/wave_reinforcements.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/wave_reinforcements.queries.py
Reads sources/blert_api/*.json (event type 7 = NPC_SPAWN, 4 = PLAYER_UPDATE, 9 = NPC_DEATH, with xCoord/yCoord/tick)
and sources/blert_api/observed_npc_events.tsv. Q1 spawn tiles of reinforcements per tile; Q2 player tile on
ticks 64-66 against the spawn tile (gate); Q3 first events and wave ends; Q4 wave-1 streams reaching tick 66."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
runs = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}
REIN = {12818, 12817, 12820, 12821}  # placeholder, replaced by name lookup below


def names():
    m = {}
    for r in csv.DictReader(open(API + "/observed_npc_events.tsv"), delimiter="\t"): m[int(r["npc_id"])] = r["npc"]
    return m


def q_tiles_gate():
    nm = names(); tiles = collections.Counter(); gate = collections.Counter(); rows = []
    for run, d in runs.items():
        for wn, ev in d["waves"].items():
            if int(wn) >= 12: continue
            pl = {e["tick"]: (e["xCoord"], e["yCoord"]) for e in ev if e["type"] == 4}
            for e in ev:
                if e["type"] == 7 and e["tick"] == 66:
                    x, y = e["xCoord"], e["yCoord"]
                    side = "north" if y > 3106 else "south"
                    tiles[(nm.get(e["npc"]["id"], e["npc"]["id"]), side, x, y)] += 1
                    p = pl.get(65) or pl.get(66)
                    if p:
                        pside = "north" if p[1] > 3106 else "south"
                        gate[(side, pside)] += 1
                        rows.append((int(wn), side, p))
    sides = collections.Counter(k[1] for k in tiles.elements())
    print("Q1 reinforcement spawn tiles (kind, side, x, y):count"); [print("  ", k, v) for k, v in sorted(tiles.items(), key=str)]
    print("Q1 side counts", dict(sides))
    print("Q2 (spawn side, player side at tick 65):count (side split at y=3106, the arena centre row)", dict(gate))


def q_ends():
    by = collections.defaultdict(list)
    for r in csv.DictReader(open(API + "/observed_npc_events.tsv"), delimiter="\t"): by[(r["run"], int(r["wave"]))].append(r)
    w1 = [max(int(r["tick"]) for r in v) for (run, w), v in by.items() if w == 1]
    print("Q4 wave 1 streams:", len(w1), "longest last-event tick", max(w1), "streams with an event on tick >=66:", sum(1 for t in w1 if t >= 66))
    ends = collections.Counter(); wr = []
    for r in csv.DictReader(open(API + "/wave_records.tsv"), delimiter="\t"):
        if int(r["wave"]) < 12: wr.append(int(r["ticks"]))
    print("Q3 wave records (ticks) 1-11:", len(wr), "with ticks in 64-68:", sorted(t for t in wr if 64 <= t <= 68), "ticks < 66:", sum(1 for t in wr if t < 66), ">= 66:", sum(1 for t in wr if t >= 66))
    kill = collections.Counter()
    for (run, w), v in by.items():
        if w >= 12: continue
        sp = [r for r in v if r["kind"] == "spawn" and int(r["tick"]) == 66]
        if sp:
            dead = {r["room_id"]: int(r["tick"]) for r in v if r["kind"] == "death"}
            for r in sp:
                lt = dead.get(r["room_id"])
                kill[(lt - 66) if lt is not None else None] += 1
    print("Q3b reinforcement lifetime after tick 66 (despawn tick - 66), first 8 bins:", sorted(((k, v) for k, v in kill.items() if k is not None))[:8], "never despawned:", kill.get(None, 0))


if __name__ == "__main__":
    q_tiles_gate(); q_ends()


def q_redflag():
    """Q5 (M50 evidence): minotaur npc id against Red Flag (handicap index 13, enum_5312) held at that wave."""
    c = collections.Counter()
    for run, d in runs.items():
        held = []
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99
            if w.get("handicap") is not None: held.append(w["handicap"] % 30)
            for v in w["npcs"].values():
                if v["spawnNpcId"] in (12812, 12813) and n < 12 and v["spawnTick"] == 66:
                    c[(v["spawnNpcId"], "red_flag_held" if 13 in held else "no_red_flag")] += 1
    print("Q5 minotaur id at tick 66 vs Red Flag held:", dict(c))


if __name__ == "__main__":
    q_redflag()


def q_movement():
    """Q6: ticks from a tick-66 spawn to the first tick the npc's tile changes (NPC_UPDATE position, observed);
    Q7: two reinforcements of one wave on the same spawn tile; Q8: wave 12 streams and reinforcement-kind spawns after tick 0."""
    mv = collections.defaultdict(list); same = collections.Counter(); w12 = collections.Counter()
    nm = names()
    for run, d in runs.items():
        for wn, ev in d["waves"].items():
            if int(wn) == 12:
                w12["streams"] += 1
                w12["reinforcement_kind_spawns"] += sum(1 for e in ev if e["type"] == 7 and e["tick"] > 0 and nm.get(e["npc"]["id"]) in ("jaguar_warrior", "serpent_shaman", "minotaur", "minotaur_12813"))
                continue
            sp = {e["npc"]["roomId"]: e for e in ev if e["type"] == 7 and e["tick"] == 66}
            tiles = [(e["xCoord"], e["yCoord"]) for e in sp.values()]
            if len(tiles) == 2: same["pair_same_tile" if tiles[0] == tiles[1] else "pair_distinct_tiles"] += 1
            first = {}
            for e in sorted((e for e in ev if e["type"] == 8 and e["npc"]["roomId"] in sp and e["tick"] > 66), key=lambda e: e["tick"]):
                r = e["npc"]["roomId"]
                if r in first: continue
                s = sp[r]
                if (e["xCoord"], e["yCoord"]) != (s["xCoord"], s["yCoord"]): first[r] = e["tick"] - 66
            for r, s in sp.items():
                if r in first: mv[nm.get(s["npc"]["id"], s["npc"]["id"])].append(first[r])
    for k, v in mv.items():
        v.sort(); print("Q6 first tile change after spawn", k, "n", len(v), "min", v[0], "median", v[len(v) // 2], "max", v[-1], "dist", sorted(collections.Counter(v).items())[:8])
    print("Q7", dict(same), "Q8", dict(w12))


if __name__ == "__main__":
    q_movement()
