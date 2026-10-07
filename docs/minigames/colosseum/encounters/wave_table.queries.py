#!/usr/bin/env python3
"""Queries behind encounters/wave_table.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/wave_table.queries.py
Reads sources/blert_api/*.json (events and the overview's per-wave handicap) and
sources/blert_api/observed_npc_events.tsv. Prints a few lines per question; the table's
rows quote these numbers. Hitpoints/prayer/stats in PLAYER_UPDATE are packed: current in
the high 16 bits, base in the low 16 (an npc's 3276850 = 50 << 16 | 50)."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
runs = {os.path.basename(f)[:8]: json.load(open(f)) for f in sorted(glob.glob(API + "/*.json"))}
FREM = {12814, 12815, 12816}


def q_d4():
    """Q1/Q2: extra Fremennik exactly when Quartet (idx 6) is held; second shockwave when Dynamic Duo (idx 9)."""
    st = collections.Counter(); kinds = collections.Counter(); w12 = []
    for run, d in runs.items():
        held = []
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99
            if w.get("handicap") is not None:
                held.append(w["handicap"] % 30)   # a choice applies from the wave it is made before
            t0 = [v for v in w["npcs"].values() if v["spawnTick"] == 0]
            fr = [v["spawnNpcId"] for v in t0 if v["spawnNpcId"] in FREM]
            sh = sum(1 for v in t0 if v["spawnNpcId"] == 12819)
            if n < 12:
                st[("fremennik", "quartet" if 6 in held else "none", len(fr))] += 1
                if 6 in held: kinds[[k for k in (12814, 12815) if fr.count(k) == 2][0]] += 1
                if n in (7, 8, 11): st[("shockwave", "dd" if 9 in held else "none", sh)] += 1
            else:
                w12.append((run, 6 in held, len(fr), [(v["spawnNpcId"], v["spawnTick"]) for v in w["npcs"].values() if v["spawnNpcId"] == 12815]))
    print("Q1/Q2", sorted(st.items(), key=str)); print("extra kind (12814 archer, 12815 seer)", dict(kinds))
    print("wave12 quartet runs", [w for w in w12 if w[1]], "wave12 tick-0 fremennik over all runs", sum(w[2] for w in w12), "of", len(w12))


def q_restore():
    """Q3 (M43): player state at tick 0 of wave n against the last PLAYER_UPDATE of wave n-1."""
    c = collections.Counter()
    for run, d in runs.items():
        W = d["waves"]
        for k in sorted(W, key=int):
            if int(k) < 2: continue
            a = [e for e in W[k] if e["type"] == 4]; b = [e for e in W.get(str(int(k) - 1), []) if e["type"] == 4]
            if not a or not b: continue
            a = min(a, key=lambda e: e["tick"])["player"]; b = max(b, key=lambda e: e["tick"])["player"]
            c["transitions"] += 1
            for s in ("hitpoints", "prayer", "attack", "strength", "defence", "ranged", "magic"):
                cur, base = b[s] >> 16, b[s] & 0xFFFF
                if cur != base:
                    c[s + " below/off base at previous end"] += 1
                    if a[s] >> 16 == base: c[s + " back at base at tick 0"] += 1
                    if a[s] >> 16 == cur: c[s + " unchanged at tick 0"] += 1
    for k, v in sorted(c.items()): print("Q3", k, v)


def q_reinforcements():
    """Q4: every wave 1-11 stream with any event on tick >= 66 has its reinforcement spawn on tick 66; none shorter has one."""
    by = collections.defaultdict(list)
    for r in csv.DictReader(open(API + "/observed_npc_events.tsv"), delimiter="\t"): by[(r["run"], int(r["wave"]))].append(r)
    res = collections.Counter(); early = 0; ends = collections.Counter()
    for (run, w), v in by.items():
        if w == 12: continue
        end = max(int(r["tick"]) for r in v)
        sp = [r for r in v if r["kind"] == "spawn" and int(r["tick"]) > 0 and r["npc"] in ("jaguar_warrior", "serpent_shaman", "minotaur", "minotaur_12813")]
        res[(end >= 66, any(int(r["tick"]) == 66 for r in sp))] += 1
        early += sum(1 for r in sp if int(r["tick"]) != 66)
        deaths = [int(r["tick"]) for r in v if r["kind"] == "death"]
        if deaths and 60 <= max(deaths) <= 70: ends[max(deaths)] += 1
    print("Q4 (end>=66, spawn at 66):count", dict(res), "reinforcement-kind spawns at a tick other than 66:", early)
    print("Q5 streams whose LAST death is on ticks 60-70 (a wave cleared at 65-67 would show here):", dict(ends))


def q_wave12():
    """Q6: wave 12 first ticks."""
    c = collections.Counter()
    for r in csv.DictReader(open(API + "/observed_npc_events.tsv"), delimiter="\t"):
        if r["wave"] == "12" and r["kind"] == "spawn" and int(r["tick"]) <= 12: c[(r["npc_id"], r["npc"], r["tick"])] += 1
    print("Q6 wave-12 spawns on tick <= 12", dict(c))
    ids = collections.Counter(e["npc"]["id"] for d in runs.values() for e in d["waves"].get("12", []) if e["type"] in (7, 8, 9))
    print("Q6 npc ids seen in wave-12 streams (12827 seated Sol absent):", dict(ids))


def q_sets():
    """Q7: first appearance of each kind at tick 0 and the per-wave tick-0 multiset size."""
    first = {}
    for run, d in runs.items():
        for w in d["overview"]["colosseum"]["waves"]:
            n = w["stage"] - 99
            for v in w["npcs"].values():
                if v["spawnTick"] == 0: first[v["spawnNpcId"]] = min(first.get(v["spawnNpcId"], 99), n)
    print("Q7 first wave at tick 0 per npc id", dict(sorted(first.items())))


if __name__ == "__main__":
    q_d4(); q_restore(); q_reinforcements(); q_wave12(); q_sets()
