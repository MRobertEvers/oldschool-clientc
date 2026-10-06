# seam22 (2), third pass: where do the chained splats land? For every BOUNCE splat (child of a
# landing at P, tick L) on tick C: on a raider's tile on SOME tick in [L-8, C] ("raider"), or on a
# tile no raider stood on in that window ("random"). Near-Reality's `splash` throws at random
# UNCOVERED tiles; Strategies :836 chains to the next raider(s). Also: recorded chained splats per
# spit landing (blert cannot record one that lands on an existing splat; a random uncovered tile
# can never be such a tile).
import json, glob, os, collections, sys
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
for scale in sys.argv[1:] or ["3"]:
    c = collections.Counter(); landings = 0
    for f in sorted(glob.glob(f"{D}/11_{scale}_*.json")):
        ev = json.load(open(f))
        pos = collections.defaultdict(set)
        for e in ev:
            if e["type"] == 4: pos[e["tick"]].add((e["xCoord"], e["yCoord"]))
        sp = sorted([e for e in ev if e["type"] == 142], key=lambda e: e["tick"])
        land = {}
        for e in sp:
            if e["xarpusSplat"]["source"] == 1:
                landings += 1; land[(e["xCoord"], e["yCoord"])] = e["tick"]
        for e in sp:
            s = e["xarpusSplat"]
            if s["source"] != 2 or not s.get("bounceFrom"): continue
            P = (s["bounceFrom"]["x"], s["bounceFrom"]["y"]); C = e["tick"]
            L = land.get(P)
            if L is None: c["child_of_a_child"] += 1; continue
            tiles = set()
            for t in range(L - 8, C + 1): tiles |= pos.get(t, set())
            c["raider" if (e["xCoord"], e["yCoord"]) in tiles else "random"] += 1
    print(f"scale {scale}: spit landings {landings}; chained splats {dict(c)}; per landing {sum(c.values())/max(landings,1):.2f}")
