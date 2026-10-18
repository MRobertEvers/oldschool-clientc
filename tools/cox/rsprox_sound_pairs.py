#!/usr/bin/env python3
"""Which event each area sound belongs to, from RSProx transcripts: a sound_area is
paired with the map anims / projectiles / loc anims / npc sequences sent on the
same tick (at the same tile, or any tile for loc anims and npc sequences).

    python3 tools/cox/rsprox_sound_pairs.py <transcript>... [--near X0 Z0 X1 Z1]
"""
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
SOUND = re.compile(r"\[sound_area\] id=(\d+), (.*?)coord=\((\d+), (\d+), (\d+)\)")
EVENT = re.compile(r"\[(map_anim|map_projanim_v2|loc_anim)\] (?:id=([a-z0-9_]+) \(\d+\)|coord=\((\d+), (\d+), \d+\), shape=\d+, rotation=\d+, anim=([a-z0-9_]+) \(\d+\))")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
SEQ = re.compile(r"\[sequence\] id=([a-z0-9_]+) \(\d+\)")
NPC = re.compile(r"npc=\(index=\d+, id=([a-z0-9_]+)")


def main(argv):
    box = None
    if "--near" in argv:
        i = argv.index("--near")
        box = tuple(int(v) for v in argv[i + 1:i + 5])
        argv = argv[:i] + argv[i + 5:]
    pairs = defaultdict(Counter)
    totals = Counter()
    for path in argv:
        tick, events, sounds, npc = 0, [], [], None

        def flush():
            for sid, params, x, z in sounds:
                totals[(sid, params)] += 1
                same = [name for kind, name, ex, ez in events if (ex is None or (ex, ez) == (x, z))]
                for name in set(same) or {"(nothing)"}:
                    pairs[(sid, params)][name] += 1

        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                m = TICK.match(line.strip())
                if m and line.startswith("["):
                    flush()
                    tick, events, sounds = int(m.group(1)), [], []
                    continue
                n = NPC.search(line)
                if n:
                    npc = n.group(1)
                m = SOUND.search(line)
                if m:
                    x, z = int(m.group(3)), int(m.group(4))
                    if box and not (box[0] <= x <= box[2] and box[1] <= z <= box[3]):
                        continue
                    sounds.append((m.group(1), re.sub(r"range=\d+, ", "", m.group(2)).rstrip(", "), x, z))
                    continue
                m = EVENT.search(line)
                if m:
                    if m.group(1) == "loc_anim":
                        events.append(("loc_anim", "loc:" + m.group(5), None, None))
                    else:
                        c = COORD.search(line)
                        events.append((m.group(1), m.group(2), int(c.group(1)) if c else None, int(c.group(2)) if c else None))
                    continue
                m = SEQ.search(line)
                if m and npc and "olm" in npc:
                    events.append(("seq", npc + ":" + m.group(1), None, None))
        flush()
    for key, n in totals.most_common(40):
        best = ", ".join("%s x%d" % kv for kv in pairs[key].most_common(3))
        print("%5d  sound %-5s %-18s -> %s" % (n, key[0], key[1], best))


if __name__ == "__main__":
    main(sys.argv[1:])
