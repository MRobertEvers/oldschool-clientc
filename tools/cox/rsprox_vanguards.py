#!/usr/bin/env python3
"""The Vanguards' room as Jagex's server sent it, read out of RSProx transcripts,
in TEMPLATE room-local tiles (tools/cox/rsprox_room.py: the turned-room fixes).

    python3 tools/cox/rsprox_vanguards.py ~/.rsprox/binary/archive/{3285,843,3389}.txt
    python3 tools/cox/rsprox_vanguards.py --section timeline FILES...

Sections: timeline, rise, attacks, shuffle, reset, trample, death.

Ticks print relative to W, the tick the dormant shells play `vanguard_spawn`.
A vanguard is 3x3; "sw" is its template room-local south-west tile, "me" the
local raider's room-local tile at the end of the tick. Heights are raw / 4.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_room import load_room, name_of, coord_of, num  # noqa: E402

DORMANT, WALKING, MELEE, RANGED, MAGIC = 7525, 7526, 7527, 7528, 7529
IDS = {DORMANT, WALKING, MELEE, RANGED, MAGIC}
SHORT = {"raids_vanguard_dormant": "dorm", "raids_vanguard_walking": "walk", "raids_vanguard_melee": "MEL",
         "raids_vanguard_ranged": "RNG", "raids_vanguard_magic": "MAG"}
SIZE = 3


def vanguards(R):
    return [n for n in R.lives if n.types and n.types[0][1] in IDS]


def wake_tick(R):
    for n in vanguards(R):
        for t in sorted(n.ev):
            for k, d in n.ev[t]:
                if k == "seq" and d.startswith("vanguard_spawn"):
                    return t
    return R.first


def label(R, n):
    """A vanguard's name: its first fighting form (styles are kept for life?)."""
    for _, _, nm in n.types:
        if nm in ("raids_vanguard_melee", "raids_vanguard_ranged", "raids_vanguard_magic"):
            return SHORT[nm]
    return "v" + n.index


def sw(R, n, t):
    p = n.tile_at(t) if hasattr(n, "tile_at") else None
    if p is None:
        ts = [k for k in n.tiles if k <= t]
        if not ts:
            return None
        p = n.tiles[max(ts)]
    return R.sw_of(p, SIZE)


def tile_at(n, t):
    ts = [k for k in n.tiles if k <= t]
    return n.tiles[max(ts)] if ts else None


def me_local(R, t):
    p = R.me_at(t)
    return R.local_of(p) if p else None


def top_rows(R, t):
    """The room's own packets on tick t, in local tiles."""
    out = []
    for kind, text, subs in R.top.get(t, []):
        if kind.startswith("map_projanim"):
            src = dst = None
            locked = False
            for sub in subs:
                if sub.startswith("[source]"):
                    src = coord_of(sub)
                elif sub.startswith("[target]"):
                    dst = coord_of(sub)
                    locked = "player=" in sub
            bits = [name_of(text)]
            if src:
                bits.append("from %s" % (R.local_of(src),))
            if dst:
                bits.append("to %s%s" % (R.local_of(dst), " (me)" if locked else ""))
            for key in ("starttime", "endtime", "progress", "angle", "startheight", "endheight"):
                v = num(text, key)
                if v is not None:
                    bits.append("%s=%d" % (key, v))
            out.append("proj " + " ".join(bits))
        elif kind.startswith("map_anim"):
            c = coord_of(text)
            out.append("map %s at %s d=%s h=%s" % (name_of(text), R.local_of(c) if c else None, num(text, "delay"), num(text, "height")))
        elif kind in ("sound_area", "synth_sound"):
            c = coord_of(text)
            out.append("%s %s%s d=%s r=%s" % ("area" if kind == "sound_area" else "synth", num(text, "id"),
                                             (" at %s" % (R.local_of(c),)) if c else "", num(text, "delay"), num(text, "range")))
        elif kind.startswith("loc_"):
            c = coord_of(text)
            out.append("%s %s at %s" % (kind, name_of(text), R.local_of(c) if c else None))
        elif kind.startswith("message"):
            m = re.search(r'message="([^"]*)"', text)
            out.append("msg %s" % (m.group(1)[:70] if m else text[:70]))
        elif kind == "hint_arrow":
            out.append("hint %s" % text[:60])
    return out


