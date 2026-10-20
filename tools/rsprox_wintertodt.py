#!/usr/bin/env python3
"""The Wintertodt as Jagex's server sent it, read out of RSProx transcripts.

Every number the content uses for the boss's tells comes from here: the
pyromancers' fire stream (wint_fire npcs), the energy each fire takes, the
area / brazier / pyromancer attacks (projectiles, snow locs, map anims, sounds,
timings), the cold's standard hit, pyromancer damage and recovery, the round
lifecycle, and the recorder's own skilling.

    python3 tools/rsprox_wintertodt.py ~/.rsprox/binary/archive/{3254,3262,732,3263}.txt
    python3 tools/rsprox_wintertodt.py --section attacks FILES...
    python3 tools/rsprox_wintertodt.py --dump 280 300 FILE      # one window, prison only

Sessions with the fight (rev 233, September 2025): 3254, 3262, 732, 3263.
Ticks are the transcript's. The prison is x 1600..1663, z 3966..4031.
"""

import argparse
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
NPC = re.compile(r"npc=\(index=(\d+), id=(\w+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\)")
PLAYER = re.compile(r"player=\(index=(\d+)(?:, name=([^,]*), coord=\((\d+), (\d+), (\d+)\))?")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
ID = re.compile(r"id=(-?\w+)(?: \((-?\d+)\))?")
VALUES = re.compile(r"values=\[([^\]]*)\]")

STORM = (1627, 4004)          # wint_snow_storm SW tile (it is 5x5? printed SW)
BRAZIERS = {(1620, 3997): "SW", (1620, 4015): "NW", (1638, 3997): "SE", (1638, 4015): "NE"}


def in_prison(x, z):
    return 1600 <= x <= 1663 and 3966 <= z <= 4031


def num(text, key, default=None):
    m = re.search(key + r"=(-?\d+)", text)
    return int(m.group(1)) if m else default


class Ev:
    __slots__ = ("tick", "kind", "text", "owner", "subs")

    def __init__(self, tick, kind, text, owner):
        self.tick, self.kind, self.text, self.owner, self.subs = tick, kind, text, owner, []

    def coord(self):
        m = COORD.search(self.text)
        return (int(m.group(1)), int(m.group(2))) if m else None

    def ident(self):
        m = ID.search(self.text)
        return m.group(1) if m else None

    def __repr__(self):
        who = ""
        if self.owner:
            who = "%s#%s@%s,%s " % (self.owner[1], self.owner[2], self.owner[3][0], self.owner[3][1])
        sub = " | ".join(s for s in self.subs)
        return "[%d] %s%s %s%s" % (self.tick, who, self.kind, self.text, (" | " + sub) if sub else "")


