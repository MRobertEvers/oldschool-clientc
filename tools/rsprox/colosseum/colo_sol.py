#!/usr/bin/env python3
"""Sol Heredit fights tick by tick, compact: Sol's npc info, the map anims
(grouped by id: count and tile span), locs, the beam crystal and solar flares,
the recorder's tile, prayer and splats, Sol's headbar.

    python3 tools/rsprox/colosseum/colo_sol.py <transcript|@list>... [--fight N] [--from +T --to +T]
"""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_colosseum import load, expand, player_track, player_at  # noqa: E402
from colo_lives import lives_of, tick_index, iid, bstr  # noqa: E402
from colo_attacks import prayer_track, prayer_at  # noqa: E402


def span(cs):
    xs = [c[0] for c in cs]
    zs = [c[1] for c in cs]
    if len(cs) == 1:
        return f"{xs[0]},{zs[0]}"
    return f"x{min(xs)}-{max(xs)} z{min(zs)}-{max(zs)} n{len(cs)}"


def main():
    a = sys.argv[1:]
    fight = None
    lo, hi = None, None
    for flag in ("--fight", "--from", "--to"):
        if flag in a:
            i = a.index(flag)
            v = int(a[i + 1].lstrip("+"))
            del a[i:i + 2]
            if flag == "--fight":
                fight = v
            elif flag == "--from":
                lo = v
            else:
                hi = v
    n = 0
    for p in expand(a):
        d = load(p)
        tix = tick_index(d)
        tr = player_track(d)
        pt = prayer_track(d)
        for (idx, t0), evs in sorted(lives_of(d, 12821).items(), key=lambda x: x[0][1]):
            n += 1
            if fight is not None and n != fight:
                continue
            last = evs[-1]["t"]
            print(f"=== fight {n} {os.path.basename(p)} sol idx {idx} add {t0} .. {last}")
            byt = defaultdict(list)
            for e in evs:
                byt[e["t"]].append(e)
            for t in range(t0 - 3, last + 6):
                if lo is not None and t - t0 < lo:
                    continue
                if hi is not None and t - t0 > hi:
                    break
                parts = []
                for e in byt.get(t, []):
                    mv = e["mv"]
                    if e.get("nc"):
                        mv += f"->{e['nc'][0]},{e['nc'][1]}"
                    bl = [bstr(b) for b in e["b"] if b[0] not in ("headbar",)]
                    bar = [b[1].get("fill") for b in e["b"] if b[0] == "headbar"]
                    s = f"SOL {mv}" + ("" if mv.startswith("idle") else "")
                    if bl:
                        s += " " + " ".join(bl)
                    if bar:
                        s += f" bar{bar[-1]}"
                    if bl or not mv.startswith("idle") or bar:
                        parts.append(s)
                mans = defaultdict(list)
                for x in tix.get(t, []):
                    if x["k"] == "man":
                        mans[(x["id"], x["delay"])].append(tuple(x["c"][:2]))
                    elif x["k"] == "loc" and x["op"] != "loc_anim":
                        parts.append(f"LOC {x['op'][:8]} {x['id']} {tuple(x['c'][:2])} r{x['rot']}")
                    elif x["k"] == "loc":
                        parts.append(f"LOCANIM {x['anim']} {tuple(x['c'][:2])}")
                    elif x["k"] == "npc" and x["nid"] in (12824, 12826, 12822, 12823, 12825, 12834, 12835, 12836) and (x["mv"] != "idle" or x["b"]):
                        parts.append(f"N{x['nid']} {x['mv']} {tuple((x.get('nc') or x['c'])[:2])} " + " ".join(bstr(b) for b in x["b"] if b[0] != "headbar"))
                    elif x["k"] == "pl":
                        h = [f"{b[1].get('id')}:{b[1].get('value')}" for b in x["b"] if b[0] == "hit"]
                        if h:
                            parts.append("PLHIT " + " ".join(h))
                    elif x["k"] == "msg" and x["type"] in ("npc_say", "gamemessage", "spam") and "Sol" in str(x["text"]):
                        parts.append(f"MSG {x['text'][:60]!r}")
                    elif x["k"] == "proj" and x["id"] not in (130, 9, 1459, 2673, 2520):
                        sc = (x.get("src") or {}).get("coord")
                        dc = (x.get("dst") or {}).get("coord")
                        parts.append(f"PROJ {x['id']} st{x['st']} et{x['et']} {tuple(sc[:2]) if sc else ''}>{tuple(dc[:2]) if dc else ''}")
                    elif x["k"] == "var" and x["id"] in (9800, 9801):
                        parts.append(f"VAR {x['name']}={x['new']}")
                for (mid, dl), cs in sorted(mans.items()):
                    parts.append(f"MAP {mid}/d{dl} {span(cs)}")
                if parts:
                    pp = player_at(tr, t)
                    pr = prayer_at(pt, t)
                    print(f" +{t - t0:4d} pl{pp[0]},{pp[1]} {pr[:5]:5s} | " + " | ".join(parts))
            if fight is not None:
                return


if __name__ == "__main__":
    main()
