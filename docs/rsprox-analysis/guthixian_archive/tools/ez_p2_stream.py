#!/usr/bin/env python3
"""Ezekiel's finale (P2), every npc-info line of every npc it drives, tick by
tick: Ezekiel, the fence (tfa_ezekiel_sword_noop), the V
(tfa_ezekiel_finale_vfx) and the death sword -- [bas] and
[body_customisation] included, which npc_timeline.py leaves out.

    python3 ez_p2_stream.py <transcript> [--from T] [--to T]
"""
import re
import sys

NPC = re.compile(r"^ {8}\[(\w+)\] npc=\(index=(\d+), id=(tfa_ezekiel(?:_sword_noop|_finale_vfx|_death_sword)?) \(\d+\)(?:, multinpc=[^)]*\))?, coord=\((\d+), (\d+), \d\)\)(.*)")


def main():
    path = sys.argv[1]
    lo = int(sys.argv[sys.argv.index("--from") + 1]) if "--from" in sys.argv else 0
    hi = int(sys.argv[sys.argv.index("--to") + 1]) if "--to" in sys.argv else 10 ** 9
    tick = None
    cur = None
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"^\[(\d+)\]$", line)
        if m:
            tick = int(m.group(1))
            cur = None
            continue
        if tick is None or not lo <= tick <= hi:
            continue
        m = NPC.match(line)
        if m:
            cur = (m.group(3).replace("tfa_ezekiel", "EZ"), m.group(2))
            extra = m.group(6).strip(", ")
            print("[%d] %-14s #%s %s (%s,%s) %s" % (tick, cur[0], cur[1], m.group(1), m.group(4), m.group(5), extra))
            continue
        if line.startswith("        [") or line.startswith("    ["):
            cur = None
            continue
        if cur and line.startswith("            "):
            print("[%d] %-14s #%s     %s" % (tick, cur[0], cur[1], line.strip()))


if __name__ == "__main__":
    main()
