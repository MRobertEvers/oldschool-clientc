#!/usr/bin/env python3
"""The Pestilent Bloat as Jagex's server sent it, read out of RSProx
transcripts: her sleep cycle and the stomp that ends it, the falling flesh
(waves, counts, where it lands against the players, flesh to splat), the
flies, the stun, and her death.

    python3 tools/rsprox_bloat.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox_bloat.py --section flesh FILES...

Parsing is tools/rsprox_nylocas.py's; player tiles are tools/rsprox_maiden.py's.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_nylocas as N  # noqa: E402
import rsprox_maiden as M  # noqa: E402

ANCHOR = "8359"


def boss(f):
    for n in f.npcs.values():
        if n.types[0][1].startswith("tob_bloat"):
            return n
    return None


def tops(f, kind_prefix, pattern=None):
    """[(tick, kind, text, subs)] of the top-level events of a kind."""
    out = []
    for t in sorted(f.top):
        for kind, text, subs in f.top[t]:
            if kind.startswith(kind_prefix) and (pattern is None or re.search(pattern, text)):
                out.append((t, kind, text, subs))
    return out


def cycle(fights, names):
    """Her sleep: the seq, the stomp that ends it, the shake and the reset."""
    sleep_to_stomp = Counter()
    awake = Counter()
    first = Counter()
    shake = Counter()
    reset = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        sleeps = [t for t, k, d in b.events if k == "seq" and "sleep" in d]
        stomps = [t for t, k, text, _ in tops(f, "sound_area", r"id=(3752|dragonslayer_dragonstomp3)\b")]
        starts = [t for t, k, text, _ in tops(f, "midi_song", r"id=571\b")]
        if starts and sleeps:
            first[sleeps[0] - starts[0]] += 1
        for s in sleeps:
            nxt = [t for t in stomps if t >= s]
            if nxt:
                sleep_to_stomp[nxt[0] - s] += 1
        for st in stomps:
            nxt = [t for t in sleeps if t > st]
            if nxt:
                awake[nxt[0] - st] += 1
            shakes = [text for t, k, text, _ in tops(f, "cam_shake") if t == st]
            shake[len(shakes)] += 1
            r = [t for t, k, text, _ in tops(f, "cam_reset") if t >= st]
            if r:
                reset[r[0] - st] += 1
    print("== cycle")
    print("  first sleep after the fight track starts: %s" % dict(sorted(first.items())))
    print("  sleep seq -> stomp: %s" % dict(sorted(sleep_to_stomp.items())))
    print("  stomp -> next sleep seq (awake): %s" % dict(sorted(awake.items())))
    print("  cam_shake packets per stomp: %s" % dict(sorted(shake.items())))
    print("  stomp -> cam_reset: %s" % dict(sorted(reset.items())))


def flesh(fights, names):
    """The falling flesh: waves, how many, where, and the splat after."""
    gaps = Counter()
    per_wave = Counter()
    to_splat = Counter()
    on_player = Counter()
    splat_sound = Counter()
    wave_vs_stomp = Counter()
    for f in fights:
        falls = tops(f, "map_anim", r"falling_flesh|id=(158[0-9]|159[0-9])\b")
        falls = [x for x in falls if "falling_flesh" in (N.named(x[2], "spotanim", names) or "")]
        splats = [x for x in tops(f, "map_anim") if "blood_splat" in (N.named(x[2], "spotanim", names) or "")]
        stomps = [t for t, k, text, _ in tops(f, "sound_area", r"id=(3752|dragonslayer_dragonstomp3)\b")]
        waves = sorted(set(t for t, _, _, _ in falls))
        for a, b in zip(waves, waves[1:]):
            gaps[b - a] += 1
        for w in waves:
            tiles = [N.coord_of(text) for t, _, text, _ in falls if t == w]
            per_wave[len(tiles)] += 1
            ps = M.player_tiles(f, w)
            for c in tiles:
                on_player["on a player" if c in ps else "elsewhere"] += 1
            for c in tiles:
                s = [t for t, _, text, _ in splats if N.coord_of(text) == c and t >= w]
                if s:
                    to_splat[s[0] - w] += 1
            st = [t for t in stomps if t <= w]
            if st:
                wave_vs_stomp[w - st[-1]] += 1
        for t, _, text, _ in splats:
            c = N.coord_of(text)
            snd = [x for x in f.top.get(t, []) if x[0] == "sound_area" and N.coord_of(x[1]) == c and ("3308" in x[1] or "barbassault" in x[1])]
            splat_sound["area sound r%s" % N.num(snd[0][1], "range") if snd else "none"] += 1
    print("== flesh")
    print("  ticks between waves: %s" % dict(sorted(gaps.items())[:20]))
    print("  flesh per wave: %s" % dict(sorted(per_wave.items())))
    print("  wave tick - last stomp: %s" % dict(sorted(wave_vs_stomp.items())[:20]))
    print("  flesh tile vs players (start of tick): %s" % dict(on_player))
    print("  flesh -> splat on its tile: %s" % dict(sorted(to_splat.items())))
    print("  splat sound: %s" % dict(splat_sound))


def flies(fights, names):
    """Fly projectiles: which, when (her state), the flight, the sound."""
    rows = Counter()
    flight = Counter()
    snd = Counter()
    state = Counter()
    for f in fights:
        b = boss(f)
        sleeps = [t for t, k, d in b.events if k == "seq" and "sleep" in d] if b else []
        for t, kind, text, subs in tops(f, "map_projanim"):
            g = N.named(text, "spotanim", names) or ""
            if "flies" not in g:
                continue
            src = tgt = None
            for s in subs:
                if s.startswith("[source]"):
                    src = N.coord_of(s)
                elif s.startswith("[target]"):
                    tgt = N.coord_of(s)
            rows[(g, N.num(text, "starttime"), N.num(text, "angle"), N.num(text, "progress"))] += 1
            if src and tgt:
                flight[(g, M.cheb(src, tgt), N.num(text, "endtime"))] += 1
            sounds = [N.named(x[1], "sound", names) for x in f.top.get(t, []) if x[0] == "sound_area" and "flies" in (N.named(x[1], "sound", names) or "")]
            snd[(len(sounds), "at the target" if any(N.coord_of(x[1]) == tgt for x in f.top.get(t, []) if x[0] == "sound_area") else "elsewhere")] += 1
            asleep = any(s <= t < s + 30 for s in sleeps)
            state["asleep" if asleep else "awake"] += 1
    print("== flies")
    print("  projectiles (gfx, start, angle, offset): %s" % dict(rows))
    print("  (gfx, distance, end): %s" % dict(sorted(flight.items(), key=str)[:30]))
    print("  fly sounds per projectile, where: %s" % dict(snd))
    print("  her state: %s" % dict(state))


def stun(fights, names):
    out = Counter()
    for f in fights:
        for t, kind, text, _ in tops(f, "message_game", r"You've been stunned"):
            splats = [x for x in f.top.get(t, []) if x[0] == "map_anim" and "blood_splat" in (N.named(x[1], "spotanim", names) or "")]
            near = []
            for dt in range(-4, 1):
                for x in f.top.get(t + dt, []):
                    if x[0] == "map_anim" and "blood_splat" in (N.named(x[1], "spotanim", names) or ""):
                        near.append(dt)
            out[("splat same tick" if splats else "no splat same tick", "nearest splat %s" % (max(near) if near else None))] += 1
    print("== stun: %s" % dict(out))


def death(fights, names):
    for f in fights:
        b = boss(f)
        if not b or b.zero_hit is None:
            continue
        z = b.zero_hit
        out = []
        for t, k, d in b.events:
            if t >= z - 1 and k in ("seq", "retype", "del", "enabled_ops"):
                out.append("%+d %s %s" % (t - z, k, d))
        for t in range(z - 1, z + 12):
            for kind, text, _ in f.top.get(t, []):
                if kind == "message_game" and ("complete!" in text or "Vampyres" in text):
                    out.append("%+d %s" % (t - z, "complete" if "complete" in text else "vampyres"))
                if kind in ("midi_song_v2", "midi_jingle"):
                    out.append("%+d %s %s" % (t - z, kind, re.sub(r", fade.*|, length.*", "", text)))
                if kind.startswith("obj_add"):
                    out.append("%+d %s" % (t - z, text[:46]))
        print("  %s %d: %s" % (f.path, z, " | ".join(out)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append", choices=["cycle", "flesh", "flies", "stun", "death"])
    ap.add_argument("--anchor", default=ANCHOR)
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    names = N.load_names()
    fights = N.load(args.transcripts, names, args.anchor)
    for s in args.section or ["cycle", "flesh", "flies", "stun", "death"]:
        if s == "death":
            print("== death (T = her killing hit)")
        globals()[s](fights, names)


if __name__ == "__main__":
    main()
