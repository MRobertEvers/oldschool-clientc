#!/usr/bin/env python3
"""Inventory of everything Jagex's server sends during one fight, from RSProx
transcripts (tools/cox/rsprox_fetch.py + RSProx `transcribe`): npc and player
animations, attached and map graphics, projectiles with their timing and heights,
area and synth sounds, loc changes and animations, messages, overhead text,
varbits, camera shakes, hitsplats and npc transformations.

    python3 tools/rsprox_tells.py --anchor 8369-8375 ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox_tells.py --anchor 8369-8375 --kind proj <txt>...
    python3 tools/rsprox_tells.py --anchor 8369-8375 --timeline <txt>     # one line per tell, in tick order

The FIGHT WINDOW is every tick from the first to the last on which an anchor npc
appears in npc_info, split where it goes unseen for more than --gap ticks. Only
lines inside a window are counted.

Rev 227 transcripts carry no symbol names; their ids are named from
OSRS-Content's compacks (seq, spotanim, npc, loc, varbit, sound) so the two
revisions read alike. A name from the compack is the tree's name for the id.

Projectiles: `starttime` and `endtime` are client cycles (30 a tick). The
signature keeps starttime, angle, progress and both heights; the flight
(endtime - starttime) is reported against the source-to-target distance in
tiles (Chebyshev), so `--kind proj` shows the length + tiles * step shape.
Raw heights are what the packet carries; our scripts take them divided by 4.
"""
import argparse
import os
import re
import sys
from collections import Counter, defaultdict

CONTENT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "OSRS-Content", "osrs239-content")
TICK = re.compile(r"^\[(\d+)\]$")
VERSION = re.compile(r"^version: (\d+)")
ME = re.compile(r"local player index: (\d+)")
# `id=<symbol> (<id>)` (named) or `id=<id>` / `<display> (id=<id>)` (rev 227)
REF = r"(?:id=([a-z0-9_]+) \((-?\d+)\)|id=(-?\d+)\b|[^(),]*\(id=(\d+)\))"
NPC_LINE = re.compile(r"^\s{8}\[(\w+)\] npc=\(index=(\d+), " + REF)
PLAYER_LINE = re.compile(r"^\s{8}\[(\w+)\] player=\(index=(\d+), name=([^,]*), coord=\((\d+), (\d+), (\d+)\)")
TOP = re.compile(r"^\s{4}\[(\w+)\] ?(.*)$")
SUB = re.compile(r"^\s{12}\[(\w+)\] ?(.*)$")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
NAMESPACES = {"seq": "all.seq.compack", "spotanim": "all.spotanim.compack", "npc": "all.npc.compack",
              "loc": "all.loc.compack", "varbit": "all.varbit.compack"}


def load_names():
    names = {}
    for space, file in NAMESPACES.items():
        table = {}
        path = os.path.join(CONTENT, "configs", file)
        if os.path.isfile(path):
            with open(path, "r", encoding="latin-1") as handle:
                for line in handle:
                    key, _, value = line.strip().partition("=")
                    if key.isdigit():
                        table[int(key)] = value
        names[space] = table
    table = {}
    path = os.path.join(CONTENT, "pack", "4_soundeffects.pack")
    if os.path.isfile(path):
        with open(path, "r", encoding="latin-1") as handle:
            for line in handle:
                key, _, value = line.strip().partition("=")
                if key.isdigit():
                    table[int(key)] = value
    names["sound"] = table
    return names


def ref(match, space, names, start=1):
    """(name, id) from a REF match whose groups begin at `start`."""
    sym, num, bare, display = match.group(start), match.group(start + 1), match.group(start + 2), match.group(start + 3)
    if sym is not None:
        return sym, int(num)
    value = int(bare if bare is not None else display)
    return names[space].get(value, "%s_%d" % (space, value)), value


FIELD_REF = re.compile(r"id=(?:([a-z0-9_]+) \((-?\d+)\)|(-?\d+))")


