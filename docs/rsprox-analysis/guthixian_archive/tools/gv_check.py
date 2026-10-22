#!/usr/bin/env python3
"""The Archive Guardian's vines and mushrooms, judged by the rules of
guardian_plan.md §6 and §8, on the recordings or on our tick logs.

    python3 gv_check.py rec                    # every --all-players --bas transcript with him
    python3 gv_check.py ours <ticklog.tsv>...  # our scriptruns

Vines: each wave's tiles and faces against the placement rule (one step from
his middle, 8-way, then toward him, then the raider's tile; the map, his 5x5
and vines block); the life (spawn, attacks +3 +6 +9, despawn +12, gone +13);
every raider-beat against the hit rule (one tile off as the tick began, one
hit a beat, alive); the damage by overhead. Mushrooms (ours): each raider's
pair one beside and one three out from their tile of the tick before; each
burst the tick after the first tick from active + 1 a raider stood within a
tile; the poison on burst + 1 to who stood within a tile at the burst's end.
"""
import math
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gv_events import parse, at  # noqa: E402

SESSIONS = [4294, 4296, 4297, 4300, 4303, 4304, 4305, 4306, 4308, 4309, 4310, 4312, 4314, 4315, 4316]
ARCHIVE = os.path.expanduser("~/.rsprox-index-cache/home-all-bas/.rsprox/binary/archive")
SPAWN = (4125, 5597)  # the inert guardian's south-west tile (template)
VINE, MUSH_SPAWN, MUSH_ACTIVE, MUSH_DESPAWN = 16664, 62578, 62579, 62580
GUARDIAN_TYPES = {16657, 16659, 16660}
PRAYER = {0: "melee", 1: "missiles", 2: "magic", None: "none"}


def load_map():
    blocked = set()
    for line in open(os.path.join(HERE, "..", "room_collision.txt")):
        p = line.rstrip("\n").split(" ", 1)
        if len(p) < 2 or not p[0].isdigit():
            continue
        for c, ch in enumerate(p[1]):
            if ch == "#":
                blocked.add((4108 + c, int(p[0])))
    return blocked


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def oct8(dx, dz):
    if dx == 0 and dz == 0:
        return (0, 0)
    k = round(math.atan2(dz, dx) / (math.pi / 4)) % 8
    return [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)][k]


def place(f, sw, blocked):
    """The vine's tile for a raider on `f` (or None), his 5x5 at `sw`."""
    p = oct8(f[0] - sw[0] - 2, f[1] - sw[1] - 2)
    for q in ((f[0] + p[0], f[1] + p[1]), (f[0] - p[0], f[1] - p[1]), f):
        if q not in blocked:
            return q
    return None


def judge(s, M, report):
    """`s`: the gv_events shape (template tiles)."""
    vs = s["vines"]
    W = Counter()
    for T in sorted({v["add"] for v in vs if v["instant"]}):
        obs = Counter((v["tile"], v["face"]) for v in vs if v["add"] == T)
        sw = at(s["boss_tile"], T)
        box = {(sw[0] + a, sw[1] + b) for a in range(5) for b in range(5)}
        taken = {w["tile"] for w in vs if w["add"] < T and (w["del"] or 10 ** 9) > T}
        raiders = sorted(f for n, p in s["pos"].items() if (f := at(p, T - 1)) and n in s["room"](T))
        pred = Counter()
        for f in raiders:
            q = place(f, sw, M | box | taken)
            if q:
                taken.add(q)
                pred[(q, f)] += 1
        W["waves"] += 1
        W["waves exact"] += pred == obs
        for (q, f), n in obs.items():
            k = "A" if q == (f[0] + oct8(f[0] - sw[0] - 2, f[1] - sw[1] - 2)[0], f[1] + oct8(f[0] - sw[0] - 2, f[1] - sw[1] - 2)[1]) else ("raider" if q == f else "toward")
            W["vines " + k] += n
        if pred != obs and report:
            print("   wave", T, "missing", dict(pred - obs), "extra", dict(obs - pred))
    L = Counter()
    for v in vs:
        if not v["instant"]:
            continue
        sig = tuple((t - v["add"], txt.split(" ")[1].split("=")[1]) for t, k, txt in v["blocks"] if k == "[sequence]")
        L[(sig, None if v["del"] is None else v["del"] - v["add"])] += 1
        f = v["face"]
        d = (f[0] - v["tile"][0], f[1] - v["tile"][1])
        L[("face instant", v["instant"])] += 1
        L[("angle", v["angle"], d)] += 1
    H = Counter()
    dmg = defaultdict(Counter)
    beats = defaultdict(list)
    for v in vs:
        for t, k, txt in v["blocks"]:
            if "attack01" in txt or "14768" in txt:
                beats[t].append(v["tile"])
    for A, tiles in beats.items():
        for n, p in s["pos"].items():
            p1 = at(p, A - 1)
            if p1 is None or n not in s["room"](A):
                continue
            hits = [h for h in s["hits"].get(n, []) if h[0] == A and h[1] in s["dmg_types"]]
            k = sum(1 for t in tiles if cheb(p1, t) == 1)
            if k == 0 and not hits:
                continue
            dead = s["dead"](n, A)
            want = 1 if k and not dead else 0
            H[(min(k, 2), len(hits), "dead" if dead else "alive")] += 1
            H["agree" if want == len(hits) else "disagree"] += 1
            for h in hits:
                if not s["capped"](n, A, h):
                    dmg[s["prayer"](n, A)][h[2]] += 1
    return W, L, H, dmg


