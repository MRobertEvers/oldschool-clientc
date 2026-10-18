#!/usr/bin/env python3
"""The Maiden of Sugadinti as Jagex's server sent it, read out of RSProx
transcripts: her attack cadence, the flight of her two projectiles against
distance, where the blood lands and how long a pool lies, the Nylocas
Matomenos (crabs) and the blood spawns through their lives, and her death.

    python3 tools/rsprox_maiden.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox_maiden.py --section blood FILES...

Parsing is tools/rsprox_nylocas.py's. Heights print in this tree's units: raw
/ 4 for the rev-234 v2 projectile packet.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_nylocas as N  # noqa: E402

ANCHOR = "8360-8367"
BOSS = re.compile(r"^tob_maiden_(100|70|50|30|dying_a|dying_b)")


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def proj_rows(f, tick, names):
    """[(name, start, end, angle, progress, sh, eh, source, target, at_player)]"""
    out = []
    for kind, text, subs in f.top.get(tick, []):
        if not kind.startswith("map_projanim"):
            continue
        g = N.named(text, "spotanim", names)
        src = tgt = None
        player = False
        for s in subs:
            if s.startswith("[source]"):
                src = N.coord_of(s)
            elif s.startswith("[target]"):
                tgt = N.coord_of(s)
                player = "player=" in s
        sh, eh = N.num(text, "startheight"), N.num(text, "endheight")
        if kind.endswith("v2"):
            sh, eh = sh / 4, eh / 4
        out.append((g, N.num(text, "starttime"), N.num(text, "endtime"), N.num(text, "angle"),
                    N.num(text, "progress"), sh, eh, src, tgt, player))
    return out


def bosses(f):
    return [n for n in f.npcs.values() if BOSS.match(n.types[0][1])]


def attacks(fights, names):
    gaps = Counter()
    first = Counter()
    kinds = Counter()
    shadow = Counter()
    shadow_flight = defaultdict(Counter)
    shadow_sound = Counter()
    blood = Counter()
    blood_flight = defaultdict(Counter)
    blood_count = Counter()
    blood_gfx = Counter()
    for f in fights:
        for b in bosses(f):
            last = None
            for t, k, d in b.events:
                if k != "seq" or "attack" not in d:
                    continue
                kind = "blood" if "blood" in d else "magic"
                kinds[(b.name_at(t), kind)] += 1
                if last is None:
                    first[t - (b.added or t)] += 1
                else:
                    gaps[t - last] += 1
                last = t
                rows = proj_rows(f, t, names)
                if kind == "magic":
                    for g, s, e, a, p, sh, eh, src, tgt, pl in rows:
                        if g != "maiden_shadow_proj":
                            continue
                        shadow[(s, a, p, sh, eh)] += 1
                        shadow_flight[cheb(src, tgt)][e - s] += 1
                        for kind2, text, _ in f.top.get(t, []):
                            if kind2 == "sound_area" and ("id=176" in text):
                                shadow_sound[("delay=end" if N.num(text, "delay") == e else "delay=%s end=%d" % (N.num(text, "delay"), e), N.coord_of(text) == src)] += 1
                else:
                    n = 0
                    ends = set()
                    for g, s, e, a, p, sh, eh, src, tgt, pl in rows:
                        if g != "maiden_blood_proj":
                            continue
                        n += 1
                        ends.add(e)
                        blood[(s, a, p, sh, eh)] += 1
                        blood_flight[s][(cheb(src, tgt), e - s)] += 1
                    blood_count[n] += 1
                    for kind2, text, _ in f.top.get(t, []):
                        if kind2 == "map_anim" and "lingering" in N.named(text, "spotanim", names):
                            blood_gfx["map_anim delay=end" if N.num(text, "delay") in ends else "other"] += 1
                        if kind2 == "sound_area" and "id=3547" in text:
                            blood_gfx["impact sound delay=end range=%s" % N.num(text, "range") if N.num(text, "delay") in ends else "impact sound other"] += 1
    print("== attacks")
    print("  per form and kind: %s" % dict(sorted(kinds.items())))
    print("  ticks between attacks: %s" % dict(sorted(gaps.items())))
    print("  first attack after her add: %s" % dict(sorted(first.items())))
    print("  magic projectile (start, angle, offset, heights): %s" % dict(shadow))
    print("  magic flight by distance from source: %s" % {d: dict(c) for d, c in sorted(shadow_flight.items())})
    print("  magic impact sound: %s" % dict(shadow_sound))
    print("  blood projectile (start, angle, offset, heights): %s" % dict(blood))
    print("  blood projectiles per attack: %s" % dict(sorted(blood_count.items())))
    for s in sorted(blood_flight):
        print("  blood flight, start %d, (distance, flight): %s" % (s, dict(sorted(blood_flight[s].items()))))
    print("  blood graphics and sounds: %s" % dict(blood_gfx))


def blood(fights, names):
    """Where a blood projectile lands, against the players and against each
    other, and the pool loc: when it appears, which rotation, how long it lies."""
    appear = Counter()
    life = Counter()
    rot = Counter()
    targets = Counter()
    inv = Counter()
    for f in fights:
        adds = {}
        for t in sorted(f.top):
            for kind, text, subs in f.top[t]:
                if kind.startswith("loc_add"):
                    name = N.named(text, "loc", names) or ""
                    c = N.coord_of(text)
                    if "maiden_blood" in name:
                        adds[c] = t
                        m = re.search(r"rotation=(\d)", text)
                        rot[m.group(1) if m else "?"] += 1
                    elif "invisible_type8" in name:
                        inv[("invisible loc on a pool tile" if c in adds else "invisible loc elsewhere")] += 1
                elif kind.startswith("loc_del"):
                    c = N.coord_of(text)
                    if c in adds:
                        life[t - adds.pop(c)] += 1
        for b in bosses(f):
            for t, k, d in b.events:
                if k != "seq" or "blood" not in d:
                    continue
                rows = [r for r in proj_rows(f, t, names) if r[0] == "maiden_blood_proj"]
                for r in rows:
                    tgt, end = r[8], r[2]
                    land = t + (end + 29) // 30
                    seen = None
                    for dt in range(0, 12):
                        for kind, text, _ in f.top.get(t + dt, []):
                            if kind.startswith("loc_add") and N.coord_of(text) == tgt and "maiden_blood" in (N.named(text, "loc", names) or ""):
                                seen = dt
                                break
                        if seen is not None:
                            break
                    appear[("pool at +%s" % seen, "landing +%d" % (land - t))] += 1
                # the start-20 projectile: is it on a player's tile?
                for r in rows:
                    targets[(r[1], "on a player" if any(r[8] == pc for pc in player_tiles(f, t)) else "not on a player")] += 1
    print("== blood")
    print("  pool appears after the attack (pool tick, landing tick): %s" % dict(sorted(appear.items(), key=str)))
    print("  pool rotation: %s" % dict(sorted(rot.items())))
    print("  pool lifetime (ticks): %s" % dict(sorted(life.items())[:20]))
    print("  invisible locs: %s" % dict(inv))
    print("  blood target by projectile start: %s" % dict(sorted(targets.items(), key=str)))


_PLAYER_CACHE = {}


def player_tiles(f, tick):
    """Every player's tile at the START of `tick` (the end of tick - 1)."""
    key = (f.path, f.span)
    if key not in _PLAYER_CACHE:
        _PLAYER_CACHE[key] = defaultdict(set)
        path = os.path.join(os.path.expanduser("~/.rsprox/binary/archive"), f.path)
        t = 0
        with open(path, encoding="utf-8", errors="replace") as h:
            for line in h:
                m = N.TICK.match(line.strip())
                if m and line.startswith("["):
                    t = int(m.group(1))
                    continue
                if t < f.span[0] - 2 or t > f.span[1] + 2:
                    continue
                m = N.PLAYER.search(line)
                if m and N.SUB8.match(line):
                    nc = N.NEWCOORD.search(line)
                    c = (int(nc.group(1)), int(nc.group(2))) if nc else (int(m.group(3)), int(m.group(4)))
                    _PLAYER_CACHE[key][t].add(c)
    return _PLAYER_CACHE[key].get(tick - 1, set())


