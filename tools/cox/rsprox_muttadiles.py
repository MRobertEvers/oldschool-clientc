#!/usr/bin/env python3
"""The Muttadiles' room as Jagex's server sent it, read out of RSProx
transcripts, in TEMPLATE room-local tiles (tools/cox/rsprox_room.py: the
turned-room fixes).

    python3 tools/cox/rsprox_muttadiles.py ~/.rsprox/binary/archive/{3285,3282,843,3389}.txt
    python3 tools/cox/rsprox_muttadiles.py --section meals FILES...

Sections: meals, attacks, projectiles, submerged, prayer, surfacing, moves,
sounds, steps (the erratic step: its tick, the cleared face, its direction).

Ticks are the transcript's. "gap" is the Chebyshev distance from the local
raider's tile to the npc's footprint (0 under it, 1 beside it), both read at
the end of the tick before the attack: an npc acts before the players move.
The small Muttadile is 3x3, the large 5x5, the meat tree 2x2. A party
transcript (820) only means anything here re-transcribed with every player
shown (tools/cox/rsprox_olm_party.py says how); the local raider is still the
one whose hits and prayers are read.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_room import load_room, name_of, num  # noqa: E402

SUBMERGED, JUNIOR, LARGE, TREE = 7561, 7562, 7563, 7564
IDS = {SUBMERGED, JUNIOR, LARGE, TREE}
SIZE = {JUNIOR: 3, LARGE: 5, SUBMERGED: 5, TREE: 2}
SHORT = {SUBMERGED: "sub", JUNIOR: "small", LARGE: "large", TREE: "tree"}
ATTACK_SEQS = ("dohgadyle_bite", "dohgadyle_bark", "dohgadyle_lazor", "dohgadyle_slam")
PROJ = ("lizardman_spit", "dragon_ranged_fire_attack")


PLAYER_LINE = re.compile(r"^ {8}\[(\w+)\] player=\(index=\d+, name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)"
                         r"(?:, newcoord=\((\d+), (\d+), (\d+)\))?")


def load(path):
    R = load_room(path, npc_ids=IDS)
    R.pray = prayers(R)
    R.max_hp = 375 if is_cm(path) else 250
    R.players = player_tiles(R, path)
    R.local_name = local_name(path, R.local)
    return R


def local_name(path, index):
    with open(path, encoding="utf-8", errors="replace") as h:
        for line in h:
            m = re.search(r"player=\(index=%s, name=([^,]*)" % index, line)
            if m:
                return m.group(1)
    return None


def player_tiles(R, path):
    """name -> {tick: printed tile at the END of the tick}, every raider the
    transcript shows (all of them only in an every-player transcript)."""
    out = defaultdict(dict)
    tick = 0
    with open(path, encoding="utf-8", errors="replace") as h:
        for line in h:
            m = re.match(r"^\[(\d+)\]$", line.rstrip("\n"))
            if m:
                tick = int(m.group(1))
                if tick > R.last + 40:
                    break
                continue
            if tick < R.first - 40:
                continue
            m = PLAYER_LINE.match(line)
            if m:
                end = (int(m.group(6)), int(m.group(7))) if m.group(6) else (int(m.group(3)), int(m.group(4)))
                out[m.group(2)].setdefault(tick - 1, (int(m.group(3)), int(m.group(4))))
                out[m.group(2)][tick] = end
    return out


def tile_of(R, name, t):
    """A raider's room-local tile at the end of tick t (the last one shown)."""
    track = R.players.get(name, {})
    best = None
    for u in sorted(track):
        if u <= t:
            best = track[u]
    return R.local_of(best) if best else None


def is_cm(path):
    with open(path, encoding="utf-8", errors="replace") as h:
        for line in h:
            if "Challenge Mode" in line:
                return True
    return False


def prayers(R):
    st = {"magic": 0, "missiles": 0, "melee": 0}
    out = {}
    for t in range(R.first - 40, R.last + 41):
        for k, text, subs in R.top.get(t, []):
            m = re.search(r"prayer_protectfrom(magic|missiles|melee) \(\d+\), old=\d+, new=(\d+)", text)
            if m:
                st[m.group(1)] = int(m.group(2))
        out[t] = dict(st)
    return out


def pray_at(R, t):
    p = R.pray.get(t, {})
    return "".join(c for k, c in (("magic", "G"), ("missiles", "R"), ("melee", "M")) if p.get(k)) or "-"


def lives(R, ids):
    return [n for n in R.lives if n.types and n.types[0][1] in ids]


