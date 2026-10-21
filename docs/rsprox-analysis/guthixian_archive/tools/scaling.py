#!/usr/bin/env python3
"""Every Archive boss's bar against the party size, over the TFA transcripts.

For each `hpbar_hud_npc` (varp 1683) the bar names, the first non-zero
`hpbar_hud_basehp` (varbit 6100) under it, and the party size then: the names
in the last `script10042` HUD list (non-empty strings), and the raid HUD's
health codes `tfa_client_p0..p7` that are non-zero. Also the npc's own
`level_change` and the `[add]` id of the named npc.

    python3 scaling.py idx4301.txt idx4308.txt ...
"""
import re
import sys

TICK = re.compile(r"^\[(\d+)\]")
NPC = re.compile(r"id=hpbar_hud_npc \(1683\), old=(-?\d+), new=(-?\d+)")
BASE = re.compile(r"id=hpbar_hud_basehp \(6100\), old=(\d+), new=(\d+)")
NAMES = re.compile(r"id=script10042 .*values=\[(.*)\]")
PCODE = re.compile(r"id=tfa_client_p(\d) \(\d+\), old=\d+, new=(\d+)")
ADD = re.compile(r"\[add\] npc=\(index=\d+, id=([a-z0-9_]+) \((\d+)\)")
MSG = re.compile(r'message="(Challenge complete: [^.]*)')


def scan(path):
    npc = -1
    names = 0
    codes = {}
    seen = {}
    ids = {}
    tick = 0
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        m = ADD.search(line)
        if m:
            ids[int(m.group(2))] = m.group(1)
        m = NAMES.search(line)
        if m:
            names = sum(1 for s in re.findall(r'"([^"]*)"', m.group(1)) if s)
        m = PCODE.search(line)
        if m:
            codes[int(m.group(1))] = int(m.group(2))
        m = NPC.search(line)
        if m:
            npc = int(m.group(2))
        m = BASE.search(line)
        if m and npc > 0 and int(m.group(2)) > 0:
            key = (npc, int(m.group(2)))
            if key not in seen:
                seen[key] = (tick, names, sum(1 for v in codes.values() if v))
        m = MSG.search(line)
        if m:
            print(f"  [{tick}] {m.group(1)}")
    for (n, base), (tick, nm, pc) in sorted(seen.items(), key=lambda kv: kv[1][0]):
        print(f"  [{tick}] npc {n} {ids.get(n, '?'):38s} basehp {base:6d}  names {nm} codes {pc}")


for p in sys.argv[1:]:
    print(p.rsplit("/", 1)[-1])
    scan(p)
