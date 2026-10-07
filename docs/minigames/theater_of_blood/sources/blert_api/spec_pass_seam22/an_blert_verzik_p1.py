# seam22 (3): Verzik P1, Normal, by scale, from blert stage-15 streams (harvested by the spec pass,
# build/spec_state/matthew-mbp-m4-raid-b1-spec-tob/blert_verzik/<mode>_<scale>_*.json). Per room: P1
# length (VERZIK_PHASE 2), the P1 autos (NPC_ATTACK 12) and, per auto, every raider's tile and the
# recorded raider's hitpoints (packed current<<16|base) from the tick before to 5 after, with
# Protect from Magic (prayerSet bit 65536). Tiles printed as room-local (x-3136, z-4288).
import json, glob, os, collections, sys
D, mode, scale = sys.argv[1], sys.argv[2], sys.argv[3]
lengths = []; drops = collections.Counter(); prayed_drops = []; unprayed_drops = []; tiles = collections.Counter()
for f in sorted(glob.glob(f"{D}/{mode}_{scale}_*.json")):
    ev = json.load(open(f))
    p2 = min([e["tick"] for e in ev if e["type"] == 150 and e.get("verzikPhase") == 2] or [0])
    autos = [e["tick"] for e in ev if e["type"] == 10 and e.get("npcAttack", {}).get("attack") == 12]
    pos = collections.defaultdict(dict); hp = {}; pray = {}
    for e in ev:
        if e["type"] != 4: continue
        p = e["player"]
        pos[e["tick"]][p["name"]] = (e["xCoord"] - 3136, e["yCoord"] - 4288)
        if "hitpoints" in p:
            hp[e["tick"]] = p["hitpoints"] >> 16; pray[e["tick"]] = p.get("prayerSet", 0)
    lengths.append(p2)
    line = []
    for a in autos:
        for n, t in pos.get(a, {}).items(): tiles[t] += 1
        h0 = hp.get(a); h1 = min([hp[t] for t in range(a + 1, a + 7) if t in hp] or [None]) if h0 is not None else None
        pm = bool(pray.get(a, 0) & 65536)
        if h0 is not None and h1 is not None:
            d = max(0, h0 - h1)
            (prayed_drops if pm else unprayed_drops).append(d)
        line.append(f"T{a}{'M' if pm else '-'} hp {h0}->{h1}")
    print(os.path.basename(f)[:13], "p2", p2, "autos", len(autos), " | ".join(line))
lengths.sort()
print(f"mode {mode} scale {scale}: P1 lengths {lengths}")
print(f"  recorded raider's lowest hitpoints within 6 ticks of an auto, drop: prayed {sorted(prayed_drops)}; unprayed {sorted(unprayed_drops)}")
print(f"  raider tiles on auto ticks (room-local): {tiles.most_common(8)}")
