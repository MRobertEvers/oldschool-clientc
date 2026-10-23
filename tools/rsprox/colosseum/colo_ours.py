#!/usr/bin/env python3
"""Our scriptrun tick log (ticklog.tsv) as the event model of rsprox_colosseum.load,
so every colo_*.py tool reads a run of ours exactly as it reads a recording.

Tiles are moved from the arena copy to the template (the landing tile is
template 1824,3094, README §8). Hitsplats: ours 28/26 are the wire's 16/12 for
the recorder. Prayers come from the raider row's bit mask (varbit 4101's bits).
Waves come from varp4133 (start) and varp4131 (completion), the same ticks the
recordings' messages carry.
"""
import os

LAND = (1824, 3094)


ANGLES = {0: "south", 256: "south-west", 512: "west", 768: "north-west", 1024: "north",
          1280: "north-east", 1536: "east", 1792: "south-east"}


def unpack(c):
    c = int(c)
    return ((c >> 14) & 0x3fff, c & 0x3fff, (c >> 28) & 3)


_LOCNAME = None


def locname(i):
    global _LOCNAME
    if _LOCNAME is None:
        import re
        _LOCNAME = {}
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "OSRS-Content", "osrs-content", "configs", "all.loc.compack")
        for m in re.finditer(r"^(\d+)=(\S+)", open(root, errors="replace").read(), re.M):
            _LOCNAME[int(m.group(1))] = m.group(2)
    return _LOCNAME.get(i)


_VARBITS = None


def varbits_of(varp):
    """[(varbit id, startbit, endbit)] packed in our varp, from configs/all.varbit."""
    global _VARBITS
    if _VARBITS is None:
        import re
        _VARBITS = {}
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "OSRS-Content", "osrs-content", "configs", "all.varbit")
        txt = open(root, errors="replace").read()
        for m in re.finditer(r"^\[varb(\d+)_[^\]]*\]\nbasevar=varp(\d+)_\S*\nstartbit=(\d+)\nendbit=(\d+)", txt, re.M):
            _VARBITS.setdefault(int(m.group(2)), []).append((int(m.group(1)), int(m.group(3)), int(m.group(4))))
    return _VARBITS.get(varp, [])