def kind_at(n, t):
    return n.type_at(t)[1]


def gap(R, n, t, me=None):
    """Chebyshev gap from me (end of t-1) to the npc's footprint (end of t-1),
    and whether I am edge-adjacent (not diagonal)."""
    sz = SIZE[kind_at(n, t)]
    sw = R.sw_of(n.tile_at(t - 1), sz)
    me = me or R.local_of(R.me_at(t - 1))
    dx = max(sw[0] - me[0], 0, me[0] - (sw[0] + sz - 1))
    dz = max(sw[1] - me[1], 0, me[1] - (sw[1] + sz - 1))
    return max(dx, dz), (dx + dz == 1)


def heals(n, t):
    return [int(d.split()[1]) for k, d in n.ev.get(t, []) if k == "hit" and d.split()[0] == "6"]


def damage_to(n, t):
    """Damage and healing the npc has taken up to and including tick t."""
    dmg = heal = 0
    for u in sorted(n.ev):
        if u > t:
            break
        for k, d in n.ev[u]:
            if k == "hit":
                i, v = [int(x) for x in d.split()]
                if i == 6:
                    heal += v
                else:
                    dmg += v
    return dmg, heal


def faces_tree(n, t):
    return any(k == "face_pathingentity" and "meat_tree" in d for k, d in n.ev.get(t, []))


def attacks(R, n):
    """[(tick, seq, meal_bite)] of one npc's attack animations."""
    out = []
    for t in sorted(n.ev):
        for k, d in n.ev[t]:
            if k == "seq" and d.split()[0] in ATTACK_SEQS:
                out.append((t, d.split()[0], d.startswith("dohgadyle_bite") and bool(heals(n, t))))
    return out


def projectiles(R, t):
    out = []
    for k, text, subs in R.top.get(t, []):
        if k == "map_projanim_v2" and name_of(text) in PROJ:
            tgt = [s for s in subs if s.startswith("[target]")]
            who = re.search(r"player=\(index=(\d+), name=([^,]*)", tgt[0]) if tgt else None
            src = [s for s in subs if s.startswith("[source]")]
            out.append({"name": name_of(text), "start": num(text, "starttime"), "end": num(text, "endtime"),
                        "angle": num(text, "angle"), "h0": num(text, "startheight"), "h1": num(text, "endheight"),
                        "progress": num(text, "progress"),
                        "to": who.group(2) if who else None,
                        "src": re.search(r"coord=\((\d+), (\d+)", src[0]).groups() if src else None})
    return out


def my_hits(R, t0, t1):
    out = []
    for u in range(t0, t1 + 1):
        for k, d in R.my_ev.get(u, []):
            if k == "hit":
                i, v = d.split()
                out.append((u, int(i), int(v)))
    return out


# ------------------------------------------------------------------ sections

def section_meals(R):
    for n in lives(R, {JUNIOR, LARGE, SUBMERGED}):
        meals = []
        cur = None
        for t in sorted(n.ev):
            if faces_tree(n, t) and (cur is None or cur.get("done")):
                dmg, heal = damage_to(n, t - 1)
                cur = {"face": t, "hp": R.max_hp - dmg + heal, "bites": []}
                meals.append(cur)
            h = heals(n, t)
            if h and cur is not None:
                cur["bites"].append((t, h[0]))
                if len(cur["bites"]) >= 1:
                    cur["last"] = t
            if cur is not None and cur.get("last") and t > cur["last"] + 1:
                cur["done"] = True
        for m in meals:
            b = m["bites"]
            print("  %s meal: faced the tree t%d at hp %d (%.0f%%); bites %s; walk %s ticks" % (
                SHORT[kind_at(n, m["face"])], m["face"], m["hp"], 100.0 * m["hp"] / R.max_hp,
                " ".join("t%d+%d" % x for x in b) or "none",
                (b[0][0] - m["face"]) if b else "-"))
        if meals:
            print("  %s: %d meal(s), heals %s" % (SHORT[n.types[-1][1]], len(meals),
                                                  Counter(v for m in meals for _, v in m["bites"])))


def target_at(R, n, t):
    """The raider an npc is on at tick t: its latest face_pathingentity on a player."""
    best = None
    for u in sorted(n.ev):
        if u > t:
            break
        for k, d in n.ev[u]:
            if k == "face_pathingentity":
                m = re.search(r"player=\(index=\d+, name=([^,]*)", d)
                best = m.group(1) if m else None
    return best


