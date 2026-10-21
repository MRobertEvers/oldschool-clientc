#!/usr/bin/env python3
"""Ocellus himself, recording against ours, tick by tick: every seq (with its
delay), spotanim (slot, delay), facing (coord / entity / reset / angle), say
and form change, aligned on each phase's start (his change into the phase's
form; phase 4 on "End...").

    python3 oc_stream.py idx4301.txt <ticklog.tsv> [--phase 1] [--diff]

--diff prints only the rows one side has and the other lacks (same relative
tick, same kind, same id); without it, the two streams side by side.
"""
import re
import sys

BOSS = ("tfa_ocellus_intro", "tfa_ocellus_p1", "tfa_ocellus_p2", "tfa_ocellus_p3")
BOSS_IDS = {16671: "intro", 16672: "intro", 16673: "intro", 16674: "p1", 16675: "p2", 16677: "p3"}
# a thrall's magic, the glaive's special, the Sanguinesti's heal, the ruby
# and onyx bolts' procs (the raiders' kit); an empty id with a delay is the
# clear that rides a thrall's hit
KIT_SPOTS = {1908, 2731, 1542, 753, 754}
TICK = re.compile(r"^\[(\d+)\]")
NPCLINE = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=([a-z0-9_]+) \((\d+)\)")
SEQ = re.compile(r"\[sequence\] id=[^(]*\((-?\d+)\)(?:, delay=(\d+))?|\[sequence\] id=(-1)")
SPOT = re.compile(r"\[spotanim\](?: slot=(\d+),)? id=[^(,]*?(?:\((\d+)\))?(?:, delay=(\d+))?(?:, height=(\d+))?")
FACE = re.compile(r"\[facing\] (.*)")
SAY = re.compile(r'\[say\] text="([^"]*)"')
TRANS = re.compile(r"\[transformation\] id=([a-z0-9_]+)")


def recording(path):
    rows = []
    tick = 0
    inside = False
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            inside = False
            continue
        m = NPCLINE.match(line)
        if m:
            inside = m.group(3) in BOSS
            if inside and m.group(1) in ("walk", "run", "teleport", "telejump"):
                c = re.search(r"newcoord=\((\d+), (\d+)", line)
                rows.append((tick, "move", f"{m.group(1)} {c.group(1)},{c.group(2)}" if c else m.group(1)))
            continue
        if re.match(r"^ {8}\[", line) or re.match(r"^ {4}\[", line):
            inside = False if not line.startswith("            ") else inside
        if not inside or not line.startswith("            "):
            continue
        s = line.strip()
        m = SEQ.search(s)
        if m:
            sid = m.group(1) or m.group(3)
            rows.append((tick, "seq", f"{sid} d{m.group(2) or 0}"))
            continue
        if s.startswith("[spotanim]"):
            slot = re.search(r"slot=(\d+)", s)
            sid = re.search(r"\((\d+)\)", s)
            dl = re.search(r"delay=(\d+)", s)
            # The raiders' own impacts on him (a thrall's magic, the glaive's
            # special) are their kit, not his.
            if sid and int(sid.group(1)) in KIT_SPOTS:
                continue
            if not sid and dl:
                continue
            rows.append((tick, "spot", f"{sid.group(1) if sid else -1} s{slot.group(1) if slot else 0} d{dl.group(1) if dl else 0}"))
            continue
        m = FACE.search(s)
        if m:
            f = m.group(1)
            if f.startswith("coord="):
                c = re.search(r"\((\d+), (\d+)", f)
                f = f"coord {c.group(1)},{c.group(2)}"
            elif f.startswith("player="):
                f = "entity player"
            elif f.startswith("type=reset"):
                f = "reset"
            elif f.startswith("angle="):
                f = "angle " + re.search(r"angle=(\d+)", f).group(1)
            rows.append((tick, "face", f))
            continue
        m = SAY.search(s)
        if m:
            rows.append((tick, "say", m.group(1)))
            continue
        m = TRANS.search(s)
        if m:
            rows.append((tick, "form", m.group(1)))
    return rows


