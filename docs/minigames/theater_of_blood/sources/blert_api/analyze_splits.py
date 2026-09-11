#!/usr/bin/env python3
"""M37 - which pillar a Nylocas split attacks.

blert's NPC_SPAWN carries each nylo's lineage (wave, parentRoomId, big, style,
spawnType) and NPC_UPDATE carries its position every tick, so the pillar a nylo
ends up attacking is recoverable without any pillar-target field.

A nylo counts as attacking a pillar only if its last four recorded positions are
all within 4 tiles of the same pillar ("parked"), which drops the ones killed in
transit.  Wave spawns are the control: blert's mechanics guide states each
wave-spawned nylo always targets the same pillar, so the purity of a wave slot's
pillar across raids measures how much signal the method can see at all.

    python3 analyze_splits.py /tmp/blertdata [out.csv]
"""
import collections, csv, glob, json, os, sys

PILLARS = {"SW": (3292, 4245), "NW": (3292, 4253),
           "SE": (3300, 4245), "NE": (3300, 4253)}


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def nearest(p):
    name, centre = min(PILLARS.items(), key=lambda kv: cheb(p, kv[1]))
    return name, cheb(p, centre)


def parked(track, tail=4, maxd=4):
    """The pillar this npc settled at, or None if it never parked at one."""
    if len(track) < tail:
        return None
    pts = [p for _, p in track[-tail:]]
    names = {nearest(p)[0] for p in pts}
    if len(names) != 1 or max(nearest(p)[1] for p in pts) > maxd:
        return None
    return names.pop()


def main(root, out=None):
    rows = [("raid", "scale", "wave", "split_pillar",
             "parent_pillar", "nearest_to_spawn")]
    control = collections.defaultdict(collections.Counter)
    tally = collections.Counter()
    for f in sorted(glob.glob(os.path.join(root, "nylo", "*.json"))):
        mode, scale, raid = os.path.basename(f)[:-5].split("_", 2)
        try:
            ev = json.load(open(f))
        except ValueError:
            continue
        spawn, track, meta = {}, collections.defaultdict(list), {}
        for e in ev:
            rid = e.get("rid")
            if rid is None:
                continue
            if e["type"] == 7:
                spawn[rid] = (e["x"], e["y"])
                meta[rid] = e.get("nylo") or {}
            if e["type"] in (8, 9):
                track[rid].append((e["tick"], (e["x"], e["y"])))

        for rid, m in meta.items():
            pillar = parked(track.get(rid, []))
            if pillar is None:
                continue
            if not m.get("parentRoomId"):
                control[(m.get("wave"), m.get("style"), m.get("big"),
                         spawn[rid])][pillar] += 1
                continue
            near = nearest(spawn[rid])[0]
            parent = parked(track.get(m["parentRoomId"], []))
            tally["total"] += 1
            tally["parent"] += parent == pillar
            tally["nearest"] += near == pillar
            tally["same_ns"] += (pillar[0] == "N") == (spawn[rid][1] >= 4249)
            tally["same_ew"] += (pillar[1] == "E") == (spawn[rid][0] >= 3296)
            rows.append((raid, scale, m.get("wave"), pillar, parent, near))

    seen = [(k, c) for k, c in control.items() if sum(c.values()) >= 5]
    purity = (sum(c.most_common(1)[0][1] for _, c in seen)
              / sum(sum(c.values()) for _, c in seen))
    n = tally["total"]
    print(f"control: {len(seen)} wave-spawn slots seen >=5 times, "
          f"pillar purity {purity:.1%} (25% would be chance)")
    print(f"splits parked at a pillar: {n}")
    print(f"  its parent's pillar     : {tally['parent'] / n:.1%}  (chance 25%)")
    print(f"  pillar nearest its spawn: {tally['nearest'] / n:.1%}  (chance 25%)")
    print(f"  same north/south half   : {tally['same_ns'] / n:.1%}  (chance 50%)")
    print(f"  same east/west half     : {tally['same_ew'] / n:.1%}  (chance 50%)")
    if out:
        with open(out, "w", newline="") as fh:
            csv.writer(fh).writerows(rows)
        print(f"wrote {out}: {len(rows) - 1} splits")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/blertdata",
         sys.argv[2] if len(sys.argv) > 2 else None)
