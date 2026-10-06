# seam22 (2): per Xarpus spit landing (blert TOB_XARPUS_SPLAT source XARPUS=1), how many splats
# start from it (BOUNCE=2 with bounceFrom = its tile: the children) and from those (grandchildren).
import json, glob, os, collections, sys
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
for scale in ("2", "3", "4"):
    files = sorted(glob.glob(f"{D}/11_{scale}_*.json"))
    first = collections.Counter(); later = collections.Counter(); grand = collections.Counter()
    unknown = 0; lag = collections.Counter(); n_sp = 0
    per_room = []
    for f in files:
        ev = json.load(open(f))
        sp = [e for e in ev if e.get("type") == 142]
        unknown += sum(1 for e in sp if e["xarpusSplat"]["source"] == 0)
        kids = collections.defaultdict(list)
        for e in sp:
            b = e["xarpusSplat"].get("bounceFrom")
            if e["xarpusSplat"]["source"] == 2 and b:
                kids[(b["x"], b["y"])].append(e)
        spits = [e for e in sp if e["xarpusSplat"]["source"] == 1]
        n_sp += len(spits)
        seq = []
        for i, e in enumerate(spits):
            k = kids.get((e["xCoord"], e["yCoord"]), [])
            g = sum(len(kids.get((c["xCoord"], c["yCoord"]), [])) for c in k)
            (first if i == 0 else later)[len(k)] += 1
            grand[g] += 1
            for c in k: lag[c["tick"] - e["tick"]] += 1
            seq.append(f"{len(k)}/{g}")
        per_room.append(os.path.basename(f)[5:13] + " " + " ".join(seq))
    print(f"scale {scale}: rooms {len(files)}, spit landings {n_sp}, unknown-source splats {unknown}")
    print("  children of the FIRST landing:", dict(sorted(first.items())))
    print("  children of every later landing:", dict(sorted(later.items())))
    print("  grandchildren per landing:", dict(sorted(grand.items())))
    print("  child lag (ticks after the landing):", dict(sorted(lag.items())))
    if "-v" in sys.argv:
        for r in per_room: print("   ", r)
