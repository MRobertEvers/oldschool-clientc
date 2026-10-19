#!/usr/bin/env python3
"""The skilling a Chambers of Xeric resource room is made of -- raking, planting,
picking herbs, cleaning them, mixing potions, lighting a fire, cooking -- as
Jagex's server answers it, from RSProx transcripts.

No recording in the archive farms, brews, fishes, catches a bat or cooks INSIDE
the raid (docs/rsprox-analysis/cox/skilling.md, "Coverage"). The raid's
patches, gourds and fires run on the engine's ordinary skilling, so this reads
the same actions in the normal game: what the click answers on its own tick,
what follows and when.

Every event is the local raider's own: an op (or an item-on-item) at tick A,
then until the next op (at most --window ticks) every seq he plays, synth
sound, game message, inventory change and xp gain, each as A+n.

Sections (--section, default all):
  rake    oploc on a farming patch with seq farming_raking
  plant   a seed used on a patch (oploct ... obj=*seed*)
  pick    oploc on a patch answered with picking_low / herbs into the pack
  clean   an inventory op on a grimy herb
  mix     herb on vial / secondary on unfinished (if_buttont between pack slots,
          answered with human_herbing_vial)
  fire    tinderbox on logs (human_createfire)
  cook    item on a fire or range (human_cooking)

    python3 tools/cox/rsprox_skilling.py ~/.rsprox/binary/archive/{1106,3282,1108,91,788}.txt
"""
import argparse
import re
import sys
from collections import Counter, defaultdict

TICK = re.compile(r"^\[(\d+)\]$")
OP = re.compile(r"^    \[(op\w+|if_button\w*)\] (.*)$")
SEQ = re.compile(r"^\s+\[sequence\] id=([\w-]+)")
SYNTH = re.compile(r"^\s+\[synth_sound\] id=(\d+)")
MES = re.compile(r'^\s+\[message_game\] type=(\w+), message="(.*)"$')
INV_HDR = re.compile(r"^\s+\[update_inv_(partial|full)\] .*id=inv \(93\)")
SLOT = re.compile(r"^\s+- slot=(\d+), id=([\w-]+)(?: \(\d+\))?, count=([\d,]+)")
XP = re.compile(r"^\s+\[update_stat_v2\] stat=(\w+), level=\d+(?:, invisiblelevel=\d+)?, experience=([\d,]+)")
PLAYER = re.compile(r"^\s+\[(idle|walk|run|teleport)\] player=\(index=\d+, name=([^,]*)")
LOC = re.compile(r"^\s+\[loc_add_change\w*\] id=([\w-]+)")

SECTIONS = {
    "rake": lambda op, ev: "oploc" in op and any(e[1] == "seq" and e[2] == "farming_raking" for e in ev),
    "plant": lambda op, ev: op.startswith("[oploct]") and "seed" in op.split("obj=")[-1],
    "pick": lambda op, ev: "oploc" in op and "herb" in op and any(e[1] == "seq" and e[2] == "picking_low" for e in ev),
    "clean": lambda op, ev: "if_button" in op and any(e[1] == "mes" and e[2].startswith("You clean") for e in ev),
    "mix": lambda op, ev: "if_buttont" in op and any(e[1] == "seq" and e[2] == "human_herbing_vial" for e in ev),
    "fire": lambda op, ev: any(e[1] == "seq" and e[2] == "human_createfire" for e in ev),
    "cook": lambda op, ev: op.startswith("[oploct]") and any(e[1] == "seq" and "cooking" in e[2] for e in ev),
}


def read(path, window):
    """[(A, op text, [(n, kind, what)])] for the local player."""
    me = None
    events = []
    cur = None
    tick = 0
    inv = None
    slots = {}
    xp = {}
    in_player = False
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        if me is None and line.startswith("local player index"):
            pass
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            inv = None
            in_player = False
            if cur and tick - cur[0] > window:
                events.append(cur)
                cur = None
            continue
        m = OP.match(line)
        if m:
            # a click on a tab, a prayer or the minimap is not a new action
            if m.group(1).startswith("if_button") and "149:" not in m.group(2) and "inventory" not in m.group(2):
                continue
            if cur:
                events.append(cur)
            cur = (tick, "[%s] %s" % (m.group(1), m.group(2)), [])
            continue
        m = PLAYER.match(line)
        if m:
            if me is None:
                me = m.group(2)
            in_player = m.group(2) == me
            continue
        if line.startswith("    [") and not line.startswith("    [player_info]"):
            in_player = False
        m = SEQ.match(line)
        if m and in_player and cur:
            cur[2].append((tick - cur[0], "seq", m.group(1)))
            continue
        m = SYNTH.match(line)
        if m and cur:
            cur[2].append((tick - cur[0], "synth", m.group(1)))
            continue
        m = MES.match(line)
        if m and cur:
            cur[2].append((tick - cur[0], "mes", m.group(2)))
            continue
        m = LOC.match(line)
        if m and cur:
            cur[2].append((tick - cur[0], "loc", m.group(1)))
            continue
        m = INV_HDR.match(line)
        if m:
            inv = m.group(1)
            if inv == "full":
                old = dict(slots)
                slots.clear()
            continue
        if inv:
            if line.strip() == "[objs]":
                continue
            m = SLOT.match(line)
            if m:
                slot, obj, count = int(m.group(1)), m.group(2), int(m.group(3).replace(",", ""))
                before = slots.get(slot)
                slots[slot] = (obj, count)
                if cur and before != (obj, count):
                    if before and before[0] != "-1" and before[0] != obj:
                        cur[2].append((tick - cur[0], "inv", "-%s" % before[0]))
                    if obj != "-1":
                        delta = count - (before[1] if before and before[0] == obj else 0)
                        cur[2].append((tick - cur[0], "inv", "%+d %s" % (delta, obj)))
                continue
            inv = None
        m = XP.match(line)
        if m:
            stat, exp = m.group(1), int(m.group(2).replace(",", ""))
            if stat in xp and exp > xp[stat] and cur:
                cur[2].append((tick - cur[0], "xp", "%s +%.1f" % (stat, (exp - xp[stat]))))
            xp[stat] = exp
    if cur:
        events.append(cur)
    return events


def compact(ev):
    return " | ".join("A+%d %s %s" % e for e in ev if not (e[1] == "inv" and e[2].startswith("-")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("transcripts", nargs="+")
    ap.add_argument("--section", choices=sorted(SECTIONS), action="append")
    ap.add_argument("--window", type=int, default=12)
    ap.add_argument("--limit", type=int, default=6, help="examples printed per section and file")
    args = ap.parse_args()
    sections = args.section or sorted(SECTIONS)
    for path in args.transcripts:
        events = read(path, args.window)
        for name in sections:
            hits = [e for e in events if SECTIONS[name](e[1], e[2])]
            if not hits:
                continue
            print("== %s: %s (%d)" % (path.rsplit("/", 1)[-1], name, len(hits)))
            first = Counter()
            for a, op, ev in hits:
                for n, kind, what in ev:
                    if kind in ("seq", "synth", "mes", "xp", "loc"):
                        key = "%s %s" % (kind, re.sub(r"\d+(\.\d+)?", "#", what) if kind in ("xp",) else what)
                        first["A+%d %s" % (n, key)] += 1
            for key, c in first.most_common(14):
                print("   %4d  %s" % (c, key))
            for a, op, ev in hits[:args.limit]:
                print("  [%d] %s" % (a, op[:150]))
                print("      " + compact(ev)[:600])
    return 0


if __name__ == "__main__":
    sys.exit(main())