def attack_rows(R):
    """Every attack (meal bites left out): npc, tick, seq, target, its gap, projectiles."""
    rows = []
    for n in lives(R, {JUNIOR, LARGE, SUBMERGED}):
        last = None
        for t, seq, meal in attacks(R, n):
            if meal:
                continue
            pj = [p for p in projectiles(R, t) if from_npc(R, n, t, p)]
            who = next((p["to"] for p in pj if p["to"]), None) or target_at(R, n, t) or R.local_name
            me = tile_of(R, who, t - 1)
            g, edge = gap(R, n, t, me) if me else (None, False)
            rows.append({"n": n, "t": t, "kind": SHORT[kind_at(n, t)], "seq": seq.replace("dohgadyle_", ""),
                         "who": who, "gap": g, "edge": edge, "dt": (t - last) if last else None, "proj": pj})
            last = t
    return rows


def from_npc(R, n, t, p):
    """Is a projectile's source tile inside the npc's footprint (or beside it)?"""
    if not p["src"]:
        return False
    src = R.local_of((int(p["src"][0]), int(p["src"][1])))
    sz = SIZE[kind_at(n, t)]
    sw = R.sw_of(n.tile_at(t - 1), sz)
    return sw[0] - 1 <= src[0] <= sw[0] + sz and sw[1] - 1 <= src[1] <= sw[1] + sz


def section_submerged(R):
    """The submerged large one's sweeps: its projectiles (none of the surfaced
    attacks'), how far apart they come, who they go at, and -- on the local
    raider -- what landed under which prayer."""
    for n in lives(R, {SUBMERGED}):
        sub_ticks = [t for t in sorted(n.tiles) if kind_at(n, t) == SUBMERGED]
        if not sub_ticks:
            continue
        lo, hi = sub_ticks[0], sub_ticks[-1]
        throws = []
        for t in range(lo, hi + 1):
            pj = [p for p in projectiles(R, t) if p["name"] == "dragon_ranged_fire_attack" and from_npc(R, n, t, p)]
            if pj:
                throws.append((t, pj))
        prev = None
        gaps = Counter()
        per = Counter()
        for t, pj in throws:
            if prev is not None:
                gaps[t - prev] += 1
            prev = t
            per[len(pj)] += 1
            for p in pj:
                if p["to"] == R.local_name:
                    src = R.local_of((int(p["src"][0]), int(p["src"][1])))
                    d = chebyshev(src, tile_of(R, p["to"], t - 1))
                    h = [(u - t, v) for u, i, v in my_hits(R, t, t + 6) if i != 6]
                    print("    t%d at me d %d end %s pray %s hits %s" % (t, d, p["end"], pray_at(R, t), h[:3]))
        span = hi - lo + 1
        raiders = len([k for k, v in R.players.items() if any(lo <= u <= hi for u in v)])
        print("  submerged t%d..t%d (%d ticks, %d raider(s) shown): %d throws on %d ticks; ticks between %s; projectiles a throw tick %s; grid mod 4 %s" % (
            lo, hi, span, raiders, sum(len(p) for _, p in throws), len(throws), dict(sorted(gaps.items())), dict(per),
            dict(Counter(t % 4 for t, _ in throws))))


PRAYER_OF = {"bite": "melee", "slam": "melee", "bark": "missiles", "lazor": "magic", "sweep": "magic"}


