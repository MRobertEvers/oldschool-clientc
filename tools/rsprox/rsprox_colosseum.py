#!/usr/bin/env python3
"""The Fortis Colosseum as RSProx recorded it: one structured event stream per
session, segmented into runs and waves, for the statistics the content is built
from (docs/rsprox-analysis/colosseum/README.md).

    python3 tools/rsprox/rsprox_colosseum.py parse <transcript>...      # -> cache, once per transcript
    python3 tools/rsprox/rsprox_colosseum.py waves <transcript>...      # one row per recorded wave

The parse is cached beside the build tree (build/colosseum_private/parsed/) and
re-made when the transcript is newer. Other subcommands live in
tools/rsprox/colosseum/ and import `load` from here.

Event kinds (every event has t = transcript tick):
  npc   idx, nid, name, mv (add/del/idle/walk/run/crawl/teleport/telejump),
        c (x, z, plane), nc, angle (spawn angle name), b: [[block, attrs]]
  pl    the recorder: mv, c, nc, b
  proj  id, name, st, et, ang, prog, sh, eh, ver, src {coord, npc}, dst {coord, player|npc}
  snd   sound_area: id, loops, delay, range, size, c
  syn   synth_sound: id, loops, delay
  man   map_anim: id, name, delay, height, c
  loc   loc_add_change / loc_del / loc_anim: op, id, name, c, shape, rot, anim
  obj   obj_add/obj_del: op, id, name, count, c
  var   varp/varbit: name, id, old, new
  msg   message_game: type, text
  stat  update_stat_v2: stat, level
  cs    runclientscript: id, name, values (raw)
  ui    if_* / resume_* / click packets: raw line (op, raw)
  mus   midi_song / midi_jingle: op, id
"""
import gzip
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, "build", "colosseum_private", "parsed")
PARSE_VERSION = 3

TICK = re.compile(r"^\[(\d+)\]\s*$")
LINE = re.compile(r"^( *)\[([a-z_0-9]+)\]\s?(.*)$")
NAMEID = re.compile(r"^(.*?) \((-?\d+)\)$")


def split_top(s):
    """Split 'a=1, b=(x, y), c=z' on top-level commas."""
    out = []
    depth = 0
    cur = []
    i = 0
    while i < len(s):
        ch = s[i]
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == '"':
            j = s.find('"', i + 1)
            while j > 0 and s[j - 1] == "\\":
                j = s.find('"', j + 1)
            if j < 0:
                j = len(s) - 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if ch == "," and depth == 0 and s[i + 1:i + 2] == " ":
            out.append("".join(cur))
            cur = []
            i += 2
            continue
        cur.append(ch)
        i += 1
    if cur:
        out.append("".join(cur))
    return out


def parse_value(v):
    v = v.strip()
    if v.startswith('"') and v.endswith('"'):
        return v[1:-1]
    if v.startswith("(") and v.endswith(")"):
        inner = v[1:-1]
        parts = split_top(inner)
        if all("=" not in p for p in parts):
            try:
                return tuple(int(p) for p in parts)
            except ValueError:
                return inner
        return parse_attrs(inner)
    m = NAMEID.match(v)
    if m:
        return {"name": m.group(1), "id": int(m.group(2))}
    try:
        return int(v)
    except ValueError:
        return v


def parse_attrs(s):
    d = {}
    for part in split_top(s):
        if "=" not in part:
            if part:
                d.setdefault("_", []).append(part)
            continue
        k, v = part.split("=", 1)
        d[k.strip()] = parse_value(v)
    return d


def ident(v):
    """(id, name) of an attribute that may be 'name (id)', an int, or missing."""
    if isinstance(v, dict) and "id" in v:
        return v["id"], v.get("name")
    if isinstance(v, int):
        return v, None
    return None, None


UI_KINDS = re.compile(r"^(if_|resume_|op(npc|loc|obj|player|held)|toplevel|move_gameclick|event_)")