def convert(path):
    rows = []
    for line in open(path, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 4 or not f[1].isdigit():
            continue
        rows.append(f)
    # the copy's offset: the first player tile far from the template after the landing
    off = (0, 0)
    for f in rows:
        if f[2] == "player_tile":
            x, z = int(f[4]), int(f[5])
            if x > 4000 and z < 1000:
                off = (x - LAND[0], z - LAND[1])
                break

    def tr(x, z):
        if x > 4000 and z < 1000:
            return (x - off[0], z - off[1])
        return (x, z)

    ev = []
    cur = {}       # (tick, slot) -> npc event
    tile = {}      # slot -> template tile
    ntype = {}
    pl = {}        # tick -> pl event
    last_pl = None
    cur_wave = [1]

    def npc_ev(t, slot, mv="idle"):
        k = (t, slot)
        if k not in cur:
            c = tile.get(slot, (0, 0))
            e = {"t": t, "k": "npc", "idx": slot, "nid": ntype.get(slot), "name": None, "mv": mv,
                 "c": [c[0], c[1], 0], "b": []}
            cur[k] = e
            ev.append(e)
        return cur[k]

    def pl_ev(t):
        if t not in pl:
            c = last_pl or (0, 0)
            e = {"t": t, "k": "pl", "mv": "idle", "c": [c[0], c[1], 0], "b": []}
            pl[t] = e
            ev.append(e)
        return pl[t]

    for f in rows:
        t = int(f[1])
        kind = f[2]
        a = f[3:]
        if kind == "npc_spawn":
            slot, nid = int(a[0]), int(a[1])
            x, z, lv = unpack(a[2])
            ntype[slot] = nid
            tile[slot] = tr(x, z)
            e = npc_ev(t, slot, "add")
            e["mv"] = "add"
            e["c"] = [tile[slot][0], tile[slot][1], lv]
            e["angle"] = ANGLES.get(int(a[3]), a[3])
        elif kind == "npc_info" and a[2] == "1":
            # The add's angle as NPC_INFO sent it (end of tick, after the script's
            # npc_facedir); the spawn row's is from the moment of npc_add.
            e = cur.get((t, int(a[0])))
            if e and e["mv"] == "add":
                e["angle"] = ANGLES.get(int(a[3]), a[3])
        elif kind == "npc_tile":
            slot = int(a[0])
            new = tr(int(a[1]), int(a[2]))
            old = tile.get(slot, new)
            ntype.setdefault(slot, int(a[4]) if a[4].lstrip("-").isdigit() else None)
            e = npc_ev(t, slot)
            if e["mv"] != "add":
                d = max(abs(new[0] - old[0]), abs(new[1] - old[1]))
                e["mv"] = "run" if d == 2 else ("walk" if d == 1 else "teleport")
                e["c"] = [old[0], old[1], 0]
                e["nc"] = [new[0], new[1], 0]
            tile[slot] = new
        elif kind == "npc_anim":
            e = npc_ev(t, int(a[0]))
            e["b"].append(["sequence", {"id": int(a[2]), "delay": int(a[3])}])
        elif kind == "npc_spotanim":
            e = npc_ev(t, int(a[0]))
            e["b"].append(["spotanim", {"id": int(a[2]), "height": int(a[3]), "delay": int(a[4]), "slot": 0}])
        elif kind == "npc_face":
            e = npc_ev(t, int(a[0]))
            x, z = int(a[2]), int(a[3])
            if x == -1:
                e["b"].append(["facing", {"player": z} if z >= 0 else {"type": "reset"}])
            elif x >= 0:
                c = tr(x, z)
                e["b"].append(["facing", {"coord": (c[0], c[1], 0)}])
        elif kind == "hit_npc":
            e = npc_ev(t, int(a[0]))
            e["b"].append(["hit", {"id": 16 if int(a[2]) > 0 else 12, "value": int(a[2])}])
        elif kind == "npc_headbar":
            e = npc_ev(t, int(a[0]))
            e["b"].append(["headbar", {"id": int(a[2]), "fill": int(a[3])}])
        elif kind == "npc_say":
            e = npc_ev(t, int(a[0]))
            e["b"].append(["say", {"text": a[6] if len(a) > 6 else ""}])
        elif kind == "npc_free":
            e = npc_ev(t, int(a[0]))
            e["mv"] = "del"
        elif kind == "projectile":
            sx, sz, sl = unpack(a[0])
            dx, dz, dl = unpack(a[1])
            s = tr(sx, sz)
            dd = tr(dx, dz)
            ev.append({"t": t, "k": "proj", "id": int(a[3]), "name": None, "st": int(a[4]), "et": int(a[5]),
                       "ang": None, "prog": None, "sh": None, "eh": None, "ver": "ours",
                       "src": {"coord": (s[0], s[1], sl)}, "dst": {"coord": (dd[0], dd[1], dl)}})
        elif kind == "map_spotanim":
            x, z, lv = unpack(a[0])
            c = tr(x, z)
            ev.append({"t": t, "k": "man", "id": int(a[1]), "name": None, "delay": int(a[3]),
                       "height": int(a[2]), "c": (c[0], c[1], lv)})
        elif kind == "player_tile":
            c = tr(int(a[1]), int(a[2]))
            e = pl_ev(t)
            prev = last_pl or c
            if prev != c:
                d = max(abs(c[0] - prev[0]), abs(c[1] - prev[1]))
                e["mv"] = "run" if d == 2 else ("walk" if d == 1 else "teleport")
                e["c"] = [prev[0], prev[1], 0]
                e["nc"] = [c[0], c[1], 0]
            last_pl = c
        elif kind == "hit_player":
            e = pl_ev(t)
            # `raw`: the damage the content dealt, before ::god zeroed the splat.
            raw = int(a[-1]) if a[-1].lstrip("-").isdigit() else int(a[2])
            e["b"].append(["hit", {"id": 16 if int(a[3]) == 28 else (12 if int(a[3]) == 26 else int(a[3])), "value": int(a[2]),
                                   "raw": raw}])
        elif kind == "raider":
            ev.append({"t": t, "k": "var", "id": 4101, "name": "prayer_allactive", "old": None, "new": int(a[3]), "vk": "varbit"})
        elif kind == "varp":
            vid = int(a[1])
            ev.append({"t": t, "k": "var", "id": vid, "name": a[-2] if len(a) > 7 else None, "old": None,
                       "new": int(a[2]), "vk": "varp"})
            # the varbits packed in it, as the recordings carry them (vk varbit)
            for vb, lo, hi in varbits_of(vid):
                if vb == 4101:      # prayers come from the raider row
                    continue
                ev.append({"t": t, "k": "var", "id": vb, "name": None, "old": None, "vk": "varbit",
                           "new": (int(a[2]) >> lo) & ((1 << (hi - lo + 1)) - 1)})
            if vid == 9456:
                cur_wave[0] = int(a[2])
            if vid == 4133 and int(a[2]) > 0:
                w = cur_wave[0]
                # Sol's line comes a tick after varp4133 (W+1, colo_sol_open_1); a wave's on it
                if w >= 12:
                    ev.append({"t": t + 1, "k": "msg", "type": "gamemessage", "text": "Sol Heredit jumps down from his seat..."})
                else:
                    ev.append({"t": t, "k": "msg", "type": "gamemessage", "text": f"Wave: {w}"})
            if vid == 4131:
                ev.append({"t": t, "k": "msg", "type": "gamemessage",
                           "text": f"Wave {cur_wave[0]} completed! Wave duration: 0:00.00"})
        elif kind == "loc_set":
            x, z, l = unpack(a[0])
            c = tr(x, z)
            lid = int(a[1])
            ev.append({"t": t, "k": "loc", "op": "loc_del" if lid < 0 else "loc_add_change_v2", "id": lid,
                       "name": locname(lid) if lid >= 0 else None, "c": (c[0], c[1], l), "shape": int(a[2]),
                       "rot": int(a[3]), "anim": None, "animname": None})
        elif kind == "sound":
            if len(a) > 6 and a[6].split(" ")[0] in ("synth", "npc"):
                ev.append({"t": t, "k": "syn", "id": int(a[1]), "loops": int(a[2]), "delay": int(a[3])})
            elif len(a) > 6 and a[6].split(" ")[0] == "area":
                x, z, l = unpack(a[4])
                c = tr(x, z)
                ev.append({"t": t, "k": "snd", "id": int(a[1]), "loops": int(a[2]), "delay": int(a[3]),
                           "range": int(a[5]), "c": (c[0], c[1], l),
                           "size": int(a[6].split("size=")[1]) if "size=" in a[6] else None})
    ev.sort(key=lambda e: e["t"])
    return {"v": 0, "path": path, "header": {"activity": "ours", "version": "241"}, "events": ev}