def thrown_at_me(R):
    """(tick, npc, seq, landing tick, damage) of every attack on the local
    raider, the sweep's included; the landing of a thrown one is end // 30."""
    out = []
    for r in attack_rows(R):
        if r["who"] != R.local_name:
            continue
        land = r["t"] + 1 if r["seq"] == "slam" else r["t"]
        for p in r["proj"]:
            if p["to"] == R.local_name:
                land = r["t"] + p["end"] // 30
        out.append((r["t"], r["kind"], r["seq"], land))
    for n in lives(R, {SUBMERGED}):
        for t in sorted(n.tiles):
            if kind_at(n, t) != SUBMERGED:
                continue
            for p in projectiles(R, t):
                if p["name"] == "dragon_ranged_fire_attack" and p["to"] == R.local_name and from_npc(R, n, t, p):
                    out.append((t, "sub", "sweep", t + p["end"] // 30))
    rows = []
    for t, kind, seq, land in sorted(out):
        hits = [v for u, i, v in my_hits(R, land, land) if i != 6]
        rows.append((t, kind, seq, land, hits))
    return rows


def section_prayer(R):
    """Damage by whether the matching protection prayer was on at the swing
    and at the landing (a hit is only counted when exactly one landed then)."""
    tab = defaultdict(list)
    for t, kind, seq, land, hits in thrown_at_me(R):
        if len(hits) != 1:
            continue
        pr = PRAYER_OF[seq]
        at_swing = bool(R.pray.get(t, {}).get(pr))
        at_land = bool(R.pray.get(land, {}).get(pr))
        tab[(kind, seq, "swing " + ("on" if at_swing else "off"), "land " + ("on" if at_land else "off"))].append(hits[0])
    for k, v in sorted(tab.items()):
        print("  %-5s %-5s %-9s %-8s n=%d max %d values %s" % (k[0], k[1], k[2], k[3], len(v), max(v), sorted(v)))


def section_surfacing(R):
    """The small one's death tick D, and the large one's emerge, walk, retype
    and first surfaced attack against it."""
    small = lives(R, {JUNIOR})
    large = lives(R, {SUBMERGED})
    if not small or not large:
        return
    D = None
    for t in sorted(small[0].ev):
        if any(k == "seq" and d.startswith("dohgadyle_death") for k, d in small[0].ev[t]):
            D = t
            break
    n = large[0]
    if D is None:
        print("  the small one never died")
        return
    emerge = next((t for t in sorted(n.ev) if any(k == "seq" and d.startswith("dohgadyle_emerge") for k, d in n.ev[t])), None)
    retype = next((t for t, i, nm in n.types if i == LARGE), None)
    steps = [(t, R.sw_of(n.tiles[t], 5)) for t in sorted(n.ev) if D <= t <= D + 15 and any(k in ("walk", "run", "teleport") for k, d in n.ev[t])]
    first = next((t for t, seq, meal in attacks(R, n) if t > D), None)
    msgs = [(t - D, m) for t in range(D - 2, D + 10) for k, text, subs in R.top.get(t, []) if k == "message_game" for m in [text[-60:]] if "Mutta" in text or "water" in text]
    print("  small dies D=t%d; emerge %s; steps %s; retype %s; first attack %s; messages %s" % (
        D, "D+%d" % (emerge - D) if emerge else None, ["D+%d %s" % (t - D, sw) for t, sw in steps],
        "D+%d" % (retype - D) if retype else None, "D+%d" % (first - D) if first else None, msgs))


def meal_ticks(R, n):
    """The ticks an npc is on a meal: from facing the tree to its last bite."""
    out = set()
    start = None
    for t in sorted(n.ev):
        if faces_tree(n, t):
            start = t
        if start is not None and heals(n, t):
            out.update(range(start, t + 1))
        if start is not None and t > start + 30:
            start = None
    return out


def section_moves(R):
    """The Muttadiles' steps outside a meal: the ticks since the npc's last
    attack, its target's gap before and after, and whether it went nearer."""
    for n in lives(R, {JUNIOR, SUBMERGED}):
        meal = meal_ticks(R, n)
        atk = [t for t, seq, m in attacks(R, n) if not m]
        rows = []
        for t in sorted(n.ev):
            if kind_at(n, t) == SUBMERGED or t in meal:
                continue
            if not any(k in ("walk", "run") for k, d in n.ev[t]):
                continue
            who = target_at(R, n, t) or R.local_name
            me = tile_of(R, who, t - 1)
            if me is None:
                continue
            before, _ = gap(R, n, t, me)
            sz = SIZE[kind_at(n, t)]
            sw = R.sw_of(n.tile_at(t), sz)
            dx = max(sw[0] - me[0], 0, me[0] - (sw[0] + sz - 1))
            dz = max(sw[1] - me[1], 0, me[1] - (sw[1] + sz - 1))
            after = max(dx, dz)
            since = [t - a for a in atk if a <= t]
            rows.append((t, since[-1] if since else None, before, after))
        if not rows:
            continue
        print("  %s: %d steps outside meals" % (SHORT[n.types[-1][1]], len(rows)))
        print("    ticks since its last attack %s" % dict(sorted(Counter(r[1] for r in rows if r[1] is not None and r[1] <= 8).items())))
        print("    gap before -> after %s" % dict(sorted(Counter((r[2], r[3]) for r in rows).items())))
        runs = []
        for t, since, b, a in rows:
            if runs and t == runs[-1][-1][0] + 1:
                runs[-1].append((t, since, b, a))
            else:
                runs.append([(t, since, b, a)])
        print("    walks (consecutive steps): %s" % "; ".join("t%d x%d since %s gap %d->%d" % (
            w[0][0], len(w), w[0][1], w[0][2], w[-1][3]) for w in runs))


def sounds_at(R, t):
    out = []
    for k, text, subs in R.top.get(t, []):
        if k == "sound_area":
            c = re.search(r"coord=\((\d+), (\d+)", text)
            out.append(("area", num(text, "id"), num(text, "delay", 0), num(text, "range"),
                        R.local_of((int(c.group(1)), int(c.group(2)))) if c else None))
        elif k == "synth_sound":
            out.append(("synth", num(text, "id"), num(text, "delay", 0), None, None))
    return out


def section_sounds(R):
    """The sounds that play on the tick of each Muttadile event, by event."""
    tab = defaultdict(Counter)
    n_ev = Counter()
    for n in lives(R, {JUNIOR, SUBMERGED}):
        meal = meal_ticks(R, n)
        for t in sorted(n.ev):
            evs = []
            for k, d in n.ev[t]:
                if k == "seq":
                    nm = d.split()[0].replace("dohgadyle_", "")
                    if nm == "bite" and heals(n, t):
                        nm = "meal_bite"
                    if nm != "defend":
                        evs.append(nm)
            if kind_at(n, t) == SUBMERGED and any(p["name"] == "dragon_ranged_fire_attack" and from_npc(R, n, t, p) for p in projectiles(R, t)):
                evs.append("sweep")
            for e in evs:
                key = "%s %s" % (SHORT[kind_at(n, t)], e)
                n_ev[key] += 1
                sz = SIZE[kind_at(n, t)]
                sw = R.sw_of(n.tile_at(t), sz)
                for kind, sid, delay, rng, at in sounds_at(R, t):
                    where = "-"
                    if at is not None:
                        where = "on npc" if sw[0] - 1 <= at[0] <= sw[0] + sz and sw[1] - 1 <= at[1] <= sw[1] + sz else "off npc"
                    tab[key][(kind, sid, delay, rng, where)] += 1
    for key in sorted(n_ev):
        common = [(k, v) for k, v in tab[key].most_common() if v >= max(2, n_ev[key] // 2)]
        print("  %-14s n=%-3d %s" % (key, n_ev[key], "; ".join("%s %s delay %s range %s %s x%d" % (k + (v,)) for k, v in common) or "-"))


def section_attacks(R):
    """Every attack: style, its target and gap, the time since the last, and --
    when the target is the local raider -- the prayer and what landed."""
    for r in attack_rows(R):
        t = r["t"]
        mine = r["who"] == R.local_name
        hit = my_hits(R, t, t + 5) if mine else []
        print("  t%d %-5s %-5s at %-12s gap %s%s dt %s%s proj %s%s" % (
            t, r["kind"], r["seq"], r["who"], r["gap"], " edge" if r["edge"] else "",
            r["dt"] if r["dt"] else "-", (" pray " + pray_at(R, t)) if mine else "",
            ["%s %s-%s" % (p["name"], p["start"], p["end"]) for p in r["proj"]],
            (" hits %s" % ["+%d:%d" % (u - t, v) for u, i, v in hit][:3]) if mine else ""))


def chebyshev(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def section_projectiles(R):
    """Every Muttadile projectile's parameters against the distance from its
    source tile to its target's, and when the local raider's hit landed."""
    rows = defaultdict(list)
    for r in attack_rows(R):
        for p in r["proj"]:
            src = R.local_of((int(p["src"][0]), int(p["src"][1]))) if p["src"] else None
            dst = tile_of(R, p["to"], r["t"] - 1) if p["to"] else None
            d = chebyshev(src, dst) if src and dst else None
            land = None
            if p["to"] == R.local_name:
                h = [u for u, i, v in my_hits(R, r["t"], r["t"] + 6) if i != 6]
                land = (h[0] - r["t"]) if h else None
            rows[(r["kind"], p["name"], p["start"], p["angle"], p["progress"], p["h0"], p["h1"])].append(
                (d, p["end"], land))
    for key, xs in sorted(rows.items(), key=lambda kv: str(kv[0])):
        fits = Counter((e - 5 * d) for d, e, _ in xs if d is not None)
        print("  %-5s %-26s start %s angle %s progress %s height %s->%s: %d; end - 5d %s; (d, end, landed +) %s" % (
            key[0], key[1], key[2], key[3], key[4], key[5], key[6], len(xs), dict(fits),
            sorted(set(xs), key=lambda x: (x[0] is None, x[0] or 0, x[1], x[2] is None, x[2] or 0))[:12]))


def section_steps(R):
    """The erratic step: when it steps against its last attack (A+k), the
    ticks its face is cleared (the target let go) and whether it stepped on
    them, every single step (alone, not part of a walk) with its direction,
    the opening against the end of its first walk, and how likely the singles
    are under three draws: a dumb step toward a random tile within r of its
    first tile ("spawn") or of where it stands ("here"), or a random
    neighbour."""
    import math

    def axis_p(cur, centre, r, sign):
        xs = range(centre - r, centre + r + 1)
        return sum(1 for x in xs if (x > cur) - (x < cur) == sign) / len(xs)

    for n in lives(R, {JUNIOR, SUBMERGED}):
        meal = meal_ticks(R, n)
        atk = sorted(t for t, seq, m in attacks(R, n) if not m)
        steps = [t for t in sorted(n.ev) if kind_at(n, t) != SUBMERGED and any(k in ("walk", "run") for k, d in n.ev[t])]
        ts = set(steps)
        nulls = [t for t in sorted(n.ev) for k, d in n.ev[t] if k == "face_pathingentity" and "entity=null" in d]
        first = {}
        for t in sorted(n.tiles):
            k = kind_at(n, t)
            if k != SUBMERGED and k not in first:
                first[k] = R.sw_of(n.tile_at(t), SIZE[k])
        faces = [t for t in sorted(n.ev) for k, d in n.ev[t] if k == "face_pathingentity" and "player=" in d]
        if faces:
            walk = [t for t in steps if t < faces[0]]
            print("  %s: first faces a raider on %d; its last step before that %s" % (
                SHORT[n.types[-1][1]], faces[0], walk[-1] if walk else None))
        since_null, since_step, ticks_at = Counter(), Counter(), Counter()
        for t in nulls:
            prev = [u for u in atk if u < t]
            if prev and t not in meal:
                since_null[(t - prev[-1], t in ts)] += 1
        if atk:
            for t in range(atk[0] + 1, max(n.tiles) + 1):
                if t in meal or kind_at(n, t) == SUBMERGED:
                    continue
                k = min(t - [u for u in atk if u < t][-1], 9)
                ticks_at[k] += 1
                if t in ts:
                    since_step[k] += 1
        print("    face cleared, by A+k (stepped that tick?): %s" % dict(sorted(since_null.items())))
        print("    steps by A+k (9 = 9+): %s" % " ".join("%d:%d/%d" % (k, since_step[k], ticks_at[k]) for k in sorted(ticks_at)))
        lik = Counter()
        for t in steps:
            if not atk or t < atk[0] or t in meal or t - 1 in ts or t + 1 in ts:
                continue
            k = kind_at(n, t)
            b, a = R.sw_of(n.tile_at(t - 1), SIZE[k]), R.sw_of(n.tile_at(t), SIZE[k])
            d = (a[0] - b[0], a[1] - b[1])
            prev = [u for u in atk if u < t]
            print("    single t%d A+%s %s%s%s" % (t, t - prev[-1] if prev else None, d, " diagonal" if d[0] and d[1] else "",
                                             " face cleared" if t in nulls else ""))
            for r in (4, 8, 12):
                for name, c in (("spawn", first[k]), ("here", b)):
                    lik[(name, r)] += math.log(max(axis_p(b[0], c[0], r, d[0]) * axis_p(b[1], c[1], r, d[1]), 1e-6))
            lik[("neighbour", 1)] += math.log(1 / 8.0)
        if lik:
            print("    log-likelihood of the singles: %s" % ", ".join("%s r%d %.1f" % (k[0], k[1], v) for k, v in sorted(lik.items(), key=lambda kv: -kv[1])))


SECTIONS = {"meals": section_meals, "attacks": section_attacks, "projectiles": section_projectiles,
            "submerged": section_submerged, "prayer": section_prayer,
            "surfacing": section_surfacing, "moves": section_moves,
            "sounds": section_sounds, "steps": section_steps}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--section", action="append")
    a = ap.parse_args()
    for path in a.files:
        R = load(path)
        print("== %s: ticks %d..%d, turn %s, flip %s, max hp %d" % (R.path, R.first, R.last, R.turn, R.flip, R.max_hp))
        for s in a.section or list(SECTIONS):
            print("-- %s" % s)
            SECTIONS[s](R)


if __name__ == "__main__":
    main()
