"""The Archive Guardian's vines (`archive_guardian_tentacle`) from an
`--all-players --bas` transcript: every life block by block, and every
raider's tile, overhead and hits per tick, for gv_check.py.

    from gv_events import parse
    s = parse(path)
    s["vines"]   # [{"add": t, "del": t, "tile": (x, z), "face": (x, z) | None,
                 #   "instant": bool, "angle": str, "blocks": [(t, kind, text)]}]
    s["pos"]     # name -> {tick: (x, z)} (the tile at the end of the tick)
    s["pray"]    # name -> {tick: icon | None} (only on the ticks it was sent)
    s["hits"]    # name -> [(tick, type, value)]
    s["sounds"]  # [(tick, id, delay, range, size, (x, z))]
    s["boss"]    # [(tick, seq name)] the guardian's seqs
    s["boss_tile"]  # {tick: (x, z)} his south-west tile
"""
import re

TICK = re.compile(r"^\[(\d+)\]")
ENT = re.compile(r"^ {8}\[(\w+)\] (npc|player)=\(index=(\d+), (?:id=(\w+) \(\d+\)|name=([^,]+))"
                 r"(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+), \d\))?"
                 r"(?:.*spawnangle=([\w-]+))?")
SEQ = re.compile(r"^\[sequence\] id=(\w+|-1)(?: \(\d+\))?(?:, delay=(\d+))?")
HIT = re.compile(r"^\[hit\] id=(\d+), value=(\d+)")
FACE = re.compile(r"^\[facing\] coord=\((\d+), (\d+), \d\)(, instant=true)?")
STATUS = re.compile(r"^\[status\](?: overheadicon=(\d+))?$")
SOUND = re.compile(r"\[sound_area\] id=(\d+), loops=\d+(?:, delay=(\d+))?, range=(\d+)(?:, size=(\d+))?, coord=\((\d+), (\d+), \d\)")


def parse(path):
    vines, live = [], {}
    pos, pray, hits = {}, {}, {}
    sounds, boss, boss_tile = [], [], {}
    t = 0
    who = None
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            t = int(m.group(1))
            who = None
            continue
        m = ENT.match(line)
        if m:
            kind, ent, idx, npc, name = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
            x, z = int(m.group(6)), int(m.group(7))
            if m.group(8):
                x, z = int(m.group(8)), int(m.group(9))
            if ent == "player":
                who = ("pl", name)
                pos.setdefault(name, {})[t] = (x, z)
            elif npc == "archive_guardian_tentacle":
                if kind == "add":
                    v = {"add": t, "del": None, "tile": (x, z), "face": None, "instant": False,
                         "angle": m.group(10), "blocks": [], "idx": idx}
                    vines.append(v)
                    live[idx] = v
                    who = ("vine", v)
                elif idx in live:
                    v = live[idx]
                    who = ("vine", v)
                    if kind == "del":
                        v["del"] = t
                        del live[idx]
                    elif kind != "idle":
                        v["blocks"].append((t, "move", kind))
                else:
                    who = None
            elif npc in ("tfa_guardian", "tfa_guardian_unshielded", "tfa_guardian_start", "tfa_guardian_dead"):
                who = ("boss", None)
                boss_tile[t] = (x, z)
            else:
                who = None
            continue
        if re.match(r"^ {4}\[", line):
            who = None
            m = SOUND.search(line)
            if m:
                sounds.append((t, int(m.group(1)), int(m.group(2) or 0), int(m.group(3)), int(m.group(4) or 0),
                               (int(m.group(5)), int(m.group(6)))))
            continue
        if who is None or not line.startswith("            "):
            continue
        s = line.strip()
        if who[0] == "pl":
            name = who[1]
            m = HIT.match(s)
            if m:
                hits.setdefault(name, []).append((t, int(m.group(1)), int(m.group(2))))
                continue
            m = STATUS.match(s)
            if m:
                pray.setdefault(name, {})[t] = int(m.group(1)) if m.group(1) else None
            continue
        if who[0] == "boss":
            m = SEQ.match(s)
            if m:
                boss.append((t, m.group(1)))
            continue
        v = who[1]
        m = FACE.match(s)
        if m and t == v["add"] and v["face"] is None:
            v["face"] = (int(m.group(1)), int(m.group(2)))
            v["instant"] = bool(m.group(3))
        v["blocks"].append((t, s.split("]")[0] + "]", s))
    return {"vines": vines, "pos": pos, "pray": pray, "hits": hits, "sounds": sounds,
            "boss": boss, "boss_tile": boss_tile}


def at(d, t):
    """The latest value in {tick: v} at or before t."""
    best = None
    bt = -1
    for k, v in d.items():
        if bt < k <= t:
            best, bt = v, k
    return best