def ours(path, origin):
    rows = []
    boss_slots = set()
    tiles = {}
    for line in open(path, errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 9 or not f[1].isdigit():
            continue
        tick, kind = int(f[1]), f[2]
        a = f[3:]
        if kind == "npc_spawn" and int(a[1]) in BOSS_IDS:
            boss_slots.add(a[0])
            packed = int(a[2])
            tiles[a[0]] = ((packed >> 14) & 0x3FFF, packed & 0x3FFF)
        if kind == "npc_retype" and int(a[2]) in BOSS_IDS:
            boss_slots.add(a[0])
            rows.append((tick, "form", "tfa_ocellus_" + BOSS_IDS[int(a[2])]))
            continue
        if not a or a[0] not in boss_slots:
            continue
        if kind == "npc_anim":
            rows.append((tick, "seq", f"{a[2]} d{a[3]}"))
        elif kind == "npc_spotanim":
            rows.append((tick, "spot", f"{a[2]} s{a[5]} d{a[4]}"))
        elif kind == "npc_face":
            if a[2] == "-1":
                rows.append((tick, "face", "reset" if a[3] == "-1" else "entity player"))
            elif a[2] == "-2":
                rows.append((tick, "face", f"angle {a[3]}"))
            else:
                x, z = int(a[2]), int(a[3])
                rows.append((tick, "face", f"coord {x - origin[0]},{z - origin[1]}"))
        elif kind == "npc_say":
            rows.append((tick, "say", f[9] if len(f) > 9 else ""))
        elif kind == "npc_tile":
            x, z = int(a[1]), int(a[2])
            prev = tiles.get(a[0])
            tiles[a[0]] = (x, z)
            if prev:
                rows.append((tick, "move", f"{max(abs(x - prev[0]), abs(z - prev[1]))} tiles {x - origin[0]},{z - origin[1]}"))
    return rows


def phases(rows):
    """Phase anchors: the form changes, and "End..." for phase 4."""
    out = {}
    for t, k, v in rows:
        if k == "form":
            n = {"tfa_ocellus_p1": 1, "tfa_ocellus_p2": 2, "tfa_ocellus_p3": 3}.get(v)
            if n and n not in out:
                out[n] = t
        if k == "say" and v.startswith("End...") and 4 not in out:
            out[4] = t
    return out


# Events both fights have, in order, that the fight's own course (how fast
# the raiders take his health) moves: each later row is timed from the latest
# of these before it, on both sides.
RESYNC = {
    1: ("seq 14812",),            # the teleport to the middle (66%, 25%)
    3: ("say Untether...", "say End..."),
}


def resync(r, o, keys):
    def marks(rows):
        out = []
        for t, k, v in rows:
            if any(f"{k} {v}".startswith(key) for key in keys):
                out.append(t)
        return out
    mr, mo = marks(r), marks(o)
    n = min(len(mr), len(mo))

    def shift(rows, ms, other):
        res = []
        for t, k, v in rows:
            i = max([j for j in range(n) if ms[j] <= t] or [-1])
            res.append((t if i < 0 else other[i] + (t - ms[i]), k, v))
        return res
    # Both sides are put on the recording's clock.
    return r, shift(o, mo, mr)


def norm(v, k):
    """What has to agree: the id (and delay/slot), not the recording's names."""
    if k == "move":
        return "move"
    if k == "face" and v.startswith("coord"):
        return "coord"
    return v


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    only = None
    if "--phase" in sys.argv:
        only = int(sys.argv[sys.argv.index("--phase") + 1])
        args = [a for a in args if a != str(only)]
    diff = "--diff" in sys.argv
    rec = recording(args[0])
    # Our coords are instance tiles; the recording's are the template's. The
    # middle of the arena is the shared origin: (4127, 5673) there, and ours
    # from his first tile (the intro stands at middle + (-1, -4)).
    tl = args[1]
    first = None
    for line in open(tl, errors="replace"):
        f = line.split("\t")
        if len(f) > 6 and f[2] == "npc_spawn" and f[4].isdigit() and int(f[4]) in BOSS_IDS:
            packed = int(f[5])
            first = ((packed >> 14) & 0x3FFF, packed & 0x3FFF)
            break
    origin = (first[0] + 1, first[1] + 4) if first else (0, 0)
    mine = ours(tl, origin)
    rec = [(t, k, (f"coord {int(v.split()[1].split(',')[0]) - 4127},{int(v.split()[1].split(',')[1]) - 5673}" if k == "face" and v.startswith("coord") else v)) for t, k, v in rec]
    pr, po = phases(rec), phases(mine)
    print(f"phase anchors: recording {pr}, ours {po}")
    for n in sorted(pr):
        if only and n != only:
            continue
        if n not in po:
            print(f"== phase {n}: not reached by ours")
            continue
        end_r = min([pr[m] for m in pr if m > n] or [10 ** 9])
        end_o = min([po[m] for m in po if m > n] or [10 ** 9])
        r = [(t - pr[n], k, v) for t, k, v in rec if pr[n] <= t < end_r]
        o = [(t - po[n], k, v) for t, k, v in mine if po[n] <= t < end_o]
        if RESYNC.get(n):
            r, o = resync(r, o, RESYNC[n])
        span = min(max([t for t, _, _ in r] or [0]), max([t for t, _, _ in o] or [0]))
        r = [x for x in r if x[0] <= span]
        o = [x for x in o if x[0] <= span]
        rk = {(t, k, norm(v, k)) for t, k, v in r if k != "move"}
        ok = {(t, k, norm(v, k)) for t, k, v in o if k != "move"}
        both = rk & ok
        print(f"== phase {n} (recording [{pr[n]}], ours [{po[n]}], first {span} ticks): "
              f"{len(both)} matched, {len(rk - ok)} recording-only, {len(ok - rk)} ours-only")
        for kind in ("seq", "spot", "face", "say"):
            rr = sum(1 for x in rk if x[1] == kind)
            oo = sum(1 for x in ok if x[1] == kind)
            bb = sum(1 for x in both if x[1] == kind)
            print(f"   {kind:5s} recording {rr:4d}  ours {oo:4d}  matched {bb:4d}")
        rows = sorted(set([(t, "R", k, v) for t, k, v in r] + [(t, "O", k, v) for t, k, v in o]))
        for t, side, k, v in rows:
            key = (t, k, norm(v, k))
            if diff and (k == "move" or (key in rk and key in ok)):
                continue
            print(f"   +{t:4d} {side} {k:5s} {v}")


main()
