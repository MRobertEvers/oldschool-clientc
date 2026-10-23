#!/usr/bin/env python3
"""Damage per Colosseum attack, from splats on the recorder that only one
attack can explain.

    python3 tools/rsprox/colosseum/colo_damage.py <transcript|@list>... [--detail TYPE]

Each attack seq gets a landing tick: projectile attacks at T + (endtime + 15) // 30
(endtime of the projectile launched from the attacker's footprint that tick); the
trio's at T + 1; the berserker and minotaur at T + 1; the jaguar at T (three splats).
A landing tick is used only when exactly one attack lands there and nothing else
hits the recorder (no other attack within its window). Prayer is read at the
attack tick (A) and at the landing (L); both are reported.
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_colosseum import load, expand  # noqa: E402
from colo_lives import lives_of, tick_index, iid  # noqa: E402
from colo_attacks import prayer_track, prayer_at, footprint_dist  # noqa: E402

ATT = {
    12814: ("archer", {10850}, 1, "proj"),
    12815: ("seer", {10853}, 1, "proj"),
    12816: ("berserker", {10856}, 1, 1),
    12811: ("shaman", {10859}, 1, "proj"),
    12810: ("jaguar", {10847}, 2, 0),
    12817: ("javelin", {10892}, 3, "proj"),
    12819: ("shockwave", {10903}, 3, "proj"),
    12812: ("minotaur", {10843}, 3, 1),
    12813: ("minotaur", {10843}, 3, 1),
}
STYLE = {"archer": "range", "seer": "mage", "berserker": "melee", "shaman": "mage", "jaguar": "melee",
         "javelin": "range", "shockwave": "mage", "minotaur": "melee"}


def main():
    args = sys.argv[1:]
    detail = None
    if "--detail" in args:
        i = args.index("--detail")
        detail = args[i + 1]
        del args[i:i + 2]
    out = defaultdict(list)   # (type, prayed_at_A, prayed_at_L) -> [values per landing]
    for p in expand(args):
        d = load(p)
        tix = tick_index(d)
        pt = prayer_track(d)
        lands = defaultdict(list)  # L -> [(type, A)]
        anyatt = Counter()
        for nid, (nm, seqs, size, kind) in ATT.items():
            for key, evs in lives_of(d, nid).items():
                for e in evs:
                    for b in e["b"]:
                        if b[0] == "sequence" and iid(b[1].get("id")) in seqs and not b[1].get("delay"):
                            T = e["t"]
                            if kind == "proj":
                                et = None
                                for x in tix.get(T, []):
                                    if x["k"] == "proj" and x.get("src") and x["src"].get("coord") and footprint_dist(x["src"]["coord"][:2], e["c"][:2], size) == 0:
                                        et = x["et"]
                                if et is None:
                                    continue
                                L = T + (et + 15) // 30
                            else:
                                L = T + kind
                            lands[L].append((nm, T))
        # every other source of player splats we know: manticore orbs, toss, sol... mark busy ticks
        busy = set()
        for nid in (12818, 12821):
            for key, evs in lives_of(d, nid).items():
                for e in evs:
                    for b in e["b"]:
                        if b[0] == "sequence":
                            for k in range(0, 8):
                                busy.add(e["t"] + k)
        for e in d["events"]:
            if e["k"] == "man" and e["id"] in (2674, 2265):
                for k in range(0, 4):
                    busy.add(e["t"] + k)
        for L, who in lands.items():
            if len(who) != 1 or L in busy:
                continue
            nm, T = who[0]
            vals = []
            for x in tix.get(L, []):
                if x["k"] == "pl":
                    for bb in x["b"]:
                        if bb[0] == "hit" and bb[1].get("id") in (12, 16, 43):
                            vals.append(bb[1].get("value"))
            want = 3 if nm == "jaguar" else 1
            if len(vals) != want:
                continue
            st = STYLE[nm]
            pa = prayer_at(pt, T) == st
            pl = prayer_at(pt, L) == st
            out[(nm, pa, pl)].append(vals)
    for k in sorted(out):
        vs = out[k]
        flat = [v for vv in vs for v in vv]
        z = sum(1 for v in flat if v == 0)
        nz = [v for v in flat if v > 0]
        print(f"{k[0]:10s} prayA={int(k[1])} prayL={int(k[2])}  n={len(vs):4d} splats={len(flat):4d} zero={z:4d} ({100*z/max(1,len(flat)):.0f}%) max={max(flat) if flat else '-'} mean_nz={sum(nz)/max(1,len(nz)):.1f}")
        if detail and k[0] == detail:
            c = Counter(flat)
            print("     ", sorted(c.items()))


if __name__ == "__main__":
    main()
