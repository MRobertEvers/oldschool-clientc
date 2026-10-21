#!/usr/bin/env python3
"""How many of an add each event brings, against the party (RAID_PLAN.md
§Scaling): every `[add]` of the named npc types, grouped into events (adds no
more than GAP ticks apart), with the party size then (the names on the raid
HUD, `script10042`).

    python3 adds.py [--gap 1] idx4301.txt ... -- archive_guardian_tentacle tfa_ezekiel_sword
"""
import re
import sys

TICK = re.compile(r"^\[(\d+)\]")
NAMES = re.compile(r"id=script10042 .*values=\[(.*)\]")
ADD = re.compile(r"\[add\] npc=\(index=\d+, id=([a-z0-9_]+) \(\d+\)(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), (\d+)\)")

args = sys.argv[1:]
gap = 1
if args and args[0] == "--gap":
    gap = int(args[1])
    args = args[2:]
split = args.index("--")
paths, kinds = args[:split], set(args[split + 1:])

for path in paths:
    tick = 0
    party = 0
    events = []  # [kind, first, last, count, party, tiles]
    for line in open(path, errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        m = NAMES.search(line)
        if m:
            party = sum(1 for s in re.findall(r'"([^"]*)"', m.group(1)) if s)
            continue
        m = ADD.search(line)
        if not m or m.group(1) not in kinds:
            continue
        kind = m.group(1)
        last = next((e for e in reversed(events) if e[0] == kind), None)
        if last and tick - last[2] <= gap:
            last[2] = tick
            last[3] += 1
        else:
            events.append([kind, tick, tick, 1, party])
    if not events:
        continue
    print(path.rsplit("/", 1)[-1])
    for kind, first, lastt, n, p in events:
        print(f"  [{first}..{lastt}] party {p} {kind} x{n}")