class Parser:
    def __init__(self, path):
        self.path = path
        self.events = []
        self.header = {}
        self.local_index = None

    def run(self):
        tick = -1
        ctx = None          # current npc/pl entity event receiving sub-blocks
        ctx_indent = None
        proj = None
        in_header = True
        for raw in open(self.path, errors="replace"):
            line = raw.rstrip("\n")
            if in_header:
                if line.startswith("version:"):
                    self.header["version"] = line.split(":", 1)[1].strip()
                elif line.startswith("world:"):
                    self.header["world"] = line
                    m = re.search(r"activity: (.*)$", line)
                    self.header["activity"] = m.group(1) if m else ""
                elif line.startswith("local player index:"):
                    self.local_index = int(line.split(":")[1])
                    self.header["local_index"] = self.local_index
                m = TICK.match(line)
                if not m:
                    continue
                in_header = False
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                ctx = None
                proj = None
                continue
            m = LINE.match(line)
            if not m:
                continue
            indent = len(m.group(1))
            kind = m.group(2)
            rest = m.group(3)
            # sub-block of an entity (npc/player)
            if ctx is not None and indent > ctx_indent:
                if kind in ("appearance", "details", "equipment", "status", "bas", "model"):
                    if kind == "status":
                        ctx["b"].append(["status", parse_attrs(rest)])
                    elif kind == "bas":
                        ctx["b"].append(["bas", parse_attrs(rest)])
                    elif kind == "model":
                        ctx["b"].append(["model", parse_attrs(rest)])
                    continue
                if kind.startswith("- ") or line.strip().startswith("- "):
                    continue
                ctx["b"].append([kind, parse_attrs(rest) if rest else {}])
                continue
            if line.strip().startswith("- "):
                if ctx is not None and "wearpos=" in line:
                    ctx["b"].append(["wear", parse_attrs(line.strip()[2:])])
                continue
            ctx = None
            if proj is not None and kind in ("source", "target") and indent > 4:
                proj["src" if kind == "source" else "dst"] = parse_attrs(rest)
                continue
            proj = None
            if indent == 8 and kind in ("add", "del", "idle", "walk", "run", "crawl", "teleport", "telejump"):
                a = parse_attrs(rest)
                ent = a.get("npc") or a.get("player")
                if ent is None:
                    continue
                if "npc" in a:
                    nid, name = ident(ent.get("id"))
                    ev = {"t": tick, "k": "npc", "idx": ent.get("index"), "nid": nid, "name": name,
                          "mv": kind, "c": ent.get("coord"), "b": []}
                    if "newcoord" in a:
                        ev["nc"] = a["newcoord"]
                    if "spawnangle" in a:
                        ev["angle"] = a["spawnangle"]
                    if "multinpc" in rest:
                        ev["multi"] = True
                    for st in ("step", "step1", "step2"):
                        if st in a:
                            ev[st] = a[st]
                else:
                    if ent.get("index") != self.local_index:
                        continue
                    ev = {"t": tick, "k": "pl", "mv": kind, "c": ent.get("coord"), "b": []}
                    if "newcoord" in a:
                        ev["nc"] = a["newcoord"]
                self.events.append(ev)
                ctx = ev
                ctx_indent = indent
                continue
            if indent != 4:
                continue
            a = parse_attrs(rest) if rest else {}
            if kind.startswith("map_projanim") or kind.startswith("projanim"):
                pid, pname = ident(a.get("id"))
                proj = {"t": tick, "k": "proj", "id": pid, "name": pname, "st": a.get("starttime"),
                        "et": a.get("endtime"), "ang": a.get("angle"), "prog": a.get("progress"),
                        "sh": a.get("startheight"), "eh": a.get("endheight"), "ver": kind}
                self.events.append(proj)
            elif kind == "sound_area":
                self.events.append({"t": tick, "k": "snd", "id": a.get("id"), "loops": a.get("loops"),
                                    "delay": a.get("delay", 0), "range": a.get("range"),
                                    "size": a.get("size"), "c": a.get("coord")})
            elif kind == "synth_sound":
                self.events.append({"t": tick, "k": "syn", "id": a.get("id"), "loops": a.get("loops", 1),
                                    "delay": a.get("delay", 0)})
            elif kind == "map_anim":
                mid, mname = ident(a.get("id"))
                self.events.append({"t": tick, "k": "man", "id": mid, "name": mname,
                                    "delay": a.get("delay", 0), "height": a.get("height", 0),
                                    "c": a.get("coord")})
            elif kind.startswith("loc_add_change") or kind == "loc_del" or kind == "loc_anim" or kind.startswith("loc_merge"):
                lid, lname = ident(a.get("id"))
                aid, aname = ident(a.get("anim"))
                self.events.append({"t": tick, "k": "loc", "op": kind, "id": lid, "name": lname,
                                    "c": a.get("coord"), "shape": a.get("shape"), "rot": a.get("rotation"),
                                    "anim": aid, "animname": aname})
            elif kind.startswith("obj_"):
                oid, oname = ident(a.get("id"))
                self.events.append({"t": tick, "k": "obj", "op": kind, "id": oid, "name": oname,
                                    "count": a.get("count"), "c": a.get("coord"), "raw": rest})
            elif kind in ("varp_small", "varp_large", "varbit"):
                vid, vname = ident(a.get("id"))
                self.events.append({"t": tick, "k": "var", "id": vid, "name": vname,
                                    "old": a.get("old"), "new": a.get("new"), "vk": kind})
            elif kind == "message_game":
                self.events.append({"t": tick, "k": "msg", "type": a.get("type"), "text": a.get("message")})
            elif kind == "update_stat_v2":
                self.events.append({"t": tick, "k": "stat", "stat": a.get("stat"), "level": a.get("level")})
            elif kind == "runclientscript":
                cid, cname = ident(a.get("id"))
                self.events.append({"t": tick, "k": "cs", "id": cid, "name": cname, "raw": rest})
            elif kind.startswith("midi_"):
                self.events.append({"t": tick, "k": "mus", "op": kind, "raw": rest,
                                    "id": a.get("id") if isinstance(a.get("id"), int) else ident(a.get("id"))[0]})
            elif kind in ("cam_shake", "cam_reset", "cam_moveto", "cam_lookat", "minimap_toggle", "hint_arrow"):
                self.events.append({"t": tick, "k": "ui", "op": kind, "raw": rest})
            elif UI_KINDS.match(kind) or kind.startswith("update_inv"):
                self.events.append({"t": tick, "k": "ui", "op": kind, "raw": rest})
            elif kind.startswith("npc_headicon"):
                self.events.append({"t": tick, "k": "ui", "op": kind, "raw": rest})
        return self


