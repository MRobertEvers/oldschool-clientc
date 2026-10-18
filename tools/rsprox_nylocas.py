#!/usr/bin/env python3
"""The Nylocas room as Jagex's server sent it, read out of RSProx transcripts.

rsprox_tells.py counts what was sent. This follows each npc through its life,
so the questions that need one npc's history can be answered: how a small or a
big dies (killed or self-destructed), what a nylocas does to a support, what a
projectile's flight is against the thrower's footprint, how Vasilias spawns and
changes form, how a support collapses, and when the waves come.

    python3 tools/rsprox_nylocas.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox_nylocas.py --section deaths FILES...

Ticks are the transcript's. "T" in a pattern is the defining event's tick (the
killing hit, the add, the swing).
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_tells import load_names, ref, field_ref, windows, REF, TICK, parse_anchor  # noqa: E402

ANCHOR = "8342-8358"
NPC_HEAD = re.compile(r"^\s{8}\[(\w+)\] npc=\(index=(\d+), " + REF + r", coord=\((\d+), (\d+), (\d+)\)")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
TOP = re.compile(r"^\s{4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^\s{8}\[(\w+)\] ?(.*)$")
NPC_SUB = re.compile(r"^\s{12}\[(\w+)\] ?(.*)$")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
PLAYER = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)")
NUM = lambda key: re.compile(key + r"=(-?\d+)")
FIELDS = {k: NUM(k) for k in ("delay", "height", "starttime", "endtime", "angle", "progress",
                              "startheight", "endheight", "range", "value", "fill", "loops")}


def num(text, key, default=None):
    m = FIELDS[key].search(text)
    return int(m.group(1)) if m else default


def size_of(name):
    if name.startswith("nylocas_boss") or name.startswith("nylocas_miniboss"):
        return 4
    if "support" in name:
        return 3
    if "_big_" in name:
        return 2
    return 1


class Npc:
    def __init__(self, index):
        self.index = index
        self.events = []        # (tick, kind, detail)
        self.types = []         # (tick, name)
        self.coords = {}        # tick -> (x, z) at the end of the tick
        self.added = None
        self.deleted = None
        self.zero_hit = None    # tick of the first hit that left the headbar empty

    def name_at(self, tick):
        name = self.types[0][1]
        for t, n in self.types:
            if t <= tick:
                name = n
        return name

    def coord_at(self, tick):
        best = None
        for t in sorted(self.coords):
            if t <= tick:
                best = self.coords[t]
        return best


class Fight:
    """One window of one transcript, parsed."""

    def __init__(self, path, span, names):
        self.path = os.path.basename(path)
        self.span = span
        self.npcs = {}
        self.retired = []                  # earlier lives of an index that was added again
        self.top = defaultdict(list)       # tick -> [(kind, text, [sub lines])]
        self.names = names


def parse(path, spans, names):
    fights = [Fight(path, s, names) for s in spans]

    def fight_of(tick):
        for f in fights:
            if f.span[0] - 5 <= tick <= f.span[1] + 30:
                return f
        return None

    tick = 0
    fight = None
    current_npc = None
    current_top = None
    block = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            m = TICK.match(line)
            if m and line.startswith("["):
                tick = int(m.group(1))
                fight = fight_of(tick)
                current_npc = current_top = None
                continue
            if fight is None or not line.strip():
                continue
            m = TOP.match(line)
            if m:
                block = m.group(1)
                current_npc = None
                current_top = (m.group(1), m.group(2), [])
                fight.top[tick].append(current_top)
                continue
            if block and block.startswith("npc_info"):
                m = NPC_HEAD.match(line)
                if m:
                    kind, index = m.group(1), int(m.group(2))
                    name, _ = ref(m, "npc", names, start=3)
                    x, z = int(m.group(7)), int(m.group(8))
                    npc = fight.npcs.get(index)
                    if npc is None or (kind == "add" and npc.deleted is not None):
                        if npc is not None:
                            fight.retired.append(npc)
                        npc = Npc(index)
                        fight.npcs[index] = npc
                    if not npc.types:
                        npc.types.append((tick, name))
                    nc = NEWCOORD.search(line)
                    end = (int(nc.group(1)), int(nc.group(2))) if nc else (x, z)
                    npc.coords.setdefault(tick - 1, (x, z))
                    npc.coords[tick] = end
                    if kind == "add":
                        npc.added = tick
                    elif kind == "del":
                        npc.deleted = tick
                    npc.events.append((tick, kind, ""))
                    current_npc = npc
                    continue
                m = NPC_SUB.match(line)
                if m and current_npc is not None:
                    sub, text = m.group(1), m.group(2)
                    if sub == "sequence":
                        seq, _ = field_ref(text, "seq", names)
                        current_npc.events.append((tick, "seq", "%s%s" % (seq, " d=%d" % num(text, "delay") if num(text, "delay") else "")))
                    elif sub == "transformation":
                        new, _ = field_ref(text, "npc", names)
                        current_npc.types.append((tick, new))
                        current_npc.events.append((tick, "retype", new))
                    elif sub == "hit":
                        current_npc.events.append((tick, "hit", text))
                    elif sub == "headbar":
                        fill = num(text, "fill")
                        current_npc.events.append((tick, "headbar", str(fill)))
                        if fill == 0 and current_npc.zero_hit is None:
                            current_npc.zero_hit = tick
                    elif sub == "spotanim":
                        g, _ = field_ref(text, "spotanim", names)
                        current_npc.events.append((tick, "spotanim", "%s %s" % (g, text)))
                    else:
                        current_npc.events.append((tick, sub, text))
                    continue
            if block and block.startswith("player_info"):
                continue
            if current_top is not None and line.startswith("        "):
                current_top[2].append(line.strip())
    return fights


def load(paths, names, anchor_text=ANCHOR):
    out = []
    anchor = parse_anchor(anchor_text)
    for path in paths:
        spans = windows(path, anchor, 50, names)
        out.extend(parse(path, spans, names))
    return out


def top_at(fight, tick, kind):
    return [t for t in fight.top.get(tick, []) if t[0] == kind]


def coord_of(text):
    m = COORD.search(text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def named(text, space, names):
    return field_ref(text, space, names)[0]


# ----------------------------------------------------------------------
# Deaths
# ----------------------------------------------------------------------
def deaths(fights, names):
    """Every nylocas' end, classified: killed or self-destructed, small or big.
    The pattern is each event's tick relative to T (the killing hit for a kill,
    the delete for a self-destruct) and the map graphics and area sounds sent
    on the npc's last tile."""
    patterns = defaultdict(Counter)
    lifetimes = defaultdict(Counter)
    gfx_offset = Counter()
    for f in fights:
        for npc in f.npcs.values():
            name0 = npc.types[0][1]
            if not name0.startswith("tob_nylocas_") or "support" in name0 or npc.deleted is None:
                continue
            last = npc.name_at(npc.deleted)
            big = "_big_" in last
            killed = npc.zero_hit is not None
            T = npc.zero_hit if killed else npc.deleted
            parts = []
            for t, kind, detail in npc.events:
                if t < T - 3 or kind in ("walk", "teleport", "idle", "run", "crawl", "hit", "headbar", "face_pathingentity"):
                    continue
                # a swing or a turn before the end is the npc's life, not its death
                if t < T - 1 and kind == "face_angle":
                    continue
                if kind == "seq" and "death" not in detail and t <= T:
                    continue
                if kind in ("seq", "face_angle", "enabled_ops", "retype", "del"):
                    d = detail
                    if kind == "enabled_ops":
                        d = detail.replace("opflags=", "")
                    elif kind == "face_angle":
                        d = ""
                    elif kind == "seq":
                        d = re.sub(r"top_spider_(melee|ranged|magic)_", "", d)
                    elif kind == "retype":
                        d = "fighting" if "fighting" in d else "incoming"
                    parts.append("%+d %s%s" % (t - T, kind, (" " + d) if d else ""))
            # graphics and sounds on the last tile, from T-1 to the delete + 1
            where = npc.coord_at(npc.deleted)
            for t in range(T - 1, npc.deleted + 2):
                for kind, text, _ in f.top.get(t, []):
                    if kind not in ("map_anim", "sound_area"):
                        continue
                    c = coord_of(text)
                    if c is None or where is None:
                        continue
                    dx, dz = c[0] - where[0], c[1] - where[1]
                    size = 2 if big else 1
                    if not (0 <= dx < size and 0 <= dz < size):
                        continue
                    what = named(text, "spotanim" if kind == "map_anim" else "sound", names)
                    if kind == "map_anim":
                        what = re.sub(r"tob_nylocas_death_(melee|ranged|magic)_", "", what)
                        gfx_offset[("big" if big else "small", dx, dz)] += 1
                    else:
                        what = "%s r%s" % (what, num(text, "range"))
                    parts.append("%+d %s %s" % (t - T, kind, what))
            # splits: smalls added on the big's tiles
            if big:
                adds = 0
                for other in f.npcs.values():
                    if other.added is not None and abs(other.added - npc.deleted) <= 1 and where:
                        c = other.coords.get(other.added)
                        if c and 0 <= c[0] - where[0] <= 1 and 0 <= c[1] - where[1] <= 1 and other is not npc:
                            adds += 1
                            parts.append("%+d split" % (other.added - T))
            key = ("big" if big else "small", "killed" if killed else "detonate")
            parts.sort(key=lambda p: (int(p.split()[0]), p))
            patterns[key][" | ".join(parts)] += 1
            if not killed and npc.added is not None:
                lifetimes[key][npc.deleted - npc.added] += 1
    print("== deaths (T = killing hit, or the delete for a self-destruct)")
    for key in sorted(patterns):
        total = sum(patterns[key].values())
        print("-- %s %s: %d" % (key[0], key[1], total))
        for pat, n in patterns[key].most_common(8):
            print("  %5d  %s" % (n, pat))
    print("-- self-destruct lifetime (add -> delete, ticks)")
    for key in sorted(lifetimes):
        print("  %s: %s" % (key[0], dict(sorted(lifetimes[key].items()))))
    print("-- death graphic offset from the npc's coord")
    for key, n in sorted(gfx_offset.items()):
        print("  %s %+d,%+d: %d" % (key[0], key[1], key[2], n))


