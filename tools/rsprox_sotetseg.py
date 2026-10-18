#!/usr/bin/env python3
"""Sotetseg as Jagex's server sent it, read out of RSProx transcripts: his
attack clock and choice, the ball he throws and the projectiles it bounces
into, the death ball, the maze and the shadow realm, and his death.

    python3 tools/rsprox_sotetseg.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox_sotetseg.py --section bounce FILES...

Parsing is tools/rsprox_nylocas.py's; projectile rows and player tiles are
tools/rsprox_maiden.py's. Heights print in this tree's units: raw / 4 for the
rev-234 v2 projectile packet.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rsprox_nylocas as N  # noqa: E402
import rsprox_maiden as M  # noqa: E402

ANCHOR = "8387-8389"


class Boss:
    """Every life of Sotetseg in a fight as one timeline. A runner who leaves
    for the shadow realm loses sight of him, so the recording deletes him and
    adds him again when they come back: several npc records, one boss."""

    def __init__(self, lives):
        self.lives = sorted(lives, key=lambda n: n.added or 0)
        self.events = sorted((e for n in self.lives for e in n.events), key=lambda e: e[0])
        self.types = sorted((x for n in self.lives for x in n.types), key=lambda x: x[0])
        self.coords = {}
        for n in self.lives:
            self.coords.update(n.coords)
        self.added = self.lives[0].added
        self.zero_hit = min((n.zero_hit for n in self.lives if n.zero_hit is not None), default=None)


def boss(f):
    lives = [n for n in list(f.npcs.values()) + f.retired if re.match(r"tob_sotetseg_(non)?combat$", n.types[0][1])]
    return Boss(lives) if lives else None


def sounds_at(f, t, names, kind="sound_area"):
    """[(name, coord, delay, range)] of the sounds of a kind on a tick."""
    out = []
    for k, text, _ in f.top.get(t, []):
        if k == kind:
            out.append((N.named(text, "sound", names) or text[:20], N.coord_of(text),
                        N.num(text, "delay", 0), N.num(text, "range")))
    return out


_PLAYERS = {}


class Players:
    """Every raider's tile, carried forward by name, and what they were sent
    (hits, graphics, seqs) - the parts of player_info the shared parser skips."""

    def __init__(self, f):
        self.moves = defaultdict(list)      # name -> [(tick, coord at the end of it)]
        self.events = defaultdict(list)     # tick -> [(name, kind, text)]
        path = os.path.join(os.path.expanduser("~/.rsprox/binary/archive"), f.path)
        t = 0
        who = None
        with open(path, encoding="utf-8", errors="replace") as h:
            for line in h:
                m = N.TICK.match(line.strip())
                if m and line.startswith("["):
                    t = int(m.group(1))
                    who = None
                    continue
                if t < f.span[0] - 40 or t > f.span[1] + 40:
                    continue
                if N.TOP.match(line):
                    who = None
                    continue
                m = N.PLAYER.search(line)
                if m and N.SUB8.match(line):
                    who = m.group(2)
                    nc = N.NEWCOORD.search(line)
                    c = (int(nc.group(1)), int(nc.group(2))) if nc else (int(m.group(3)), int(m.group(4)))
                    self.moves[who].append((t, c))
                    continue
                m = re.match(r"^\s{12}\[(\w+)\] ?(.*)$", line)
                if m and who:
                    self.events[t].append((who, m.group(1), m.group(2)))

    def at(self, name, tick):
        """Where `name` stood at the START of `tick`."""
        last = None
        for t, c in self.moves.get(name, []):
            if t >= tick:
                break
            last = c
        return last

    def tiles(self, tick):
        return {n: self.at(n, tick) for n in self.moves if self.at(n, tick)}

    def got(self, name, tick, kind):
        return [text for n, k, text in self.events.get(tick, []) if n == name and k == kind]


def players(f):
    key = (f.path, f.span)
    if key not in _PLAYERS:
        _PLAYERS[key] = Players(f)
    return _PLAYERS[key]


def target_name(f, t, tg):
    for k, text, subs in f.top.get(t, []):
        if k.startswith("map_projanim"):
            for s in subs:
                if s.startswith("[target]") and N.coord_of(s) == tg:
                    m = re.search(r"name=([^,]*)", s)
                    if m:
                        return m.group(1)
    return None


def projs(f, t, names, pattern):
    return [p for p in M.proj_rows(f, t, names) if p[0] and re.search(pattern, p[0])]


def centre(b, t):
    sw = b.coords.get(t) or b.coords.get(t - 1)
    if not sw:
        last = [c for tt, c in sorted(b.coords.items()) if tt <= t]
        sw = last[-1] if last else None
    return (sw[0] + 2, sw[1] + 2) if sw else None


def attacks(fights, names):
    """His clock, what he chooses, and from where."""
    gaps = Counter()
    kinds = Counter()
    first = Counter()
    melee_range = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        last = None
        combat_at = [t for t, name in b.types if "combat" in name and "noncombat" not in name]
        for t, k, d in b.events:
            if k != "seq" or "attack" not in d:
                continue
            ball = bool(projs(f, t, names, "sharedattack"))
            kind = "melee" if "melee" in d else ("death ball" if ball else "ball")
            kinds[kind] += 1
            if last is not None and t - last < 40:
                gaps[(last_kind, t - last)] += 1
            prior = [c for c in combat_at if c <= t]
            if prior and (last is None or last < prior[-1]):
                first[t - prior[-1]] += 1
            if kind == "melee":
                c = centre(b, t)
                ps = M.player_tiles(f, t)
                melee_range[min((max(abs(p[0] - c[0]), abs(p[1] - c[1])) - 2 for p in ps), default=None) if c else None] += 1
            last, last_kind = t, kind
    print("== attacks")
    print("  kinds: %s" % dict(kinds))
    print("  (previous attack, ticks to the next): %s" % dict(sorted(gaps.items(), key=str)))
    print("  first attack after he turns to combat: %s" % dict(sorted(first.items())))
    print("  melee: nearest raider's distance from his footprint: %s" % dict(melee_range))


def primary(fights, names):
    """The ball he throws: parameters, flight, the sounds, the landing."""
    rows = Counter()
    flight = defaultdict(Counter)
    src = Counter()
    snd = Counter()
    land = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        for t, k, d in b.events:
            if k != "seq" or "attack_ranged" not in d:
                continue
            ps = projs(f, t, names, "maging|ranging")
            for g, st, en, ang, prog, sh, eh, s, tg, pl in ps:
                if st == 15:
                    continue
                rows[(g, st, ang, prog, sh, eh)] += 1
                c = centre(b, t)
                src["his centre" if s == c else "sw" if c and s == (c[0] - 2, c[1] - 2) else "other %s" % (None if not c else (s[0] - c[0], s[1] - c[1]))] += 1
                if c:
                    flight[M.cheb(c, tg)][en] += 1
                lt = t + en // 30
                at = [x for x in sounds_at(f, lt, names) if x[1] == tg]
                land[("landing tick = throw + end//30", "sound " + (at[0][0] + " r%s d%s" % (at[0][3], at[0][2]) if at else "none at the target"))] += 1
            for name, coord, delay, rng in sounds_at(f, t, names):
                c = centre(b, t)
                where = "sw" if c and coord == (c[0] - 2, c[1] - 2) else "centre" if coord == c else "other"
                snd[(name, where, delay, rng)] += 1
    print("== primary (the ball at one raider)")
    print("  (gfx, start, angle, offset, height): %s" % dict(rows))
    print("  source: %s" % dict(src))
    for d in sorted(flight):
        print("  distance %2d from his centre: end %s" % (d, dict(sorted(flight[d].items()))))
    print("  sounds on the throw tick (name, where, delay, range): %s" % dict(snd))
    print("  landing: %s" % dict(land))


def bounce(fights, names):
    """What a landed ball bounces into."""
    per = Counter()
    rows = Counter()
    flight = defaultdict(Counter)
    who = Counter()
    snd = Counter()
    lag = Counter()
    pairs = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        for t, k, d in b.events:
            if k != "seq" or "attack_ranged" not in d:
                continue
            for g, st, en, ang, prog, sh, eh, s, tg, pl in projs(f, t, names, "maging|ranging"):
                if st == 15:
                    continue
                lt = t + en // 30
                found = []
                for dt in (0, 1):
                    for p in projs(f, lt + dt, names, "maging|ranging"):
                        if p[1] == 15 and p[7] == tg:
                            found.append((dt, p))
                ps = M.player_tiles(f, lt)
                others = [p for p in ps if p != tg]
                per[(len(ps), len(found))] += 1
                pairs[tuple(sorted(p[0] for _, p in found))] += 1
                for dt, (g2, st2, en2, ang2, prog2, sh2, eh2, s2, tg2, pl2) in found:
                    lag[dt] += 1
                    rows[(g2, st2, ang2, prog2, sh2, eh2)] += 1
                    flight[M.cheb(s2, tg2)][en2] += 1
                    if others:
                        near = min(M.cheb(tg, o) for o in others)
                        who["nearest other raider" if M.cheb(tg, tg2) == near else "not the nearest (%d vs %d)" % (M.cheb(tg, tg2), near)] += 1
                    at = [x for x in sounds_at(f, lt + dt, names) if x[1] == tg2]
                    snd[(g2, tuple((x[0], x[2] - en2, x[3]) for x in at))] += 1
    print("== bounce (start 15, from the ball's landing tile)")
    print("  (raiders on the landing tick, bounces): %s" % dict(sorted(per.items())))
    print("  bounce gfx per landing: %s" % dict(pairs))
    print("  ticks after the landing: %s" % dict(lag))
    print("  (gfx, start, angle, offset, height): %s" % dict(rows))
    for d in sorted(flight):
        print("  distance %2d: end %s" % (d, dict(sorted(flight[d].items()))))
    print("  target: %s" % dict(who))
    print("  sounds at its target, sent with it (name, delay - end, range): %s" % dict(snd))


NOISE = re.compile(r"ii_dragon_arrow|rune_arrow|longbow|^id=(2700|4019)\b|date_seconds|fossil|tracking_damage|human_hit|blade2|leather_hit|"
                   r"plaintile|darktile|lighttile|runclientscript|update_stat|league|khopesh|vfx_league|id=99\d\d\b|"
                   r"^(npc_info|player_info)|id=(426|1116|24)\b|dragon_arrow|human_bow|human_eat|if_button|opnpc|opplayer|"
                   r"varbit id=(4101|4116|4117|4118|5464|12392|6448)\b|liquid|^synth_sound (eat|cancel_prayer|protect_|sharp_eye|equip)|"
                   r"drink|Bottomless|You eat|It heals|refreshed|corrupted shark|dose|move_|update_inv|scythe|halberd|^message_public|"
                   r"^varbit id=(357|4141)\b|^varp")


def signature(kind, text, names):
    if kind.startswith("map_projanim"):
        g = N.named(text, "spotanim", names)
        return "proj %s start=%s end=%s" % (g, N.num(text, "starttime"), N.num(text, "endtime"))
    if kind in ("midi_song_v2", "midi_jingle"):
        return "%s %s" % (kind, re.sub(r", length.*", "", text))
    if kind in ("sound_area", "synth_sound"):
        return "%s %s%s" % (kind, N.named(text, "sound", names) or re.sub(r",.*", "", text),
                            "" if kind != "sound_area" else " d%s r%s" % (N.num(text, "delay", 0), N.num(text, "range")))
    if kind.startswith("loc_add"):
        m = re.search(r"id=(\w+)", text)
        return "%s %s" % (kind, m.group(1) if m else text[:30])
    if kind == "message_game":
        return "message " + re.sub(r"Duration.*", "", text)[:90]
    if kind in ("varbit", "varp_small", "varp_large"):
        return "%s %s" % (kind, re.sub(r", old=-?\d+", "", text)[:60])
    return "%s %s" % (kind, text[:70])


def moment(fights, names, trigger, span=(-1, 5), npc_kinds=("seq", "retype", "enabled_ops", "level_change", "del", "add", "face_angle")):
    """Aggregate what is sent at each offset from every trigger tick:
    offset -> signature -> how many of the triggers had it."""
    table = defaultdict(Counter)
    n = 0
    for f in fights:
        b = boss(f)
        if not b:
            continue
        P = players(f)
        for t0 in trigger(f, b):
            n += 1
            for dt in range(span[0], span[1] + 1):
                t = t0 + dt
                seen = set()
                for kind, text, _ in f.top.get(t, []):
                    sig = signature(kind, text, names)
                    if not NOISE.search(sig) and not NOISE.search(text):
                        seen.add(sig)
                for tt, k, d in b.events:
                    if tt == t and k in npc_kinds:
                        seen.add("boss %s %s" % (k, d[:60]))
                for name, k, text in P.events.get(t, []):
                    if k in ("sequence", "spotanim") and not NOISE.search(text):
                        seen.add("player %s %s" % (k, re.sub(r"\(\d+\)", "", text)[:60]))
                for sig in seen:
                    table[dt][sig] += 1
    print("  (%d occurrences)" % n)
    for dt in sorted(table):
        for sig, c in sorted(table[dt].items(), key=lambda x: -x[1]):
            if c * 4 >= n or c >= 3:
                print("  %+d %3d  %s" % (dt, c, sig))


def maze(fights, names):
    """The maze's start (he turns non-combat) and its end (combat again)."""
    print("== maze start (T = he turns non-combat)")
    moment(fights, names, lambda f, b: [t for t, k, d in b.events if k == "retype" and "noncombat" in d], (-1, 5))
    print("== maze end (T = he turns combat again, after a maze)")
    moment(fights, names, lambda f, b: maze_ends(b), (-2, 8))