def section_timeline(R, W):
    vs = vanguards(R)
    names = {n.index: label(R, n) for n in vs}
    lo = W - 5
    hi = max([n.deleted or R.last for n in vs] + [R.last]) + 6
    print("   turn %s flip %s origin %s" % (R.turn, R.flip, R.origin))
    for n in vs:
        print("   %s idx %s first sw %s" % (names[n.index], n.index, sw(R, n, min(n.tiles))))
    for t in range(lo, hi + 1):
        bits = []
        for n in vs:
            for k, d in n.ev.get(t, []):
                if k in ("add", "idle", "walk", "run", "teleport"):
                    m = re.match(r"\((\d+), (\d+)\)->\((\d+), (\d+)\)", d)
                    if m and (m.group(1), m.group(2)) != (m.group(3), m.group(4)):
                        bits.append("%s %s->%s" % (names[n.index], R.sw_of((int(m.group(1)), int(m.group(2))), SIZE),
                                                   R.sw_of((int(m.group(3)), int(m.group(4))), SIZE)))
                    elif k == "add":
                        bits.append("%s add" % names[n.index])
                elif k == "retype":
                    bits.append("%s ->%s" % (names[n.index], SHORT.get(d, d)))
                elif k == "seq":
                    bits.append("%s seq %s" % (names[n.index], d))
                elif k == "hit":
                    bits.append("%s hit %s" % (names[n.index], d))
                elif k == "headbar":
                    bits.append("%s bar %s" % (names[n.index], d))
                elif k == "spotanim":
                    bits.append("%s spot %s" % (names[n.index], name_of(d)))
                elif k == "face":
                    pass
                elif k == "del":
                    bits.append("%s DEL" % names[n.index])
                else:
                    bits.append("%s %s %s" % (names[n.index], k, d[:50]))
        for k, d in R.my_ev.get(t, []):
            if k == "hit":
                bits.append("ME hit %s" % d)
            elif k in ("sequence", "spotanim"):
                bits.append("me %s %s" % (k[:3], d))
        bits += top_rows(R, t)
        if bits:
            print("   W%+d me %s | %s" % (t - W, me_local(R, t), " | ".join(bits)))


def projs(R, t):
    """map_projanim_v2 rows on tick t: (name, src local, dst local, locked, start, end, progress, h0, h1)."""
    out = []
    for kind, text, subs in R.top.get(t, []):
        if not kind.startswith("map_projanim"):
            continue
        src = dst = None
        locked = False
        for sub in subs:
            if sub.startswith("[source]"):
                src = coord_of(sub)
            elif sub.startswith("[target]"):
                dst = coord_of(sub)
                locked = "player=" in sub
        out.append((name_of(text), R.local_of(src) if src else None, R.local_of(dst) if dst else None, locked,
                    num(text, "starttime"), num(text, "endtime"), num(text, "progress"),
                    num(text, "startheight"), num(text, "endheight")))
    return out


def map_anims(R, t, name):
    out = []
    for kind, text, subs in R.top.get(t, []):
        if kind.startswith("map_anim") and name_of(text) == name:
            out.append((R.local_of(coord_of(text)), num(text, "delay")))
    return out


def sounds(R, t):
    out = []
    for kind, text, subs in R.top.get(t, []):
        if kind == "sound_area":
            c = coord_of(text)
            out.append((num(text, "id"), R.local_of(c) if c else None, num(text, "delay"), num(text, "range")))
    return out


def my_hits(R, t):
    return [d for k, d in R.my_ev.get(t, []) if k == "hit"]


def gap(a, n, x, z):
    """Chebyshev gap from the n-square at a to (x, z): 0 = on it."""
    dx = max(a[0] - x, 0, x - (a[0] + n - 1))
    dz = max(a[1] - z, 0, z - (a[1] + n - 1))
    return max(dx, dz)


IMPACT = {"raids_vanguard_range_0": "castlewars_catapult_splash", "raids_vanguard_range_1": "castlewars_catapult_splash",
          "raids_vanguard_range_2": "castlewars_catapult_splash", "raids_vanguard_magic": "wild_falloff_meteor_blast"}