def field_ref(text, space, names):
    m = FIELD_REF.search(text)
    if not m:
        return None, None
    if m.group(1):
        return m.group(1), int(m.group(2))
    value = int(m.group(3))
    return names[space].get(value, "%s_%d" % (space, value)), value


def rest_after_id(text):
    """The fields after `id=...`, minus coordinates."""
    text = FIELD_REF.sub("", text, count=1).lstrip(", ")
    text = re.sub(r",? ?coord=\(\d+, \d+, \d+\)", "", text)
    return text.strip(", ")


def parse_anchor(text):
    ids = set()
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            ids.update(range(int(a), int(b) + 1))
        else:
            ids.add(int(part))
    return ids


def windows(path, anchor, gap, names):
    """[(first, last)] ticks with an anchor npc present."""
    seen = []
    tick = 0
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = TICK.match(line.strip())
            if m and line.startswith("["):
                tick = int(m.group(1))
                continue
            if "npc=" not in line:
                continue
            for m in re.finditer(r"npc=\(index=\d+, " + REF, line):
                _, value = ref(m, "npc", names)
                if value in anchor:
                    if not seen or seen[-1] != tick:
                        seen.append(tick)
                    break
    spans = []
    for t in seen:
        if spans and t - spans[-1][1] <= gap:
            spans[-1][1] = t
        else:
            spans.append([t, t])
    return [tuple(s) for s in spans]


