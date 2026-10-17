import collections, glob, json

BIG = 40
by_offset = collections.Counter()
hit_by_offset = {d: collections.Counter() for d in (0, 1, 2)}
rooms = 0
events = []
for f in sorted(glob.glob("raw/*.json")):
    ev = json.load(open(f))
    phase = {e["xarpusPhase"]: e["tick"] for e in ev if e["type"] == 140}
    if 2 not in phase:
        continue
    q = phase[2]
    turns = sorted(e["tick"] for e in ev if e["type"] == 10 and e["npcAttack"]["attack"] == 11 and e["tick"] > q)
    if not turns:
        continue
    hp = collections.defaultdict(dict)
    for e in ev:
        if e["type"] == 4 and "hitpoints" in e["player"]:
            hp[e["player"]["name"]][e["tick"]] = e["player"]["hitpoints"] >> 16
    if not hp:
        continue
    rooms += 1
    for e in ev:
        name = e.get("player", {}).get("name")
        if e["type"] != 5 or name not in hp or e["tick"] < turns[0]:
            continue
        if e["attack"]["target"].get("id") not in (8340, 10768, 10772):
            continue
        W = e["tick"]
        last = max(t for t in turns if t <= W)
        k = W - last
        if k > 7:
            continue
        by_offset[k] += 1
        h = hp[name]
        for d in (0, 1, 2):
            a, b = h.get(W + d - 1), h.get(W + d)
            if a is not None and b is not None and a - b >= BIG:
                hit_by_offset[d][k] += 1
                events.append((f[-13:-5], name, W, "turn+%d" % k, "hp@+%d" % d, a, b))
print("rooms", rooms)
print("offset  swings  big-drop@+0  @+1  @+2")
for k in range(8):
    print(f"{k:6d}  {by_offset[k]:6d}  {hit_by_offset[0][k]:11d}  {hit_by_offset[1][k]:3d}  {hit_by_offset[2][k]:3d}")
for x in events:
    print(x)
