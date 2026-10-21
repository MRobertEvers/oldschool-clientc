#!/usr/bin/env python3
"""Every scythe of vitur swing the recording player made, with the sweep the
server sent for it, from RSProx transcripts (tools/cox/rsprox_fetch.py +
RSProx `transcribe`).

    python3 tools/rsprox_scythe.py ~/.rsprox/binary/archive/{820,788}.txt
    python3 tools/rsprox_scythe.py --tsv out.tsv <txt>...

A swing is the recording player's `[sequence] id=scythe_of_vitur_attack (8056)`.
Its sweep is the `[map_anim] id=dragon_halberd_special_<dir>_<colour>` of the
same tick on a tile at most one step from that player (another raider's sweep
lands on its own tile). The target is the npc the player last faced
(`face_pathingentity`), at the coordinate npc_info last gave it; its size comes
from the npc's config in OSRS-Content.

For each swing the offsets are DOUBLED so an even footprint's centre stays
integral: dx2 = (2*sw.x + size) - (2*player.x + 1), likewise dz2. Positive dx2
means the target's centre lies east, positive dz2 north.

Coordinates: RSProx prints instance coordinates mapped back to their template.
In a template chunk the instance rotated, a real-world step comes out rotated
while the copy's name does not, so every rotated room shows one fixed
copy->step mismatch. A swing whose sweep tile is not the step its copy names is
reported as `rotated` and kept out of the rule tests. So is a swing whose
target changed tile in the last two ticks, or whose player moved this tick
(`moving`): npc_info's end-of-tick tile need not be where the server judged it.

Output: the sweep packet fields, each candidate rule's score, the misses of
`measured` (the rule scythe_of_vitur.rs2 implements), and one grid per target
size. docs/rsprox-analysis/scythe/README.md is the write-up.
"""
import argparse
import os
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
VERSION = re.compile(r"^version: (\d+)")
ACTIVITY = re.compile(r"activity: (.*)$")
PLAYER = re.compile(r"^\s{8}\[(\w+)\] player=\(index=(\d+), name=[^,]*, coord=\((\d+), (\d+), (\d+)\)\)"
                    r"(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
# A named transcript prints `id=<symbol> (<id>)`; one without the revision's
# names (rev 227) prints `<display name> (id=<id>)`. Group 3 is the id either way.
NPC_ID = r"(?:id=[a-z0-9_]+ \((\d+)\)|[^()]*\(id=(\d+)\))"
NPC = re.compile(r"^\s{8}\[(\w+)\] npc=\(index=(\d+), " + NPC_ID + r", coord=\((\d+), (\d+), (\d+)\)"
                 r"(?:, angle=\d+)?\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?")
FACE = re.compile(r"\[face_pathingentity\] npc=\(index=(\d+), " + NPC_ID + r", coord=\((\d+), (\d+), (\d+)\)")
FACE_OTHER = re.compile(r"\[face_pathingentity\] (?!npc=)")
SWING = re.compile(r"\[sequence\] id=(?:scythe_of_vitur_attack \()?8056\)?(.*)$")
SWEEP = re.compile(r"\[map_anim\] id=(?:dragon_halberd_special_\w+ \()?(\d+)\)?, "
                   r"delay=(\d+), height=(\d+), coord=\((\d+), (\d+), (\d+)\)")
# the twelve sweep copies, by spotanim id: (direction, colour)
COPIES = {478: ("south", "red"), 506: ("north", "red"), 1172: ("east", "red"), 1231: ("west", "red"),
          1891: ("south", "darkred"), 1892: ("north", "darkred"), 1893: ("east", "darkred"),
          1894: ("west", "darkred"), 1895: ("south", "blue"), 1896: ("north", "blue"),
          1897: ("east", "blue"), 1898: ("west", "blue")}
STEP = {"north": (0, 1), "south": (0, -1), "east": (1, 0), "west": (-1, 0)}
NPC_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "OSRS-Content", "osrs-content",
                          "configs", "all.npc")


def npc_sizes():
    """npc id -> (name, size), from OSRS-Content's all.npc and its compack."""
    names = {}
    with open(NPC_CONFIG + ".compack", "r", encoding="latin-1") as handle:
        for line in handle:
            key, _, value = line.strip().partition("=")
            if key.isdigit():
                names[int(key)] = value
    by_name = {}
    name = None
    with open(NPC_CONFIG, "r", encoding="latin-1") as handle:
        for line in handle:
            line = line.strip()
            if line.startswith("["):
                name = line[1:-1]
            elif name and line.startswith("size=") and line[5:].isdigit():
                by_name[name] = int(line[5:])
    return {npc: (name, by_name.get(name)) for npc, name in names.items()}


