#!/usr/bin/env python3
"""Per-life records of one Colosseum npc type: every tick of its npc info, the
projectiles it fired, and the hits the recorder took while it lived.

    python3 tools/rsprox/colosseum/colo_lives.py life <type> <transcript>... [--max N]
    python3 tools/rsprox/colosseum/colo_lives.py blocks <type> <transcript>...    # every block kind and value seen

<type> is a short name from colo_waves.SHORT (archer, seer, shaman, javelin, ...).
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_colosseum import load, waves, player_track, player_at, expand  # noqa: E402
from colo_waves import SHORT, wave_windows  # noqa: E402

BYNAME = {v: k for k, v in SHORT.items()}


def lives_of(d, nid):
    """{(idx, add_tick): [events]} for npc type nid; a life is add..del of one index."""
    cur = {}
    out = {}
    for e in d["events"]:
        if e["k"] != "npc" or e["idx"] is None:
            continue
        if e["mv"] == "add":
            if e["nid"] == nid:
                key = (e["idx"], e["t"])
                cur[e["idx"]] = key
                out[key] = [e]
            else:
                cur.pop(e["idx"], None)
            continue
        key = cur.get(e["idx"])
        if key is None:
            continue
        out[key].append(e)
        if e["mv"] == "del":
            cur.pop(e["idx"], None)
    return out


def tick_index(d):
    by = defaultdict(list)
    for e in d["events"]:
        by[e["t"]].append(e)
    return by


def iid(v):
    return v["id"] if isinstance(v, dict) else v


def bstr(b):
    k, a = b
    if k == "sequence":
        s = f"seq {iid(a.get('id'))}"
        if a.get("delay"):
            s += f"/d{a['delay']}"
        return s
    if k == "spotanim":
        return f"spot{a.get('slot', 0)} {iid(a.get('id'))} h{a.get('height', 0)} d{a.get('delay', 0)}"
    if k == "facing":
        if "player" in a:
            return "face pl"
        if "npc" in a:
            return "face npc"
        if "coord" in a:
            return f"face {a['coord'][0]},{a['coord'][1]}"
        if a.get("type") == "reset":
            return "face reset"
        if "angle" in a:
            return f"face ang{a['angle']}"
        return f"face {a}"
    if k == "hit":
        return f"hit {a.get('id')}:{a.get('value')}"
    if k == "headbar":
        return f"bar {a.get('id')}:{a.get('fill')}"
    if k == "enabled_ops":
        return f"ops {a.get('opflags')}"
    if k == "say":
        return f"say {a.get('text')!r}"
    if k == "transformation":
        return f"xform {iid(a.get('id'))}"
    return k


def cmd_life(nm, paths, maxn=4):
    nid = BYNAME[nm]
    shown = 0
    for p in paths:
        d = load(p)
        tix = tick_index(d)
        tr = player_track(d)
        for (idx, t0), evs in sorted(lives_of(d, nid).items(), key=lambda x: x[0][1]):
            print(f"=== {os.path.basename(p)} {nm} idx {idx} add {t0} at {tuple(evs[0]['c'][:2])} {evs[0].get('angle')}")
            tiles = {}
            for e in evs:
                tiles[e["t"]] = tuple((e.get("nc") or e["c"])[:2])
            last = evs[-1]["t"]
            pos = tuple(evs[0]["c"][:2])
            for t in range(t0, last + 1):
                parts = []
                for e in [x for x in evs if x["t"] == t]:
                    mv = e["mv"]
                    if mv in ("walk", "run", "crawl", "teleport", "telejump"):
                        mv += f"->{tuple(e['nc'][:2])}"
                    parts.append(" ".join([mv] + [bstr(b) for b in e["b"]]))
                prev = pos
                pos = tiles.get(t, pos)
                for e in tix.get(t, []):
                    if e["k"] == "proj" and e.get("src"):
                        sc = e["src"].get("coord")
                        if sc and (abs(sc[0] - prev[0]) <= 3 and abs(sc[1] - prev[1]) <= 3):
                            parts.append(f"PROJ {e['id']} st{e['st']} et{e['et']} a{e['ang']} p{e['prog']} h{e['sh']}>{e['eh']} src{tuple(sc[:2])}")
                    elif e["k"] == "pl":
                        for b in e["b"]:
                            if b[0] == "hit":
                                parts.append(f"PLHIT {b[1].get('id')}:{b[1].get('value')}")
                if parts:
                    pp = player_at(tr, t)
                    print(f"  [{t}] +{t - t0} pl{tuple(pp[:2]) if pp else '-'}: " + " | ".join(parts))
            shown += 1
            if shown >= maxn:
                return


def cmd_blocks(nm, paths):
    nid = BYNAME[nm]
    c = Counter()
    for p in paths:
        d = load(p)
        for key, evs in lives_of(d, nid).items():
            for e in evs:
                c[("mv", e["mv"])] += 1
                for b in e["b"]:
                    c[(b[0], bstr(b) if b[0] in ("sequence", "spotanim", "enabled_ops") else "")] += 1
    for k, v in sorted(c.items()):
        print(f"{v:7d}  {k[0]:16s} {k[1]}")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return
    cmd, nm = sys.argv[1], sys.argv[2]
    args = expand(sys.argv[3:])
    maxn = 4
    if "--max" in args:
        i = args.index("--max")
        maxn = int(args[i + 1])
        del args[i:i + 2]
    if cmd == "life":
        cmd_life(nm, args, maxn)
    elif cmd == "blocks":
        cmd_blocks(nm, args)


if __name__ == "__main__":
    main()
