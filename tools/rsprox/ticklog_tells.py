#!/usr/bin/env python3
"""Our server's tick log (scriptrun <session>/ticklog.tsv) as a named timeline of
the same tells tools/rsprox/rsprox_tells.py reads from an RSProx transcript:
npc animations and graphics, projectiles, map graphics, sounds, music, loc
changes and animations, spawns and removals, hits. Ids are named from
OSRS-Content/osrs239-content/configs/all.<type>.compack and pack/4_soundeffects.pack.

    python3 tools/rsprox/ticklog_tells.py <ticklog.tsv> [--from T] [--to T] [--npc SUBSTR ...]

Coordinates print as world x,z (level dropped); a projectile's distance is the
Chebyshev distance between its source and destination tiles.
"""
import argparse
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content", "osrs239-content")
SKIP = {"npc_state", "varp", "raider", "digest", "player_tile", "npc_face", "input", "start", "tick", "player_state"}


def table(path):
    out = {}
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            key, _, value = line.rstrip("\n").partition("=")
            if key.isdigit():
                out[int(key)] = value.split(" ")[0]
    return out


def xz(c):
    c = int(c)
    if c < 0:
        return "-"
    return "%d,%d" % ((c >> 14) & 0x3FFF, c & 0x3FFF)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ticklog")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", dest="end", type=int, default=10 ** 9)
    ap.add_argument("--npc", nargs="*", default=[])
    a = ap.parse_args()
    cfg = os.path.join(ROOT, "configs")
    npc = table(os.path.join(cfg, "all.npc.compack"))
    seq = table(os.path.join(cfg, "all.seq.compack"))
    spot = table(os.path.join(cfg, "all.spotanim.compack"))
    loc = table(os.path.join(cfg, "all.loc.compack"))
    snd = table(os.path.join(ROOT, "pack", "4_soundeffects.pack"))
    slots = {}
    with open(a.ticklog, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            f = line.rstrip("\n").split("\t")
            if len(f) < 11 or not f[1].isdigit():
                continue
            tick, kind = int(f[1]), f[2]
            v = [int(x) if x.lstrip("-").isdigit() else 0 for x in f[3:9]]
            label = f[9]
            if kind == "npc_spawn":
                slots[v[0]] = npc.get(v[1], str(v[1]))
            if kind in SKIP or tick < a.start or tick > a.end:
                continue
            who = None
            if kind in ("npc_anim", "npc_spotanim", "npc_spawn", "npc_free", "npc_death", "hit_npc", "npc_heal", "npc_tile"):
                kind_type = v[4] if kind == "npc_tile" else v[1]
                who = npc.get(kind_type, slots.get(v[0], str(kind_type)))
                if a.npc and not any(s in who for s in a.npc):
                    continue
            if kind == "npc_anim":
                text = "%s: %s%s" % (who, seq.get(v[2], v[2]), ", delay=%d" % v[3] if v[3] else "")
            elif kind == "npc_spotanim":
                text = "%s: %s height=%d delay=%d" % (who, spot.get(v[2], v[2]), v[3], v[4])
            elif kind == "projectile":
                sx, sz = (v[0] >> 14) & 0x3FFF, v[0] & 0x3FFF
                dx, dz = (v[1] >> 14) & 0x3FFF, v[1] & 0x3FFF
                d = max(abs(dx - sx), abs(dz - sz))
                text = "%s %s->%s d=%d %d->%d" % (spot.get(v[3], v[3]), xz(v[0]), xz(v[1]), d, v[4], v[5])
            elif kind == "map_spotanim":
                text = "%s at %s height=%d delay=%d" % (spot.get(v[1], v[1]), xz(v[0]), v[2], v[3])
            elif kind == "player_spotanim":
                text = "%s height=%d delay=%d" % (spot.get(v[1], v[1]), v[2], v[3])
            elif kind == "player_anim":
                text = "%s delay=%d" % (seq.get(v[1], v[1]), v[2])
            elif kind == "sound":
                text = "%s (%d) delay=%d %s%s" % (snd.get(v[1], v[1]), v[1], v[3], label,
                                                 " at %s r%d" % (xz(v[4]), v[5]) if v[4] > 0 else "")
            elif kind == "loc_set":
                text = "%s at %s shape=%d angle=%d %s" % (loc.get(v[1], v[1]) if v[1] > 0 else "del", xz(v[0]), v[2], v[3], v[4])
            elif kind == "loc_anim":
                text = "%s at %s" % (seq.get(v[4], v[4]), xz(v[0]))
            elif kind in ("npc_spawn", "npc_free", "npc_death"):
                text = "%s %s" % (who, xz(v[2]) if kind == "npc_spawn" else "")
            elif kind == "npc_tile":
                text = "%s %d,%d" % (who, v[1], v[2])
            elif kind in ("hit_npc", "npc_heal"):
                text = "%s %s" % (who, " ".join(str(x) for x in v[2:5]))
            else:
                text = " ".join(str(x) for x in v) + (" " + label if label else "")
            print("%5d  %-15s %s" % (tick, kind, text))


if __name__ == "__main__":
    main()
