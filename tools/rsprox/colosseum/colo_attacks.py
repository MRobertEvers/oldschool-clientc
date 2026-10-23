#!/usr/bin/env python3
"""Attack statistics of one Colosseum npc type across transcripts.

    python3 tools/rsprox/colosseum/colo_attacks.py <type> <seq,seq..> <transcript|@list>... [--size N]

Per attack seq: the gap to the npc's previous attack (any seq of the set), the
first attack after the add, the projectiles launched from the npc's footprint on
that tick (every field, endtime against Chebyshev distance), the sound_area and
synth_sound sent that tick near it, the recorder's protection prayer, and the
first splat on the recorder at attack + k for the k the projectile predicts.
"""
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_colosseum import load, player_track, player_at, expand  # noqa: E402
from colo_lives import lives_of, tick_index, iid, BYNAME  # noqa: E402

PRAY = {4096: "mage", 8192: "range", 16384: "melee"}


def prayer_track(d):
    out = []
    for e in d["events"]:
        if e["k"] == "var" and e["id"] == 4101 and e.get("vk") == "varbit":
            out.append((e["t"], e["new"]))
    return out


def prayer_at(track, t):
    v = 0
    for tt, nv in track:
        if tt > t:
            break
        v = nv
    s = [n for b, n in PRAY.items() if v & b]
    return s[0] if len(s) == 1 else ("none" if not s else "+".join(s))


def footprint_dist(p, sw, size):
    dx = max(sw[0] - p[0], 0, p[0] - (sw[0] + size - 1))
    dz = max(sw[1] - p[1], 0, p[1] - (sw[1] + size - 1))
    return max(dx, dz)


def main():
    a = sys.argv[1:]
    size = 1
    if "--size" in a:
        i = a.index("--size")
        size = int(a[i + 1])
        del a[i:i + 2]
    nm, seqs = a[0], set(int(x) for x in a[1].split(","))
    paths = expand(a[2:])
    nid = BYNAME[nm]
    gaps = Counter()
    first = Counter()
    nattack = Counter()
    proj = Counter()
    et_by_d = defaultdict(Counter)
    sounds = Counter()
    synths = Counter()
    hitk = Counter()
    hits = defaultdict(list)
    dist_at = Counter()
    deathrow = Counter()
    spot_on = Counter()
    for p in paths:
        d = load(p)
        tix = tick_index(d)
        tr = player_track(d)
        pt = prayer_track(d)
        for (idx, t0), evs in lives_of(d, nid).items():
            pos = tuple(evs[0]["c"][:2])
            tilemap = {}
            for e in evs:
                tilemap[e["t"]] = (tuple(e["c"][:2]), tuple((e.get("nc") or e["c"])[:2]))
            last = None
            zero_t = None
            for e in evs:
                for b in e["b"]:
                    if b[0] == "hit" and b[1].get("value") is not None:
                        pass
                    if b[0] == "headbar" and b[1].get("fill") == 0 and zero_t is None:
                        zero_t = e["t"]
                    if b[0] != "sequence":
                        continue
                    s = iid(b[1].get("id"))
                    if s in (10852, 10855, 10858) or (s not in seqs):
                        if zero_t is not None and s not in seqs and b[1].get("delay", 0) == 0:
                            deathrow[(s, e["t"] - zero_t)] += 1
                        continue
                    t = e["t"]
                    nattack[s] += 1
                    if last is None:
                        first[t - t0] += 1
                    else:
                        gaps[t - last] += 1
                    last = t
                    start_tile = e["c"][:2]
                    pp = player_at(tr, t, before=True)
                    if pp:
                        dist_at[footprint_dist(pp, start_tile, size)] += 1
                    pray = prayer_at(pt, t)
                    pred = None
                    for x in tix.get(t, []):
                        if x["k"] == "proj" and x.get("src"):
                            sc = x["src"].get("coord")
                            if sc and footprint_dist(sc[:2], start_tile, size) == 0:
                                proj[(x["id"], x["st"], x["ang"], x["prog"], x["sh"], x["eh"])] += 1
                                dc = x["dst"].get("coord") if x.get("dst") else None
                                if dc:
                                    et_by_d[max(abs(dc[0] - sc[0]), abs(dc[1] - sc[1]))][x["et"]] += 1
                                pred = x["et"]
                        elif x["k"] == "snd" and x.get("c") and footprint_dist(x["c"][:2], start_tile, size + 2) == 0:
                            sounds[(x["id"], x["delay"], x["range"], x.get("size"))] += 1
                        elif x["k"] == "syn":
                            synths[(x["id"], x["delay"])] += 1
                        elif x["k"] == "pl":
                            for bb in x["b"]:
                                if bb[0] == "spotanim":
                                    spot_on[(iid(bb[1].get("id")), bb[1].get("height"), bb[1].get("delay"))] += 1
                    # first player splat after the attack
                    for k in range(0, 6):
                        got = None
                        for x in tix.get(t + k, []):
                            if x["k"] == "pl":
                                for bb in x["b"]:
                                    if bb[0] == "hit":
                                        got = bb[1]
                        if got is not None and k >= 1:
                            hitk[k] += 1
                            break
    print(f"== {nm}: attacks {dict(nattack)}")
    print("first attack after add:", sorted(first.items())[:20])
    print("gaps:", sorted(gaps.items())[:20])
    print("player distance (footprint, start of tick) at attack:", sorted(dist_at.items())[:20])
    print("projectiles (id, st, angle, prog, sh, eh):", proj.most_common(6))
    print("endtime by distance:", {k: dict(v.most_common(3)) for k, v in sorted(et_by_d.items())})
    print("sound_area near the npc (id, delay, range, size):", sounds.most_common(8))
    print("synth that tick (id, delay):", synths.most_common(8))
    print("player spotanims that tick:", spot_on.most_common(6))
    print("first splat on the recorder at attack + k:", sorted(hitk.items()))
    print("non-attack seqs after hp 0 (seq, ticks after the 0 bar):", deathrow.most_common(6))


if __name__ == "__main__":
    main()
