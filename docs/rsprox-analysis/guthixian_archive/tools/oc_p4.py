#!/usr/bin/env python3
"""Ocellus's phase 4, every fight, parsed tick by tick from the --all-players
--bas transcripts (home-all-bas): the raiders' tiles (before and after the
tick's move), seqs, spotanims, splats, overhead prayers and weapons, his own
npc info, the other npcs (rifts, the arena npc), sounds, projectiles, locs and
varbits. E = "End...", D = each `deflect_in` (the cycle's tick).

    python3 oc_p4.py [ids...]          -> pickles idx<id>.oc_p4.pkl beside the transcripts
    import oc_p4; fights = oc_p4.load()   (list of Fight)

Each Fight: .sid, .E, .D (list of deflect ticks), .K (death seq tick or None),
.raiders (names), .t[tick] = Tick with .pl[name] = P(before, after, move,
seqs, spots, hits, icon, weapon, facing), .npc[index] = N(type, before,
after, move, seqs, spots, hits, faces, icons, say, extra), .top (list of
(kind, text)), .var {name: (old, new)}.
"""
import os
import pickle
import re
import sys
from collections import defaultdict

ARCH = os.path.expanduser("~/.rsprox-index-cache/home-all-bas/.rsprox/binary/archive")
SESSIONS = (4301, 4308, 4309, 4312, 4315, 4316, 4318, 4319, 4320)

TICK = re.compile(r"^\[(\d+)\]")
C = r"coord=\((\d+), (\d+), (\d+)\)"
PL = re.compile(r"^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]*), " + C + r"\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
NPC = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)(?:, multinpc=[^)]*\))?, " + C + r"\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?(.*)")
SEQ = re.compile(r"^\[sequence\] id=(?:[a-z0-9_]+ )?\(?(-?\d+)\)?(?:, delay=(\d+))?")
SEQN = re.compile(r"^\[sequence\] id=([a-z0-9_]+)")
SPOT = re.compile(r"^\[spotanim\](?: slot=(\d+),)? id=([a-z0-9_]+|-1)(?: \(\d+\))?(.*)")
HIT = re.compile(r"^\[hit\] id=(\d+), value=(-?\d+)(.*)")
ICON = re.compile(r"overheadicon=(-?\d+)")
WEAR = re.compile(r"- wearpos=righthand, id=([a-z0-9_]+)")
HEADICON = re.compile(r"\[headicon\] headiconslot=(\d+), id=(\d+), spriteindex=(\d+)")
VARBIT = re.compile(r"^ {4}\[varbit\] id=([a-z0-9_]+) \((\d+)\), old=(-?\d+), new=(-?\d+)")


class P:
    __slots__ = ("before", "after", "move", "seqs", "spots", "hits", "icon", "weapon", "facing", "headbar", "other")

    def __init__(self, move, before, after):
        self.move, self.before, self.after = move, before, after
        self.seqs, self.spots, self.hits, self.facing, self.headbar, self.other = [], [], [], [], [], []
        self.icon = None
        self.weapon = None


class N:
    __slots__ = ("type", "before", "after", "move", "seqs", "spots", "hits", "faces", "icons", "say", "other")

    def __init__(self, typ, move, before, after):
        self.type, self.move, self.before, self.after = typ, move, before, after
        self.seqs, self.spots, self.hits, self.faces, self.say, self.other = [], [], [], [], [], []
        self.icons = None


class Tick:
    __slots__ = ("pl", "npc", "top", "var")

    def __init__(self):
        self.pl, self.npc, self.top, self.var = {}, {}, [], {}


def parse(path):
    ticks = defaultdict(Tick)
    tick = 0
    cur = None
    sect = None
    in_icons = False
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        T = ticks[tick]
        if line.startswith("    [") :
            cur = None
            s = line.strip()
            if s.startswith("[player_info]"):
                sect = "pl"
                continue
            if s.startswith("[npc_info"):
                sect = "npc"
                continue
            sect = None
            m = VARBIT.match(line)
            if m:
                T.var[m.group(1)] = (int(m.group(3)), int(m.group(4)))
                continue
            if s.startswith("[varp") or s.startswith("[if_set") or s.startswith("[runclientscript] id=hp_hud") or "script10152" in s or "music_now" in s or "midi_song" in s:
                if s.startswith("[varp"):
                    T.top.append(("varp", s))
                continue
            T.top.append(("top", s))
            continue
        if line.startswith("        [") and sect in (None,) and T.top:
            # a projectile's source/target, an obj list: attach to the last top row
            k, s = T.top[-1]
            T.top[-1] = (k, s + " || " + line.strip())
            continue
        if sect == "pl":
            m = PL.match(line)
            if m:
                b = (int(m.group(4)), int(m.group(5)))
                a = (int(m.group(7)), int(m.group(8))) if m.group(7) else b
                cur = P(m.group(1), b, a)
                T.pl[m.group(3)] = cur
                continue
            if cur is None:
                continue
            s = line.strip()
            if s.startswith("[sequence]"):
                n = SEQN.match(s)
                d = re.search(r"delay=(\d+)", s)
                cur.seqs.append((n.group(1) if n else s, int(d.group(1)) if d else 0))
            elif s.startswith("[spotanim]"):
                m = SPOT.match(s)
                if m:
                    cur.spots.append((int(m.group(1) or 0), m.group(2), m.group(3)))
            elif s.startswith("[hit]"):
                m = HIT.match(s)
                cur.hits.append((int(m.group(1)), int(m.group(2))))
            elif s.startswith("[headbar]"):
                cur.headbar.append(s)
            elif s.startswith("[facing]"):
                cur.facing.append(s[9:])
            elif "overheadicon" in s:
                cur.icon = int(ICON.search(s).group(1))
            elif s.startswith("- wearpos=righthand"):
                cur.weapon = WEAR.match(s).group(1)
            elif s.startswith("[appearance]") or s.startswith("[details]") or s.startswith("- wearpos") or s.startswith("[equipment]") or s.startswith("[bas]") or s.startswith("[status]"):
                # A status line with no overhead: no prayer. A transcript
                # with no status lines at all (4315's) says nothing.
                if s.startswith("[status]"):
                    cur.icon = -1
            else:
                cur.other.append(s)
            continue
        if sect == "npc":
            m = NPC.match(line)
            if m:
                b = (int(m.group(5)), int(m.group(6)))
                a = (int(m.group(8)), int(m.group(9))) if m.group(8) else b
                cur = N(m.group(3), m.group(1), b, a)
                if m.group(11):
                    cur.other.append(m.group(11).strip(", "))
                T.npc[int(m.group(2))] = cur
                in_icons = False
                continue
            if cur is None:
                continue
            s = line.strip()
            if s.startswith("[headicon_customisation]"):
                cur.icons = []
                continue
            if s.startswith("[headicon]"):
                m = HEADICON.search(s)
                if cur.icons is None:
                    cur.icons = []
                cur.icons.append(int(m.group(3)))
                continue
            if s.startswith("[sequence]"):
                n = SEQN.match(s)
                d = re.search(r"delay=(\d+)", s)
                cur.seqs.append((n.group(1) if n else s, int(d.group(1)) if d else 0))
            elif s.startswith("[spotanim]"):
                m = SPOT.match(s)
                if m:
                    cur.spots.append((int(m.group(1) or 0), m.group(2), m.group(3)))
            elif s.startswith("[hit]"):
                m = HIT.match(s)
                cur.hits.append((int(m.group(1)), int(m.group(2))))
            elif s.startswith("[facing]"):
                cur.faces.append(s[9:])
            elif s.startswith("[say]"):
                cur.say.append(s[12:-1])
            else:
                cur.other.append(s)
    return ticks