# ----------------------------------------------------------------------
# Support bites
# ----------------------------------------------------------------------
def bites(fights, names):
    """An incoming nylocas chewing a support: its seq, the seq's delay, the
    interval between two bites by one nylocas, and the support's sound."""
    seqs = Counter()
    delays = Counter()
    gaps = Counter()
    sound_lag = Counter()
    support_hits = Counter()
    for f in fights:
        supports = {}
        for npc in f.npcs.values():
            if "support" in npc.types[0][1]:
                c = npc.coord_at(npc.added or f.span[0])
                if c:
                    supports[npc.index] = c
                for t, kind, detail in npc.events:
                    if kind == "hit":
                        support_hits[detail.split(",")[0]] += 1
        for npc in f.npcs.values():
            last = None
            for t, kind, detail in npc.events:
                if kind != "seq":
                    continue
                name = npc.name_at(t)
                if "incoming" not in name:
                    continue
                seq = detail.split()[0]
                seqs[(re.sub(r"tob_nylocas_", "", name), seq)] += 1
                m = re.search(r"d=(\d+)", detail)
                delays[int(m.group(1)) if m else 0] += 1
                if last is not None:
                    gaps[t - last] += 1
                last = t
                # the nearest id=-1 area sound at a support, this tick or the next two
                for lag in range(0, 3):
                    hit = [x for x in f.top.get(t + lag, []) if x[0] == "sound_area" and "id=-1" in x[1]]
                    if hit:
                        sound_lag[lag] += 1
                        break
                else:
                    sound_lag["none"] += 1
    print("== support bites")
    for k, n in seqs.most_common():
        print("  %6d  %s %s" % (n, k[0], k[1]))
    print("  seq delay: %s" % dict(sorted(delays.items())))
    print("  ticks between two bites by one nylocas: %s" % dict(sorted(gaps.most_common(8))))
    print("  id=-1 area sound after a bite (ticks): %s" % dict(sound_lag))
    print("  hitsplats on supports: %s" % dict(support_hits.most_common(6)))