def print_judgement(W, L, H, dmg):
    print("vine placement:", dict(W))
    print("vine lives (seqs, add -> del):")
    for k, n in sorted(((k, n) for k, n in L.items() if isinstance(k[0], tuple)), key=lambda x: -x[1])[:4]:
        print(f"   {n:5d} {k}")
    print("   face instant:", {k[1]: n for k, n in L.items() if k[0] == "face instant"})
    bad = sum(n for k, n in L.items() if k[0] == "angle" and k[1] != ANGLE.get(k[2]))
    print("   spawn angle toward the face:", sum(n for k, n in L.items() if k[0] == "angle") - bad, "of", sum(n for k, n in L.items() if k[0] == "angle"))
    print("vine hits (vines in reach, splats, alive):", {k: n for k, n in H.items() if isinstance(k, tuple)})
    print("   rule agrees:", H["agree"], "of", H["agree"] + H["disagree"])
    for p, c in dmg.items():
        print(f"   damage, prayer {p}: {sorted(c.items())}")


ANGLE = {(0, 0): "south", (0, -1): "south", (0, 1): "north", (1, 0): "east", (-1, 0): "west",
         (1, 1): "north-east", (-1, 1): "north-west", (1, -1): "south-east", (-1, -1): "south-west"}


def rec():
    M = load_map()
    tot = None
    for i in SESSIONS:
        s = parse(f"{ARCHIVE}/idx{i}.txt")
        names = set(s["pos"])
        s["room"] = lambda T, s=s: {n for n, p in s["pos"].items() if (q := at(p, T - 1)) and 4108 <= q[0] <= 4146 and 5578 <= q[1] <= 5620}
        s["dmg_types"] = {12, 13, 16, 17, 18, 19, 20, 21}
        s["dead"] = lambda n, A: False
        s["capped"] = lambda n, A, h: h[2] < 10
        s["prayer"] = lambda n, A, s=s, i=i: (PRAYER.get(at(s["pray"][n], A), "?") if s["pray"].get(n) else ("no overheads on the wire" if i not in (4303, 4304, 4308, 4309, 4310, 4312, 4316) else "none"))
        r = judge(s, M, report=False)
        if tot is None:
            tot = r
        else:
            for a, b in zip(tot[:3], r[:3]):
                a.update(b)
            for p, c in r[3].items():
                tot[3][p].update(c)
        del names
    print_judgement(*tot)


