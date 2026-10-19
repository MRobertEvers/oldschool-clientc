#!/usr/bin/env python3
"""A CoX room against its RSProx recordings, six ways: how each npc FACES when
it appears, its overhead HEALTH BARS, the boss HP OVERLAY, every loc's
ROTATION and shape, the SOUNDS, and how long dropped objs last (DESPAWN).

    python3 tools/cox/rsprox_room_audit.py <room> --rec ~/.rsprox/binary/archive/{820,843,3389}.txt \\
        --layouts <room_layouts.txt> --ours <run dir>...

<room> is QD.COX.ROOM's name (tekton, vasa, ...). Each --ours dir holds a
`ticklog.tsv` from the room's own test, and its basename ends in the run's n
(vespula5, tekton_5, v5); --layouts is the probe's answers, one
"room <name> n <n> v <variant> t <turn> at <ox>,<oz>,<level>" line each
(test/raids/_cox_room_probe.lua), which places our run's room.

Frames. RSProx prints instanced tiles in the TEMPLATE frame (chunk-mirrored
when the turn is odd: rsprox_room.Room.fix); angles and loc rotations it
prints as the instance's. Ours are all instance. Both are reported as
template tiles; angles as instance and template (instance = template + 512 x
turn); loc rotations as instance and template (instance = template + turn).
A loc whose instance rotation matches across turns carries a FIXED rotation
(Vespula's do); one whose template rotation matches turns with the room.
"""
import argparse
import math
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsprox_room import load_room, TURNS  # noqa: E402

CONTENT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "OSRS-Content", "osrs239-content")

# the room's own npcs (all.npc.compack), which find its window and its cell
ROOMS = {
    "tekton": range(7540, 7546), "vasa": range(7565, 7569), "vanguards": range(7525, 7530),
    "vespula": range(7530, 7540), "guardians": range(7569, 7573), "shamans": range(7573, 7576),
    "mystics": range(7604, 7607), "muttadiles": range(7561, 7565), "crabs": range(7576, 7584),
    "icedemon": range(7584, 7586), "tightrope": range(7559, 7561), "thieving": range(7602, 7604),
    "scavenger_small": range(7548, 7550), "scavenger_large": range(7548, 7550),
    "olm": range(7550, 7558),
}
HPBAR_NPC_VARP = 1683
BOSS_VARP, BOSS_BIT = 243, 28          # varb12401_hpbar_hud_boss
TICK = re.compile(r"^\[(\d+)\]$")
ACTOR = re.compile(r"^\s{8}\[(\w+)\] npc=\(index=(\d+), id=(?:([a-z0-9_]+) \()?(\d+)\)?, coord=\((\d+), (\d+), (\d+)\)(?:, angle=(\d+))?")
SUB = re.compile(r"^\s{12}\[(\w+)\] ?(.*)$")
TOP = re.compile(r"^\s{4}\[(\w+)\] ?(.*)$")
COORD = re.compile(r"coord=\((\d+), (\d+), (\d+)\)")
IDREF = re.compile(r"id=(?:([a-z0-9_]+) \((-?\d+)\)|(-?\d+))")


def num(text, key):
    m = re.search(r"\b%s=(-?\d+)" % key, text)
    return int(m.group(1)) if m else None


def idref(text):
    m = IDREF.search(text)
    if not m:
        return None, None
    if m.group(1):
        return m.group(1), int(m.group(2))
    return None, int(m.group(3))


def compack(name):
    table = {}
    with open(os.path.join(CONTENT, "configs", name), encoding="latin-1") as h:
        for line in h:
            k, _, v = line.strip().partition("=")
            if k.isdigit():
                table[int(k)] = v
    return table


NPCS, LOCS = compack("all.npc.compack"), compack("all.loc.compack")
LOC_SIZE = {}
_cur = None
with open(os.path.join(CONTENT, "configs", "all.loc"), encoding="latin-1") as _h:
    for _line in _h:
        _line = _line.strip()
        if _line.startswith("[") and _line.endswith("]"):
            _cur = _line[1:-1]
        elif _cur and (_line.startswith("width=") or _line.startswith("length=")):
            k, _, v = _line.partition("=")
            if v.isdigit():
                w, l = LOC_SIZE.get(_cur, (1, 1))
                LOC_SIZE[_cur] = (int(v), l) if k == "width" else (w, int(v))


