#!/usr/bin/env python3
"""What each weapon sends on our wire, per tick-log mark `w:<weapon>:<phase>`.

test/combat/raid_weapons_tells.lua marks a window per weapon and phase
(normal, spec). For each window this prints the player's seqs (with delay),
the player's spotanims (id, height, slot, delay), projectiles (id, start, end,
distance; with TORIRSSERVER_PROJ_DEBUG's stderr, also heights, angle and
progress), map graphics and sounds, counted, named through the content packs.

    python3 tools/rsprox/weapon_tells.py <ticklog.tsv> [--debug <stderr.log>]
"""
import argparse
import collections
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, "OSRS-Content", "osrs-content", "pack")


def names(pack):
    out = {}
    path = os.path.join(PACK, pack)
    if not os.path.exists(path):
        return out
    for line in open(path, errors="replace"):
        if "=" in line:
            k, v = line.rstrip("\n").split("=", 1)
            if k.isdigit():
                out[int(k)] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ticklog")
    ap.add_argument("--debug", help="stderr with TORIRSSERVER_PROJ_DEBUG=1 projanim lines")
    a = ap.parse_args()
    seq = names("0_animations.pack") or names("seq.pack")
    spot = names("spotanim.pack")
    snd = names("4_soundeffects.pack")

    shapes = collections.defaultdict(list)
    if a.debug:
        for line in open(a.debug, errors="replace"):
            m = re.search(r"projanim: tick=(\d+) .* spot=(\d+) delay=(\d+) dur=(\d+) h=(-?\d+)/(-?\d+) "
                          r"angle=(-?\d+) progress=(-?\d+)", line)
            if m:
                t, s = int(m.group(1)), int(m.group(2))
                shapes[(t, s)].append("h=%s/%s angle=%s progress=%s" % m.group(5, 6, 7, 8))

    windows = collections.OrderedDict()
    cur = None
    for line in open(a.ticklog, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 4 or not f[1].isdigit():
            continue
        tick, kind = int(f[1]), f[2]
        v = [int(x) if re.fullmatch(r"-?\d+", x or "") else 0 for x in f[3:9]]
        label = f[9] if len(f) > 9 else ""
        if kind == "mark":
            m = re.match(r"w:(.+):(normal|spec|end)$", label)
            cur = None
            if m and m.group(2) != "end":
                cur = (m.group(1), m.group(2))
                windows.setdefault(cur, collections.Counter())
            continue
        if cur is None:
            continue
        c = windows[cur]
        if kind == "player_anim":
            c["seq   %s(%d) d=%d" % (seq.get(v[1], v[1]), v[1], v[2])] += 1
        elif kind == "player_spotanim":
            c["spot  %s(%d) height=%d slot=%d delay=%d" % (spot.get(v[1], v[1]), v[1], v[2], v[4], v[3])] += 1
        elif kind == "projectile":
            sx, sz = (v[0] >> 14) & 0x3FFF, v[0] & 0x3FFF
            dx, dz = (v[1] >> 14) & 0x3FFF, v[1] & 0x3FFF
            d = max(abs(dx - sx), abs(dz - sz))
            sh = shapes.get((tick, v[3]))
            c["proj  %s(%d) start=%d end=%d d=%d %s" % (spot.get(v[3], v[3]), v[3], v[4], v[5], d,
                                                       sh.pop(0) if sh else "")] += 1
        elif kind == "map_spotanim":
            c["map   %s(%d) height=%d delay=%d" % (spot.get(v[1], v[1]), v[1], v[2], v[3])] += 1
        elif kind == "sound":
            c["sound %s(%d) delay=%d %s" % (snd.get(v[1], v[1]), v[1], v[3], label)] += 1
    for (w, phase), c in windows.items():
        print("== %s %s" % (w, phase))
        for text, n in sorted(c.items(), key=lambda kv: (kv[0][:5], -kv[1])):
            print("   %3d  %s" % (n, text))


if __name__ == "__main__":
    main()