def chebyshev(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def scan(path, anchor, gap, names, sink, timeline):
    spans = windows(path, anchor, gap, names)
    label = os.path.basename(path)
    tick = 0
    inside = False
    actor = None          # ('npc', name) / ('player', name, is_me)
    me = None
    revision = None
    pending_proj = None   # (name, id, fields, source coord)
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if revision is None:
                m = VERSION.match(line)
                if m:
                    revision = int(m.group(1))
            if me is None:
                m = ME.search(line)
                if m:
                    me = m.group(1)
            m = TICK.match(line.strip())
            if m and line.startswith("["):
                tick = int(m.group(1))
                inside = any(a <= tick <= b for a, b in spans)
                actor = None
                continue
            if not inside:
                continue
            rel = None
            for a, b in spans:
                if a <= tick <= b:
                    rel = (a, tick - a)

            def emit(kind, signature):
                sink[kind][signature] += 1
                sink["_where"][(kind, signature)].add(label)
                if timeline is not None:
                    timeline.append((label, rel[0], rel[1], kind, signature))

            m = NPC_LINE.match(line)
            if m:
                name, value = ref(m, "npc", names, start=3)
                actor = ("npc", name)
                if m.group(1) == "add":
                    emit("npc_add", name)
                continue
            m = PLAYER_LINE.match(line)
            if m:
                actor = ("player", m.group(3), m.group(2) == me)
                continue
            m = SUB.match(line)
            if m and actor is not None:
                kind, body = m.group(1), m.group(2)
                who = actor[1] if actor[0] == "npc" else ("ME" if actor[2] else "player")
                if kind == "sequence":
                    seq, value = field_ref(body, "seq", names)
                    if value is not None and value >= 0:
                        emit(actor[0] + "_seq", (who, seq, rest_after_id(body)))
                elif kind == "spotanim":
                    spot, value = field_ref(body, "spotanim", names)
                    if value is not None and value >= 0:
                        emit(actor[0] + "_spotanim", (who, spot, rest_after_id(re.sub(r"slot=\d+, ", "", body))))
                elif kind in ("say", "transformation", "exactmove", "headicon_customisation", "npc_headicon_specific"):
                    if kind == "transformation":
                        body = field_ref(body, "npc", names)[0]
                    emit(actor[0] + "_" + kind, (who, body))
                elif kind == "hit":
                    emit("hitsplat", (actor[0] if actor[0] == "npc" else who, re.sub(r", value=\d+", "", body)))
                continue
            m = TOP.match(line)
            if not m:
                if pending_proj and ("[source]" in line or "[target]" in line):
                    c = COORD.search(line)
                    what = "npc" if "npc=" in line else ("player" if "player=" in line else "tile")
                    if "npc=" in line:
                        nm = re.search(r"npc=\(index=\d+, " + REF, line)
                        # an npc the recorder cannot see prints its index only
                        what = ref(nm, "npc", names)[0] if nm else "npc"
                    elif "player=" in line:
                        what = "player"
                    pending_proj["source" if "[source]" in line else "target"] = (what, (int(c.group(1)), int(c.group(2))))
                    if "source" in pending_proj and "target" in pending_proj:
                        p = pending_proj
                        tiles = chebyshev(p["source"][1], p["target"][1])
                        fields = dict(re.findall(r"(\w+)=(-?\d+)", p["body"]))
                        start, end = int(fields.get("starttime", 0)), int(fields.get("endtime", 0))
                        emit("proj_core", (p["name"], "start=%d" % start))
                        # what the shot leaves from (MAP_PROJANIM sourceIndex) and homes on
                        emit("proj_source", (p["name"], p["source"][0], p["target"][0]))
                        emit("proj", (p["name"], p["source"][0], p["target"][0],
                                      "start=%s angle=%s progress=%s height=%s->%s" % (
                                          start, fields.get("angle"), fields.get("progress"),
                                          fields.get("startheight"), fields.get("endheight"))))
                        sink["proj_flight"][(p["name"], tiles, end - start)] += 1
                        pending_proj = None
                continue
            kind, body = m.group(1), m.group(2)
            actor = None
            pending_proj = None
            if kind.startswith("map_projanim"):
                name, value = field_ref(body, "spotanim", names)
                pending_proj = {"name": name, "body": body}
            elif kind == "map_anim":
                name, _ = field_ref(body, "spotanim", names)
                emit("map_anim", (name, rest_after_id(body)))
            elif kind == "sound_area":
                name, value = field_ref(body, "sound", names)
                emit("sound_area", ("%s (%d)" % (name, value), rest_after_id(body)))
                emit("sound", ("%s (%d)" % (name, value), re.sub(r",? ?range=\d+", "", rest_after_id(body)), "area"))
            elif kind == "synth_sound":
                name, value = field_ref(body, "sound", names)
                emit("synth_sound", ("%s (%d)" % (name, value), rest_after_id(body)))
                # RSProx leaves out a synth's default loops=1; ours always states it
                rest = rest_after_id(body)
                if "loops=" not in rest:
                    rest = "loops=1" + (", " + rest if rest else "")
                emit("sound", ("%s (%d)" % (name, value), rest, "synth"))
            elif kind in ("loc_add_change_v2", "loc_add_change_v1", "loc_add_change"):
                name, _ = field_ref(body, "loc", names)
                emit("loc_add", (name, rest_after_id(body)))
            elif kind == "loc_anim":
                anim = re.search(r"anim=(?:([a-z0-9_]+) \((\d+)\)|(\d+))", body)
                seq = anim.group(1) if anim and anim.group(1) else (
                    names["seq"].get(int(anim.group(3)), anim.group(3)) if anim else "?")
                emit("loc_anim", (seq, re.sub(r".*shape=(\d+), rotation=(\d+).*", r"shape=\1", body)))
            elif kind == "loc_del":
                emit("loc_del", re.sub(r"coord=\(\d+, \d+, \d+\), ", "", body))
            elif kind == "message_game":
                emit("message", body)
            elif kind == "varbit":
                name, _ = field_ref(body, "varbit", names)
                new = re.search(r"new=(-?\d+)", body)
                emit("varbit", (name, new.group(1) if new else "?"))
            elif kind in ("cam_shake", "cam_reset", "midi_song_v2", "midi_jingle", "midi_song", "hint_arrow",
                          "if_opensub", "if_closesub", "obj_add", "obj_add_specific"):
                emit(kind, re.sub(r"coord=\(\d+, \d+, \d+\)", "", body))
    return spans, revision


def unpack(coord):
    coord = int(coord)
    return (coord >> 14) & 0x3fff, coord & 0x3fff, (coord >> 28) & 3


def fields(text):
    """'delay=20, height=92' with zero fields left out, the way RSProx prints."""
    out = []
    for key, value in text:
        if int(value) != 0:
            out.append("%s=%s" % (key, value))
    return ", ".join(out)


def scan_ticklog(path, anchor, gap, names, sink, timeline):
    """The same signatures from OUR server's ticklog.tsv (a scriptrun session or
    a raid_gate run), so the two can be diffed kind by kind.

    What a ticklog cannot say is left out of the signature on both sides by
    `--compare`: projectile angle, progress and heights, sound ranges. Sounds
    are per-player SYNTH_SOUND rows on our side (one per raider in range); they
    are counted once per tick, sound and source coord."""
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        header = handle.readline()
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 11:
                continue
            rows.append(parts)
    seen = sorted({int(r[1]) for r in rows if r[2] in ("npc_spawn", "npc_anim", "npc_tile", "npc_retype", "npc_spotanim")
                   and (int(r[4]) in anchor or (r[2] == "npc_retype" and int(r[5]) in anchor))})
    spans = []
    for t in seen:
        if spans and t - spans[-1][1] <= gap:
            spans[-1][1] = t
        else:
            spans.append([t, t])
    spans = [tuple(s) for s in spans]
    label = os.path.basename(os.path.dirname(path)) + "/" + os.path.basename(path)
    slot_type = {}
    sounds_seen = set()
    for r in rows:
        tick, kind = int(r[1]), r[2]
        a, b, c, d, e, f = (int(x) if x.lstrip("-").isdigit() else 0 for x in r[3:9])
        text = r[9]
        if kind == "npc_spawn":
            slot_type[a] = b
        elif kind == "npc_retype":
            slot_type[a] = c
        span = next((sp for sp in spans if sp[0] <= tick <= sp[1]), None)
        if span is None:
            continue

        def emit(k, signature):
            sink[k][signature] += 1
            sink["_where"][(k, signature)].add(label)
            if timeline is not None:
                timeline.append((label, span[0], tick - span[0], k, signature))

        npc = lambda t: names["npc"].get(t, "npc_%d" % t)
        seq = lambda t: names["seq"].get(t, "seq_%d" % t)
        spot = lambda t: names["spotanim"].get(t, "spotanim_%d" % t)
        if kind == "npc_anim" and c >= 0:
            emit("npc_seq", (npc(b), seq(c), fields([("delay", d)])))
        elif kind == "npc_spotanim" and c >= 0:
            emit("npc_spotanim", (npc(b), spot(c), fields([("delay", e), ("height", d)])))
        elif kind == "npc_spawn":
            emit("npc_add", npc(b))
        elif kind == "npc_retype":
            emit("npc_transformation", (npc(b), npc(c)))
        elif kind == "npc_headicon":
            emit("npc_headicon_customisation", (npc(b), ""))
        elif kind == "npc_say":
            emit("npc_say", (npc(b), 'text="%s"' % text))
        elif kind == "projectile":
            sx, sz, _ = unpack(a)
            tx, tz, _ = unpack(b)
            target = "tile" if c == 0 else "entity"
            emit("proj_core", (spot(d), "start=%d" % e))
            source = re.search(r"\bsource npc (\d+)", text)
            homes = "tile" if c == 0 else ("player" if c < 0 else npc(slot_type.get(c - 1, -1)))
            emit("proj_source", (spot(d), npc(int(source.group(1))) if source else "tile", homes))
            sink["proj_flight"][(spot(d), max(abs(sx - tx), abs(sz - tz)), f - e)] += 1
        elif kind == "map_spotanim":
            emit("map_anim", (spot(b), fields([("delay", d), ("height", c)])))
        elif kind == "player_anim" and b >= 0:
            emit("player_seq", ("ME", seq(b), fields([("delay", c)])))
        elif kind == "player_spotanim" and b >= 0:
            emit("player_spotanim", ("ME", spot(b), fields([("delay", d), ("height", c)])))
        elif kind == "loc_set":
            if b < 0:
                emit("loc_del", "shape=%d, rotation=%d" % (c, d))
            else:
                emit("loc_add", (names["loc"].get(b, "loc_%d" % b), "shape=%d, rotation=%d" % (c, d)))
        elif kind == "loc_anim":
            emit("loc_anim", (seq(e), "shape=%d" % c))
        elif kind == "sound":
            key = (tick, b, e)
            if key in sounds_seen:
                continue
            sounds_seen.add(key)
            # an area row's label carries the drop-off: "area size=5"
            size = re.search(r"\bsize=(\d+)", text)
            emit("sound", ("%s (%d)" % (names["sound"].get(b, "sound_%d" % b), b),
                           fields([("loops", c), ("delay", d), ("size", int(size.group(1)) if size else 0)]),
                           text.split(" ")[0]))
        elif kind == "music":
            emit("midi_song_v2", "id=%d" % b)
        elif kind == "jingle":
            emit("midi_jingle", "id=%d" % b)
    return spans, "ticklog"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("transcripts", nargs="+")
    parser.add_argument("--anchor", required=True, help="npc ids that define the fight, e.g. 8369-8375")
    parser.add_argument("--gap", type=int, default=50)
    parser.add_argument("--kind", help="print only this kind (proj, npc_seq, map_anim, sound_area, ...)")
    parser.add_argument("--timeline", action="store_true")
    parser.add_argument("--ticklog", action="store_true", help="the inputs are our server's ticklog.tsv files")
    parser.add_argument("--ours", nargs="+", help="with RSProx transcripts as the inputs: our ticklog.tsv files to "
                        "compare against them, per kind, as a rate per fight window")
    args = parser.parse_args()
    anchor = parse_anchor(args.anchor)
    names = load_names()
    sink = defaultdict(Counter)
    sink["_where"] = defaultdict(set)
    timeline = [] if args.timeline else None
    for path in args.transcripts:
        if args.ticklog:
            spans, revision = scan_ticklog(path, anchor, args.gap, names, sink, timeline)
        else:
            spans, revision = scan(path, anchor, args.gap, names, sink, timeline)
        print("# %s rev %s: windows %s" % (os.path.basename(path), revision,
                                           ", ".join("%d-%d" % s for s in spans)))
    if args.ours:
        ours = defaultdict(Counter)
        ours["_where"] = defaultdict(set)
        their_windows = sum(len(windows(p, anchor, args.gap, names)) for p in args.transcripts)
        our_windows = 0
        for path in args.ours:
            spans, _ = scan_ticklog(path, anchor, args.gap, names, ours, None)
            our_windows += len(spans)
        print("# recorded windows %d, ours %d; columns: recorded per window, ours per window" % (
            their_windows, our_windows))
        for kind in sorted(k for k in set(sink) | set(ours) if not k.startswith("_") and k not in (
                "proj", "sound_area", "synth_sound", "proj_flight")):
            if args.kind and kind != args.kind:
                continue
            keys = set(sink[kind]) | set(ours[kind])
            if not keys:
                continue
            print("== %s" % kind)
            for key in sorted(keys, key=lambda k: (-(sink[kind][k] / their_windows + ours[kind][k] / max(1, our_windows)),
                                                    str(k))):
                a = sink[kind][key] / their_windows
                b = ours[kind][key] / max(1, our_windows)
                mark = "  " if a and b else ("R " if a else "O ")
                print("  %s%7.2f %7.2f  %s" % (mark, a, b, key))
        return 0
    if timeline is not None:
        for label, start, offset, kind, signature in timeline:
            if args.kind and kind != args.kind:
                continue
            print("%s %d +%d %s %s" % (label, start, offset, kind, signature))
        return 0
    for kind in sorted(k for k in sink if not k.startswith("_")):
        if args.kind and kind != args.kind and not (args.kind == "proj" and kind == "proj_flight"):
            continue
        print("== %s" % kind)
        rows = sink[kind]
        for signature, count in sorted(rows.items(), key=lambda kv: (str(kv[0]) if kind == "proj_flight" else "",
                                                                       -kv[1], str(kv[0]))):
            where = sink["_where"].get((kind, signature))
            print("  %5d  %s%s" % (count, signature, "  [%s]" % ",".join(sorted(where)) if where else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