def combat_turns(b):
    return [t for t, k, d in b.events if k == "retype" and "noncombat" not in d and "combat" in d]


def maze_ends(b):
    """Every turn to combat that follows a turn to non-combat."""
    downs = [t for t, k, d in b.events if k == "retype" and "noncombat" in d]
    return [t for t in combat_turns(b) if any(d < t for d in downs)]


def start(fights, names):
    """The fight's start: his first turn to combat."""
    print("== fight start (T = his first turn to combat)")
    def first(f, b):
        downs = [t for t, k, d in b.events if k == "retype" and "noncombat" in d]
        return [t for t in combat_turns(b) if not any(d < t for d in downs)][:1]
    moment(fights, names, first, (-3, 8))


def realm(fights, names):
    """The runner in the shadow realm: the drain, its hits, the way out."""
    drain = Counter()
    hits = Counter()
    first = Counter()
    for f in fights:
        b = boss(f)
        if not b:
            continue
        P = players(f)
        downs = [t for t, k, d in b.events if k == "retype" and "noncombat" in d]
        for t0 in downs:
            ends = [t for t in maze_ends(b) if t > t0]
            t1 = ends[0] if ends else t0 + 60
            for name in P.moves:
                ds = [t for t in range(t0, t1) if any("1608" in x or "drain" in x for x in P.got(name, t, "spotanim"))]
                if not ds:
                    continue
                first[ds[0] - t0] += 1
                for a, c in zip(ds, ds[1:]):
                    drain[c - a] += 1
                for i, d in enumerate(ds):
                    hs = [(dt, N.num(h, "value")) for dt in range(0, 4) for h in P.got(name, d + dt, "hit")]
                    hits[(i + 1, tuple(hs[:1]))] += 1
    print("== realm (the runner)")
    print("  first drain graphic after the maze starts: %s" % dict(sorted(first.items())))
    print("  ticks between drains: %s" % dict(sorted(drain.items())))
    print("  (drain number, (ticks to its hit, value)): %s" % dict(sorted(hits.items())))


