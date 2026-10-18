#!/usr/bin/env python3
"""Vasa Nistirio as Jagex's server sent it, read out of RSProx transcripts.

Every Vasa fight in the given transcripts, from the rocks waking to his
death: the special (teleport + explosion) tick by tick, every boulder against
where the raider stood, the crystal cycle (pick, walk, siphon, heal, break),
the stomp, and the death. Ticks print relative to the event that defines the
section (S = the special's `vasa_stun_spawn`, B = the boulder's launch, P = a
crystal's add).

    python3 tools/cox/rsprox_vasa.py ~/.rsprox/binary/archive/{3285,843,3282,820}.txt
    python3 tools/cox/rsprox_vasa.py --section boulders FILES...

Heights print in this tree's units (raw / 4 for the rev-234 v2 projectile
packet). Coordinates are absolute; "rel" is relative to Vasa's south-west tile
(he is 5x5, his middle is +2,+2), "mid" a Chebyshev distance to his middle.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

VASA = re.compile(r"raids_vasanistirio_(dormant|walking|healing)\b")
CRYSTAL = "raids_vasanistirio_crystal"
TICK = re.compile(r"^\[(\d+)\]")
LOCAL = re.compile(r"local player index: (\d+)")
TOP = re.compile(r"^    \[(\w+)\] ?(.*)$")
ENT = re.compile(r"^        \[(\w+)\] (npc|player)=\(index=(\d+), (?:id=)?([^,]*?)(?: \((\d+)\))?, coord=\((\d+), (\d+), (\d+)\)")
NEWC = re.compile(r"newcoord=\((\d+), (\d+), \d+\)")
SUB = re.compile(r"^            \[(\w+)\] ?(.*)$")
COORD = re.compile(r"coord=\((\d+), (\d+), \d+\)")
IDNAME = re.compile(r"id=([A-Za-z0-9_]+)")


def num(text, key):
    m = re.search(r"\b" + key + r"=(-?\d+)", text)
    return int(m.group(1)) if m else None


def name_of(text):
    m = IDNAME.search(text)
    return m.group(1) if m else "?"


class T:
    """Everything that happened on one tick that this reader keeps."""

    def __init__(self):
        self.me = None            # (x, z) at the end of the tick
        self.me_from = None
        self.me_tele = False
        self.me_hits = []         # values
        self.me_seq = []
        self.me_spot = []
        self.hp = None            # hitpoints level after the tick's stats
        self.npc = {}             # index -> dict(name, at, to, seq[], spot[], hits[], retype, add, del)
        self.proj = []            # (name, start, end, src, tgt, tgt_npc)
        self.anim = []            # (name, delay, coord)
        self.locs = []            # (kind, name, coord, rot)
        self.msgs = []
        self.sounds = []          # (id, delay, range, coord)
        self.varbits = []         # (name, old, new)


def parse(path):
    ticks = defaultdict(T)
    local = None
    tick = 0
    block = None
    ent = None        # (kind, index) of the entity the 12-space lines belong to
    top = None        # last top line (kind, text) for its 8-space subs
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if local is None:
                m = LOCAL.search(line)
                if m:
                    local = int(m.group(1))
                    continue
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                block = ent = top = None
                continue
            m = TOP.match(line)
            if m:
                block, text = m.group(1), m.group(2)
                ent = None
                top = [block, text, []]
                t = ticks[tick]
                if block.startswith("map_projanim"):
                    t.proj.append(top)
                elif block == "map_anim":
                    t.anim.append((name_of(text), num(text, "delay") or 0, COORD.search(text) and tuple(map(int, COORD.search(text).groups()))))
                elif block.startswith("loc_add_change") or block == "loc_del":
                    c = COORD.search(text)
                    t.locs.append((block, name_of(text), c and tuple(map(int, c.groups())), num(text, "rotation")))
                elif block == "message_game":
                    mm = re.search(r'message="(.*)"', text)
                    t.msgs.append(mm.group(1) if mm else text)
                elif block == "sound_area":
                    c = COORD.search(text)
                    t.sounds.append((num(text, "id"), num(text, "delay") or 0, num(text, "range"), c and tuple(map(int, c.groups()))))
                elif block == "varbit":
                    mm = re.search(r"id=(\w+) \(\d+\), old=(-?\d+), new=(-?\d+)", text)
                    if mm:
                        t.varbits.append((mm.group(1), int(mm.group(2)), int(mm.group(3))))
                elif block == "update_stat_v2" and "stat=hitpoints" in text:
                    t.hp = num(text, "level")
                continue
            m = ENT.match(line)
            if m and block and (block.startswith("npc_info") or block.startswith("player_info")):
                kind, who, index = m.group(1), m.group(2), int(m.group(3))
                x, z = int(m.group(6)), int(m.group(7))
                nc = NEWC.search(line)
                to = (int(nc.group(1)), int(nc.group(2))) if nc else (x, z)
                t = ticks[tick]
                if who == "player":
                    ent = ("player", index)
                    if index == local:
                        t.me_from, t.me = (x, z), to
                        t.me_tele = kind == "teleport"
                else:
                    ent = ("npc", index)
                    d = t.npc.setdefault(index, {"name": m.group(4), "at": (x, z), "to": to, "seq": [], "spot": [],
                                                 "hits": [], "retype": None, "kind": kind})
                    d["to"] = to
                continue
            m = SUB.match(line)
            if m and ent is not None:
                sub, text = m.group(1), m.group(2)
                t = ticks[tick]
                if ent[0] == "player" and ent[1] == local:
                    if sub == "hit":
                        t.me_hits.append(num(text, "value"))
                    elif sub == "sequence":
                        t.me_seq.append(name_of(text))
                    elif sub == "spotanim":
                        t.me_spot.append(name_of(text))
                elif ent[0] == "npc":
                    d = t.npc[ent[1]]
                    if sub == "sequence":
                        d["seq"].append(name_of(text) + (" d=%d" % num(text, "delay") if num(text, "delay") else ""))
                    elif sub == "transformation":
                        d["retype"] = name_of(text)
                    elif sub == "hit":
                        d["hits"].append((num(text, "id"), num(text, "value")))
                    elif sub == "spotanim":
                        d["spot"].append(name_of(text))
                continue
            if top is not None and line.startswith("        ["):
                top[2].append(line.strip())
    # flatten projectiles
    for t in ticks.values():
        rows = []
        for kind, text, subs in t.proj:
            src = tgt = None
            tgt_npc = None
            for s in subs:
                c = COORD.search(s)
                if s.startswith("[source]") and c:
                    src = tuple(map(int, c.groups()))
                elif s.startswith("[target]") and c:
                    tgt = tuple(map(int, c.groups()))
                    if "npc=" in s:
                        tgt_npc = name_of(s.split("npc=", 1)[1])
            rows.append((name_of(text), num(text, "starttime"), num(text, "endtime"), src, tgt, tgt_npc))
        t.proj = rows
    return ticks


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def foot(sw, size, p):
    """Chebyshev distance from p to a size x size footprint at sw (0 = inside)."""
    dx = max(sw[0] - p[0], 0, p[0] - (sw[0] + size - 1))
    dz = max(sw[1] - p[1], 0, p[1] - (sw[1] + size - 1))
    return max(dx, dz)


class Fight:
    def __init__(self, path, ticks, index, start, end):
        self.path, self.ticks, self.index, self.start, self.end = path, ticks, index, start, end

    def vasa(self, tick):
        return self.ticks[tick].npc.get(self.index) if tick in self.ticks else None

    def vasa_sw(self, tick):
        best = None
        for t in range(self.start, tick + 1):
            d = self.vasa(t)
            if d:
                best = d["to"]
        return best

    def me(self, tick):
        best = None
        for t in range(self.start - 60, tick + 1):
            if t in self.ticks and self.ticks[t].me:
                best = self.ticks[t].me
        return best

    def hp(self, tick):
        best = None
        for t in range(self.start - 200, tick + 1):
            if t in self.ticks and self.ticks[t].hp is not None:
                best = self.ticks[t].hp
        return best

    def rng(self):
        return range(self.start, self.end + 1)


def fights(path):
    ticks = parse(path)
    found = {}
    for tick in sorted(ticks):
        for index, d in ticks[tick].npc.items():
            if VASA.match(d["name"]) or (d["retype"] and VASA.match(d["retype"])):
                f = found.get(index)
                if f is None:
                    found[index] = [tick, tick]
                else:
                    f[1] = tick
    return [Fight(os.path.basename(path), ticks, i, s, e) for i, (s, e) in found.items()]


# ---------------------------------------------------------------- sections

def section_special(fs):
    print("== special: ticks from S (vasa_stun_spawn)")
    for f in fs:
        wake = None
        for t in f.rng():
            d = f.vasa(t)
            if d and "vasa_spawn" in d["seq"]:
                wake = t
        for s in f.rng():
            d = f.vasa(s)
            if not d or "vasa_stun_spawn" not in d["seq"]:
                continue
            sw = f.vasa_sw(s)
            mid = (sw[0] + 2, sw[1] + 2)
            print("-- %s vasa %d  S=%d%s  vasa sw %s  form %s -> %s" % (
                f.path, f.index, s, "  (wake S-%d)" % (s - wake) if wake and wake < s and s - wake < 20 else "",
                sw, d["name"], d["retype"]))
            hp0 = f.hp(s - 1)
            for t in range(s - 1, s + 22):
                tt = f.ticks.get(t)
                if not tt:
                    continue
                bits = []
                v = f.vasa(t)
                if v and (v["seq"] or v["retype"] or v["spot"]):
                    bits.append("vasa seq=%s%s%s" % (",".join(v["seq"]), " ->%s" % v["retype"] if v["retype"] else "",
                                                       " spot=%s" % ",".join(v["spot"]) if v["spot"] else ""))
                if v and v["hits"]:
                    bits.append("vasa hits=%s" % v["hits"])
                if tt.me_seq or tt.me_spot:
                    bits.append("me seq=%s spot=%s" % (",".join(tt.me_seq), ",".join(tt.me_spot)))
                if tt.me_tele:
                    bits.append("me TELEPORT %s -> %s (mid %d -> %d)" % (tt.me_from, tt.me, cheb(tt.me_from, mid), cheb(tt.me, mid)))
                if tt.me_hits:
                    bits.append("me hit %s (hp before %s)" % (tt.me_hits, f.hp(t - 1)))
                pv = [b for b in tt.varbits if b[0].startswith("prayer_allactive")]
                if pv:
                    bits.append("prayers %s->%s" % (pv[0][1], pv[-1][2]))
                for p in tt.proj:
                    if "magic_travel" in p[0]:
                        bits.append("proj %s %d..%d tgt rel %s" % (p[0][19:], p[1], p[2], (p[4][0] - sw[0], p[4][1] - sw[1])))
                for a in tt.anim:
                    if "vasanistirio" in a[0]:
                        bits.append("anim %s d=%d rel %s" % (a[0][19:], a[1], a[2] and (a[2][0] - sw[0], a[2][1] - sw[1])))
                for m in tt.msgs:
                    if "Vasa" in m:
                        bits.append('msg "%s"' % m[:60])
                for so in tt.sounds:
                    bits.append("sound %s d=%s r=%s" % so[:3])
                if bits:
                    print("   S%+d  %s" % (t - s, " | ".join(bits)))
            print("   hp at S-1: %s; me at S-1 %s (mid %s)" % (hp0, f.me(s - 1), f.me(s - 1) and cheb(f.me(s - 1), mid)))


def section_boulders(fs):
    print("== boulders: B = launch tick")
    gaps = Counter()
    flights = Counter()
    hit_rows = []
    for f in fs:
        last = None
        for b in f.rng():
            tt = f.ticks.get(b)
            if not tt:
                continue
            for p in tt.proj:
                if "range_travel" not in p[0]:
                    continue
                if last is not None and b != last:
                    gaps[b - last] += 1
                last = b
                flights[(p[1], p[2])] += 1
                tgt = p[4]
                me_prev, me_now = f.me(b - 1), f.me(b)
                hits = []
                for dt in range(0, 5):
                    t2 = f.ticks.get(b + dt)
                    if t2:
                        for a in t2.anim:
                            if "range_impact" in a[0]:
                                hits.append("impact@B+%d d=%d" % (dt, a[1]))
                        if t2.me_hits:
                            hits.append("me hit@B+%d %s (at %s, from tgt %s)" % (
                                dt, t2.me_hits, f.me(b + dt - 1), f.me(b + dt - 1) and cheb(f.me(b + dt - 1), tgt)))
                sw = f.vasa_sw(b)
                hit_rows.append((f.path, b, p[1], p[2], tgt, me_prev, me_now, sw and foot(sw, 5, me_prev or tgt), hits))
    for row in hit_rows:
        print("   %s B=%d flight %s..%s tgt %s me@B-1 %s me@B %s (me from his footprint %s)  %s" % (
            row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7], "; ".join(row[8])))
    print("   flights:", dict(flights))
    print("   gaps between boulders:", sorted(gaps.items()))


def section_crystals(fs):
    print("== crystals: P = the crystal npc's add")
    for f in fs:
        crystals = []
        for t in f.rng():
            tt = f.ticks.get(t)
            if not tt:
                continue
            for i, d in tt.npc.items():
                if d["name"] == CRYSTAL and d["kind"] == "add":
                    crystals.append((t, i, d["to"]))
        for p, ci, csw in crystals:
            print("-- %s crystal npc %d at %s, P=%d; vasa sw %s" % (f.path, ci, csw, p, f.vasa_sw(p)))
            moved_last = None
            for t in range(p - 3, min(p + 90, f.end + 3)):
                tt = f.ticks.get(t)
                if not tt:
                    continue
                bits = []
                v = f.vasa(t)
                if v:
                    if v["at"] != v["to"]:
                        moved_last = t
                    if v["seq"] or v["retype"]:
                        bits.append("vasa seq=%s%s @%s" % (",".join(v["seq"]), " ->%s" % v["retype"] if v["retype"] else "", v["to"]))
                    if v["hits"]:
                        bits.append("vasa hits=%s" % v["hits"])
                c = tt.npc.get(ci)
                if c:
                    if c["kind"] in ("add", "del"):
                        bits.append("crystal %s" % c["kind"])
                    if c["retype"]:
                        bits.append("crystal ->%s" % c["retype"])
                    if c["seq"]:
                        bits.append("crystal seq=%s" % ",".join(c["seq"]))
                    if c["hits"]:
                        bits.append("crystal hits=%s" % c["hits"])
                for l in tt.locs:
                    if "crystal_on" in l[1] or ("crystal_off" in l[1] and l[2] == csw):
                        if l[0] != "loc_add_change_v1" or "crystal_on" in l[1]:
                            pass
                if bits:
                    print("   P%+d  %s" % (t - p, " | ".join(bits)))
            print("   vasa's last step before P+90: %s" % (moved_last and "P%+d" % (moved_last - p)))


def section_walk(fs):
    print("== walk: Vasa's tile changes")
    for f in fs:
        path = []
        for t in f.rng():
            v = f.vasa(t)
            if v and v["at"] != v["to"]:
                path.append((t, v["at"], v["to"], v["kind"]))
        runs = []
        for t, a, b, k in path:
            if runs and runs[-1][1] == t - 1:
                runs[-1][1] = t
                runs[-1][3] = b
            else:
                runs.append([t, t, a, b])
        for s, e, a, b in runs:
            print("   %s %d..%d (%d ticks) %s -> %s" % (f.path, s, e, e - s + 1, a, b))


def section_stomp(fs):
    print("== stomp: hits on me while under his 5x5 (no boulder impact that tick)")
    rows = Counter()
    for f in fs:
        for t in f.rng():
            tt = f.ticks.get(t)
            if not tt or not tt.me_hits:
                continue
            sw = f.vasa_sw(t - 1)
            me = f.me(t - 1)
            if sw and me and foot(sw, 5, me) == 0:
                for v in tt.me_hits:
                    rows[v] += 1
                print("   %s t=%d me %s vasa sw %s hits %s" % (f.path, t, me, sw, tt.me_hits))
    print("   values:", sorted(rows.items()))


def section_death(fs):
    print("== death")
    for f in fs:
        for t in f.rng():
            v = f.vasa(t)
            if v and any("death" in s for s in v["seq"]):
                print("-- %s vasa %d death seq at %d" % (f.path, f.index, t))
                for dt in range(0, 8):
                    tt = f.ticks.get(t + dt)
                    if not tt:
                        continue
                    bits = [("loc %s %s" % (l[0], l[1])) for l in tt.locs if "fire" in l[1] or "crystal_on" in l[1] or "smash" in l[1]]
                    bits += ["msg %s" % m[:70] for m in tt.msgs if "Vasa" in m]
                    bits += ["anim %s" % a[0] for a in tt.anim if "vasa" in a[0] or "smash" in a[0]]
                    for i, d in tt.npc.items():
                        if d["kind"] == "del" or (i == f.index and d["seq"]):
                            bits.append("npc %s %s %s" % (d["name"], d["kind"], ",".join(d["seq"])))
                    if bits:
                        print("   D%+d %s" % (dt, " | ".join(bits)))


SECTIONS = {"special": section_special, "boulders": section_boulders, "crystals": section_crystals,
            "walk": section_walk, "stomp": section_stomp, "death": section_death}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS))
    args = ap.parse_args()
    fs = []
    for p in args.transcripts:
        fs.extend(fights(p))
    for name, fn in SECTIONS.items():
        if args.section in (None, name):
            fn(fs)


if __name__ == "__main__":
    main()
