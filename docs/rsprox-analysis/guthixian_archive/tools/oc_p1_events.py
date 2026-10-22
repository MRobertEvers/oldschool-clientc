"""Ocellus's phase 1 as one event list per life, the same shape from an
`--all-players` transcript and from our tick log, for oc_p1_check.py.

    from oc_p1_events import recording, ours
    lives = recording(path)        # [{"t0":, "t1":, "ev": [(tick, kind, *fields)]}]
    lives = ours(ticklog, origin)  # origin: our instance tile of the arena's middle

Tiles are relative to the arena's middle (4127,5673) in both. Kinds:
  oc_move (x, z, how)      oc_seq (name)        oc_spot (name, slot, delay)
  oc_say (text)            oc_face (what)       oc_hit (type, value)
  pl_hit (who, type, value)    anim (name, x, z, delay)
  proj (name, start, end, target)    sound (id, delay, range, size, x, z)
  meter (value)            loc (name, x, z)
"""
import re

MID = (4127, 5673)
KIT = re.compile(r"thrall|leagues|glaive|ii_dragon|toxic|amethyst|arrow|bolt|sanguinesti|"
                 r"dragon_|blood|xbows|zcb|chin|dart|venator|scorching|windsurge|"
                 r"waterstrike|firewave|ice_|barrage|bloom|druidic|failedspell|ruby|onyx")
TICK = re.compile(r"^\[(\d+)\]")
ENT = re.compile(r"^ {8}\[(\w+)\] (npc|player)=\(index=\d+, (?:id=(\w+) \(\d+\)|name=([^,]+))"
                 r"(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), \d\)\)(?:, newcoord=\((\d+), (\d+))?")
SEQ = re.compile(r"\[sequence\] id=(\w+|-1)")
SPOT = re.compile(r"\[spotanim\](?: slot=(\d+),)? id=(\w+|-1)(?: \(\d+\))?(?:, delay=(\d+))?")
SAY = re.compile(r'\[say\] text="([^"]*)"')
FACE = re.compile(r"\[facing\] (\S+)")
HIT = re.compile(r"\[hit\] id=(\d+), value=(\d+)")
ANIM = re.compile(r"\[map_anim\] id=(\w+) \(\d+\)(?:, delay=(\d+))?(?:, height=\d+)?, coord=\((\d+), (\d+), \d\)")
PROJ = re.compile(r"\[(?:map_projanim\w*|scriptedproj_add)\] id=(\w+) \(\d+\),(?: slot=\d+,)? starttime=(\d+), endtime=(\d+)")
TARGET = re.compile(r"\[target\] coord=\((\d+), (\d+), \d\)(?:, player=\(index=\d+, name=([^,]+))?")
SOUND = re.compile(r"\[sound_area\] id=(\d+), loops=\d+(?:, delay=(\d+))?, range=(\d+)(?:, size=(\d+))?, coord=\((\d+), (\d+), \d\)")
METER = re.compile(r"\[varbit\] id=hpbar_hud_extra_current \(\d+\), old=-?\d+, new=(-?\d+)")
LOC = re.compile(r"\[loc_add_change\w*\] id=(\w+) \(\d+\), coord=\((\d+), (\d+), \d\)")


def rel(x, z):
    return int(x) - MID[0], int(z) - MID[1]


def recording(path):
    lives = []
    cur_life = None
    t = 0
    who = None
    proj = None
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            t = int(m.group(1))
            who = None
            continue
        m = ENT.match(line)
        if m:
            kind, ent, npc, name = m.group(1), m.group(2), m.group(3), m.group(4)
            if ent == "npc":
                who = "oc" if npc == "tfa_ocellus_p1" else None
                if npc == "tfa_ocellus_p1" and cur_life is None:
                    cur_life = {"t0": t, "t1": None, "ev": []}
                    lives.append(cur_life)
                if npc == "tfa_ocellus_p2" and cur_life is not None and cur_life["t1"] is None:
                    cur_life["t1"] = t
                    cur_life = None
                if who == "oc" and cur_life and m.group(7):
                    cur_life["ev"].append((t, "oc_move") + rel(m.group(7), m.group(8)) + (kind,))
            else:
                who = ("pl", name)
            continue
        if cur_life is None:
            continue
        ev = cur_life["ev"]
        if line.startswith("            ") and who is not None:
            s = line.strip()
            if who == "oc":
                if KIT.search(s):
                    continue
                if (m := SEQ.search(s)):
                    ev.append((t, "oc_seq", m.group(1)))
                elif (m := SPOT.search(s)) and s.startswith("[spotanim]"):
                    if m.group(2) == "-1" and m.group(3):
                        continue
                    ev.append((t, "oc_spot", m.group(2), int(m.group(1) or 0), int(m.group(3) or 0)))
                elif (m := SAY.search(s)):
                    ev.append((t, "oc_say", m.group(1)))
                elif (m := FACE.search(s)):
                    ev.append((t, "oc_face", m.group(1)))
                elif (m := HIT.search(s)):
                    ev.append((t, "oc_hit", int(m.group(1)), int(m.group(2))))
            elif who[0] == "pl" and (m := HIT.search(s)):
                ev.append((t, "pl_hit", who[1], int(m.group(1)), int(m.group(2))))
            continue
        if re.match(r"^ {4}\[", line):
            who = None
        if (m := ANIM.search(line)) and not KIT.search(m.group(1)):
            ev.append((t, "anim", m.group(1)) + rel(m.group(3), m.group(4)) + (int(m.group(2) or 0),))
        elif (m := PROJ.search(line)) and not KIT.search(m.group(1)):
            proj = [t, "proj", m.group(1), int(m.group(2)), int(m.group(3)), None]
            ev.append(proj)
        elif (m := TARGET.search(line)) and proj is not None and proj[5] is None:
            proj[5] = m.group(3) or rel(m.group(1), m.group(2))
        elif (m := SOUND.search(line)):
            if m.group(1) not in ("211", "212", "2700", "918"):
                ev.append((t, "sound", int(m.group(1)), int(m.group(2) or 0), int(m.group(3)),
                           int(m.group(4) or 0)) + rel(m.group(5), m.group(6)))
        elif (m := METER.search(line)):
            ev.append((t, "meter", int(m.group(1))))
        elif (m := LOC.search(line)):
            ev.append((t, "loc", m.group(1)) + rel(m.group(2), m.group(3)))
    for life in lives:
        life["ev"] = [tuple(e) for e in life["ev"]]
    return lives


