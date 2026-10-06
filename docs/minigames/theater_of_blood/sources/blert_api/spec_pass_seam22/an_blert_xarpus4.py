# seam22 (2), fourth pass: per spit landing, its chained splats split into "raider" (on a tile a
# raider stood on in [L-8, C]) and "random" (no raider there), first landing vs later, by scale.
# A random chained splat goes to an UNCOVERED tile, so blert always records it; a chain onto a
# raider can land on an existing splat and go unrecorded.
import json, glob, os, collections, sys
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
for scale in sys.argv[1:]:
    first = collections.Counter(); later = collections.Counter(); rooms = 0
    for f in sorted(glob.glob(f"{D}/11_{scale}_*.json")):
        rooms += 1
        ev = json.load(open(f))
        pos = collections.defaultdict(set)
        for e in ev:
            if e["type"] == 4: pos[e["tick"]].add((e["xCoord"], e["yCoord"]))
        sp = sorted([e for e in ev if e["type"] == 142], key=lambda e: e["tick"])
        kids = collections.defaultdict(list)
        for e in sp:
            b = e["xarpusSplat"].get("bounceFrom")
            if e["xarpusSplat"]["source"] == 2 and b: kids[(b["x"], b["y"])].append(e)
        i = 0
        for e in sp:
            if e["xarpusSplat"]["source"] != 1: continue
            P, L = (e["xCoord"], e["yCoord"]), e["tick"]
            r = q = 0
            for c in kids.get(P, []):
                tiles = set()
                for t in range(L - 8, c["tick"] + 1): tiles |= pos.get(t, set())
                if (c["xCoord"], c["yCoord"]) in tiles: r += 1
                else: q += 1
            (first if i == 0 else later)[f"{r}r+{q}q"] += 1
            i += 1
    print(f"scale {scale} ({rooms} rooms): first {dict(first.most_common())}; later {dict(later.most_common())}")
