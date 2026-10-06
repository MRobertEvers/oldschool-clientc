# seam22 (2), second pass: is a spit's chain "the next player(s)" (Strategies :836) or a coin
# (Near-Reality)? For every spit landing (TOB_XARPUS_SPLAT source XARPUS) at tile P on tick L, the
# spit is the Xarpus attack (type 10) on tick S in [L-7, L-2] whose player snapshot holds P. The
# other raiders' tiles on S are the predicted bounce targets. A child (source BOUNCE, bounceFrom
# P) is "player" if its tile is one of them, "other" if not. A predicted target with no child is
# "masked" if a splat already stood on that tile before L+12 (blert drops a splat on a tile that
# already has one: XarpusDataTracker.java `if (splat.hasLanded()) return;`), else "missing".
import json, glob, os, collections, sys
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blert_xarpus")
for scale in sys.argv[1:] or ["3"]:
    tot = collections.Counter(); per_first = collections.Counter(); per_later = collections.Counter()
    for f in sorted(glob.glob(f"{D}/11_{scale}_*.json")):
        ev = json.load(open(f))
        pos = collections.defaultdict(dict)
        for e in ev:
            if e["type"] == 4: pos[e["tick"]][e["player"]["name"]] = (e["xCoord"], e["yCoord"])
        spits = sorted(e["tick"] for e in ev if e["type"] == 10 and e.get("npcAttack", {}).get("attack") == 10)
        sp = sorted([e for e in ev if e["type"] == 142], key=lambda e: e["tick"])
        first_tick = {}
        for e in sp: first_tick.setdefault((e["xCoord"], e["yCoord"]), e["tick"])
        kids = collections.defaultdict(list)
        for e in sp:
            b = e["xarpusSplat"].get("bounceFrom")
            if e["xarpusSplat"]["source"] == 2 and b: kids[(b["x"], b["y"])].append(e)
        idx = 0
        for e in sp:
            if e["xarpusSplat"]["source"] != 1: continue
            P, L = (e["xCoord"], e["yCoord"]), e["tick"]
            S = None
            for s in reversed(spits):
                if L - 7 <= s <= L - 2 and P in pos.get(s, {}).values(): S = s; break
            if S is None:
                tot["landing_without_spit_snapshot"] += 1; continue
            others = set(pos[S].values()) - {P}
            got = set((c["xCoord"], c["yCoord"]) for c in kids.get(P, []))
            n_player = len(got & others); n_other = len(got - others)
            masked = sum(1 for o in others - got if first_tick.get(o, 10**9) < L + 12)
            missing = len(others - got) - masked
            key = f"{n_player}p/{n_other}o/{masked}m/{missing}x"
            (per_first if idx == 0 else per_later)[key] += 1
            tot["landings"] += 1; tot["child_player"] += n_player; tot["child_other"] += n_other
            tot["masked"] += masked; tot["missing"] += missing; tot["others"] += len(others)
            idx += 1
    print(f"scale {scale}: {dict(tot)}")
    print("  first landing (children on a raider's tile / elsewhere / predicted-but-masked / predicted-and-missing):", dict(per_first.most_common()))
    print("  later landings:", dict(per_later.most_common(12)))