class Fight:
    pass


def fights_of(sid, ticks):
    out = []
    order = sorted(ticks)
    ends = [t for t in order for n in ticks[t].npc.values() if n.type.startswith("tfa_ocellus") and "End..." in n.say]
    for E in ends:
        f = Fight()
        f.sid, f.E, f.ticks = sid, E, ticks
        boss = next(i for i, n in ticks[E].npc.items() if "End..." in n.say)
        f.boss = boss
        f.D, f.K, last = [], None, E
        for t in order:
            if t < E:
                continue
            n = ticks[t].npc.get(boss)
            if n is None:
                if t - last > 30:
                    break
                continue
            last = t
            for s, _ in n.seqs:
                if s == "npc_ocellus_phase3_deflect_in":
                    f.D.append(t)
                if s == "npc_ocellus_phase3_death" and f.K is None:
                    f.K = t
            if n.move == "del":
                break
        f.last = last
        f.t = {t: ticks[t] for t in order if E - 40 <= t <= last + 3}
        # The party: the raid HUD's names (script 10042) last sent before E;
        # a spectator sees other parties' raiders too.
        party = None
        for t in order:
            if t > E:
                break
            for k, s in ticks[t].top:
                if s.startswith('[runclientscript] id=script10042'):
                    party = [x for x in re.findall(r'"([^"]*)"', s.split('values=')[1]) if x]
        names = set()
        for t in range(E, (f.K or last) + 1):
            if t in ticks:
                names.update(ticks[t].pl)
        if party:
            norm = {x.replace('\xa0', ' ') for x in party}
            names = {n for n in names if n.replace('\xa0', ' ') in norm}
        f.raiders = sorted(names)
        f.party = party
        out.append(f)
    return out


def load(ids=SESSIONS, force=False):
    res = []
    for sid in ids:
        path = os.path.join(ARCH, f"idx{sid}.txt")
        pk = path + ".oc_p4.pkl"
        if not force and os.path.exists(pk) and os.path.getmtime(pk) > os.path.getmtime(path) and os.path.getmtime(pk) > os.path.getmtime(__file__):
            with open(pk, "rb") as fh:
                ticks = pickle.load(fh)
        else:
            ticks = parse(path)
            with open(pk, "wb") as fh:
                pickle.dump(dict(ticks), fh)
        res.extend(fights_of(sid, ticks))
    return res


# ---- state helpers --------------------------------------------------------
def state(f):
    """Each raider's tile (after each tick's move), prayer and weapon, carried
    forward from every tick of the session up to the fight's end."""
    tile, icon, weapon = {}, {}, {}
    out = {}
    for t in sorted(f.ticks):
        if t > f.last + 3:
            break
        T = f.ticks[t]
        for name, p in T.pl.items():
            tile[name] = p.after
            if p.icon is not None:
                icon[name] = p.icon
            if p.weapon is not None:
                weapon[name] = p.weapon
        if t >= f.E - 40:
            out[t] = (dict(tile), dict(icon), dict(weapon))
    return out


def box(sw, size, tile):
    dx = max(sw[0] - tile[0], 0, tile[0] - (sw[0] + size - 1))
    dz = max(sw[1] - tile[1], 0, tile[1] - (sw[1] + size - 1))
    return max(dx, dz)


if __name__ == "__main__":
    import oc_p4  # pickled classes must live in the module, not __main__
    ids = [int(x) for x in sys.argv[1:]] or SESSIONS
    for f in oc_p4.load(ids, force=True):
        print(f.sid, "E", f.E, "D", [d - f.E for d in f.D], "K", f.K and f.K - f.E, "raiders", f.raiders)