# ----------------------------------------------------------------------
# Attacks on players
# ----------------------------------------------------------------------
def footprint_distance(sw, size, at):
    dx = max(sw[0] - at[0], 0, at[0] - (sw[0] + size - 1))
    dz = max(sw[1] - at[1], 0, at[1] - (sw[1] + size - 1))
    return max(dx, dz)


def attacks(fights, names):
    """Projectiles a nylocas or Vasilias threw at a player: the thrower (the npc
    that swung that tick and whose footprint holds the source tile), the source
    tile against its footprint, and the flight against the footprint distance."""
    rows = Counter()
    src = Counter()
    flight = defaultdict(Counter)
    seq_of = Counter()
    for f in fights:
        for tick, tops in f.top.items():
            for kind, text, subs in tops:
                if not kind.startswith("map_projanim"):
                    continue
                if not any("player=" in s for s in subs if s.startswith("[target]")):
                    continue
                g = named(text, "spotanim", names)
                if g is None or not ("nylocas" in g or "babysplitter" in g):
                    continue
                source = target = None
                for s in subs:
                    if s.startswith("[source]"):
                        source = coord_of(s)
                    elif s.startswith("[target]"):
                        target = coord_of(s)
                thrower = None
                for npc in f.npcs.values():
                    swung = [d for t, k, d in npc.events if t == tick and k == "seq" and "attack" in d]
                    if not swung:
                        continue
                    sw = npc.coord_at(tick)
                    size = size_of(npc.name_at(tick))
                    if sw and source and 0 <= source[0] - sw[0] < size and 0 <= source[1] - sw[1] < size:
                        thrower = (npc, sw, size, swung[0])
                        break
                v2 = kind.endswith("v2")
                sh, eh = num(text, "startheight"), num(text, "endheight")
                if v2:
                    sh, eh = sh / 4, eh / 4
                start, end = num(text, "starttime"), num(text, "endtime")
                rows[(g, "start=%d angle=%d progress=%d height=%g->%g" % (start, num(text, "angle"), num(text, "progress"), sh, eh))] += 1
                if thrower:
                    npc, sw, size, swung = thrower
                    who = re.sub(r"tob_nylocas_", "", npc.name_at(tick))
                    src[(g, who, "src-sw=%+d,%+d" % (source[0] - sw[0], source[1] - sw[1]))] += 1
                    seq_of[(who, swung.split()[0])] += 1
                    d = footprint_distance(sw, size, target)
                    flight[g][(d, end - start)] += 1
                else:
                    src[(g, "?", "")] += 1
    print("== projectiles at players (heights in our units, raw/4 for v2)")
    for k, n in rows.most_common():
        print("  %5d  %s %s" % (n, k[0], k[1]))
    print("-- thrower and source tile")
    for k, n in sorted(src.items()):
        print("  %5d  %s %s %s" % (n, k[0], k[1], k[2]))
    print("-- the thrower's seq")
    for k, n in sorted(seq_of.items()):
        print("  %5d  %s %s" % (n, k[0], k[1]))
    print("-- flight (cycles) by footprint distance")
    for g in sorted(flight):
        print("  %s: %s" % (g, " ".join("d%d:%d x%d" % (d, fl, n) for (d, fl), n in sorted(flight[g].items()))))


