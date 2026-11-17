#!/usr/bin/env python3
"""Every long move of the recorder in an RSProx transcript, with the click that
started it and the shape the server sent it in.

A boat, a ferry, a cart, a charter: the content is "click X, then N ticks later
the player is somewhere else", and the parity question is HOW the server got
there -- the ship_journey interface (299), the fade overlay (174 / clientscript
948), a level-1 hold at sea, an arrival mesbox, the delay. This prints one row
per teleport of the recorder's own player of at least --min tiles (or a level
change), so a whole session's transports can be read side by side with ours:

    python3 tools/rsprox/rsprox_transport_shape.py <transcript>... [--min 20] [--npc NAME]...

Per row: the tick, from -> to (level), the last op the client sent within
--back ticks before it (opnpc/oploc and its target name), the ticks from that op
to the move, and between the op and the move + 3 ticks: the interfaces opened,
the fade clientscripts (948 values), the messages and mesboxes, and the
jingles. Written 2026-11-17 for the Port Sarim ship (index 4378: fade, no 299)
after our content still sent the 2023 ship_journey shape.
"""
import argparse
import re
import sys

TICK = re.compile(r"^\[(\d+)\]")
TELE = re.compile(r"\[teleport\] player=\(index=\d+, name=[^,]*, coord=\((\d+), (\d+), (\d+)\)\), newcoord=\((\d+), (\d+), (\d+)\)")
OP = re.compile(r"^\s+\[(op(?:npc|loc|obj|worldentity)[0-9tu]*(?:_v\d)?)\] (.*)$")
NAME = re.compile(r"id=([a-z0-9_]+) \(\d+\)|id=(\d+)")
OPENSUB = re.compile(r"\[if_opensub\] com=([^ ]+) [^,]*, id=([a-z0-9_]+ \(\d+\)|\d+)")
FADE = re.compile(r"\[runclientscript\] id=(?:fade_overlay \()?948\)?, .*values=\[([^\]]*)\]")
MESSAGE = re.compile(r"\[message_game\] type=(gamemessage|mesbox|dialogue), message=\"(.*)\"")
# Older transcripts print the bare id, rsprox-c the name and then "(id)".
JINGLE = re.compile(r"\[midi_jingle\] id=(?:[^(,]*\()?(\d+)")
REBUILD = re.compile(r"\[(rebuild_[a-z0-9_]+)\]")


def read(path):
    ticks = []  # (tick, [lines])
    tick, lines = -1, []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            m = TICK.match(line)
            if m:
                ticks.append((tick, lines))
                tick, lines = int(m.group(1)), []
            else:
                lines.append(line)
    ticks.append((tick, lines))
    return ticks


def target(text):
    m = NAME.search(text)
    if not m:
        return text[:60]
    return m.group(1) or m.group(2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--min", type=int, default=20, help="tiles (Chebyshev) that count as a long move")
    ap.add_argument("--back", type=int, default=25, help="ticks before the move to look for the op")
    ap.add_argument("--npc", action="append", default=[], help="only moves whose op names this target (substring)")
    args = ap.parse_args()

    for path in args.transcripts:
        ticks = read(path)
        index = {t: i for i, (t, _) in enumerate(ticks)}
        for i, (tick, lines) in enumerate(ticks):
            for line in lines:
                m = TELE.search(line)
                if not m:
                    continue
                x0, z0, l0, x1, z1, l1 = map(int, m.groups())
                if max(abs(x1 - x0), abs(z1 - z0)) < args.min and l0 == l1:
                    continue
                op_tick, op_text = None, None
                for j in range(i, -1, -1):
                    t, ls = ticks[j]
                    if t < tick - args.back:
                        break
                    for l in reversed(ls):
                        o = OP.match(l)
                        if o:
                            op_tick, op_text = t, "%s %s" % (o.group(1), target(o.group(2)))
                            break
                    if op_text:
                        break
                if args.npc and not (op_text and any(n in op_text for n in args.npc)):
                    continue
                start = index.get(op_tick, i) if op_tick is not None else i
                feats = []
                end_tick = tick + 3
                for t, ls in ticks[start:]:
                    if t > end_tick:
                        break
                    rel = t - (op_tick if op_tick is not None else tick)
                    for l in ls:
                        for rx, fmt in ((OPENSUB, "open %s->%s"), (FADE, "fade[%s]"), (MESSAGE, "%s:%s"),
                                        (JINGLE, "jingle %s"), (REBUILD, "%s")):
                            f = rx.search(l)
                            if f:
                                g = f.groups()
                                if rx is OPENSUB and "fade_overlay" not in g[1] and "(174)" not in g[1] and g[1] != "174" \
                                        and "mainmodal" not in g[0] and "(299)" not in g[1] and g[1] != "299":
                                    continue
                                if rx is MESSAGE and g[0] == "dialogue":
                                    continue
                                text = fmt % (g if len(g) > 1 else g[0])
                                feats.append("+%d %s" % (rel, text[:90]))
                print("%s [%d] %d,%d,%d -> %d,%d,%d  op=%s  op->move=%s" % (
                    path.rsplit("/", 1)[-1], tick, x0, z0, l0, x1, z1, l1, op_text or "-",
                    (tick - op_tick) if op_tick is not None else "-"))
                for f in feats:
                    print("    " + f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
