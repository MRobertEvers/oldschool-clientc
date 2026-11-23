#!/usr/bin/env python3
"""Every stance the recorder's player was put in: each [bas] that differs from the
one before it, as a span from the tick it went on to the tick the next one came.

The default transcript never prints a player [bas]; transcribe with --bas
(rsprox_index.py get <id> --bas) or read the home-wire transcript. A span that
is not the normal stance (human_ready / the worn weapon's) is content: a crawl,
a seat, a cart, wading, a carried prop. Each one needs a verdict in the port.

    python3 tools/rsprox/player_bas_spans.py <bas or home-wire transcript> [--from T0 --to T1] [--all]

--all also prints the spans of the normal stance (human_ready ...).
"""
import argparse
import re

ap = argparse.ArgumentParser()
ap.add_argument("transcript")
ap.add_argument("--from", dest="t0", type=int, default=0)
ap.add_argument("--to", dest="t1", type=int, default=10 ** 9)
ap.add_argument("--all", action="store_true")
a = ap.parse_args()

TICK = re.compile(r"^\[(\d+)\]")
BAS = re.compile(r"^\s+\[bas\]\s+(.*)$")
# The player block's subject line: `[walk] player=(...` etc. A [bas] under an npc
# line belongs to the npc.
SUBJECT = re.compile(r"^\s{8}\[(\w+)\] (player|npc)=")
NORMAL = re.compile(r"ready=human_ready \(")

tick = 0
subject = None
last = None
spans = []  # [start, end or None, bas]
seen_bas = 0
for line in open(a.transcript, errors="replace"):
    m = TICK.match(line)
    if m:
        tick = int(m.group(1))
        subject = None
        continue
    m = SUBJECT.match(line)
    if m:
        subject = m.group(2)
        continue
    m = BAS.match(line)
    if not m or subject == "npc":
        continue
    seen_bas += 1
    bas = re.sub(r"turn=[^,]*, ", "", m.group(1)).strip()
    if bas == last:
        continue
    if spans:
        spans[-1][1] = tick
    spans.append([tick, None, bas])
    last = bas

if seen_bas == 0:
    print("no player [bas] in this transcript: transcribe it with --bas (rsprox_index.py get <id> --bas)"
          " or read the home-wire one")
    raise SystemExit(1)

for start, end, bas in spans:
    stop = end if end is not None else 10 ** 9
    if stop < a.t0 or start > a.t1:
        continue
    if not a.all and NORMAL.search(bas):
        continue
    until = f"{end}" if end is not None else "end"
    print(f"[{start}..{until}] {bas}")