def crabs(fights, names):
    """Nylocas Matomenos: when they come (against her form changes), what they
    are sent through life, how they end."""
    spawn_vs_form = Counter()
    per_wave = Counter()
    seqs = Counter()
    ends = Counter()
    for f in fights:
        forms = []
        for b in bosses(f):
            for t, k, d in b.events:
                if k == "retype":
                    forms.append((t, d))
        waves = defaultdict(int)
        for n in f.npcs.values():
            if n.types[0][1] != "maiden_elemental" or n.added is None:
                continue
            near = min(forms, key=lambda x: abs(x[0] - n.added)) if forms else None
            if near:
                spawn_vs_form[(near[1], n.added - near[0])] += 1
                waves[near] += 1
            for t, k, d in n.events:
                if k == "seq":
                    seqs[("%+d" % (t - n.added), d)] += 1
            if n.deleted is not None:
                death = [t for t, k, d in n.events if k == "seq" and "death" in d]
                if death:
                    ends[("death seq at hp0%+d" % (death[0] - n.zero_hit) if n.zero_hit else "death seq, no hp0", "delete +%d" % (n.deleted - death[0]))] += 1
                else:
                    ends[("no death seq", "deleted %d after add" % (n.deleted - n.added))] += 1
        for k, v in waves.items():
            per_wave[(k[1], v)] += 1
    print("== crabs (maiden_elemental)")
    print("  added against her form change (form, ticks): %s" % dict(sorted(spawn_vs_form.items())))
    print("  crabs per form change: %s" % dict(sorted(per_wave.items())))
    print("  seqs (tick from add): %s" % dict(seqs.most_common(10)))
    print("  ends: %s" % dict(ends.most_common(10)))