def swings(path, sizes):
    """Yield one dict per recorded swing of the recording player."""
    me = None
    tick = 0
    header = {}
    actor = None            # ('p', index, coord) or ('n',) for the block being read
    npcs = {}               # index -> (id, (x, z))
    moved = {}              # ('n', index) / ('p', index) -> last tick its coordinate changed
    face = None             # npc index the recording player faces
    pending = None          # this tick's swing, waiting for its sweep
    out = []

    def close():
        if pending is not None:
            out.append(pending)

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if me is None:
                m = ME.search(line)
                if m:
                    me = int(m.group(1))
                m = VERSION.match(line)
                if m:
                    header["revision"] = int(m.group(1))
                m = ACTIVITY.search(line)
                if m:
                    header["activity"] = m.group(1)
                continue
            m = TICK.match(line.strip())
            if m and line.startswith("["):
                close()
                pending = None
                tick = int(m.group(1))
                actor = None
                continue
            m = PLAYER.match(line)
            if m:
                coord = (int(m.group(3)), int(m.group(4)))
                if m.group(6):
                    coord = (int(m.group(6)), int(m.group(7)))
                if m.group(6):
                    moved[("p", int(m.group(2)))] = tick
                actor = ("p", int(m.group(2)), coord)
                continue
            m = NPC.match(line)
            if m:
                coord = (int(m.group(5)), int(m.group(6)))
                if m.group(8):
                    coord = (int(m.group(8)), int(m.group(9)))
                index = int(m.group(2))
                if index in npcs and npcs[index][1] != coord:
                    moved[("n", index)] = tick
                npcs[index] = (int(m.group(3) or m.group(4)), coord)
                actor = ("n",)
                continue
            mine = actor is not None and actor[0] == "p" and actor[1] == me
            m = FACE.search(line)
            if m and mine:
                face = int(m.group(1))
                npcs.setdefault(face, (int(m.group(2) or m.group(3)), (int(m.group(4)), int(m.group(5)))))
                continue
            if FACE_OTHER.search(line) and mine:
                face = None
                continue
            m = SWING.search(line)
            if m and mine:
                target = npcs.get(face) if face is not None else None
                known = sizes.get(target[0], ("npc_%d" % target[0], None)) if target else (None, None)
                pending = {"file": os.path.basename(path), "tick": tick, "player": actor[2], "extra": m.group(1).strip(),
                           "target": known[0], "sw": target[1] if target else None, "size": known[1], "sweeps": [],
                           # ticks since the target / the player last changed tile
                           "target_still": tick - moved.get(("n", face), -99) if target else None,
                           "player_still": tick - moved.get(("p", me), -99)}
                pending.update(header)
                continue
            m = SWEEP.search(line)
            if m and pending is not None:
                spotanim = int(m.group(1))
                tile = (int(m.group(4)), int(m.group(5)))
                step = (tile[0] - pending["player"][0], tile[1] - pending["player"][1])
                if spotanim in COPIES and abs(step[0]) <= 1 and abs(step[1]) <= 1:
                    direction, colour = COPIES[spotanim]
                    pending["sweeps"].append({"dir": direction, "colour": colour, "id": spotanim,
                                              "delay": int(m.group(2)), "height": int(m.group(3)), "step": step})
    close()
    return out


def cardinal(v):
    return {(0, 1): "north", (0, -1): "south", (1, 0): "east", (-1, 0): "west"}.get(v)


def rule_z_first(dx2, dz2):
    """North/south whenever the target's centre is off the player's row."""
    if dz2 > 0:
        return "north"
    if dz2 < 0:
        return "south"
    return "east" if dx2 > 0 else "west"


def rule_nearest(dx2, dz2):
    """The nearest cardinal to the target's centre (ties to north/south)."""
    if abs(dz2) >= abs(dx2):
        return "north" if dz2 > 0 else "south"
    return "east" if dx2 > 0 else "west"