def world_of(t, lx, lz):
    """Template room-local -> instance room-local (cox.rs2 ~cox_room_at)."""
    L = 31
    if t == 1:
        return lz, L - lx
    if t == 2:
        return L - lx, L - lz
    if t == 3:
        return L - lz, lx
    return lx, lz


def tmpl_of(t, dx, dz):
    L = 31
    if t == 1:
        return L - dz, dx
    if t == 2:
        return L - dx, L - dz
    if t == 3:
        return dz, L - dx
    return dx, dz


def loc_sw(name, point, rot, turn):
    """A loc's template south-west tile from the template point of its INSTANCE
    south-west tile (what both sides print), its size and instance rotation."""
    if turn is None or point is None:
        return point
    w, l = LOC_SIZE.get(name, (1, 1))
    if rot % 2 == 1:
        w, l = l, w
    x0, z0 = world_of(turn, point[0], point[1])
    pts = [tmpl_of(turn, x0 + i, z0 + j) for i in (0, w - 1) for j in (0, l - 1)]
    return (min(p[0] for p in pts), min(p[1] for p in pts))
OBJS = compack("all.obj.compack") if os.path.exists(os.path.join(CONTENT, "configs", "all.obj.compack")) else {}
SOUNDS = {}
with open(os.path.join(CONTENT, "pack", "4_soundeffects.pack"), encoding="latin-1") as _h:
    for _line in _h:
        _k, _, _v = _line.strip().partition("=")
        if _k.isdigit():
            SOUNDS[int(_k)] = _v


def angle_toward(dx, dz):
    """The OSRS angle (0 S, 512 W, 1024 N, 1536 E) facing along (dx, dz)."""
    return int(round(math.atan2(-dx, -dz) * 2048 / (2 * math.pi))) % 2048


