#!/usr/bin/env python3
"""Tekton as Jagex's server sent him, read out of RSProx transcripts
(tools/cox/rsprox_fetch.py + RSProx `transcribe`).

    python3 tools/cox/rsprox_tekton.py ~/.rsprox/binary/archive/{820,3285,843,3389}.txt
    python3 tools/cox/rsprox_tekton.py --section sparks FILES...

Sections: timeline, phases, swings, sparks, heals, hits, death.

Ticks are the transcript's. A player's tile "at T" is where player_info put
them after tick T's movement (the end of T). Tekton's footprint is his 4x4
from the npc_info coordinate (the end of the tick, after any step).

COORDINATES: the transcriber maps every instance tile back to the TEMPLATE
tile it was copied from. A player (1x1) comes out right. A 4x4 npc's printed
tile is its instance south-west tile mapped back, which in a turned room is
another corner of the template footprint, and its step labels and face
angles stay in the turned (instance) frame. The room's turn is read off the
walk steps (label vs coordinate delta); in a 180-degree room the template
south-west is the printed tile - (3, 3) and a face angle is turned by 1024.
A 90-degree room breaks the mapping at every chunk edge (the coordinates jump),
so its geometry is not used (`rot` None).
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
NPC = re.compile(r"npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\), coord=\((\d+), (\d+), (\d+)\), angle=(-?\d+)\)")
PLAYER = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
NAMEID = re.compile(r"id=([a-z0-9_]+) \((\d+)\)")
BAREID = re.compile(r"id=(-?\d+)")
LOCAL = re.compile(r"local player index: (\d+)")
TEKTON = re.compile(r"raids_tekton_(waiting|walking_standard|fighting_standard|walking_enraged|fighting_enraged|hammering)")

SWINGS = {"tekton_attack_stab": "stab", "tekton_slash": "slash", "tekton_hammer_crush": "crush",
          "tekton_attack_stab_enraged": "stab", "tekton_slash_enraged": "slash",
          "tekton_hammer_crush_enraged": "crush"}
SIZE = 4


def num(text, key, default=None):
    m = re.search(r"\b" + key + r"=(-?\d+)", text)
    return int(m.group(1)) if m else default


def name_of(text):
    m = NAMEID.search(text)
    if m:
        return m.group(1)
    m = BAREID.search(text)
    return m.group(1) if m else ""


def coord_of(text):
    m = COORD.search(text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def box_dist(sw, p, size=SIZE):
    """Chebyshev distance from tile p to a size x size footprint at sw (0 = under it)."""
    dx = max(sw[0] - p[0], 0, p[0] - (sw[0] + size - 1))
    dz = max(sw[1] - p[1], 0, p[1] - (sw[1] + size - 1))
    return max(dx, dz)


def side_of(sw, p, size=SIZE):
    """Which edge p is directly outside of: 'N','E','S','W', 'corner', 'under', or None (not adjacent)."""
    x0, z0, x1, z1 = sw[0], sw[1], sw[0] + size - 1, sw[1] + size - 1
    inx, inz = x0 <= p[0] <= x1, z0 <= p[1] <= z1
    if inx and inz:
        return "under"
    if inx and p[1] == z1 + 1:
        return "N"
    if inx and p[1] == z0 - 1:
        return "S"
    if inz and p[0] == x1 + 1:
        return "E"
    if inz and p[0] == x0 - 1:
        return "W"
    if p[0] in (x0 - 1, x1 + 1) and p[1] in (z0 - 1, z1 + 1):
        return "corner"
    return None


def compass(angle):
    """A face angle (0 = south, 512 = west, 1024 = north, 1536 = east) to the nearest of N/E/S/W."""
    a = angle % 2048
    return ["S", "W", "N", "E"][((a + 256) // 512) % 4]


class Fight:
    def __init__(self, path):
        self.path = os.path.basename(path)
        self.local = None
        self.index = None             # tekton's npc index
        self.ev = defaultdict(list)   # tick -> [(kind, detail)] for tekton
        self.sw = {}                  # tick -> tekton SW at the end of tick
        self.form = {}                # tick -> tekton form at the end of tick
        self.me = {}                  # tick -> local player's tile at the end of tick
        self.others = defaultdict(dict)  # tick -> {index: tile} other players seen moving
        self.myhits = defaultdict(list)  # tick -> [(id, value)]
        self.top = defaultdict(list)  # tick -> [(kind, text, subs)]
        self.varbits = defaultdict(list)  # tick -> [(name/id, old, new)]
        self.first = None
        self.last = None
        self.steps = []               # (label, (dx, dz)) of tekton's walk steps, as printed
        self.rot = None               # quarter turns, instance -> template (0 or 2 used)


def parse(path):
    """One Fight per Tekton life in the transcript (by npc index + add)."""
    fights = []
    local = None
    tick = 0
    cur = None          # fight in progress
    block = None
    actor = None        # ('npc', index) / ('player', index)
    top = None
    me_tile = None
    with open(path, encoding="utf-8", errors="replace") as h:
        for raw in h:
            line = raw.rstrip("\n")
            if local is None:
                m = LOCAL.search(line)
                if m:
                    local = m.group(1)
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                top = None
                actor = None
                if cur is not None and me_tile is not None:
                    cur.me.setdefault(tick, me_tile)
                if cur is not None and cur.last is not None and tick > cur.last + 60:
                    cur = None
                continue
            m = TOP.match(line)
            if m:
                block = m.group(1)
                actor = None
                top = (m.group(1), m.group(2), [])
                if cur is not None:
                    cur.top[tick].append(top)
                    if block == "varbit":
                        vm = re.search(r"id=([a-z0-9_]+ \(\d+\)|\d+), old=(-?\d+), new=(-?\d+)", m.group(2))
                        if vm:
                            cur.varbits[tick].append((vm.group(1), int(vm.group(2)), int(vm.group(3))))
                continue
            m = SUB8.match(line)
            if m:
                kind, text = m.group(1), m.group(2)
                if block and block.startswith("npc_info"):
                    nm = NPC.search(text)
                    if nm and TEKTON.search(nm.group(2)):
                        index = nm.group(1)
                        if kind == "add" or cur is None or cur.index != index:
                            if cur is None or cur.index != index or kind == "add":
                                cur = Fight(path)
                                cur.local = local
                                cur.index = index
                                cur.first = tick
                                fights.append(cur)
                                if me_tile is not None:
                                    cur.me[tick] = me_tile
                        sw = (int(nm.group(4)), int(nm.group(5)))
                        nc = NEWCOORD.search(text)
                        end = (int(nc.group(1)), int(nc.group(2))) if nc else sw
                        cur.sw.setdefault(tick - 1, sw)
                        cur.sw[tick] = end
                        cur.form[tick] = nm.group(2)
                        st = re.search(r"step=([a-z-]+)", text)
                        if st and nc:
                            cur.steps.append((st.group(1), (end[0] - sw[0], end[1] - sw[1])))
                        cur.ev[tick].append((kind, "%s %s->%s angle=%s" % (nm.group(2), sw, end, nm.group(7))))
                        cur.last = tick
                        actor = ("tekton", index)
                    else:
                        actor = ("npc", nm.group(1) if nm else None)
                    continue
                if block and block.startswith("player_info"):
                    pm = PLAYER.search(text)
                    if pm:
                        nc = NEWCOORD.search(text)
                        tile = (int(nc.group(1)), int(nc.group(2))) if nc else (int(pm.group(3)), int(pm.group(4)))
                        if pm.group(1) == local:
                            actor = ("me", None)
                            me_tile = tile
                            if cur is not None:
                                cur.me[tick] = tile
                        else:
                            actor = ("player", pm.group(1))
                            if cur is not None:
                                cur.others[tick][pm.group(1)] = tile
                    continue
                if top is not None:
                    top[2].append(line.strip())
                continue
            m = SUB12.match(line)
            if m and cur is not None and actor is not None:
                kind, text = m.group(1), m.group(2)
                if actor[0] == "tekton":
                    if kind == "sequence":
                        cur.ev[tick].append(("seq", "%s%s" % (name_of(text), " delay=%d" % num(text, "delay") if num(text, "delay") else "")))
                    elif kind == "transformation":
                        cur.ev[tick].append(("retype", name_of(text)))
                        cur.form[tick] = name_of(text)
                    elif kind == "face_angle":
                        cur.ev[tick].append(("face", str(num(text, "angle"))))
                    elif kind == "hit":
                        cur.ev[tick].append(("hit", "%s %s" % (num(text, "id"), num(text, "value"))))
                    elif kind in ("spotanim", "face_pathingentity", "headbar", "say"):
                        cur.ev[tick].append((kind, text))
                    else:
                        cur.ev[tick].append((kind, text))
                elif actor[0] == "me" and kind == "hit":
                    cur.myhits[tick].append((num(text, "id"), num(text, "value")))
    for f in fights:
        f.last = max(f.last or f.first, max(f.top) if f.top else 0)
        f.rot = turn_of(f.steps)
        if f.rot == 2:
            f.sw = {t: (x - (SIZE - 1), z - (SIZE - 1)) for t, (x, z) in f.sw.items()}
            f.ev = defaultdict(list, {t: [(k, str((int(d) + 1024) % 2048) if k == "face" and d.lstrip("-").isdigit() else d)
                                          for k, d in rows] for t, rows in f.ev.items()})
        elif f.rot != 0:
            f.sw = {}
    return fights


STEP = {"north": (0, 1), "south": (0, -1), "east": (1, 0), "west": (-1, 0),
        "north-east": (1, 1), "north-west": (-1, 1), "south-east": (1, -1), "south-west": (-1, -1)}


def turn_of(steps):
    """Quarter turns r with rotate(label, r) == the printed delta on every step, else None."""
    def rotate(v, r):
        x, z = v
        for _ in range(r):
            x, z = z, -x
        return (x, z)
    for r in range(4):
        ok = [rotate(STEP[l], r) == d for l, d in steps if l in STEP]
        if ok and all(ok):
            return r
    return None


# ---------------------------------------------------------------- helpers

def tek_seqs(f, pat):
    return [(t, d) for t in sorted(f.ev) for k, d in f.ev[t] if k == "seq" and re.search(pat, d)]


def tek_kind(f, kind, pat=""):
    return [(t, d) for t in sorted(f.ev) for k, d in f.ev[t] if k == kind and re.search(pat, d)]


def form_at(f, t):
    best = None
    for tt in sorted(f.form):
        if tt <= t:
            best = f.form[tt]
    return best


def sw_at(f, t):
    best = None
    for tt in sorted(f.sw):
        if tt <= t:
            best = f.sw[tt]
    return best


def me_at(f, t):
    best = None
    for tt in sorted(f.me):
        if tt <= t:
            best = f.me[tt]
    return best


def varbit_at(f, key, t):
    """The last value set at or before t of a varbit whose label contains `key`."""
    best = None
    for tt in sorted(f.varbits):
        if tt > t:
            break
        for name, old, new in f.varbits[tt]:
            if key in name:
                best = new
    return best


def varbit_first_old(f, key):
    for tt in sorted(f.varbits):
        for name, old, new in f.varbits[tt]:
            if key in name:
                return old
    return None


def pfm_at(f, t):
    v = varbit_at(f, "prayer_protectfrommelee", t)
    if v is None:
        v = varbit_first_old(f, "prayer_protectfrommelee")
    return v


def tops(f, prefix, pat=None):
    out = []
    for t in sorted(f.top):
        for kind, text, subs in f.top[t]:
            if kind.startswith(prefix) and (pat is None or re.search(pat, text)):
                out.append((t, kind, text, subs))
    return out


def party(f):
    v = varbit_at(f, "raids_client_partysize", 10 ** 9)
    if v is None:
        v = varbit_first_old(f, "raids_client_partysize")
    return v


def label(f):
    return "%s@%d" % (f.path, f.first)


# ---------------------------------------------------------------- sections

def timeline(fights):
    """Everything Tekton did, tick by tick, with the room's tells and the local raider's hits."""
    for f in fights:
        print("== %s  tekton index %s, party %s" % (label(f), f.index, party(f)))
        for t in range(f.first, f.last + 1):
            row = []
            for k, d in f.ev.get(t, []):
                if k in ("idle",):
                    continue
                if k in ("headbar",):
                    continue
                if k == "seq" and d.startswith("tekton_ready_defend"):
                    continue
                if k in ("walk", "teleport", "add", "del"):
                    row.append("%s %s" % (k, d.split(" ", 1)[1] if " " in d else d))
                else:
                    row.append("%s=%s" % (k, d))
            for kind, text, subs in f.top.get(t, []):
                if kind.startswith("loc_") and "tekton" in text or kind.startswith("loc_") and "blockage" in text \
                        or kind.startswith("loc_") and ("crystal" in text or "book" in text):
                    if "loc_add_change" in kind and t - f.first > 0 and any(
                            name_of(text) == name_of(tx) for kk, tx, _ in f.top.get(t - 4, []) if kk == kind):
                        continue
                    row.append("%s %s %s" % (kind, name_of(text), coord_of(text)))
                elif kind.startswith("map_projanim") and "meteor" in text:
                    tgt = [s for s in subs if s.startswith("[target]")]
                    row.append("spark st=%s et=%s -> %s" % (num(text, "starttime"), num(text, "endtime"), coord_of(tgt[0]) if tgt else None))
                elif kind == "map_anim" and "meteor" in text:
                    row.append("blast d=%s %s" % (num(text, "delay", 0), coord_of(text)))
                elif kind.startswith("obj_add"):
                    row.append("obj %s" % name_of(text))
                elif kind.startswith("message") and "tekton" in text.lower():
                    row.append(text)
            if f.myhits.get(t):
                row.append("ME hit %s @%s pfm=%s" % (f.myhits[t], me_at(f, t), pfm_at(f, t)))
            if row:
                print("  %6d  %s" % (t, " | ".join(row)))


def sessions(f):
    """[(contact tick, stance seq, [swing ticks], end tick, end kind)] per combat stance."""
    out = []
    contacts = tek_seqs(f, r"^tekton_idle_transition_to_ready")
    leaves = tek_seqs(f, r"^tekton_ready_transition_to_idle")
    leans = tek_seqs(f, r"^tekton_idle_transition_to_anvil")
    deaths = tek_seqs(f, r"^tekton_death")
    swings = [(t, d) for t, d in tek_seqs(f, r"") if d.split()[0] in SWINGS]
    for i, (c, d) in enumerate(contacts):
        nxt = contacts[i + 1][0] if i + 1 < len(contacts) else 10 ** 9
        sw = [t for t, _ in swings if c <= t < nxt]
        # the session ends at the first retype to a walking form after contact
        end = None
        kind = None
        for t, r in tek_kind(f, "retype"):
            if t > c + 1 and r.startswith("raids_tekton_walking") and t < nxt:
                end, kind = t, "walk"
                break
        for t, _ in leaves:
            if c < t < nxt and (end is None or t <= end):
                kind = "leave seq"
                end = t
                break
        for t, _ in deaths:
            if c <= t < nxt and (end is None or t < end):
                end, kind = t, "death"
        out.append((c, d, sw, end, kind))
    return out


def phases(fights):
    """Wake, walk out, contact, the session (swings, budget, enrage), the walk in, the repair, the rise."""
    print("== phases")
    for f in fights:
        print("-- %s party %s" % (label(f), party(f)))
        rise = tek_seqs(f, r"^tekton_avil_transition_to_idle")
        for t, d in rise:
            steps = [tt for tt in sorted(f.ev) if tt > t and any(k == "walk" for k, _ in f.ev[tt])]
            first_step = steps[0] if steps else None
            retype = [tt for tt, r in tek_kind(f, "retype") if tt >= t and r.startswith("raids_tekton_walking")]
            fire = [tt for tt, k, tx, _ in tops(f, "loc_add", "raids_tekton_fire") if tt >= t - 1]
            sw0 = sw_at(f, t - 1)
            me1 = me_at(f, t - 1)
            me2 = me_at(f, t - 2)
            # how close the raider had been BEFORE this tick, while he slept
            closest = None
            if "enraged" not in d:
                for tt in range(f.first, t - 1):
                    p = me_at(f, tt)
                    if p is not None and sw0 is not None:
                        bd = box_dist(sw0, p)
                        closest = bd if closest is None else min(closest, bd)
            print("  rise %-44s @%d: first step +%s, walking form +%s, fire +%s; me end T-1 %s box_dist %s, end T-2 box_dist %s; closest before %s"
                  % (d, t, first_step - t if first_step else None, (retype[0] - t) if retype else None,
                     (fire[0] - t) if fire else None, me1, box_dist(sw0, me1) if me1 and sw0 else None,
                     box_dist(sw0, me2) if me2 and sw0 else None, closest))
        for c, d, sw, end, kind in sessions(f):
            gaps = [b - a for a, b in zip(sw, sw[1:])]
            retypes = [(tt - c, r) for tt, r in tek_kind(f, "retype") if c - 1 <= tt <= (end or c + 200)]
            wb = [tt - c for tt, k, tx, _ in tops(f, "loc_add", "raids_tekton_walkblocker") if c - 1 <= tt <= c + 3]
            wbdel = [tt - c for tt, k, tx, _ in tops(f, "loc_del") if c <= tt <= (end or c) + 2]
            print("  contact %-40s @%d  first swing +%s, swings %d, gaps %s, end %s (%s) +%s after the last swing; retypes %s; walkblocker +%s"
                  % (d, c, (sw[0] - c) if sw else None, len(sw), dict(Counter(gaps)), (end - c) if end else None, kind,
                     (end - sw[-1]) if end and sw else None, retypes, wb))
        for t, d in tek_seqs(f, r"^tekton_idle_transition_to_anvil"):
            retype = [tt - t for tt, r in tek_kind(f, "retype") if t <= tt <= t + 2]
            rise_after = [tt - t for tt, _ in rise if tt > t]
            print("  lean @%d  retype +%s, rise +%s" % (t, retype, rise_after[0] if rise_after else None))


def swings(fights):
    """Each swing: the facing, the raider's edge, and the hitsplat that follows -- per style,
    because a slash lands a tick later than a stab or a crush."""
    print("== swings")
    off = Counter()
    by_side = Counter()
    dmg = defaultdict(list)
    prev_tab = Counter()
    for f in fights:
        rows = [(t, d) for t, d in tek_seqs(f, r"") if d.split()[0] in SWINGS]
        last_face = None
        for t, d in rows:
            style = SWINGS[d.split()[0]]
            # the facing he held going INTO this swing: the last face_angle sent before tick t
            faces_before = [int(x) for tt in sorted(f.ev) if tt < t for k, x in f.ev[tt] if k == "face" and x.lstrip("-").isdigit()]
            held = compass(faces_before[-1]) if faces_before else None
            face = [int(x) for k, x in f.ev.get(t, []) if k == "face" and x.lstrip("-").isdigit()]
            facing = compass(face[0]) if face else None
            sw = sw_at(f, t)
            sides = []
            for dt in (-1, 0, 1, 2):
                p = me_at(f, t + dt)
                sides.append(side_of(sw, p) if p and sw else None)
            hits = []
            for dt in (1, 2, 3):
                for hid, val in f.myhits.get(t + dt, []):
                    if hid in (12, 16):
                        hits.append((dt, hid, val))
            for dt, hid, val in hits:
                off[(style, dt)] += 1
            print("  %s %6d %-28s face=%s(%s) me T-1/T/T+1/T+2 %s  hits %s pfm=%s"
                  % (f.path, t, d, facing, face[0] if face else None, sides, hits, pfm_at(f, t + 1)))
            if not facing:
                continue
            # read the edge at the end of every candidate tick; the strike lands at T+delay
            delay = 2 if style == "slash" else 1
            for read in (-1, 0, 1):
                side = sides[read + 1]
                if side not in ("N", "E", "S", "W"):
                    rel = side or "away"
                else:
                    rel = ["front", "right", "behind", "left"][(("N", "E", "S", "W").index(side) - ("N", "E", "S", "W").index(facing)) % 4]
                landed = [v for dt, hid, v in hits if dt == delay]
                by_side[(style, "read end T%+d" % read if read else "read end T", rel, "hit" if landed else "none")] += 1
            side = sides[1]
            if held and side in ("N", "E", "S", "W"):
                rel = ["front", "right", "behind", "left"][(("N", "E", "S", "W").index(side) - ("N", "E", "S", "W").index(held)) % 4]
                prev_tab[(style, rel, "hit" if [v for dt, hid, v in hits if dt == delay] else "none")] += 1
            for v in [v for dt, hid, v in hits if dt == delay and hid == 16]:
                dmg[(f.path, "red" if "enraged" in d else "orange", "pfm" if pfm_at(f, t + delay) else "off")].append(v)
    print("  my hitsplats after a swing, (style, delay):", dict(sorted(off.items())))
    print("  the raider's edge vs his facing -> a hitsplat on the style's landing tick (stab/crush T+1, slash T+2):")
    for k in sorted(by_side):
        print("    %-7s %-12s %-7s %-5s %d" % (k + (by_side[k],)))
    print("  the raider's edge at the end of T vs the facing he HELD going into the swing (the face_angle before T):")
    for k in sorted(prev_tab):
        print("    %-7s %-7s %-5s %d" % (k + (prev_tab[k],)))
    for k, v in sorted(dmg.items()):
        print("  damage %s: n=%d max=%d values=%s" % (k, len(v), max(v), sorted(v)))


def sparks(fights):
    """Volleys: when (from the lean), how many, from where, at what, how long they fly, what they hit."""
    print("== sparks")
    lean_off = Counter()
    per_volley = Counter()
    flight = defaultdict(list)
    target_vs = Counter()
    blast_dt = Counter()
    blast_delay = Counter()
    hit_dist = Counter()
    hits = []
    for f in fights:
        leans = [t for t, _ in tek_seqs(f, r"^tekton_idle_transition_to_anvil")]
        throws = tops(f, "map_projanim", r"meteor_flying")
        blasts = tops(f, "map_anim", r"meteor_blast")
        by_tick = defaultdict(list)
        for t, kind, text, subs in throws:
            src = tgt = None
            for s in subs:
                if s.startswith("[source]"):
                    src = coord_of(s)
                elif s.startswith("[target]"):
                    tgt = coord_of(s)
            by_tick[t].append((src, tgt, num(text, "starttime"), num(text, "endtime"), num(text, "angle"),
                               num(text, "progress"), num(text, "startheight"), num(text, "endheight")))
        for a in leans:
            vt = sorted(t for t in by_tick if a <= t <= a + 30)
            lean_off[tuple(t - a for t in vt)] += 1
            print("  %s lean %d: volleys at %s" % (f.path, a, [t - a for t in vt]))
        for t, rows in sorted(by_tick.items()):
            per_volley[len(rows)] += 1
            for src, tgt, st, et, ang, prog, sh, eh in rows:
                d = cheb(src, tgt) if src and tgt else None
                flight[d].append((st, et))
                me_prev, me_now = me_at(f, t - 1), me_at(f, t)
                if tgt in (me_prev, me_now) and me_prev != me_now:
                    target_vs["moved on T: target = end of T-1" if tgt == me_prev else "moved on T: target = end of T"] += 1
                elif tgt == me_now:
                    target_vs["still: my tile"] += 1
                # landing: the blast on the target tile
                for bt, kind, text, subs in blasts:
                    if coord_of(text) == tgt and t < bt <= t + 8:
                        blast_dt[(et // 30 if et else None, bt - t)] += 1
                        blast_delay[(et, num(text, "delay", 0))] += 1
                        p_prev = me_at(f, bt - 1)
                        p_now = me_at(f, bt)
                        got = [v for hid, v in f.myhits.get(bt, []) + f.myhits.get(bt + 1, []) if hid in (12, 16)]
                        if p_prev is not None:
                            hits.append((f.path, t, bt, tgt, cheb(tgt, p_prev), cheb(tgt, p_now), got))
                        break
    print("  volley offsets from the lean:", dict(lean_off))
    print("  sparks per volley:", dict(per_volley))
    print("  flight (cheb distance source->target: [(starttime, endtime)]):")
    for d in sorted(k for k in flight if k is not None):
        ets = [e for _, e in flight[d]]
        sts = [s for s, _ in flight[d]]
        print("    d=%2d n=%2d endtime %s..%s  starttime %s..%s  endtime-starttime %s"
              % (d, len(ets), min(ets), max(ets), min(sts), max(sts), sorted(set(e - s for s, e in flight[d]))))
    print("  target:", dict(target_vs))
    print("  blast tick - throw tick by endtime//30:", dict(sorted(blast_dt.items(), key=repr)))
    print("  (endtime, blast delay):", dict(sorted(blast_delay.items(), key=repr)))
    hd = Counter()
    vals = []
    for path, t, bt, tgt, d_prev, d_now, got in hits:
        hd[(d_prev, d_now, "hit" if got else "-")] += 1
        if got and d_prev <= 1:
            vals.extend(got)
    print("  landing vs me (cheb to the blast at end of land-1, end of land, hitsplat on land or land+1):",
          dict(sorted(hd.items())))
    print("  spark damage on me (blast within 1 at end of land-1): %s" % sorted(vals))


def heals(fights):
    """His heal hitsplats at the anvil, against the HUD bar."""
    print("== heals")
    for f in fights:
        base = varbit_at(f, "hpbar_hud_basehp", 10 ** 9) or varbit_first_old(f, "hpbar_hud_basehp")
        for a, _ in tek_seqs(f, r"^tekton_idle_transition_to_anvil"):
            rise = [t for t, _ in tek_seqs(f, r"^tekton_avil_transition_to_idle_enraged") if t > a]
            end = rise[0] if rise else a + 30
            hs = [(t - a, d) for t, d in tek_kind(f, "hit") if a <= t <= end + 2 and d.startswith("6 ")]
            print("  %s lean %d base %s: heals %s (rise +%s)" % (f.path, a, base, hs, end - a))


def hits(fights):
    """Every hitsplat on the local raider in the room, with its cause guess."""
    print("== hits on the raider")
    small = Counter()
    for f in fights:
        swing_ticks = set(t for t, d in tek_seqs(f, r"") if d.split()[0] in SWINGS)
        blast_ticks = set(t for t, k, tx, s in tops(f, "map_anim", r"meteor_blast"))
        for t in sorted(f.myhits):
            for hid, val in f.myhits[t]:
                cause = "swing" if (t - 1) in swing_ticks else ("spark" if t in blast_ticks or (t - 1) in blast_ticks else "?")
                sw = sw_at(f, t)
                p = me_at(f, t - 1)
                if cause == "?":
                    small[(val, form_at(f, t), box_dist(sw, p) if sw and p else None)] += 1
                    print("  %s %6d id=%s %3s  me %s box %s  form %s" % (f.path, t, hid, val, p, box_dist(sw, p) if sw and p else None, form_at(f, t)))
    print("  unexplained:", dict(small))


def death(fights):
    print("== death")
    for f in fights:
        for t, d in tek_seqs(f, r"^tekton_death"):
            print("-- %s death seq @%d (%s)" % (f.path, t, form_at(f, t)))
            for tt in range(t - 2, t + 12):
                for kind, text, subs in f.top.get(tt, []):
                    if kind.startswith(("loc_", "obj_", "message", "midi", "sound_area", "synth")) and \
                            not ("loc_add_change" in kind and ("raids_blockage" in text or "raids_tekton_fire" in text) and tt < t):
                        print("    +%d %s %s" % (tt - t, kind, text[:150]))
                for k, d in f.ev.get(tt, []):
                    if k in ("del", "seq", "retype", "hit", "spotanim"):
                        print("    +%d tekton %s %s" % (tt - t, k, d[:120]))


def main():
    secs = ["timeline", "phases", "swings", "sparks", "heals", "hits", "death"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append", choices=secs)
    ap.add_argument("transcripts", nargs="+")
    args = ap.parse_args()
    fights = []
    for p in args.transcripts:
        fights.extend(parse(p))
    for s in args.section or [x for x in secs if x != "timeline"]:
        globals()[s](fights)


if __name__ == "__main__":
    main()
