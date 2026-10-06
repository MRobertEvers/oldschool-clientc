#!/usr/bin/env python3
"""Queries behind encounters/death_and_fee.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/death_and_fee.queries.py
Q1 per run: status, totalDeaths (overview), PLAYER_DEATH (type 6) events, the wave and tick they carry.
Q2 for each death: the doom hitsplats (type 201) in that wave, and whether Doom (enum_5312 index 8) was chosen by then
   (wave_records.tsv handicap_id); a death with Doom active and doom hitsplats >= the cap is a Doom kill candidate.
Q3 the waves completed before the first death (a run that died keeps its earlier waves)."""
import collections, csv, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
recs = list(csv.DictReader(open(API + "/wave_records.tsv"), delimiter="\t"))
runs = deaths = 0
st = collections.Counter(); td = collections.Counter()
for f in sorted(glob.glob(API + "/*.json")):
    d = json.load(open(f)); runs += 1
    run = os.path.basename(f)[:8]; ov = d["overview"]
    st[ov["status"]] += 1; td[ov["totalDeaths"]] += 1
    for w, ev in d["waves"].items():
        dm = sum(1 for e in ev if e["type"] == 201)
        for e in ev:
            if e["type"] == 6:
                deaths += 1
                chosen = [r["handicap_id"] for r in recs if r["run"] == run and int(r["wave"]) <= int(w)]
                lastwave = max((int(r["wave"]) for r in recs if r["run"] == run), default=0)
                print("Q2 run", run, "status", ov["status"], "death wave", w, "tick", e["tick"], "doom hitsplats that wave", dm,
                      "doom(8) chosen by then", "8" in chosen, "handicaps", ",".join(chosen), "last wave record", lastwave)
print("Q1 runs", runs, "status", dict(st), "totalDeaths", dict(td), "PLAYER_DEATH events", deaths)
