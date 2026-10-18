#!/usr/bin/env python3
"""Tombs of Amascut as Jagex's server sent it, room by room, out of RSProx
transcripts.

Every packet is filed under the room the recording player stands in (the
rooms are fixed template regions, so coordinates are real and comparable
between raids). A visit is one unbroken stay in a room. For each room the
inventory lists, over every visit:

  npcs        per type: adds (and where), sequences, graphics, retypes, hits,
              tints, forced moves, say, ops/level changes, and the gap between
              repeats of each sequence on the same npc
  projectiles id, thrower, target, every parameter, flight against distance
  graphics    map_anim / map_anim_specific with height and delay, and tiles
  locs        loc_add_change / loc_del / loc_anim by loc, shape, rotation, tile
  objs        obj_add / obj_del
  sounds      sound_area, synth_sound, midi_song, midi_jingle
  interfaces  opentop/opensub/closesub/sethide/settext/setcolour/setevents/
              setanim/setnpchead/setobject/movesub, runclientscript
  vars        varbit / varp changes
  camera      cam_*, minimap_toggle
  messages    game messages
  player      the recording player's sequences, graphics, tints, moves, hits

    python3 tools/rsprox_toa.py FILES... [--room NAME ...] [--timeline]
        [--visit FILE:N] [--out DIR]

--timeline prints each kept visit tick by tick, ticks relative to its entry.
--out writes <room>.out per room into DIR instead of printing.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
CONTENT = os.path.join(HERE, "..", "OSRS-Content", "osrs239-content")

ROOMS = {
    (55, 80): "nexus",
    (61, 82): "crondis", (61, 84): "zebak",
    (55, 82): "scabaras", (55, 84): "kephri",
    (57, 82): "het", (57, 84): "akkha",
    (59, 82): "apmeken", (59, 84): "baba",
    (59, 80): "wardens_p1p2", (61, 80): "wardens_p3",
    (57, 80): "loot",
}
ORDER = ["nexus", "crondis", "zebak", "scabaras", "kephri", "het", "akkha",
         "apmeken", "baba", "wardens_p1p2", "wardens_p3", "loot"]

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
TOP = re.compile(r"^ {4}\[(\w+)\] ?(.*)$")
SUB8 = re.compile(r"^ {8}\[(\w+)\] ?(.*)$")
SUB12 = re.compile(r"^ {12}\[(\w+)\] ?(.*)$")
REF = r"(?:id=([a-z0-9_]+) \((-?\d+)\)|id=(-?\d+)\b|[^(),]*\(id=(\d+)\))"
NPC_HEAD = re.compile(r"npc=\(index=(\d+), " + REF + r", coord=\((\d+), (\d+), (\d+)\)")
PLAYER_HEAD = re.compile(r"player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)\)")
NEWCOORD = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
ID = re.compile(r"\bid=(?:([a-z0-9_]+) \((-?\d+)\)|(-?\d+))")
COM = re.compile(r"com=(\d+):(\d+)")
KV = re.compile(r"(\w+)=(-?\d+(?:,\d{3})*(?:ms)?|true|false|\[[^\]]*\]|\([^)]*\))")


def load_table(path):
    table = {}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                key, _, value = line.rstrip("\n").partition("=")
                if key.isdigit():
                    table[int(key)] = value.split(" ")[0]
    except OSError:
        pass
    return table


NAMES = {}
for space, rel in (("seq", "configs/all.seq.compack"), ("spotanim", "configs/all.spotanim.compack"),
                   ("npc", "configs/all.npc.compack"), ("loc", "configs/all.loc.compack"),
                   ("obj", "configs/all.obj.compack"), ("varbit", "configs/all.varbit.compack"),
                   ("varp", "configs/all.varp.compack"), ("if", "pack/3_interfaces.pack"),
                   ("synth", "pack/4_soundeffects.pack"), ("script", "pack/12_clientscripts.pack"),
                   ("song", "pack/6_musictracks.pack"), ("jingle", "pack/11_musicjingles.pack")):
    NAMES[space] = load_table(os.path.join(CONTENT, rel))


def nm(space, n):
    """`name(id)`, or the bare id when our tables do not know it."""
    if n is None:
        return "?"
    n = int(n)
    if n < 0:
        return str(n)
    name = NAMES[space].get(n)
    return "%s(%d)" % (name, n) if name else str(n)


def id_of(text):
    m = ID.search(text)
    if not m:
        return None
    return int(m.group(2) if m.group(2) is not None else m.group(3))


def kv(text):
    return {k: v for k, v in KV.findall(text)}


def com(text):
    m = COM.search(text)
    if not m:
        return "?"
    return "%s:%s" % (nm("if", m.group(1)), m.group(2))


def coord(text):
    m = COORD.search(text)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def npc_ref(m, start):
    g = m.groups()
    n = g[start + 1] or g[start + 2] or g[start + 3]
    return int(n)


class Visit:
    def __init__(self, path, room, start):
        self.path = path
        self.room = room
        self.start = start
        self.end = start
        self.events = []          # (tick, cat, key, detail)
        self.npcs = {}            # index -> type name (latest)
        self.seq_ticks = defaultdict(list)   # (type, seq) -> [(index, tick)]
        self.players = Counter()
        self.done = []

    def add(self, tick, cat, key, detail=""):
        self.events.append((tick, cat, key, detail))


def parse(path):
    """Yield visits of one transcript."""
    me = None
    tick = 0
    room = None
    visit = None
    visits = []
    block = None            # current top-level packet kind
    top_text = None
    proj = None             # pending projectile (text, [sub lines])
    who = None              # ("npc", index, type, xyz) or ("player", local?, xyz)
    npc_type = {}           # index -> npc id (numeric) as last seen
    npc_at = {}

    def flush_proj():
        nonlocal proj
        if proj is None or visit is None:
            proj = None
            return
        text, subs = proj
        f = kv(text)
        pid = id_of(text)
        src = dst = None
        thrower = target = "?"
        for s in subs:
            if s.startswith("[source]"):
                src = coord(s)
                mm = NPC_HEAD.search(s)
                if mm:
                    thrower = nm("npc", npc_ref(mm, 1))
                elif "player=" in s:
                    thrower = "player"
                else:
                    thrower = "tile"
            elif s.startswith("[target]"):
                dst = coord(s)
                mm = NPC_HEAD.search(s)
                if mm:
                    target = nm("npc", npc_ref(mm, 1))
                elif "player=" in s:
                    target = "player"
                else:
                    target = "tile"
        st, en = int(f.get("starttime", "0")), int(f.get("endtime", "0"))
        sh, eh = int(f.get("startheight", "0")), int(f.get("endheight", "0"))
        if "v2" in block_name_of_proj[0]:
            sh, eh = sh // 4, eh // 4      # v2 heights are raw; ours are /4
        d = cheb(src, dst) if src and dst else -1
        key = "%s %s -> %s" % (nm("spotanim", pid), thrower, target)
        detail = "start=%d end=%d flight=%d angle=%s slope=%s h=%d->%d dist=%d" % (
            st, en, en - st, f.get("angle"), f.get("progress"), sh, eh, d)
        visit.add(tick, "projectile", key, detail)
        if src and dst:
            visit.add(tick, "projtile", key, "(%d,%d) -> (%d,%d)" % (src[0], src[1], dst[0], dst[1]))
        proj = None

    block_name_of_proj = [""]

    def room_of(x, z):
        return ROOMS.get((x >> 6, z >> 6))

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if me is None:
                m = ME.search(line)
                if m:
                    me = int(m.group(1))
            m = TICK.match(line)
            if m and line.startswith("["):
                flush_proj()
                tick = int(m.group(1))
                block = None
                who = None
                if visit is not None:
                    visit.end = tick
                continue
            if not line.strip():
                continue
            m = TOP.match(line)
            if m:
                flush_proj()
                block, top_text = m.group(1), m.group(2)
                who = None
                if block.startswith("map_projanim"):
                    block_name_of_proj[0] = block
                    proj = (top_text, [])
                    continue
                if visit is not None:
                    top_event(visit, tick, block, top_text)
                continue
            if proj is not None and line.startswith("        ["):
                proj[1].append(line.strip())
                continue
            m = SUB8.match(line)
            if m and block and (block.startswith("npc_info") or block == "player_info"):
                kind, text = m.group(1), m.group(2)
                mp = PLAYER_HEAD.search(text)
                mn = NPC_HEAD.search(text) if not mp else None
                if mp:
                    index = int(mp.group(1))
                    xyz = (int(mp.group(3)), int(mp.group(4)), int(mp.group(5)))
                    nc = NEWCOORD.search(text)
                    end = (int(nc.group(1)), int(nc.group(2)), int(nc.group(3))) if nc else xyz
                    local = index == me
                    who = ("player", local, end)
                    if local:
                        r = room_of(end[0], end[1])
                        if r != room:
                            room = r
                            if visit is not None:
                                visits.append(visit)
                            visit = Visit(os.path.basename(path), r, tick) if r else None
                    if visit is not None:
                        visit.players[mp.group(2)] += 1
                        if kind in ("teleport", "exactmove") and local:
                            visit.add(tick, "player", "teleport", "%s -> %s" % (xyz[:2], end[:2]))
                    continue
                if mn:
                    index = int(mn.group(1))
                    n = npc_ref(mn, 1)
                    xyz = (int(mn.group(6)), int(mn.group(7)), int(mn.group(8)))
                    nc = NEWCOORD.search(text)
                    end = (int(nc.group(1)), int(nc.group(2)), int(nc.group(3))) if nc else xyz
                    if kind == "add" or index not in npc_type:
                        npc_type[index] = n
                    tname = nm("npc", npc_type[index])
                    npc_at[index] = end
                    who = ("npc", index, tname, end)
                    if visit is not None:
                        visit.npcs[index] = tname
                        if kind in ("add", "del"):
                            visit.add(tick, "npc", tname, "%s idx=%d at %s" % (kind, index, end[:2]))
                        elif kind in ("teleport", "crawl"):
                            visit.add(tick, "npc", tname, "%s idx=%d %s -> %s" % (kind, index, xyz[:2], end[:2]))
                    continue
                if kind == "telejump" and visit is not None:
                    mn = NPC_HEAD.search(text)
                    if mn:
                        index = int(mn.group(1))
                        nc = NEWCOORD.search(text)
                        visit.add(tick, "npc", nm("npc", npc_type.get(index, npc_ref(mn, 1))),
                                  "telejump idx=%d -> %s" % (index, nc.groups()[:2] if nc else "?"))
                continue
            m = SUB12.match(line)
            if m and who is not None and visit is not None:
                sub, text = m.group(1), m.group(2)
                if who[0] == "npc":
                    npc_sub(visit, tick, who, sub, text, npc_type)
                elif who[1]:
                    player_sub(visit, tick, who, sub, text)
                else:
                    if sub in ("sequence", "spotanim"):
                        visit.add(tick, "party", sub, describe(sub, text))
                continue
    flush_proj()
    if visit is not None:
        visits.append(visit)
    return visits


def describe(sub, text):
    f = kv(text)
    if sub == "sequence":
        s = nm("seq", id_of(text))
        return s + (" delay=%s" % f["delay"] if f.get("delay") not in (None, "0") else "")
    if sub == "spotanim":
        s = nm("spotanim", id_of(text))
        extra = " ".join("%s=%s" % (k, f[k]) for k in ("slot", "height", "delay") if f.get(k) not in (None, "0"))
        return (s + " " + extra).strip()
    return text


def npc_sub(visit, tick, who, sub, text, npc_type):
    _, index, tname, end = who
    f = kv(text)
    if sub == "sequence":
        d = describe(sub, text)
        visit.add(tick, "npc", tname, "seq " + d)
        visit.seq_ticks[(tname, d.split(" ")[0])].append((index, tick))
    elif sub == "spotanim":
        visit.add(tick, "npc", tname, "spotanim " + describe(sub, text))
    elif sub == "transformation":
        new = id_of(text)
        visit.add(tick, "npc", tname, "retype -> %s" % nm("npc", new))
        npc_type[index] = new
        visit.npcs[index] = nm("npc", new)
    elif sub == "hit":
        visit.add(tick, "npc", tname, "hit type=%s" % f.get("id"))
    elif sub == "headbar":
        if f.get("fill") == "0":
            visit.add(tick, "npc", tname, "headbar %s empty" % f.get("id"))
    elif sub == "tinting":
        visit.add(tick, "npc", tname, "tint " + text)
    elif sub in ("exact_move", "exactmove"):
        t1, t2 = re.findall(r"to\d=\((\d+), (\d+)", text)[:2] or [("0", "0"), ("0", "0")]
        rel = lambda t: (int(t[0]) - end[0], int(t[1]) - end[1])
        visit.add(tick, "npc", tname, "exact_move to1%s d1=%s to2%s d2=%s angle=%s" % (
            rel(t1), f.get("delay1"), rel(t2), f.get("delay2"), f.get("angle")))
    elif sub == "say":
        visit.add(tick, "npc", tname, "say " + text)
    elif sub in ("enabled_ops", "level_change", "face_angle"):
        visit.add(tick, "npc", tname, "%s %s" % (sub, text))
    elif sub in ("face_coord", "face_pathingentity"):
        pass
    else:
        visit.add(tick, "npc", tname, "%s %s" % (sub, text))


def player_sub(visit, tick, who, sub, text):
    end = who[2]
    f = kv(text)
    if sub in ("sequence", "spotanim"):
        visit.add(tick, "player", sub, describe(sub, text))
    elif sub == "hit":
        visit.add(tick, "player", "hit", "type=%s value=%s" % (f.get("id"), f.get("value")))
    elif sub == "tinting":
        visit.add(tick, "player", "tint", text)
    elif sub in ("exact_move", "exactmove"):
        t = re.findall(r"to\d=\((\d+), (\d+)", text)
        rel = ["(%d,%d)" % (int(a) - end[0], int(b) - end[1]) for a, b in t]
        visit.add(tick, "player", "exact_move", "to %s d1=%s d2=%s angle=%s from %s" % (
            " ".join(rel), f.get("delay1"), f.get("delay2"), f.get("angle"), end[:2]))
    elif sub in ("face_coord", "face_pathingentity", "face_angle", "appearance", "details", "objs", "headbar"):
        pass
    else:
        visit.add(tick, "player", sub, text)


SKIP_VARP = {"update_stat_v2"}


def top_event(visit, tick, kind, text):
    f = kv(text)
    i = id_of(text)
    at = coord(text)
    xy = "(%d,%d)" % at[:2] if at else ""
    if kind in ("map_anim", "map_anim_specific"):
        extra = " ".join("%s=%s" % (k, f[k]) for k in ("height", "delay") if f.get(k) not in (None, "0"))
        visit.add(tick, "graphic", "%s %s" % (kind, nm("spotanim", i)), (extra + " " + xy).strip())
    elif kind.startswith("loc_add_change"):
        visit.add(tick, "loc", "add_change %s shape=%s rot=%s" % (nm("loc", i), f.get("shape"), f.get("rotation")), xy)
    elif kind == "loc_del":
        visit.add(tick, "loc", "del shape=%s rot=%s" % (f.get("shape"), f.get("rotation")), xy)
    elif kind == "loc_anim":
        visit.add(tick, "loc", "anim %s shape=%s rot=%s" % (nm("seq", f.get("anim")), f.get("shape"), f.get("rotation")), xy)
    elif kind in ("obj_add", "obj_del", "obj_count"):
        visit.add(tick, "obj", "%s %s" % (kind, nm("obj", i)), xy)
    elif kind == "sound_area":
        extra = " ".join("%s=%s" % (k, f[k]) for k in ("loops", "range", "delay", "size") if f.get(k) not in (None, "0"))
        visit.add(tick, "sound", "area %s" % nm("synth", i), (extra + " " + xy).strip())
    elif kind == "synth_sound":
        extra = " ".join("%s=%s" % (k, f[k]) for k in ("loops", "delay") if f.get(k) not in (None, "0"))
        visit.add(tick, "sound", "synth %s" % nm("synth", i), extra)
    elif kind.startswith("midi_song"):
        visit.add(tick, "sound", "song %s" % nm("song", i), text.split(",", 1)[1].strip() if "," in text else "")
    elif kind == "midi_jingle":
        visit.add(tick, "sound", "jingle %s" % nm("jingle", i), "")
    elif kind == "if_opentop":
        visit.add(tick, "interface", "opentop %s" % nm("if", i), "")
    elif kind == "if_opensub":
        visit.add(tick, "interface", "opensub %s -> %s %s" % (com(text), nm("if", i), f.get("type", "")), "")
    elif kind == "if_closesub":
        visit.add(tick, "interface", "closesub %s" % com(text), "")
    elif kind == "if_movesub":
        visit.add(tick, "interface", "movesub", text)
    elif kind == "if_sethide":
        visit.add(tick, "interface", "sethide %s %s" % (com(text), f.get("hide")), "")
    elif kind == "if_settext":
        m = re.search(r'text="(.*)"$', text)
        visit.add(tick, "interface", "settext %s" % com(text), m.group(1) if m else text)
    elif kind == "if_setcolour":
        visit.add(tick, "interface", "setcolour %s" % com(text), f.get("colour", ""))
    elif kind.startswith("if_setevents"):
        visit.add(tick, "interface", "setevents %s %s..%s" % (com(text), f.get("start"), f.get("end")), f.get("events", ""))
    elif kind == "if_setanim":
        visit.add(tick, "interface", "setanim %s %s" % (com(text), nm("seq", i)), "")
    elif kind == "if_setnpchead":
        visit.add(tick, "interface", "setnpchead %s %s" % (com(text), nm("npc", i)), "")
    elif kind == "if_setobject":
        visit.add(tick, "interface", "setobject %s %s" % (com(text), nm("obj", i)), f.get("zoomorcount", ""))
    elif kind in ("if_setplayerhead", "if_setmodel", "if_setangle", "if_setposition", "if_setscrollpos"):
        visit.add(tick, "interface", "%s %s" % (kind, com(text)), text)
    elif kind == "runclientscript":
        m = re.search(r"types=(\[[^\]]*\]), values=(\[.*\])$", text)
        visit.add(tick, "clientscript", nm("script", i), m.group(2) if m else "")
    elif kind == "varbit":
        visit.add(tick, "var", "varbit %s" % nm("varbit", i), f.get("new", ""))
    elif kind.startswith("varp"):
        visit.add(tick, "var", "varp %s" % nm("varp", i), f.get("new", ""))
    elif kind.startswith("cam_") or kind == "minimap_toggle" or kind.startswith("fade"):
        visit.add(tick, "camera", kind, text)
    elif kind == "message_game":
        m = re.search(r'message="(.*)"$', text)
        visit.add(tick, "message", f.get("type", "?"), m.group(1) if m else text)
        if m and "Challenge complete" in m.group(1):
            visit.done.append(m.group(1))
    elif kind in ("clear_entities", "hint_arrow", "set_map_flag", "logout", "update_run_energy",
                  "reset_anims", "spotanim_specific", "npc_anim_specific", "player_anim_specific"):
        visit.add(tick, "other", kind, text)


def gaps(ticks):
    by = defaultdict(list)
    for index, t in ticks:
        by[index].append(t)
    out = Counter()
    for ts in by.values():
        ts.sort()
        for a, b in zip(ts, ts[1:]):
            if b - a <= 60:
                out[b - a] += 1
    return out


def top(counter, n=12):
    return ", ".join("%s x%d" % (k, v) for k, v in counter.most_common(n))


def inventory(room, visits, out):
    w = out.write
    w("=" * 78 + "\n%s: %d visit(s)\n" % (room, len(visits)))
    for v in visits:
        players = [p for p, c in v.players.most_common() if c >= 50]
        w("  %s %d..%d (%d ticks) players=%d %s %s\n" % (
            v.path, v.start, v.end, v.end - v.start, len(players),
            "|".join(players[:6]), "; ".join(re.sub(r"<[^>]*>", "", d) for d in v.done)))
    by = defaultdict(lambda: defaultdict(Counter))
    samples = defaultdict(lambda: defaultdict(list))
    firsts = defaultdict(list)     # (cat,key) -> relative first ticks per visit
    for v in visits:
        seen = set()
        for t, cat, key, detail in v.events:
            by[cat][key][detail] += 1
            if (cat, key) not in seen:
                seen.add((cat, key))
                firsts[(cat, key)].append(t - v.start)
    seq_gaps = defaultdict(Counter)
    for v in visits:
        for k, ticks in v.seq_ticks.items():
            seq_gaps[k].update(gaps(ticks))

    # repeat period of a loc/graphic/sound at one tile (the cycle of a trap)
    period = defaultdict(Counter)
    for v in visits:
        at = defaultdict(list)
        for t, cat, key, detail in v.events:
            if cat in ("loc", "graphic", "sound"):
                m = re.search(r"\((\d+),(\d+)\)$", detail)
                if m:
                    at[(cat, key, m.group(0), re.sub(r"\s*\(\d+,\d+\)$", "", detail))].append(t)
        for (cat, key, xy, d), ts in at.items():
            for a, b in zip(ts, ts[1:]):
                if 0 < b - a <= 60:
                    period[(cat, key)][b - a] += 1

    def section(cat, title, detail_n=10, show_first=True):
        if cat not in by:
            return
        w("\n-- %s\n" % title)
        for key in sorted(by[cat], key=lambda k: -sum(by[cat][k].values())):
            total = sum(by[cat][key].values())
            fs = sorted(firsts[(cat, key)])
            first = (" first@+%s" % ",".join(str(x) for x in fs[:8])) if show_first else ""
            w("  %s  [%d]%s\n" % (key, total, first))
            details = by[cat][key]
            if (cat, key) in period:
                w("      period at a tile: %s\n" % top(period[(cat, key)], 6))
            if len(details) == 1 and "" in details:
                continue
            w("      %s\n" % top(details, detail_n))

    # npcs: one block per type with its sub-kinds split out
    if "npc" in by:
        w("\n-- npcs\n")
        for tname in sorted(by["npc"], key=lambda k: -sum(by["npc"][k].values())):
            details = by["npc"][tname]
            w("  %s  [%d events] first@+%s\n" % (tname, sum(details.values()),
                                                ",".join(str(x) for x in sorted(firsts[("npc", tname)])[:8])))
            kinds = defaultdict(Counter)
            for d, c in details.items():
                head = d.split(" ", 1)[0]
                if head in ("add", "del"):
                    kinds[head][d.split(" at ")[-1]] += c
                elif head in ("teleport", "crawl"):
                    mm = re.findall(r"\((\d+), (\d+)\)", d)
                    if len(mm) == 2:
                        (ax, az), (bx, bz) = mm
                        kinds[head]["step(%d,%d)" % (int(bx) - int(ax), int(bz) - int(az))] += c
                    else:
                        kinds[head][d] += c
                elif head == "telejump":
                    kinds[head][re.sub(r"idx=\d+ ", "", d)] += c
                else:
                    kinds[head][d.split(" ", 1)[1] if " " in d else d] += c
            for head in sorted(kinds):
                w("      %-12s %s\n" % (head, top(kinds[head], 14)))
            for (t2, seq), g in sorted(seq_gaps.items()):
                if t2 == tname and g:
                    w("      %-12s %s: %s\n" % ("seq gap", seq, top(g, 8)))
    section("projectile", "projectiles (thrower -> target; heights in our units)", 14)
    section("projtile", "projectile tiles (source -> target)", 8, show_first=False)
    section("graphic", "map graphics", 8)
    section("loc", "locs", 6)
    section("obj", "objs", 6)
    section("sound", "sounds", 8)
    section("camera", "camera", 8)
    section("interface", "interfaces", 6)
    section("clientscript", "clientscripts (values)", 6)
    section("var", "varbits / varps (values set)", 10)
    section("message", "messages", 20, show_first=True)
    section("player", "recording player", 12)
    section("party", "other players", 10)
    section("other", "other", 6)
    w("\n")


def compact(visit, out, upto=None):
    """One line per tick and event kind; the tiles of a kind are collapsed."""
    w = out.write
    w("=" * 78 + "\n%s %s %d..%d (compact)\n" % (visit.room, visit.path, visit.start, visit.end))
    rows = {}
    for t, cat, key, detail in visit.events:
        if cat in ("var", "clientscript", "party", "projtile") or "setcolour" in key:
            continue
        rel = t - visit.start
        if upto is not None and rel > upto:
            break
        text = "%s %s" % (key, detail)
        m = re.search(r"\((\d+),(\d+)\)$", text)
        tile = m.group(0) if m else None
        if tile:
            text = text[:m.start()].rstrip()
        if "npc" == cat:
            m2 = re.search(r" at \((\d+), (\d+)\)$", text)
            if m2:
                tile = "(%s,%s)" % m2.groups()
                text = text[:m2.start()]
            text = re.sub(r"idx=\d+ ?", "", text)
        rows.setdefault(rel, {}).setdefault(text, []).append(tile)
    for rel in sorted(rows):
        for text, tiles in rows[rel].items():
            ts = [x for x in tiles if x]
            tail = ""
            if ts:
                tail = " " + " ".join(ts[:6]) + (" +%d" % (len(ts) - 6) if len(ts) > 6 else "")
            n = " x%d" % len(tiles) if len(tiles) > 1 and not ts else ""
            w("+%-5d %s%s%s\n" % (rel, text[:150], n, tail))


def timeline(visit, out):
    w = out.write
    w("=" * 78 + "\n%s %s %d..%d\n" % (visit.room, visit.path, visit.start, visit.end))
    skip = {"var"}
    for t, cat, key, detail in visit.events:
        if cat in skip and not key.startswith("varbit"):
            continue
        w("+%-5d %-11s %s %s\n" % (t - visit.start, cat, key, detail))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--room", nargs="*", default=[])
    ap.add_argument("--timeline", action="store_true")
    ap.add_argument("--compact", action="store_true", help="with --timeline: one line per tick and kind")
    ap.add_argument("--upto", type=int, help="with --compact: stop after this many ticks into the visit")
    ap.add_argument("--visit", nargs="*", default=[], help="FILE:N, the Nth visit (from 1) of a room in FILE")
    ap.add_argument("--min-ticks", type=int, default=10, help="drop visits shorter than this")
    ap.add_argument("--out")
    a = ap.parse_args()
    rooms = defaultdict(list)
    for path in a.files:
        for v in parse(path):
            if v.end - v.start >= a.min_ticks:
                rooms[v.room].append(v)
    keep = [r for r in ORDER if r in rooms and (not a.room or r in a.room)]
    if a.timeline:
        for r in keep:
            for n, v in enumerate(rooms[r], 1):
                tag = "%s:%d" % (v.path.split(".")[0], n)
                if a.visit and tag not in a.visit:
                    continue
                if a.compact:
                    compact(v, sys.stdout, a.upto)
                else:
                    timeline(v, sys.stdout)
        return
    for r in keep:
        if a.out:
            os.makedirs(a.out, exist_ok=True)
            with open(os.path.join(a.out, "%s.out" % r), "w") as out:
                inventory(r, rooms[r], out)
        else:
            inventory(r, rooms[r], sys.stdout)


if __name__ == "__main__":
    main()
