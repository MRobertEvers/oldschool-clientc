#!/usr/bin/env python3
"""Each ToA raid in an RSProx transcript: its raid level and invocations, from the party panel.

Clientscript 6729 carries the panel: values[3] is the raid level and values[5], values[6] the
invocation masks. The first mask holds invocation indices 0-30 (bit i = index i), the second
31 onward (bit k = index 31 + k) - a 31-bit split, not 32. Read as 32 every invocation from
Keep Back (31) up names its neighbour, and the sums stop matching the panel's level (index 4323:
355 against 310). Indices are `toa_invocation.dbrow`'s (param_1159); 45 and 46 are the Leagues
Blazing Tombs, absent from that table.

A raid's set is the last panel before its "You enter the Tombs".

    python3 tools/rsprox/toa_raid_invocations.py <transcript>...
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DBROW = ROOT / "OSRS-Content/osrs-content/server/scripts/minigames/minigame_toa/configs/toa_invocation.dbrow"
PANEL = re.compile(r"\[runclientscript\] id=(?:script)?6729\b.*values=\[([^\]]*)\]")
TICK = re.compile(r"^\[(\d+)\]$")


def table():
    names, levels = {}, {}
    for block in re.split(r"\n(?=\[)", DBROW.read_text()):
        head = re.match(r"\[toa_inv_(\w+)\]", block)
        if not head:
            continue
        data = dict(re.findall(r"^data=(\w+),(.*)$", block, re.M))
        names[int(data["index"])] = head.group(1)
        levels[int(data["index"])] = int(data["raidlevel"])
    names[45], names[46] = "blazing_tombs_1", "blazing_tombs_2"
    levels[45], levels[46] = 200, 200
    return names, levels


def indices(lo, hi):
    return [i for i in range(31) if lo >> i & 1] + [31 + k for k in range(31) if hi >> k & 1]


def main():
    names, levels = table()
    for path in sys.argv[1:]:
        tick, panel, raid = 0, None, 0
        for line in open(path, errors="replace"):
            m = TICK.match(line.strip())
            if m:
                tick = int(m.group(1))
                continue
            m = PANEL.search(line)
            if m:
                panel = [int(v) for v in m.group(1).split(", ")]
                continue
            if "You enter the Tombs" in line and panel:
                raid += 1
                on = indices(panel[5] & 0x7FFFFFFF, panel[6] & 0x7FFFFFFF)
                total = sum(levels.get(i, 0) for i in on)
                check = "" if total == panel[3] else "  (sum %d != panel)" % total
                print("%s raid %d [%d] level %d%s" % (pathlib.Path(path).name, raid, tick, panel[3], check))
                print("    " + " ".join(names.get(i, "?%d" % i) for i in on))


if __name__ == "__main__":
    main()
