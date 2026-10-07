# seam22 (3): what a Normal trio swings in Verzik P1 (blert PLAYER_ATTACK, type 5, before the P2
# phase event): per room, attacks by weapon id and attack type; the Dawnbringer is 22516.
import json, glob, os, collections, sys
D, mode, scale = sys.argv[1], sys.argv[2], sys.argv[3]
allw = collections.Counter(); per_room = []
for f in sorted(glob.glob(f"{D}/{mode}_{scale}_*.json")):
    ev = json.load(open(f))
    p2 = min([e["tick"] for e in ev if e["type"] == 150 and e.get("verzikPhase") == 2] or [10**9])
    w = collections.Counter()
    for e in ev:
        if e["type"] == 5 and e["tick"] < p2:
            a = e.get("attack", {})
            w[(a.get("weapon", {}).get("id"), a.get("type"))] += 1
    allw.update(w)
    dawn = sum(v for (wid, t), v in w.items() if wid == 22516)
    per_room.append(f"{os.path.basename(f)[5:13]} p1 {p2} ticks: {sum(w.values())} attacks, dawnbringer {dawn}")
print("\n".join(per_room))
print("all rooms, (weapon id, attack type): count:", allw.most_common(12))