def section_attacks(R, W):
    vs = vanguards(R)
    names = {n.index: label(R, n) for n in vs}
    swing_gaps = defaultdict(Counter)
    for n in vs:
        for t in sorted(n.ev):
            for k, d in n.ev[t]:
                if k != "seq" or not d.startswith("vanguard_attack_"):
                    continue
                style = d.split()[0].replace("vanguard_attack_", "")
                a = sw(R, n, t - 1)
                me0, me1 = me_local(R, t - 1), me_local(R, t)
                g = gap(a, SIZE, *me0) if me0 and a else None
                swing_gaps[style][g] += 1
                line = "   W%+d %s %s sw %s me@t-1 %s (gap %s)" % (t - W, names[n.index], style, a, me0, g)
                if style != "melee":
                    centre = (a[0] + 1, a[1] + 1)
                    mine = [p for p in projs(R, t) if p[1] == centre]
                    for p in mine:
                        rel = (p[2][0] - me0[0], p[2][1] - me0[1]) if me0 and p[2] else None
                        land = None
                        for dt in range(0, 6):
                            for tile, delay in map_anims(R, t + dt, IMPACT.get(p[0], "")):
                                if tile == p[2] and land is None:
                                    land = (dt, delay)
                        line += "\n        %s to %s%s rel-me %s start %s end %s prog %s h %s->%s land %s" % (
                            p[0], p[2], " LOCKED" if p[3] else "", rel, p[4], p[5], p[6], p[7], p[8], land)
                hits = ["T+%d %s" % (dt, h) for dt in range(0, 5) for h in my_hits(R, t + dt)]
                snd = [s for s in sounds(R, t)]
                line += "\n        my hits %s\n        sounds %s" % (hits, snd)
                print(line)
    for style, c in swing_gaps.items():
        print("   %s swings by my gap at t-1: %s" % (style, sorted(c.items(), key=lambda kv: (kv[0] is None, kv[0]))))


BASE = 180   # solo max hp (the headbar: a 62 takes it 30 -> 20)


def hp_track(R, n):
    """tick -> hp at the end of the tick, from its splats (16 damage, 6 heal), from BASE."""
    hp, out = BASE, {}
    for t in sorted(n.ev):
        for k, d in n.ev[t]:
            if k == "hit":
                typ, val = d.split()
                if typ == "6":
                    hp = min(BASE, hp + int(val))
                elif val not in ("None",) and typ not in ("12",):
                    hp -= int(val)
        out[t] = hp
    return out


def hp_at(track, t):
    best = BASE
    for k in sorted(track):
        if k <= t:
            best = track[k]
    return best


def events(n, prefix):
    return [t for t in sorted(n.ev) for k, d in n.ev[t] if k == "seq" and d.startswith(prefix)]


def section_cycles(R, W):
    vs = vanguards(R)
    names = {n.index: label(R, n) for n in vs}
    tracks = {n.index: hp_track(R, n) for n in vs}
    # every retype, by tick
    rt = defaultdict(list)
    for n in vs:
        for t, typ, nm in n.types[1:]:
            rt[t].append((names[n.index], SHORT.get(nm, nm)))
    for t in sorted(rt):
        print("   W%+d retype %s" % (t - W, rt[t]))
    # the shells: a tick where any fighting form turns walking
    shells = sorted({t for t, rows in rt.items() if any(f == "walk" for _, f in rows)})
    emerges = sorted({t for t, rows in rt.items() if any(f in ("MEL", "RNG", "MAG") for _, f in rows)})
    print("   emerge ticks %s" % [t - W for t in emerges])
    print("   shell ticks %s" % [t - W for t in shells])
    for s in shells:
        e = max([t for t in emerges if t < s], default=None)
        alive = [n for n in vs if not (n.deleted and n.deleted <= s) and not events(n, "vanguard_death") or
                 (events(n, "vanguard_death") and min(events(n, "vanguard_death")) > s)]
        hps = {names[n.index]: hp_at(tracks[n.index], s - 1) for n in alive}
        hps0 = {names[n.index]: hp_at(tracks[n.index], s - 2) for n in alive}
        heals = [(t - s, names[n.index], d) for n in vs for t in range(s, s + 15) for k, d in n.ev.get(t, [])
                 if k == "hit" and d.startswith("6 ")]
        g = max(hps.values()) - min(hps.values()) if hps else 0
        g0 = max(hps0.values()) - min(hps0.values()) if hps0 else 0
        arrive = None
        later = [t for t in emerges if t > s]
        print("   SHELL W%+d: active %s ticks (from W%+d); hp end of S-1 %s gap %d (%.0f%%), at S-2 gap %d; heals %s; next emerge W%s" % (
            s - W, (s - e) if e else None, (e - W) if e else 0, hps, g, 100.0 * g / BASE, g0, heals,
            ("%+d" % (later[0] - W)) if later else "-"))
    # the holes
    for n in vs:
        stops = []
        for t in emerges:
            p = sw(R, n, t)
            if p and (not stops or stops[-1][1] != p):
                stops.append((t - W, p))
        print("   %s emerges at %s" % (names[n.index], stops))


