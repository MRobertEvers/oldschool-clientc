# seam22 (2), fifth pass: chained splats PER SPIT. Blert keys a splat by its tile and drops one that
# lands on a tile already holding one, and a spit at a raider standing on an old puddle is such a
# landing - so per-landing child counts are confounded (a solo raider standing on one tile collects
# every later chain under that tile's first landing). Counting per spit is not: in SOLO every chain
# goes to a random UNCOVERED tile (Strategies :838 "only one person is present ... a random uncovered
# tile"), which blert always records. Strategies :836 predicts 2 per spit after the first (1 + 2(n-1)
# over n spits); Near-Reality's splash predicts 1.5 per spit (`1 + random(2)`).
import json, glob, os, collections, sys
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
for scale in sys.argv[1:]:
    tot_sp = tot_ch = 0
    for f in sorted(glob.glob(f"{D}/11_{scale}_*.json")):
        ev = json.load(open(f))
        ph = sorted(e["tick"] for e in ev if e["type"] == 140)
        end = ph[1] if len(ph) > 1 else 10**9
        spits = [e["tick"] for e in ev if e["type"] == 10 and e.get("npcAttack", {}).get("attack") == 10 and e["tick"] < end]
        ch = [e for e in ev if e["type"] == 142 and e["xarpusSplat"]["source"] == 2 and e["tick"] < end + 12]
        land = [e for e in ev if e["type"] == 142 and e["xarpusSplat"]["source"] == 1 and e["tick"] < end + 8]
        tot_sp += len(spits); tot_ch += len(ch)
        print(f"  scale {scale} {os.path.basename(f)[5:13]}: p2 ticks {ph[0] if ph else '?'}..{end}, spits {len(spits)}, recorded landings {len(land)}, chained {len(ch)}, chained per spit {len(ch)/max(len(spits),1):.2f}, predicted 2-per-spit {2*len(spits)-1}")
    print(f"scale {scale}: spits {tot_sp}, chained {tot_ch}, per spit {tot_ch/max(tot_sp,1):.2f}")