def ours(paths):
    M0 = load_map()
    for path in paths:
        rows = [l.rstrip("\n").split("\t") for l in open(path, errors="replace")]
        rows = [r for r in rows if len(r) > 8 and r[1].isdigit()]
        xz = lambda c: ((int(c) >> 14) & 0x3FFF, int(c) & 0x3FFF)
        off = None
        boss_slot = None
        for r in rows:
            if r[2] == "npc_spawn" and r[4] == "16657":
                bx, bz = xz(r[5])
                off = (bx - SPAWN[0], bz - SPAWN[1])
                boss_slot = r[3]
                break
        if off is None:
            print(path, ": no guardian")
            continue
        tpl = lambda p: (p[0] - off[0], p[1] - off[1])
        s = {"vines": [], "pos": defaultdict(dict), "hits": defaultdict(list), "boss_tile": {}, "pray": defaultdict(dict)}
        live = {}
        mush = {}
        mrows = []
        hp = defaultdict(dict)
        for r in rows:
            t, k, v = int(r[1]), r[2], r[3:]
            if k == "player_tile":
                s["pos"]["p" + v[0]][t] = tpl((int(v[1]), int(v[2])))
            elif k == "raider":
                lab = r[9] if len(r) > 9 else ""
                head = lab.split("head ")[1].split()[0] if "head " in lab else "-1"
                # our headicons are a mask, 1 << icon
                m = int(head)
                s["pray"]["p" + v[0]][t] = None if m <= 0 else (m & -m).bit_length() - 1
                hp["p" + v[0]][t] = int(v[1])
            elif k == "npc_spawn" and v[1] == str(VINE):
                w = {"add": t, "del": None, "tile": tpl(xz(v[2])), "face": None, "instant": True, "angle": None, "blocks": [(t, "[sequence]", "[sequence] id=spawn01")]}
                s["vines"].append(w)
                live[v[0]] = w
            elif k == "npc_face" and v[0] in live and live[v[0]]["add"] == t:
                live[v[0]]["face"] = tpl((int(v[2]), int(v[3])))
            elif k == "npc_info" and v[0] in live and "spawnangle" in (r[9] if len(r) > 9 else ""):
                live[v[0]]["angle"] = {0: "south", 256: "south-west", 512: "west", 768: "north-west", 1024: "north", 1280: "north-east", 1536: "east", 1792: "south-east"}.get(int(v[3]), v[3])
            elif k == "npc_anim" and v[0] in live and v[2] in ("14768", "14769"):
                live[v[0]]["blocks"].append((t, "[sequence]", "[sequence] id=" + ("attack01" if v[2] == "14768" else "despawn01")))
            elif k == "npc_free" and v[0] in live:
                live.pop(v[0])["del"] = t
            elif k == "hit_player":
                s["hits"]["p" + v[0]].append((t, int(v[3]), int(v[2]), v[5]))
            elif (k == "npc_spawn" and v[0] == boss_slot) or (k == "npc_tile" and v[0] == boss_slot):
                s["boss_tile"][t] = tpl(xz(v[2]) if k == "npc_spawn" else (int(v[1]), int(v[2])))
            elif k == "loc_set" and v[1] in (str(MUSH_SPAWN), str(MUSH_ACTIVE), str(MUSH_DESPAWN)):
                mrows.append((t, int(v[1]), tpl(xz(v[0]))))
        # the spawn angle: the npc_info row's value is the client's 0..2047, south 0
        s["room"] = lambda T: set(s["pos"])
        s["dmg_types"] = None
        vine_hits = {n: [(t, ty, d) for t, ty, d, src in hl if src == str(VINE)] for n, hl in s["hits"].items()}
        s["hits"] = vine_hits
        s["dmg_types"] = {ty for hl in vine_hits.values() for _, ty, _ in hl}
        s["dead"] = lambda n, A: (at(hp[n], A - 1) or 1) <= 0
        s["capped"] = lambda n, A, h: h[2] < 10
        s["prayer"] = lambda n, A: PRAYER.get(at(s["pray"][n], A), "?")
        print("==", path)
        print_judgement(*judge(s, M0, report=True))
        mushrooms(s, mrows, rows, tpl, M0)


def mushrooms(s, mrows, rows, tpl, M):
    """Ours: placement round the tile of the tick before, the burst, the poison."""
    specials = sorted({int(r[1]) for r in rows if r[2] == "npc_anim" and r[5] == "14753"})
    C = Counter()
    spawned = defaultdict(list)
    for t, loc, q in mrows:
        if loc == MUSH_SPAWN:
            spawned[t].append(q)
    for T in specials:
        tiles = spawned.get(T, [])
        for n, p in s["pos"].items():
            f = at(p, T - 1)
            if f is None:
                continue
            rings = sorted(cheb(f, q) for q in tiles)
            C["raiders"] += 1
            C["one beside and one three out"] += 1 in rings and 3 in rings
            g = at(p, T)
            if g != f:
                C["moved that tick"] += 1
                C["moved: still round the tick-before tile"] += 1 in rings and 3 in rings
    lives = []  # (tile, active, burst): a tile can hold several in a fight
    for t, loc, q in sorted(mrows):
        if loc == MUSH_ACTIVE:
            lives.append([q, t, None])
        elif loc == MUSH_DESPAWN:
            for m in lives:
                if m[0] == q and m[2] is None:
                    m[2] = t
    for q, a, b in lives:
        first = None
        for T in range(a + 1, (b or a + 400)):
            if any(cheb(at(p, T) or (0, 0), q) <= 1 for p in s["pos"].values()):
                first = T
                break
        if b is None and first is None:
            continue
        C["bursts"] += 1
        good = b is not None and first is not None and b == first + 1
        C["burst = first tick from active + 1 within a tile, + 1"] += good
        if not good:
            print("   mushroom", q, "active", a, "first within a tile", first, "burst", b)
    burst = [(q, b) for q, a, b in lives if b is not None]
    poisons = [(int(r[1]), "p" + r[3]) for r in rows if r[2] == "hit_player" and r[6] == "68"]  # ours: hitsplat_poison
    for q, b in burst:
        for n, p in s["pos"].items():
            near = cheb(at(p, b) or (0, 0), q) <= 1
            got = any(t == b + 1 and m == n for t, m in poisons)
            if near or got:
                C["poison: within 1 at the burst's end"] += near
                C["poison: splat on burst + 1 (if not already poisoned)"] += got
    print("mushrooms:", dict(C))


if __name__ == "__main__":
    if sys.argv[1] == "rec":
        rec()
    else:
        ours(sys.argv[2:])
