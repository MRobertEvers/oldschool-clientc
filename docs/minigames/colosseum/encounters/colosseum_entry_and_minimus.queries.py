#!/usr/bin/env python3
"""Queries behind encounters/colosseum_entry_and_minimus.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/colosseum_entry_and_minimus.queries.py
Q1 Minimus (npc 12808) rows in observed_npc_events.tsv: M36 asks when/where he appears; the recorder does not track him
(he is not a tracked npc type, B:PROVENANCE.md section 1 'The wave number'), so the count is the answer.
Q2 COLOSSEUM_HANDICAP_CHOICE (type 200) events per wave stream and the tick they carry (the wave's tick 0, not the offer).
Q3 the three options before wave 1 and the choice, from wave_records.tsv (the API json carries no options)."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
rows = list(csv.DictReader(open(API + "/observed_npc_events.tsv"), delimiter="\t"))
print("Q1 npc 12808 rows:", sum(1 for r in rows if r["npc_id"] == "12808"), "of", len(rows), "npc rows;",
      "npc ids seen:", len({r["npc_id"] for r in rows}))
streams = ticks = 0
by_wave, tk = collections.Counter(), collections.Counter()
for f in sorted(glob.glob(API + "/*.json")):
    d = json.load(open(f))
    for w, ev in d["waves"].items():
        streams += 1
        for e in ev:
            if e["type"] == 200:
                by_wave[w] += 1
                tk[e["tick"]] += 1
print("Q2 wave streams:", streams, "handicap-choice events:", sum(by_wave.values()), "ticks carried:", dict(tk))
recs = list(csv.DictReader(open(API + "/wave_records.tsv"), delimiter="\t"))
w1 = [r for r in recs if r["wave"] == "1"]
print("Q3 wave-1 option sets:", dict(collections.Counter(r["options"] for r in w1)), "n =", len(w1))
print("Q3 wave-1 chosen id:", dict(collections.Counter(r["handicap_id"] for r in w1)))
print("Q3 runs:", len({r["run"] for r in recs}), "wave records:", len(recs))
