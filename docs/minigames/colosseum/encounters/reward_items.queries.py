#!/usr/bin/env python3
"""reward_items: offline query over the cached Blert API sample (no network).
Counts PLAYER_ATTACK (type 5) events that use the Tonalztics attack types (62 spec, 63 auto,
64 uncharged spec) or weapons 28919 / 28922 (blert/protos/attack_definitions.json:695-717).
Run: python3 reward_items.queries.py  (output kept in reward_items.queries.out)"""
import glob, json, os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
runs = attacks = hits = 0
for f in sorted(glob.glob(os.path.join(D, "*.json"))):
    d = json.load(open(f))
    runs += 1
    for ev in d["waves"].values():
        for e in ev:
            if e.get("type") != 5:
                continue
            attacks += 1
            a = e.get("attack") or {}
            if a.get("type") in (62, 63, 64) or (a.get("weapon") or {}).get("id") in (28919, 28922):
                hits += 1
print("runs %d, PLAYER_ATTACK events %d, Tonalztics attacks %d" % (runs, attacks, hits))