def variant_of(origin):
    ox = origin[0]
    return ((ox // 64) - 51) * 2 + (ox % 64) // 32


class Facts:
    """One run's six categories, template tiles throughout."""

    def __init__(self, tag):
        self.tag = tag
        self.spawn = []            # (type name, (lx, lz), instance angle, template angle)
        self.bars = defaultdict(Counter)   # type -> Counter: hits, bars, bar_no_hit, hit_no_bar, id:<n>
        self.overlay = []          # (tick from first, what)
        self.locs = {}             # (name, (lx, lz), shape) -> set of (instance rot, template rot)
        self.drops = defaultdict(Counter)  # obj -> Counter of duration ticks
        self.sounds = Counter()    # (name, kind)


# ------------------------------------------------------------------ recorded
import rsprox_room as _RR  # noqa: E402
_detect_turn = _RR.detect_turn


def _lenient_turn(R, npc_ids, anchor=None):
    try:
        return _detect_turn(R, npc_ids, anchor)
    except AssertionError:
        return None


_RR.detect_turn = _lenient_turn


def anchored_turn(R, path, ids, mine):
    """The recorded room's turn from an npc whose template south-west tile one
    of our runs of the same variant knows: the turn (of the flip's parity)
    under which its printed tile is that south-west tile."""
    for F0 in mine:
        if F0.variant != variant_of(R.origin):
            continue
        for nm, sw, _, _ in F0.spawn:
            for n in R.lives:
                if not n.types or n.types[0][2] != nm:
                    continue
                fits = []
                for M in TURNS:
                    if (M[0] == 0) != R.flip:
                        continue
                    R.turn = M
                    if R.sw_of(n.tiles[min(n.tiles)], SIZES.get(nm, 1)) == sw:
                        fits.append(M)
                R.turn = None
                if len(fits) == 1:
                    return fits[0]
    return None


def recorded(path, room, mine, sizes):
    ids = set(ROOMS[room])
    R = load_room(path, ids, pad=40)
    if R.turn is None:
        R.turn = anchored_turn(R, path, ids, mine)
    turn = None
    if R.turn in TURNS:
        turn = (4 - TURNS.index(R.turn)) % 4
    F = Facts("%s v%d t%s" % (os.path.basename(path), variant_of(R.origin), turn))
    F.variant, F.turn = variant_of(R.origin), turn
    lo, hi = R.first - 40, R.last + 20
    level = None
    for n in R.lives:
        if n.types and n.types[0][1] in ids:
            level = level  # the npcs' level is read below from the add lines

    def local(x, z):
        p = R.local_of((x, z))
        return p if p and 0 <= p[0] < 32 and 0 <= p[1] < 32 else None

    tick = 0
    cur = None             # (index, type name) of the npc block being read
    in_npc = False
    per_tick = defaultdict(lambda: defaultdict(set))  # tick -> index -> kinds
    types = {}
    with open(path, encoding="utf-8", errors="replace") as h:
        for raw in h:
            line = raw.rstrip("\n")
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                cur = None
                if tick > hi:
                    break
                continue
            if tick < lo:
                continue
            m = TOP.match(line)
            if m:
                kind, text = m.group(1), m.group(2)
                in_npc = kind.startswith("npc_info")
                cur = None
                if kind in ("varp_small", "varp_large") and num(text, "id") is None:
                    nm, vid = idref(text)
                    if vid == HPBAR_NPC_VARP:
                        F.overlay.append((tick - R.first, "hpbar_hud_npc %s" % NPCS.get(num(text, "new"), num(text, "new"))))
                    continue
                if kind == "varbit":
                    nm, vid = idref(text)
                    if vid == 12401:
                        F.overlay.append((tick - R.first, "boss %d" % num(text, "new")))
                    continue
                c = COORD.search(text)
                lt = local(int(c.group(1)), int(c.group(2))) if c else None
                if kind.startswith("loc_add_change") and lt:
                    nm, lid = idref(text)
                    nm = nm or LOCS.get(lid, str(lid))
                    rot = num(text, "rotation")
                    trot = (rot - turn) % 4 if turn is not None else None
                    F.locs.setdefault((nm, loc_sw(nm, lt, rot, turn), num(text, "shape")), set()).add((rot, trot))
                elif kind == "loc_del" and lt:
                    F.locs.setdefault(("(deleted)", lt, num(text, "shape")), set()).add((num(text, "rotation"), None))
                elif kind == "obj_add_specific" and lt:
                    nm, oid = idref(text)
                    F.drops[nm or OBJS.get(oid, str(oid))][num(text, "despawn") + 1] += 1
                elif kind == "sound_area":
                    nm, sid = idref(text)
                    F.sounds[(SOUNDS.get(sid, str(sid)), "area")] += 1
                elif kind == "synth_sound":
                    nm, sid = idref(text)
                    F.sounds[(SOUNDS.get(sid, str(sid)), "synth")] += 1
                continue
            if in_npc:
                m = ACTOR.match(line)
                if m:
                    kind, index, nm, nid = m.group(1), m.group(2), m.group(3), int(m.group(4))
                    nm = nm or NPCS.get(nid, str(nid))
                    types[index] = (nm, nid)
                    cur = index
                    if kind == "add" and nid in ids:
                        xz = (int(m.group(5)), int(m.group(6)))
                        lt = R.sw_of(xz, sizes.get(nm, 1)) if R.turn else R.local_of(xz)
                        a = int(m.group(8) or 0)
                        F.spawn.append([nm, lt, a, (a - 512 * turn) % 2048 if turn is not None else None, tick, index])
                    continue
                m = SUB.match(line)
                if m and cur is not None and types[cur][1] in ids:
                    kind, text = m.group(1), m.group(2)
                    nm = types[cur][0]
                    if kind == "hit":
                        per_tick[tick][cur].add("hit")
                    elif kind == "headbar":
                        per_tick[tick][cur].add("bar")
                        F.bars[nm]["id %s" % num(text, "id")] += 1
                    elif kind == "face_angle":
                        # a turn on the add tick is the facing it appears with
                        for s in F.spawn:
                            if s[4] == tick and s[5] == cur and s[1] is not None:
                                a = num(text, "angle")
                                s[2], s[3] = a, (a - 512 * turn) % 2048 if turn is not None else None
                    per_tick[tick][cur].add("t:" + nm)
    for t, by in per_tick.items():
        for index, kinds in by.items():
            names = [k[2:] for k in kinds if k.startswith("t:")]
            nm = names[0] if names else types[index][0]
            if "hit" in kinds:
                F.bars[nm]["hit ticks"] += 1
            if "bar" in kinds:
                F.bars[nm]["bar ticks"] += 1
            if "bar" in kinds and "hit" not in kinds:
                F.bars[nm]["bar, no hit"] += 1
            if "hit" in kinds and "bar" not in kinds:
                F.bars[nm]["hit, no bar"] += 1
    F.spawn = [tuple(s[:4]) for s in F.spawn]
    return F


# ------------------------------------------------------------------ ours
def unpack(c):
    return (c >> 14) & 0x3FFF, c & 0x3FFF, (c >> 28) & 3


def template(room, x, z):
    L = 31
    dx, dz = x - room["ox"], z - room["oz"]
    t = room["turn"]
    if t == 1:
        return L - dz, dx
    if t == 2:
        return L - dx, L - dz
    if t == 3:
        return dz, L - dx
    return dx, dz


SIZES = {}
# every npc's footprint from the cache config (all.npc `size=`); a tick log's
# npc_tile rows override it
_cur = None
with open(os.path.join(CONTENT, "configs", "all.npc"), encoding="latin-1") as _h:
    for _line in _h:
        _line = _line.strip()
        if _line.startswith("[") and _line.endswith("]"):
            _cur = _line[1:-1]
        elif _cur and _line.startswith("size=") and _line[5:].isdigit():
            SIZES[_cur] = int(_line[5:])


def read_sizes(run_dir):
    with open(os.path.join(run_dir, "ticklog.tsv")) as h:
        for line in h:
            c = line.rstrip("\n").split("\t")
            if len(c) > 8 and c[2] == "npc_tile" and c[8].lstrip("-").isdigit() and int(c[8]) > 0:
                SIZES[NPCS.get(int(c[7]), c[7])] = int(c[8])


def ours(run_dir, room_name, layouts):
    n = int(re.search(r"(\d+)$", os.path.basename(run_dir.rstrip("/"))).group(1))
    lay = layouts[(room_name, n)]
    room = {"ox": lay[2], "oz": lay[3], "turn": lay[1]}
    F = Facts("ours n%d v%d t%d" % (n, lay[0], lay[1]))
    F.variant, F.turn = lay[0], lay[1]
    ids = set(ROOMS[room_name])
    t = room["turn"]

    def local(x, z):
        if not (room["ox"] <= x < room["ox"] + 32 and room["oz"] <= z < room["oz"] + 32):
            return None
        return template(room, x, z)

    rows = []
    with open(os.path.join(run_dir, "ticklog.tsv")) as h:
        for line in h:
            c = line.rstrip("\n").split("\t")
            if len(c) < 9 or c[0].startswith("ticklog"):
                continue
            rows.append(c)
    first = None
    spawns = {}            # slot -> index into F.spawn
    sizes = {}
    hits = defaultdict(set)
    bars = defaultdict(set)
    slot_type = {}
    last_boss = None
    for c in rows:
        tick, kind = int(c[1]), c[2]
        a, b, cc, d, e = (int(c[3]), int(c[4]), int(c[5]), int(c[6]), int(c[7]))
        if kind == "npc_tile" and int(c[8] or 0):
            sizes[a] = int(c[8])
        if kind in ("npc_spawn", "npc_retype"):
            slot_type[a] = cc if kind == "npc_retype" else b
        if kind == "npc_spawn" and b in ids:
            x, z, _ = unpack(cc)
            if local(x, z) is None:
                continue
            size = SIZES.get(NPCS.get(b), 1)
            corners = [template(room, x + i, z + j) for i in (0, size - 1) for j in (0, size - 1)]
            lt = (min(p[0] for p in corners), min(p[1] for p in corners))
            if first is None:
                first = tick
            spawns[a] = len(F.spawn)
            F.spawn.append([NPCS.get(b, str(b)), lt, d, (d - 512 * t) % 2048, tick, (x, z)])
        elif kind == "npc_face" and a in spawns:
            s = F.spawn[spawns[a]]
            if s[4] == tick:
                size = SIZES.get(s[0], 1)
                cx, cz = s[5][0] + (size - 1) / 2, s[5][1] + (size - 1) / 2
                ang = angle_toward(cc - cx, d - cz)
                s[2], s[3] = ang, (ang - 512 * t) % 2048
        elif kind == "hit_npc" and b in ids:
            hits[(a, b)].add(tick)
        elif kind == "npc_headbar" and b in ids:
            bars[(a, b)].add(tick)
            F.bars[NPCS.get(b, str(b))]["id %d" % cc] += 1
        elif kind == "varp" and b == HPBAR_NPC_VARP:
            F.overlay.append((tick - (first or tick), "hpbar_hud_npc %s" % NPCS.get(cc, cc)))
        elif kind == "varp" and b == BOSS_VARP:
            bit = (cc >> BOSS_BIT) & 1
            if bit != last_boss:
                F.overlay.append((tick - (first or tick), "boss %d" % bit))
                last_boss = bit
        elif kind == "loc_set":
            x, z, _ = unpack(a)
            lt = local(x, z)
            if lt is None:
                continue
            if b < 0:
                F.locs.setdefault(("(deleted)", lt, cc), set()).add((d, (d - t) % 4))
            else:
                nm = LOCS.get(b, str(b))
                F.locs.setdefault((nm, loc_sw(nm, lt, d, t), cc), set()).add((d, (d - t) % 4))
        elif kind == "obj_add":
            x, z, _ = unpack(a)
            if local(x, z) is not None:
                F.drops[OBJS.get(b, str(b))][e] += 1
        elif kind == "sound" and a == 0:
            F.sounds[(SOUNDS.get(b, str(b)), "synth" if c[9].startswith(("synth", "npc")) else "area")] += 1
    for slot in set(hits) | set(bars):
        nm = NPCS.get(slot[1], "?")
        for tk in hits[slot] | bars[slot]:
            h, br = tk in hits[slot], tk in bars[slot]
            if h:
                F.bars[nm]["hit ticks"] += 1
            if br:
                F.bars[nm]["bar ticks"] += 1
            if br and not h:
                F.bars[nm]["bar, no hit"] += 1
            if h and not br:
                F.bars[nm]["hit, no bar"] += 1
    F.spawn = [tuple(s[:4]) for s in F.spawn]
    return F


# ------------------------------------------------------------------ report
def report(room, recs, mine):
    runs = recs + mine
    print("# %s: recorded %s; ours %s" % (room, ", ".join(r.tag for r in recs), ", ".join(r.tag for r in mine)))
    print("\n## FACINGS (npc type @ template tile: angle instance/template, per run)")
    keys = sorted({(s[0], s[1]) for r in runs for s in r.spawn if s[1] is not None}, key=str)
    for k in keys:
        cells = []
        for r in runs:
            vals = sorted({"%s/%s" % (s[2], s[3]) for s in r.spawn if (s[0], s[1]) == k})
            cells.append("%s:%s" % (r.tag.split()[0], ",".join(vals) or "-"))
        print("  %-40s %s" % ("%s @%s" % k, "  ".join(cells)))
    print("\n## HEALTH BARS (per npc type)")
    for nm in sorted({k for r in runs for k in r.bars}):
        for r in runs:
            if nm in r.bars:
                print("  %-40s %-28s %s" % (nm, r.tag, dict(sorted(r.bars[nm].items()))))
    print("\n## HP OVERLAY (ticks from the room's first npc)")
    for r in runs:
        print("  %-28s %s" % (r.tag, "; ".join("%+d %s" % ov for ov in r.overlay) or "none"))
        print()
    print("\n## LOCS (name @ template tile shape: rotations instance/template)")
    for k in sorted({k for r in runs for k in r.locs}, key=str):
        cells = []
        for r in runs:
            v = r.locs.get(k)
            cells.append("%s:%s" % (r.tag.split()[0], ",".join("%s/%s" % p for p in sorted(v, key=str)) if v else "-"))
        print("  %-52s %s" % ("%s @%s s%s" % k, "  ".join(cells)))
    print("\n## DESPAWN (obj: duration ticks x count)")
    for o in sorted({o for r in runs for o in r.drops}):
        print("  %-40s %s" % (o, "  ".join("%s:%s" % (r.tag.split()[0], dict(r.drops[o])) for r in runs if o in r.drops)))
    print("\n## SOUNDS (per run, count)")
    for s in sorted({s for r in runs for s in r.sounds}, key=str):
        print("  %-52s %s" % ("%s (%s)" % s, "  ".join("%s:%d" % (r.tag.split()[0], r.sounds[s]) for r in runs if s in r.sounds)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("room", choices=sorted(ROOMS))
    ap.add_argument("--rec", nargs="*", default=[])
    ap.add_argument("--ours", nargs="*", default=[])
    ap.add_argument("--layouts")
    args = ap.parse_args()
    layouts = {}
    if args.layouts:
        for line in open(args.layouts):
            m = re.match(r"room (\S+) n (\d+) v (\d+) t (\d+) at (\d+),(\d+),(\d+)", line.strip())
            if m:
                layouts[(m.group(1), int(m.group(2)))] = tuple(int(m.group(i)) for i in range(3, 8))
    for d in args.ours:
        read_sizes(d)
    mine = [ours(d, args.room, layouts) for d in args.ours]
    recs = []
    for p in args.rec:
        try:
            recs.append(recorded(p, args.room, mine, SIZES))
        except AssertionError as e:
            if "no npc of" in str(e):
                continue
            print("# %s: %s" % (os.path.basename(p), e))
    report(args.room, recs, mine)


if __name__ == "__main__":
    main()