def ours(path, origin, names):
    """`origin`: our instance (x, z) of the arena's middle; `names`: id -> name
    for seqs, spotanims and map anims (from the transcripts)."""
    ox, oz = origin
    lives = []
    cur = None
    slot = None
    for line in open(path, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 9 or not f[1].isdigit():
            continue
        t, k, v = int(f[1]), f[2], f[3:]
        label = f[9] if len(f) > 9 else ""

        def xz(c):
            c = int(c)
            return ((c >> 14) & 0x3FFF) - ox, (c & 0x3FFF) - oz
        if k == "npc_retype" and v[2] == "16674":
            slot = v[0]
            cur = {"t0": t, "t1": None, "ev": []}
            lives.append(cur)
            continue
        if k == "npc_retype" and v[2] == "16675" and cur:
            cur["t1"] = t
            cur = None
            continue
        if cur is None:
            continue
        ev = cur["ev"]
        if v and v[0] == slot and k in ("npc_anim", "npc_spotanim", "npc_say", "npc_face", "npc_tile", "hit_npc", "npc_heal"):
            if k == "npc_anim":
                ev.append((t, "oc_seq", names.get(int(v[2]), v[2]) if v[2] != "-1" else "-1"))
            elif k == "npc_spotanim":
                ev.append((t, "oc_spot", names.get(int(v[2]), v[2]) if v[2] != "-1" else "-1", int(v[5]), int(v[4])))
            elif k == "npc_say":
                ev.append((t, "oc_say", label))
            elif k == "npc_face":
                # x = -1: an entity face, z its target, or its reset (z = -1).
                if v[2] == "-2":
                    face = "angle"
                elif v[2] != "-1":
                    face = "coord"
                else:
                    face = "type=reset" if v[3] == "-1" else "entity"
                ev.append((t, "oc_face", face))
            elif k == "npc_tile":
                ev.append((t, "oc_move", int(v[1]) - ox, int(v[2]) - oz, "move"))
            elif k == "hit_npc":
                ev.append((t, "oc_hit", int(v[3]), int(v[2])))
            elif k == "npc_heal":
                ev.append((t, "oc_heal", int(v[2])))
        elif k == "hit_player":
            ev.append((t, "pl_hit", "p" + v[0], int(v[3]), int(v[2])))
        elif k == "map_spotanim":
            ev.append((t, "anim", names.get(int(v[1]), v[1])) + xz(v[0]) + (int(v[3]),))
        elif k == "projectile":
            ev.append((t, "proj", names.get(int(v[3]), v[3]), int(v[4]), int(v[5]),
                       "player" if int(v[2]) < 0 else xz(v[1])))
        elif k == "sound" and "area" in label:
            s = re.search(r"size=(\d+)", label)
            ev.append((t, "sound", int(v[1]), int(v[3]), int(v[5]), int(s.group(1)) if s else 0) + xz(v[4]))
        elif k == "loc_set":
            ev.append((t, "loc", names.get(int(v[1]), v[1])) + xz(v[0]))
    return lives


def names_from(paths):
    """id -> name for every `name (id)` pair the transcripts print."""
    out = {}
    pat = re.compile(r"id=(\w+) \((\d+)\)")
    for p in paths:
        for line in open(p, errors="replace"):
            for m in pat.finditer(line):
                out[int(m.group(2))] = m.group(1)
    return out