def slugs(fights, names):
    adds = Counter()
    ends = Counter()
    for f in fights:
        battacks = []
        for b in bosses(f):
            battacks += [t for t, k, d in b.events if k == "seq" and "blood" in d]
        for n in f.npcs.values():
            if n.types[0][1] != "maiden_blood_slug" or n.added is None:
                continue
            prior = [t for t in battacks if t <= n.added]
            adds["%+d after a blood attack" % (n.added - prior[-1]) if prior else "no blood attack before"] += 1
            death = [t for t, k, d in n.events if k == "seq" and "death" in d]
            snd = [t for t in range(n.deleted or 0, (n.deleted or 0) + 1) for kk, tx, _ in f.top.get(t, []) if kk == "sound_area" and "id=785" in tx]
            if n.deleted is not None:
                ends[("death seq at hp0%+d" % (death[0] - n.zero_hit) if death and n.zero_hit else "no death seq",
                      "delete %+d" % (n.deleted - death[0]) if death else "", "slime_death on delete" if snd else "")] += 1
    print("== blood spawns (maiden_blood_slug)")
    print("  added: %s" % dict(adds.most_common(8)))
    print("  ends: %s" % dict(ends.most_common(8)))


def death(fights, names):
    for f in fights:
        bs = bosses(f)
        if not bs:
            continue
        b = bs[0]
        z = b.zero_hit
        if z is None:
            continue
        out = []
        for t, k, d in b.events:
            if t >= z - 1 and k in ("seq", "retype", "del", "enabled_ops"):
                out.append("%+d %s %s" % (t - z, k, d))
        for t in range(z - 1, z + 20):
            for kind, text, _ in f.top.get(t, []):
                if kind.startswith("loc_add") and "remains" in (N.named(text, "loc", names) or ""):
                    out.append("%+d loc_add remains" % (t - z))
                if kind == "message_game" and "complete!" in text:
                    out.append("%+d complete" % (t - z))
                if kind in ("midi_song_v2", "midi_jingle"):
                    out.append("%+d %s %s" % (t - z, kind, re.sub(r", fade.*", "", text)))
                if kind.startswith("obj_add"):
                    out.append("%+d %s" % (t - z, text[:40]))
        print("  %s %d: %s" % (f.path, z, " | ".join(out)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append", choices=["attacks", "blood", "crabs", "slugs", "death"])
    ap.add_argument("--anchor", default=ANCHOR)
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    names = N.load_names()
    fights = N.load(args.transcripts, names, args.anchor)
    for s in args.section or ["attacks", "blood", "crabs", "slugs", "death"]:
        if s == "death":
            print("== death (T = her killing hit)")
        globals()[s](fights, names)


if __name__ == "__main__":
    main()