class Session:
    def __init__(self, path):
        self.path = path
        self.name = path.rsplit("/", 1)[-1]
        self.events = []
        self.by_tick = defaultdict(list)
        self.me = None
        self.parse()

    def parse(self):
        tick = -1
        owner = None
        last_top = None
        me_index = None
        with open(self.path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                line = line.rstrip("\n")
                m = TICK.match(line)
                if m:
                    tick = int(m.group(1))
                    owner = None
                    continue
                if line.startswith("local player index:"):
                    me_index = line.split(":")[1].strip()
                m = TOP.match(line)
                if m:
                    kind, text = m.groups()
                    owner = None
                    ev = Ev(tick, kind, text, None)
                    self.add(ev)
                    last_top = ev
                    continue
                m = SUB8.match(line)
                if m:
                    kind, text = m.groups()
                    if kind in ("source", "target", "objs"):
                        if last_top is not None:
                            last_top.subs.append("%s %s" % (kind, text))
                        continue
                    n = NPC.search(text)
                    p = PLAYER.search(text) if not n else None
                    if n:
                        idx, name, nid, x, z, _ = n.groups()
                        nc = NEWCOORD.search(text)
                        at = (int(nc.group(1)), int(nc.group(2))) if nc else (int(x), int(z))
                        owner = ("npc", int(idx), name, at, (int(x), int(z)))
                    elif p and p.group(3):
                        idx, pname, x, z, _ = p.groups()
                        nc = NEWCOORD.search(text)
                        at = (int(nc.group(1)), int(nc.group(2))) if nc else (int(x), int(z))
                        if me_index is not None and idx == me_index:
                            pname = "ME"
                        owner = ("player", int(idx), pname, at, (int(x), int(z)))
                    else:
                        owner = None
                    self.add(Ev(tick, kind, text, owner))
                    continue
                m = SUB12.match(line)
                if m and owner is not None:
                    kind, text = m.groups()
                    self.add(Ev(tick, kind, text, owner))
                    continue
                if line.startswith(" " * 12 + "- ") and last_top is not None:
                    last_top.subs.append(line.strip())

    def add(self, ev):
        self.events.append(ev)
        self.by_tick[ev.tick].append(ev)

    def hud(self):
        """tick -> wint_update values (points, energy, pyro1..4, brazier1..4)."""
        out = {}
        for ev in self.events:
            if ev.kind == "runclientscript" and "wint_update" in ev.text:
                vals = [int(v) for v in VALUES.search(ev.text).group(1).split(",")]
                out[ev.tick] = vals
        return out


def load(paths):
    return [Session(p) for p in paths]


# ---------------------------------------------------------------- sections

def fires(sessions):
    """Each wint_fire: spawn tile, pyromancer cast pairing, path, despawn tile and energy."""
    print("== fires")
    spawn_tiles = Counter()
    del_tiles = Counter()
    lifetimes = Counter()
    cast_to_add = Counter()
    gaps = Counter()
    energy_per_del = Counter()
    spawnangles = Counter()
    for s in sessions:
        hud = s.hud()
        adds = {}
        last_add_by_tile = {}
        casts = defaultdict(list)  # pyro coord -> ticks of seq 4432
        for ev in s.events:
            if ev.owner and ev.owner[2] == "wint_wizard" and ev.kind == "sequence" and "4432" in ev.text:
                casts[ev.owner[3]].append(ev.tick)
        for ev in s.events:
            if ev.kind == "add" and "wint_fire" in ev.text:
                n = NPC.search(ev.text)
                idx = int(n.group(1))
                at = (int(n.group(4)), int(n.group(5)))
                adds[idx] = (ev.tick, at)
                spawn_tiles[at] += 1
                spawnangles[re.search(r"spawnangle=(\S+)", ev.text).group(1)] += 1
                if at in last_add_by_tile:
                    gaps[ev.tick - last_add_by_tile[at]] += 1
                last_add_by_tile[at] = ev.tick
                # nearest pyromancer cast at or before
                best = None
                for pc, ticks in casts.items():
                    if abs(pc[0] - at[0]) <= 3 and abs(pc[1] - at[1]) <= 3:
                        before = [t for t in ticks if t <= ev.tick]
                        if before:
                            best = ev.tick - before[-1]
                if best is not None:
                    cast_to_add[best] += 1
            elif ev.kind == "del" and "wint_fire" in ev.text:
                n = NPC.search(ev.text)
                idx = int(n.group(1))
                at = (int(n.group(4)), int(n.group(5)))
                del_tiles[at] += 1
                if idx in adds:
                    lifetimes[ev.tick - adds[idx][0]] += 1
                    # energy at this tick vs previous HUD
                    if ev.tick in hud:
                        prev = max((t for t in hud if t < ev.tick), default=None)
                        if prev is not None:
                            energy_per_del[hud[prev][1] - hud[ev.tick][1]] += 1
    print("  spawn tiles:", spawn_tiles.most_common(8))
    print("  spawnangle:", spawnangles.most_common())
    print("  despawn tiles:", del_tiles.most_common(8))
    print("  lifetime add->del ticks:", sorted(lifetimes.items()))
    print("  same-tile spawn gap:", sorted(gaps.items())[:20])
    print("  ticks since pyro cast seq at add:", sorted(cast_to_add.items())[:12])
    print("  hud energy drop on a del tick (vs previous hud):", sorted(energy_per_del.items()))


def energy(sessions):
    """Energy steps between HUD updates, and how they line up with fire despawns."""
    print("== energy")
    steps = Counter()
    for s in sessions:
        hud = s.hud()
        dels = Counter(ev.tick for ev in s.events if ev.kind == "del" and "wint_fire" in ev.text)
        ticks = sorted(hud)
        for a, b in zip(ticks, ticks[1:]):
            if b - a > 4:
                continue
            drop = hud[a][1] - hud[b][1]
            n = sum(dels[t] for t in range(a + 1, b + 1))
            steps[(n, drop)] += 1
    print("  (fire dels in (a,b], energy drop):", sorted(steps.items(), key=lambda kv: -kv[1])[:20])


def pyros(sessions):
    """Pyromancer seqs, says, hits, down/up transformations."""
    print("== pyromancers")
    seqs = Counter()
    says = Counter()
    hits = Counter()
    for s in sessions:
        for ev in s.events:
            if not ev.owner or not ev.owner[2].startswith("wint_wizard"):
                continue
            if ev.kind == "sequence":
                seqs[(ev.owner[2], ev.text)] += 1
            elif ev.kind == "say":
                says[(ev.owner[2], ev.text)] += 1
            elif ev.kind == "hit":
                hits[(ev.owner[2], re.sub(r"value=(\d+)", lambda m: "value=" + m.group(1), ev.text))] += 1
            elif ev.kind in ("spotanim", "transformation", "face_pathingentity"):
                seqs[(ev.owner[2], ev.kind + " " + ev.text)] += 1
    for k, n in sorted(seqs.items(), key=lambda kv: -kv[1]):
        print("  %5d seq/etc %s %s" % (n, k[0], k[1]))
    for k, n in sorted(says.items(), key=lambda kv: -kv[1]):
        print("  %5d say %s %s" % (n, k[0], k[1]))
    for k, n in sorted(hits.items()):
        print("  %5d hit %s %s" % (n, k[0], k[1]))


PYROS = {(1619, 3996): "SW", (1619, 4018): "NW", (1641, 3996): "SE", (1641, 4018): "NE"}
STORM_CENTRE = (1630, 4007)
SKIP = ("walk", "face_angle", "objs", "update_stat_v2", "run", "player_info", "npc_info_small_v5",
        "update_inv_partial", "idle", "headbar", "enabled_ops", "level_change", "add", "del")


def norm(ev):
    t = re.sub(r"coord=\(\d+, \d+, \d+\),? ?", "", ev.text)
    t = re.sub(r"rotation=\d", "rotation=R", t)
    t = re.sub(r"value=\d+", "value=V", t)
    t = re.sub(r"npc=\(index=\d+, ", "npc=(", t)
    return t.strip()


def projectile(ev):
    src = re.search(r"source coord=\((\d+), (\d+)", " ".join(ev.subs))
    tgt = re.search(r"target coord=\((\d+), (\d+)", " ".join(ev.subs))
    return ((int(src.group(1)), int(src.group(2))) if src else None,
            (int(tgt.group(1)), int(tgt.group(2))) if tgt else None)


def classify(ev):
    """A storm throw (spotanim 501 from the storm centre) -> attack kind, or None."""
    if "wyvern_skeleton_travel_breath" not in ev.text:
        return None
    _, tgt = projectile(ev)
    end = num(ev.text, "endtime")
    if tgt in PYROS:
        return "pyro"
    if tgt in BRAZIERS:
        return "brazier_break" if num(ev.text, "startheight") == 150 else "brazier_light"
    if end == 200:
        return "area"
    if end == 150:
        return None  # the area attack's second throw, three ticks after the first
    return "unknown"


def episodes(sessions):
    out = []
    for s in sessions:
        for ev in s.events:
            if ev.kind == "map_projanim_v2":
                kind = classify(ev)
                if kind:
                    out.append((s, kind, ev.tick, projectile(ev)[1], ev))
    return out


def about(e, tgt, kind):
    """Is this event about the attacked thing?"""
    if e.kind in ("varbit", "message_game", "synth_sound"):
        return True
    c = e.owner[3] if e.owner else e.coord()
    if not c:
        return False
    reach = 1 if kind == "pyro" else 2
    if kind.startswith("brazier"):
        # brazier SW tile; footprint 3x3 plus its pyromancer two tiles out
        cx, cz = tgt[0] + 1, tgt[1] + 1
        return max(abs(c[0] - cx), abs(c[1] - cz)) <= 3
    return max(abs(c[0] - tgt[0]), abs(c[1] - tgt[1])) <= reach


def attacks(sessions, window=(-2, 16), floor=6):
    """Every storm attack, its tells by tick offset from the throw (T)."""
    print("== attacks (T = the storm throw)")
    eps = episodes(sessions)
    by_kind = defaultdict(list)
    for ep in eps:
        by_kind[ep[1]].append(ep)
    for kind, rows in sorted(by_kind.items()):
        sig = Counter()
        projs = Counter()
        for s, _, T, tgt, ev in rows:
            projs[norm(ev)] += 1
            seen = set()
            for t in range(T + window[0], T + window[1]):
                for e in s.by_tick.get(t, []):
                    if e.kind in SKIP or (e.owner and e.owner[2] == "wint_fire"):
                        continue
                    if e.owner and e.owner[0] == "player" and e.owner[2] != "ME":
                        continue
                    if not about(e, tgt, kind):
                        continue
                    who = e.owner[2] if e.owner else ""
                    key = (t - T, e.kind, who, norm(e))
                    if key not in seen:
                        seen.add(key)
                        sig[key] += 1
        print("  -- %s: %d throws" % (kind, len(rows)))
        for k, n in projs.most_common():
            print("     throw x%d %s" % (n, k))
        lim = max(2, len(rows) * floor // 100)
        for k, n in sorted(sig.items()):
            if n >= lim:
                print("     %+3d %4d %-18s %-14s %s" % (k[0], n, k[1], k[2], k[3][:130]))


def dump(sessions, t0, t1, kinds):
    for s in sessions:
        print("== %s %d..%d" % (s.name, t0, t1))
        for t in range(t0, t1 + 1):
            for ev in s.by_tick.get(t, []):
                if kinds and not re.search(kinds, ev.kind):
                    continue
                if ev.kind in ("walk", "objs", "update_stat_v2", "face_angle", "run", "player_info",
                               "npc_info_small_v5", "update_inv_partial"):
                    if not (ev.kind == "walk" and ev.owner and ev.owner[0] == "player" and ev.owner[2] == "ME"):
                        continue
                if ev.kind == "runclientscript" and re.search(r"stat_boosts|spec_regen|regen_timer", ev.text):
                    continue
                if ev.owner and ev.owner[2] == "wint_fire" and ev.kind != "add" and ev.kind != "del":
                    continue
                if ev.kind in ("add", "del") and "wint_fire" in ev.text:
                    continue
                print(" ", ev)


SECTIONS = {"fires": fires, "energy": energy, "pyros": pyros, "attacks": attacks}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", action="append")
    ap.add_argument("--dump", nargs=2, type=int)
    ap.add_argument("--kinds", default="")
    a = ap.parse_args()
    sessions = load(a.transcripts)
    if a.dump:
        dump(sessions, a.dump[0], a.dump[1], a.kinds)
        return 0
    for name in a.section or SECTIONS:
        SECTIONS[name](sessions)
    return 0


if __name__ == "__main__":
    sys.exit(main())
