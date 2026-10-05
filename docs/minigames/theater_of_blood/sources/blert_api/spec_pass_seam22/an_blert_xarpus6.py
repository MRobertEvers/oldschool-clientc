# seam22 (2), sixth pass: does a landing chain only when it HITS a raider (a raider on the landing
# tile on the landing tick T, or T-1: the previous-tick tile rule)? Per recorded spit landing:
# hit or missed, and how many chained splats start from it within 12 ticks (later chains from a
# re-landing on the same tile are not counted).
import json, glob, os, collections, sys
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
for scale in sys.argv[1:]:
    c = collections.Counter()
    for f in sorted(glob.glob(f"{D}/11_{scale}_*.json")):
        ev = json.load(open(f))
        pos = collections.defaultdict(set)
        for e in ev:
            if e["type"] == 4: pos[e["tick"]].add((e["xCoord"], e["yCoord"]))
        sp = sorted([e for e in ev if e["type"] == 142], key=lambda e: e["tick"])
        for e in sp:
            if e["xarpusSplat"]["source"] != 1: continue
            P, L = (e["xCoord"], e["yCoord"]), e["tick"]
            hit = P in pos.get(L, set()) or P in pos.get(L - 1, set())
            k = sum(1 for x in sp if x["xarpusSplat"]["source"] == 2 and x["xarpusSplat"].get("bounceFrom")
                    and (x["xarpusSplat"]["bounceFrom"]["x"], x["xarpusSplat"]["bounceFrom"]["y"]) == P and L < x["tick"] <= L + 12)
            c[("hit" if hit else "miss", min(k, 3))] += 1
    hits = {k[1]: v for k, v in c.items() if k[0] == "hit"}; miss = {k[1]: v for k, v in c.items() if k[0] == "miss"}
    print(f"scale {scale}: landings that HIT a raider -> chained {dict(sorted(hits.items()))}; landings that MISSED -> chained {dict(sorted(miss.items()))}")
