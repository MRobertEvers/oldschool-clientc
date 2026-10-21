"""Parse an RSProx transcript into per-tick events for the Guardian analysis.

    from tfa_events import load
    ev = load(path)     # list of dicts: tick, who, kind, ...

who: "me" (the recorder), "npc:<index>" (with name), or "zone".
kinds: tile (me/npc position after the tick's movement), add, del, move
(walk/run/crawl/teleport/telejump with from/to), seq, spot, hit, headbar,
transform, level, ops, face, exactmove, proj, sound_area, synth, loc_add,
loc_del, loc_anim, map_anim, message, varbit, varp, cam_shake, if_opensub,
if_closesub, if_settext, script, midi, jingle.
"""
import re

TICK = re.compile(r"^\[(\d+)\]$")
COORD = r"\((\d+), (\d+), (\d+)\)"
NPC_HEAD = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\), coord=" + COORD + r"\)(.*)$")
PLAYER_HEAD = re.compile(r"^ {8}\[(\w+)\] player=\(index=(\d+), name=([^,]*), coord=" + COORD + r"\)(.*)$")
TOP = re.compile(r"^ {4}\[([a-z0-9_]+)\] ?(.*)$")
SUB = re.compile(r"^ {12}\[([a-z0-9_]+)\] ?(.*)$")


def _kv(rest):
    out = {}
    for m in re.finditer(r"(\w+)=((?:\([^)]*\)\)?)|\"[^\"]*\"|\[[^\]]*\]|[^,]*)", rest):
        out[m.group(1)] = m.group(2).strip()
    return out


def _coord(s):
    m = re.search(COORD, s)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def _named_id(s):
    m = re.match(r"([a-z0-9_]+|-1) ?(?:\((\d+)\))?", s or "")
    if not m:
        return s, None
    return m.group(1), int(m.group(2)) if m.group(2) else None