def deathball(fights, names):
    print("== death ball (T = the throw)")
    moment(fights, names, lambda f, b: sorted(set(t for t in f.top for p in projs(f, t, names, "sharedattack"))), (0, 17))


def death(fights, names):
    print("== death (T = his death seq)")
    moment(fights, names, lambda f, b: [t for t, k, d in b.events if k == "seq" and "death" in d], (-1, 6))


def wrongtile(fights, names):
    """A wrong step on the maze: the tile, the explosion, the zap, the hit."""
    sig = Counter()
    hit = Counter()
    life = Counter()
    for f in fights:
        P = players(f)
        for t in sorted(f.top):
            for kind, text, subs in f.top[t]:
                if not (kind == "map_anim" and "devious_explosion" in (N.named(text, "spotanim", names) or "")):
                    continue
                at = N.coord_of(text)
                row = ["map_anim h%s" % N.num(text, "height", 0)]
                for k2, t2, s2 in f.top[t]:
                    if N.coord_of(t2) == at and k2 != "map_anim":
                        row.append(signature(k2, t2, names))
                zaps = [p for p in projs(f, t, names, "zap") if p[7] == at]
                for p in zaps:
                    row.append("zap start=%s end=%s angle=%s offset=%s height %s->%s" % (p[1], p[2], p[3], p[4], p[5], p[6]))
                    name = target_name(f, t, p[8])
                    hs = [(dt, N.num(h, "value")) for dt in range(0, 3) for h in P.got(name, t + dt, "hit")] if name else []
                    hit[hs[0][0] if hs else None] += 1
                sig[tuple(sorted(set(row)))] += 1
                back = [tt for tt in range(t + 1, t + 60) for k2, t2, _ in f.top.get(tt, []) if k2.startswith("loc_add") and N.coord_of(t2) == at]
                life[back[0] - t if back else None] += 1
    print("== wrong tile (T = the explosion)")
    for k, v in sorted(sig.items(), key=lambda x: -x[1])[:8]:
        print("  %4d  %s" % (v, " | ".join(k)))
    print("  zap -> its target's hitsplat, ticks: %s" % dict(hit))
    print("  the tile's next loc change, ticks: %s" % dict(sorted(life.items(), key=str)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append", choices=["attacks", "primary", "bounce", "deathball", "start", "maze", "realm", "wrongtile", "death"])
    ap.add_argument("--anchor", default=ANCHOR)
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    names = N.load_names()
    fights = N.load(args.transcripts, names, args.anchor)
    for s in args.section or ["attacks", "primary", "bounce", "deathball", "start", "maze", "realm", "wrongtile", "death"]:
        globals()[s](fights, names)


if __name__ == "__main__":
    main()
