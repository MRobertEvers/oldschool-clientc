"""Ocellus's phase 1, recordings against our tick log, aligned on each
mechanic's own anchor (oc_p1_events.py).

    python3 oc_p1_check.py charge|middle|lines|volley <ticklog.tsv> <transcript>...
"""
import re, sys, pickle, os
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oc_p1_events import recording, ours, names_from

OUR_MID = (6431, 169)


def load(paths, ticklog):
    cache = "/tmp/oc_p1_cache.pkl" if not os.environ.get("OC_CACHE") else os.environ["OC_CACHE"]
    key = tuple((p, os.path.getmtime(p)) for p in paths)
    if os.path.exists(cache):
        c = pickle.load(open(cache, "rb"))
        if c["key"] == key:
            rec = c["rec"]; names = c["names"]
        else:
            rec = None
    else:
        rec = None
    if rec is None:
        names = names_from(paths)
        rec = [(os.path.basename(p), life) for p in paths for life in recording(p)]
        pickle.dump({"key": key, "rec": rec, "names": names}, open(cache, "wb"))
    our = [("ours", life) for life in ours(ticklog, OUR_MID, names)]
    return rec, our


def classify(e):
    k = e[1]
    if k in ("oc_seq",):
        return f"seq {e[2]}"
    if k == "oc_spot":
        return f"spot {e[2]} s{e[3]} d{e[4]}"
    if k == "oc_say":
        return "say"
    if k == "oc_face":
        f = e[2]
        return "face " + ("coord" if f.startswith("coord") else "entity" if f.startswith("player") or f.startswith("entity") else f.split(",")[0].split("=")[-1])
    if k == "oc_hit":
        return f"oc_hit type{e[2]}" + (f" v{e[3]}" if e[2] == 6 else "")
    if k == "oc_heal":
        return f"oc_heal {e[2]}"
    if k == "anim":
        n = re.sub(r"_(90|180|270)$", "", e[2])
        n = re.sub(r"_(90|180|270)_", "_", n)
        return f"anim {n}"
    if k == "proj":
        return f"proj {e[2]} {e[3]}/{e[4]}"
    if k == "sound":
        return f"sound {e[2]} d{e[3]} r{e[4]} s{e[5]}"
    if k == "meter":
        return "meter"
    if k == "loc":
        return f"loc {e[2]}"
    if k == "pl_hit":
        return f"pl_hit type{e[3]}"
    if k == "oc_move":
        return "move"
    return k


def anchors(life, kind):
    ev = life["ev"]
    if kind == "charge":
        ts = sorted({e[0] for e in ev if e[1] == "oc_seq" and e[2] == "npc_ocellus_phase1_charge"})
        out = []
        for t in ts:
            if not out or t - out[-1] > 20:
                # a corner charge has seven seqs 2 apart; the middle's beats are single
                n = sum(1 for u in ts if t <= u <= t + 12)
                if n >= 6:
                    out.append(t)
        return out
    if kind == "middle":
        return sorted({e[0] for e in ev if e[1] == "oc_seq" and e[2] == "npc_ocellus_phase1_teleport_out"})
    return []


def profile(lives, kind, lo, hi):
    rows = defaultdict(Counter)
    n = 0
    for name, life in lives:
        for a in anchors(life, kind):
            n += 1
            seen = set()
            for e in life["ev"]:
                r = e[0] - a
                if lo <= r <= hi:
                    c = classify(e)
                    if c in ("move",):
                        continue
                    if (r, c) in seen:
                        continue
                    seen.add((r, c))
                    rows[c][r] += 1
    return rows, n


def main():
    mode, ticklog, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    rec, our = load(paths, ticklog)
    lo, hi = (-10, 24) if mode == "charge" else (-2, 60)
    R, nr = profile(rec, mode, lo, hi)
    O, no = profile(our, mode, lo, hi)
    print(f"{mode}: recorded {nr}, ours {no}")
    for c in sorted(set(R) | set(O)):
        rr = {r: v / nr for r, v in R[c].items()} if nr else {}
        oo = {r: v / no for r, v in O[c].items()} if no else {}
        ticks = sorted(set(rr) | set(oo))
        diff = [r for r in ticks if abs(rr.get(r, 0) - oo.get(r, 0)) > 0.34]
        mark = "  <<" if diff else ""
        def fmt(d):
            return " ".join(f"{r:+d}:{d[r]:.2f}" for r in sorted(d) if d[r] >= 0.05)
        print(f"{c}{mark}\n   R {fmt(rr)}\n   O {fmt(oo)}")


main()