# ----------------------------------------------------------------------
# Waves
# ----------------------------------------------------------------------
def waves(fights, names):
    """Wave spawns: the ticks on which nylocas were added in the tunnels (a
    split is added on its big's tiles and is not a wave) and the gaps between
    them. A wave held back by the cap can arrive over two ticks, so the count
    of add ticks runs a little over the 31 waves."""
    for f in fights:
        adds = defaultdict(list)
        bigs_dead = {}
        for npc in f.npcs.values():
            if npc.deleted is not None and "_big_" in npc.name_at(npc.deleted):
                bigs_dead[npc.index] = (npc.deleted, npc.coord_at(npc.deleted))
        for npc in f.npcs.values():
            name = npc.types[0][1]
            if npc.added is None or "incoming" not in name:
                continue
            c = npc.coords.get(npc.added)
            split = any(abs(d - npc.added) <= 1 and w and 0 <= c[0] - w[0] <= 1 and 0 <= c[1] - w[1] <= 1
                        for d, w in bigs_dead.values())
            if not split:
                adds[npc.added].append((name, c))
        ticks = sorted(adds)
        if not ticks:
            continue
        gaps = [b - a for a, b in zip(ticks, ticks[1:])]
        boss = [n.added for n in f.npcs.values() if n.types[0][1].startswith("nylocas_boss") and n.added]
        sup = [n.added for n in f.npcs.values() if "support" in n.types[0][1] and n.added]
        t0 = min(sup) if sup else f.span[0]
        print("%s %d-%d add-ticks=%d first=+%d last=+%d boss=+%s gaps=%s" % (
            f.path, f.span[0], f.span[1], len(ticks), ticks[0] - t0, ticks[-1] - t0,
            ",".join("%d" % (b - t0) for b in sorted(boss)[:2]), ",".join(map(str, gaps))))


