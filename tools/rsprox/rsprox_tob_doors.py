#!/usr/bin/env python3
"""rsprox_tob_doors.py <transcript>... : where every Theatre of Blood door puts the player.

For each op the recording player makes on a ToB loc (tob_*, by name, or by id through
OSRS-Content's loc compack on the nameless rev-227 transcripts) this follows the
LOCAL player (the header's "local player index") through the next ticks and reports:
the tile the player stood on when the op went out, the tile the server put them on
(a TELEPORT, an exact move, or the walk the click made), how many ticks later, the
loc's own tile, and the fade / messages / animations that came with it.

Instance coordinates print mapped back to the template, so the tiles compare
directly with our template-relative room constants.

    python3 tools/rsprox/rsprox_tob_doors.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox/rsprox_tob_doors.py --tsv FILES   # one row per op
"""

import re
import sys

COMPACK = "/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs239-content/configs/all.loc.compack"
LOC = {}
for line in open(COMPACK, encoding="utf-8", errors="replace"):
    if "=" in line and line[0].isdigit():
        k, v = line.rstrip("\n").split("=", 1)
        LOC[int(k)] = v

TICK = re.compile(r"^\[(\d+)\]$")
OPLOC = re.compile(r"^\s*\[oploc(\d)\] id=(?:(\S+) \()?(\d+)\)?, coord=\((\d+), (\d+), (\d+)\)")
PLAYER = re.compile(r"^\s*\[(idle|walk|run|teleport|add)\] player=\(index=(\d+)[^)]*coord=\((\d+), (\d+), (\d+)\)\)(.*)")
NEWC = re.compile(r"newcoord=\((\d+), (\d+), (\d+)\)")
EXACT = re.compile(r"^\s*\[exactmove\](.*)")
SEQ = re.compile(r"^\s*\[sequence\] id=(?:(\S+) \()?(-?\d+)\)?")
MSG = re.compile(r'\[message_game\] type=(\w+), message="(.*)"')
FADE = re.compile(r"\[if_opensub\] .*id=(?:fade_overlay \()?174\)?")
LOCAL = re.compile(r"local player index: (\d+)")
WINDOW = 40  # ticks followed after an op


def tob_name(name, lid):
    n = name or LOC.get(lid, str(lid))
    return n if n.startswith("tob_") else None


def scan(path):
    rows = []
    local = None
    tick = 0
    pending = []          # ops still being followed
    pos = None            # local player's tile now
    cur_is_local = False
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        if local is None:
            m = LOCAL.search(line)
            if m:
                local = m.group(1)
                continue
        m = TICK.match(line.strip())
        if m:
            tick = int(m.group(1))
            for op in pending:
                if tick - op["tick"] > WINDOW:
                    op["done"] = True
            rows.extend(op for op in pending if op.get("done"))
            pending = [op for op in pending if not op.get("done")]
            continue
        m = OPLOC.match(line)
        if m:
            name = tob_name(m.group(2), int(m.group(3)))
            if name:
                pending.append({"file": path.split("/")[-1], "tick": tick, "op": m.group(1), "loc": name,
                                "loc_at": (int(m.group(4)), int(m.group(5)), int(m.group(6))),
                                "from": pos, "moves": [], "notes": [], "path": []})
            continue
        m = PLAYER.match(line)
        if m:
            cur_is_local = m.group(2) == local
            if cur_is_local:
                here = (int(m.group(3)), int(m.group(4)), int(m.group(5)))
                n = NEWC.search(m.group(6))
                to = (int(n.group(1)), int(n.group(2)), int(n.group(3))) if n else here
                if m.group(1) == "teleport":
                    for op in pending:
                        op["moves"].append(("TELEPORT", tick - op["tick"], here, to))
                for op in pending:
                    if not op["path"] or op["path"][-1][1] != to:
                        op["path"].append((tick - op["tick"], to, m.group(1)))
                pos = to
            continue
        if cur_is_local:
            m = EXACT.match(line)
            if m:
                for op in pending:
                    op["moves"].append(("EXACTMOVE", tick - op["tick"], pos, m.group(1).strip()[:80]))
                continue
            m = SEQ.match(line)
            if m and line.startswith("            "):
                for op in pending:
                    op["notes"].append("t+%d anim %s" % (tick - op["tick"], m.group(1) or m.group(2)))
                continue
        m = MSG.search(line)
        if m and m.group(1) != "dialogue":
            for op in pending:
                op["notes"].append('t+%d mes "%s"' % (tick - op["tick"], m.group(2)[:70]))
            continue
        if FADE.search(line):
            for op in pending:
                op["notes"].append("t+%d fade" % (tick - op["tick"]))
    rows.extend(pending)
    for op in rows:
        op["end"] = pos
    return rows


def main(argv):
    tsv = "--tsv" in argv
    files = [a for a in argv if a != "--tsv"]
    if tsv:
        print("file\ttick\top\tloc\tloc_at\tfrom\tmove\tdelay\tmove_from\tmove_to\tnotes")
    for path in files:
        for op in scan(path):
            moves = op["moves"] or [("", "", "", "")]
            first = moves[0]
            if tsv:
                print("\t".join(str(x) for x in (op["file"], op["tick"], op["op"], op["loc"], op["loc_at"], op["from"],
                                                 first[0], first[1], first[2], first[3], "; ".join(op["notes"][:6]))))
            else:
                print("%s t%d op%s %s at %s from %s" % (op["file"], op["tick"], op["op"], op["loc"], op["loc_at"], op["from"]))
                for mv in op["moves"][:3]:
                    print("    %s +%s %s -> %s" % mv)
                print("    path: " + " ".join("+%d%s%s" % (t, "" if k in ("walk", "run", "idle") else k[0].upper(), (x, z)) for t, (x, z, l), k in op["path"][:12]))
                for n in op["notes"][:6]:
                    print("    " + n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