def cache_path(transcript):
    base = os.path.basename(transcript).replace(".txt", "")
    variant = os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(transcript)))))
    return os.path.join(CACHE, f"{variant}_{base}.json.gz")


def load(transcript):
    if transcript.endswith("ticklog.tsv"):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "colosseum"))
        from colo_ours import convert
        return convert(transcript)
    cp = cache_path(transcript)
    if os.path.exists(cp) and os.path.getmtime(cp) >= os.path.getmtime(transcript):
        with gzip.open(cp, "rt") as f:
            d = json.load(f)
        if d.get("v") == PARSE_VERSION:
            return d
    p = Parser(transcript).run()
    d = {"v": PARSE_VERSION, "path": transcript, "header": p.header, "events": p.events}
    os.makedirs(CACHE, exist_ok=True)
    with gzip.open(cp, "wt") as f:
        json.dump(d, f)
    return d


WAVE_START = re.compile(r"Wave: (\d+)")
WAVE_DONE = re.compile(r"Wave (\d+) completed! Wave duration: (?:<col=[0-9a-f]+>)?([0-9:.]+)")
SOL_START = "Sol Heredit jumps down from his seat"
SOL_DONE = re.compile(r"Colosseum duration: (?:<col=[0-9a-f]+>)?([0-9:.]+)")


def waves(d):
    """[{wave, t0, t_done, duration, run}] in order. A run restarts at 'Wave: 1'."""
    out = []
    run = 0
    cur = None
    for e in d["events"]:
        if e["k"] != "msg" or not isinstance(e.get("text"), str):
            continue
        m = WAVE_START.search(e["text"])
        if SOL_START in e["text"] or (m and "completed" not in e["text"]):
            w = 12 if SOL_START in e["text"] else int(m.group(1))
            if w == 1 or cur is None or w != cur["wave"] + 1:
                run += 1
            cur = {"wave": w, "t0": e["t"], "t_done": None, "duration": None, "run": run}
            out.append(cur)
            continue
        m = WAVE_DONE.search(e["text"])
        if m and cur is not None and int(m.group(1)) == cur["wave"]:
            cur["t_done"] = e["t"]
            cur["duration"] = m.group(2)
        m = SOL_DONE.search(e["text"])
        if m and cur is not None and cur["wave"] == 12:
            cur["t_done"] = e["t"]
            cur["duration"] = "total " + m.group(1)
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd = sys.argv[1]
    paths = sys.argv[2:]
    if cmd == "parse":
        for p in paths:
            d = load(p)
            print(p, len(d["events"]), "events", d["header"].get("activity"))
    elif cmd == "waves":
        for p in paths:
            d = load(p)
            ws = waves(d)
            sid = os.path.basename(p)
            for w in ws:
                print(f"{sid}\t{d['header'].get('version')}\t{d['header'].get('activity')}\trun{w['run']}\twave {w['wave']}\tt0 {w['t0']}\tdone {w['t_done']}\t{w['duration']}")
    else:
        print(__doc__)


if __name__ == "__main__":
    main()


def player_track(d):
    """[(tick, coord)] of the recorder: the tile after each tick's movement."""
    out = []
    for e in d["events"]:
        if e["k"] == "pl":
            out.append((e["t"], tuple(e.get("nc") or e["c"])))
    return out


def player_at(track, t, before=False):
    """The recorder's tile at the END of tick t (before=True: at the start of t)."""
    import bisect
    keys = [x[0] for x in track]
    i = bisect.bisect_right(keys, t - 1 if before else t) - 1
    return track[i][1] if i >= 0 else None


def expand(paths):
    """Arguments as transcript paths; '@file' reads one path per line."""
    out = []
    for p in paths:
        if p.startswith("@"):
            out += [x for x in open(p[1:]).read().split() if x]
        else:
            out.append(p)
    return out