def section_wake(R, W):
    vs = vanguards(R)
    for t in range(W - 3, W + 1):
        me = me_local(R, t)
        print("   W%+d me %s gaps %s" % (t - W, me, [(label(R, n), gap(sw(R, n, t), SIZE, *me)) for n in vs if me]))
    for n in vs:
        print("   %s: spawn W%+d, transition seqs %s" % (label(R, n), min(events(n, "vanguard_spawn")) - W,
              [(t - W, d) for t in sorted(n.ev) for k, d in n.ev[t] if k == "seq" and "transition" in d][:2]))


def section_death(R, W):
    vs = vanguards(R)
    for n in vs:
        d = events(n, "vanguard_death")
        if not d:
            continue
        D = d[0]
        print("   %s death seq W%+d, del W%+d, sw %s" % (label(R, n), D - W, (n.deleted - W) if n.deleted else 0, sw(R, n, D)))
    last = max(events(n, "vanguard_death")[0] for n in vs if events(n, "vanguard_death"))
    for t in range(last, last + 12):
        rows = [r for r in top_rows(R, t) if "blockage_orange" not in r or "loc_anim" in r] + \
               ["obj %s" % sub for kind, text, subs in R.top.get(t, []) if kind.startswith("obj_add") for sub in [text[:80]]]
        rows += ["%s %s" % (k, d) for k, d in R.my_ev.get(t, []) if k == "spotanim"]
        if rows:
            print("   D%+d %s" % (t - last, " | ".join(rows)))


def moves(n):
    """tick -> (from, to) printed tiles where the npc moved."""
    out = {}
    for t in sorted(n.ev):
        for k, d in n.ev[t]:
            m = re.match(r"\((\d+), (\d+)\)->\((\d+), (\d+)\)", d)
            if m and (m.group(1), m.group(2)) != (m.group(3), m.group(4)):
                out[t] = ((int(m.group(1)), int(m.group(2))), (int(m.group(3)), int(m.group(4))))
    return out


def retypes(n):
    return [(t, SHORT.get(nm, nm)) for t, _, nm in n.types[1:]]


def section_shuffle(R, W):
    vs = vanguards(R)
    for n in vs:
        rts = retypes(n)
        mv = moves(n)
        for i, (t, f) in enumerate(rts):
            if f != "walk":
                continue
            nxt = next((tt for tt, ff in rts[i + 1:] if ff != "walk"), None)
            steps = sorted(tt for tt in mv if t <= tt <= (nxt or t + 30))
            heal = [(tt - t, d) for tt in range(t, (nxt or t + 30) + 1) for k, d in n.ev.get(tt, []) if k == "hit" and d.startswith("6 ")]
            path = [R.sw_of(mv[tt][1], SIZE) for tt in steps]
            print("   %s shell W%+d: steps S+%s, arrive S+%s at %s, emerge S+%s; heal %s; path %s" % (
                label(R, n), t - W, (steps[0] - t) if steps else None, (steps[-1] - t) if steps else None,
                path[-1] if path else None, (nxt - t) if nxt else None, heal, path))
    # trample: my hits while inside a walking vanguard's 3x3 (end of the tick before)
    for n in vs:
        rts = retypes(n)
        for t in sorted(R.my_ev):
            form = None
            for tt, f in rts:
                if tt <= t:
                    form = f
            if form != "walk":
                continue
            me0 = me_local(R, t - 1)
            a = sw(R, n, t - 1)
            if me0 and a and gap(a, SIZE, *me0) == 0:
                print("   TRAMPLE? W%+d %s at %s me %s hits %s" % (t - W, label(R, n), a, me0, my_hits(R, t)))


SECTIONS = {"timeline": section_timeline, "attacks": section_attacks, "cycles": section_cycles, "shuffle": section_shuffle,
            "wake": section_wake, "death": section_death}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--section", default="timeline")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    for f in a.files:
        R = load_room(f, IDS)
        W = wake_tick(R)
        print("== %s %s: W=%d (ticks %d..%d)" % (a.section, os.path.basename(f), W, R.first, R.last))
        SECTIONS[a.section](R, W)


if __name__ == "__main__":
    main()