# ----------------------------------------------------------------------
# Timeline for one kind of moment
# ----------------------------------------------------------------------
def boss(fights, names):
    """Vasilias: the spawn, form changes, attacks, death."""
    form_gaps = Counter()
    swings_per_form = Counter()
    for f in fights:
        bosses = sorted((n for n in f.npcs.values() if n.types[0][1].startswith("nylocas_boss")), key=lambda n: n.added or 0)
        if not bosses:
            continue
        t0 = bosses[0].added
        print("-- %s boss at %d" % (f.path, t0))
        for b in bosses:
            out = []
            for t, kind, detail in b.events:
                if kind in ("walk", "teleport", "idle", "run", "crawl", "hit", "headbar", "face_pathingentity", "face_coord"):
                    continue
                out.append("%+d %s %s" % (t - t0, kind, detail))
            print("   npc %d %s added %+d deleted %s: %s" % (b.index, b.types[0][1], (b.added or 0) - t0,
                  "%+d" % (b.deleted - t0) if b.deleted else "-", "; ".join(out[:12])))
            last_form = None
            swings = 0
            for t, kind, detail in b.events:
                if kind == "retype":
                    if last_form is not None:
                        form_gaps[t - last_form] += 1
                    swings_per_form[swings] += 1
                    swings = 0
                    last_form = t
                elif kind == "seq" and "attack" in detail:
                    swings += 1
        # what else happened around the spawn
        for t in range(t0 - 3, t0 + 6):
            for kind, text, _ in f.top.get(t, []):
                if kind in ("map_anim", "sound_area", "midi_song_v2", "midi_song", "message_game", "loc_add_change", "loc_anim", "cam_shake", "synth_sound"):
                    if kind == "sound_area" and "id=-1" in text:
                        continue
                    print("      %+d %s %s" % (t - t0, kind, text[:120]))
    print("-- ticks between form changes: %s" % dict(sorted(form_gaps.items())))
    print("-- attacks per form: %s" % dict(sorted(swings_per_form.items())))


def moments(fights, names, kinds, pattern, before, after):
    """Every top-level event of `kinds` from `before` to `after` ticks around
    each top-level event matching `pattern`."""
    for f in fights:
        for tick in sorted(f.top):
            for kind, text, _ in f.top[tick]:
                if re.search(pattern, kind + " " + text):
                    print("-- %s %d %s %s" % (f.path, tick, kind, text[:100]))
                    for t in range(tick - before, tick + after + 1):
                        for k2, t2, s2 in f.top.get(t, []):
                            if k2 in kinds and not (k2 == "sound_area" and "id=-1" in t2):
                                print("    %+d %s %s %s" % (t - tick, k2, t2[:150], " ".join(s2)[:80]))
                    break


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append",
                    choices=["deaths", "bites", "attacks", "waves", "boss", "collapse", "wrongstyle", "end"])
    ap.add_argument("--anchor", default=ANCHOR,
                    help="npc ids that define the room (default Normal; Hard 10791-10811, Entry 10774-10790)")
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    names = load_names()
    fights = load(args.transcripts, names, args.anchor)
    sections = args.section or ["waves", "deaths", "bites", "attacks", "boss", "collapse", "wrongstyle", "end"]
    for s in sections:
        if s == "deaths":
            deaths(fights, names)
        elif s == "bites":
            bites(fights, names)
        elif s == "attacks":
            attacks(fights, names)
        elif s == "waves":
            print("== waves (ticks from the supports' add)")
            waves(fights, names)
        elif s == "boss":
            print("== Vasilias")
            boss(fights, names)
        elif s == "collapse":
            print("== support collapse")
            moments(fights, names, ("map_anim", "sound_area", "loc_add_change", "loc_anim", "loc_del", "cam_shake", "cam_reset",
                                    "message_game", "map_projanim_v1", "map_projanim_v2", "synth_sound"),
                    r"loc_anim .*(nylocas_pillar_collapse|id=8074)|message_game.*support was destroyed", 4, 6)
        elif s == "wrongstyle":
            print("== wrong style")
            moments(fights, names, ("map_anim", "message_game", "synth_sound", "map_projanim_v1", "map_projanim_v2"),
                    r"map_anim .*(tob_nylocas_shielded|id=1558)", 0, 0)
        elif s == "end":
            print("== room end")
            moments(fights, names, ("map_anim", "sound_area", "loc_add_change", "loc_del", "message_game", "midi_song_v2",
                                    "midi_jingle", "obj_add", "obj_add_specific", "synth_sound", "cam_reset", "if_opensub"),
                    r"Wave 'The Nylocas'", 6, 6)


if __name__ == "__main__":
    main()