def load(path, local_index=None):
    events = []
    tick = -1
    cur = None  # ("me"|"npc", idx, name)
    block = None
    me = local_index
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            cur = None
            block = None
            continue
        if tick < 0:
            if me is None:
                mm = re.match(r"local player index: (\d+)", line)
                if mm:
                    me = int(mm.group(1))
            continue
        m = TOP.match(line)
        if m and not line.startswith("     "):
            kind, rest = m.group(1), m.group(2)
            block = kind
            cur = None
            kv = _kv(rest)
            e = None
            if kind.startswith("map_projanim"):
                name, pid = _named_id(kv.get("id"))
                e = dict(kind="proj", id=name, idn=pid, **{k: kv.get(k) for k in
                         ("starttime", "endtime", "angle", "progress", "startheight", "endheight")})
            elif kind in ("loc_add_change_v2", "loc_add_change"):
                name, lid = _named_id(kv.get("id"))
                e = dict(kind="loc_add", id=name, idn=lid, at=_coord(rest), shape=kv.get("shape"),
                         rot=kv.get("rotation"), opflags=kv.get("opflags"))
            elif kind == "loc_del":
                e = dict(kind="loc_del", at=_coord(rest), shape=kv.get("shape"))
            elif kind == "loc_anim":
                name, sid = _named_id(kv.get("id"))
                e = dict(kind="loc_anim", id=name, at=_coord(rest))
            elif kind == "map_anim":
                name, sid = _named_id(kv.get("id"))
                e = dict(kind="map_anim", id=name, at=_coord(rest), delay=kv.get("delay"), height=kv.get("height"))
            elif kind == "sound_area":
                e = dict(kind="sound_area", id=kv.get("id"), at=_coord(rest), delay=kv.get("delay"),
                         range=kv.get("range"), loops=kv.get("loops"), size=kv.get("size"))
            elif kind == "synth_sound":
                e = dict(kind="synth", id=kv.get("id"), delay=kv.get("delay"), loops=kv.get("loops"))
            elif kind == "message_game":
                e = dict(kind="message", type=kv.get("type"), text=re.search(r'message="(.*)"$', rest).group(1)
                         if 'message="' in rest else "")
            elif kind == "varbit":
                name, vid = _named_id(kv.get("id"))
                e = dict(kind="varbit", id=name, idn=vid, old=kv.get("old"), new=kv.get("new"))
            elif kind in ("varp_small", "varp_large"):
                name, vid = _named_id(kv.get("id"))
                e = dict(kind="varp", id=name, idn=vid, old=kv.get("old"), new=kv.get("new"))
            elif kind == "cam_shake":
                e = dict(kind="cam_shake", **kv)
            elif kind == "if_opensub":
                e = dict(kind="if_opensub", com=kv.get("com"), iface=kv.get("id"), type=kv.get("type"))
            elif kind == "if_closesub":
                e = dict(kind="if_closesub", com=kv.get("com"))
            elif kind == "if_settext":
                e = dict(kind="if_settext", com=kv.get("com"), text=kv.get("text"))
            elif kind == "runclientscript":
                e = dict(kind="script", id=kv.get("id"), values=kv.get("values"))
            elif kind.startswith("midi_song"):
                e = dict(kind="midi", id=kv.get("id") or kv.get("primaryid"))
            elif kind.startswith("midi_jingle"):
                e = dict(kind="jingle", id=kv.get("id"))
            elif kind in ("if_sethide", "if_setevents_v2", "hint_arrow", "set_npc_update_origin"):
                e = dict(kind=kind, raw=rest)
            if e:
                e.update(tick=tick, who="zone")
                events.append(e)
            continue
        m = NPC_HEAD.match(line)
        if m:
            mk, idx, name, nid, x, z, lvl, rest = m.groups()
            cur = ("npc", int(idx), name)
            who = f"npc:{idx}"
            pos = (int(x), int(z), int(lvl))
            if mk == "add":
                kv = _kv(rest)
                events.append(dict(tick=tick, who=who, name=name, kind="add", at=pos, spawnangle=kv.get("spawnangle")))
            elif mk == "del":
                events.append(dict(tick=tick, who=who, name=name, kind="del", at=pos))
            elif mk in ("walk", "run", "crawl", "teleport", "telejump"):
                to = _coord(rest)
                events.append(dict(tick=tick, who=who, name=name, kind="move", mode=mk, frm=pos, at=to))
            continue
        m = PLAYER_HEAD.match(line)
        if m:
            mk, idx, pname, x, z, lvl, rest = m.groups()
            is_me = me is None or int(idx) == me
            cur = ("me", int(idx), pname) if is_me else ("other", int(idx), pname)
            pos = (int(x), int(z), int(lvl))
            if cur[0] == "other":
                if mk in ("walk", "run", "crawl", "teleport", "telejump"):
                    events.append(dict(tick=tick, who=f"pl:{pname}", kind="move", mode=mk, frm=pos, at=_coord(rest)))
                else:
                    events.append(dict(tick=tick, who=f"pl:{pname}", kind="tile", at=pos))
            if cur[0] == "me":
                if mk in ("walk", "run", "crawl", "teleport", "telejump"):
                    to = _coord(rest)
                    events.append(dict(tick=tick, who="me", kind="move", mode=mk, frm=pos, at=to))
                else:
                    events.append(dict(tick=tick, who="me", kind="tile", at=pos))
            continue
        m = SUB.match(line)
        if m and cur and cur[0] in ("me", "npc", "other"):
            kind, rest = m.group(1), m.group(2)
            who = "me" if cur[0] == "me" else (f"npc:{cur[1]}" if cur[0] == "npc" else f"pl:{cur[2]}")
            base = dict(tick=tick, who=who, name=cur[2])
            kv = _kv(rest)
            if kind == "sequence":
                name, sid = _named_id(kv.get("id"))
                events.append(dict(base, kind="seq", id=name, delay=kv.get("delay")))
            elif kind == "spotanim":
                name, sid = _named_id(kv.get("id"))
                events.append(dict(base, kind="spot", id=name, delay=kv.get("delay"), height=kv.get("height"),
                                   slot=kv.get("slot")))
            elif kind == "hit":
                events.append(dict(base, kind="hit", type=kv.get("id"), value=int(kv.get("value") or 0),
                                   delay=kv.get("delay")))
            elif kind == "headbar":
                events.append(dict(base, kind="headbar", id=kv.get("id"), fill=kv.get("fill"),
                                   removed=kv.get("removed")))
            elif kind == "transformation":
                name, tid = _named_id(kv.get("id"))
                events.append(dict(base, kind="transform", id=name))
                cur = (cur[0], cur[1], name)
            elif kind == "level_change":
                events.append(dict(base, kind="level", level=kv.get("level")))
            elif kind == "enabled_ops":
                events.append(dict(base, kind="ops", opflags=kv.get("opflags")))
            elif kind == "facing":
                events.append(dict(base, kind="face", raw=rest, at=_coord(rest)))
            elif kind == "exact_move" or kind == "exactmove":
                events.append(dict(base, kind="exactmove", raw=rest))
            elif kind in ("tinting", "say", "bas_change", "headicon_custom"):
                events.append(dict(base, kind=kind, raw=rest))
    return events