def rule_centre_tile(dx2, dz2, size):
    """North/south unless the player stands on the target's centre ROW, where
    the centre tile is sw + size // 2 (the upper middle of an even footprint);
    on that row, east/west."""
    # undo the doubling: centre tile minus player tile, with size // 2 in place of size / 2
    dz = (dz2 + 1 - size + 2 * (size // 2)) // 2
    dx = (dx2 + 1 - size + 2 * (size // 2)) // 2
    if dz > 0:
        return "north"
    if dz < 0:
        return "south"
    return "east" if dx > 0 else "west"


def centre_row(size):
    """The row (and column) offset from the south-west tile on which the player
    sweeps east/west (north/south): size // 2, except a 2x2, which sweeps from
    its south-west row as a 1x1 would."""
    return 0 if size == 2 else size // 2


def rule_measured(dx2, dz2, size):
    """North/south toward the target unless the player stands on its centre_row;
    there, east/west toward it."""
    # back to whole tiles: target sw minus player
    sw_dz = (dz2 + 1 - size) // 2
    sw_dx = (dx2 + 1 - size) // 2
    dz = sw_dz + centre_row(size)
    dx = sw_dx + centre_row(size)
    if dz > 0:
        return "north"
    if dz < 0:
        return "south"
    return "east" if dx > 0 else "west"


RULES = {"measured": rule_measured, "centre_tile": rule_centre_tile, "z_first": lambda dx2, dz2, size: rule_z_first(dx2, dz2),
         "nearest": lambda dx2, dz2, size: rule_nearest(dx2, dz2)}


def print_grids(rows, kind):
    """One grid per target size: each cell is a tile the player stood on, by
    column and row from the target's south-west tile (north up), holding the
    copies sent there (initials, commonest first) and the swing count."""
    cells = defaultdict(Counter)
    for row in rows:
        if row.get("kind") != kind:
            continue
        col = row["player"][0] - row["sw"][0]
        line = row["player"][1] - row["sw"][1]
        cells[(row["size"], col, line)][row["sweeps"][0]["dir"][0].upper()] += 1
    print("grids, %s swings (player tile relative to the target's south-west tile; north up):" % kind)
    for size in sorted({key[0] for key in cells}):
        print("  size %d" % size)
        for line in range(size, -2, -1):
            out = []
            for col in range(-1, size + 1):
                seen = cells.get((size, col, line))
                if not seen:
                    out.append("   .    ")
                    continue
                initials = "".join(sorted(seen, key=lambda k: -seen[k]))
                out.append("%3s%-5d" % (initials[:3], sum(seen.values())))
            print("    row %2d " % line + " ".join(out))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("transcripts", nargs="+")
    parser.add_argument("--tsv")
    args = parser.parse_args()
    sizes = npc_sizes()
    rows = []
    for path in args.transcripts:
        rows.extend(swings(path, sizes))

    packets = Counter()
    kinds = Counter()
    scores = {name: Counter() for name in RULES}
    misses = []
    for row in rows:
        mine = row["sweeps"]
        if len(mine) != 1:
            kinds["sweeps=%d" % len(mine)] += 1
            row["kind"] = "sweeps=%d" % len(mine)
            continue
        sweep = mine[0]
        packets[(sweep["colour"], sweep["delay"], sweep["height"], row["extra"] or "-")] += 1
        if STEP[sweep["dir"]] != sweep["step"]:
            kinds["rotated"] += 1
            row["kind"] = "rotated"
            continue
        if row["sw"] is None or row["size"] is None:
            kinds["no target"] += 1
            row["kind"] = "no target"
            continue
        if row["target_still"] < 2 or row["player_still"] < 1:
            kinds["moving"] += 1
            row["kind"] = "moving"
            continue
        if False:
            kinds["no target"] += 1
            row["kind"] = "no target"
            continue
        dx2 = 2 * row["sw"][0] + row["size"] - (2 * row["player"][0] + 1)
        dz2 = 2 * row["sw"][1] + row["size"] - (2 * row["player"][1] + 1)
        row.update(kind="ok", dx2=dx2, dz2=dz2)
        kinds["ok"] += 1
        for name, rule in RULES.items():
            right = rule(dx2, dz2, row["size"]) == sweep["dir"]
            scores[name][right] += 1
            if name == "measured" and not right:
                misses.append(row)

    print("swings %d" % len(rows))
    for kind, count in sorted(kinds.items()):
        print("  %-12s %d" % (kind, count))
    print("sweep packet (colour, delay, height, seq extras) for every single-sweep swing:")
    for key, count in sorted(packets.items()):
        print("  %s %d" % (key, count))
    print("rules, over the 'ok' swings (right / wrong):")
    for name in RULES:
        print("  %-8s %d / %d" % (name, scores[name][True], scores[name][False]))
    for row in misses:
        print("  measured MISS %s tick %d player %s target %s size %d sw %s -> %s" % (
            row["file"], row["tick"], row["player"], row["target"], row["size"], row["sw"], row["sweeps"][0]["dir"]))
    for kind in ("ok", "moving"):
        print_grids(rows, kind)
    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8") as handle:
            handle.write("file\trevision\tactivity\ttick\tplayer_x\tplayer_z\ttarget\tsize\tsw_x\tsw_z\tdx2\tdz2"
                         "\tsweep\tcolour\tspotanim\tdelay\theight\tstep_x\tstep_z\tkind\n")
            for row in rows:
                sweep = row["sweeps"][0] if len(row["sweeps"]) == 1 else None
                handle.write("\t".join(str(v) for v in (
                    row["file"], row.get("revision"), row.get("activity"), row["tick"], row["player"][0],
                    row["player"][1], row["target"], row["size"], row["sw"][0] if row["sw"] else "",
                    row["sw"][1] if row["sw"] else "", row.get("dx2", ""), row.get("dz2", ""),
                    sweep["dir"] if sweep else "", sweep["colour"] if sweep else "", sweep["id"] if sweep else "",
                    sweep["delay"] if sweep else "", sweep["height"] if sweep else "",
                    sweep["step"][0] if sweep else "", sweep["step"][1] if sweep else "", row["kind"])) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
