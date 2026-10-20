#!/usr/bin/env python3
"""What Jagex's server sent in answer to one interaction, tick by tick, from an
RSProx transcript: every client packet matching --op (oploc/opnpc/opobj/opheld/
if_button/resume_*/...) and the ticks around it, with the noise taken out.

    python3 tools/rsprox/rsprox_interact.py <transcript> --op 'oploc1.*spiralstairs' [--after 8] [--before 2]
    python3 tools/rsprox/rsprox_interact.py <transcript> --op 'opnpc1.*lara' --max 3 --npc lara
    python3 tools/rsprox/rsprox_interact.py <transcript> --from 4120 --to 4140           # a plain window
    python3 tools/rsprox/rsprox_interact.py <transcript> --list                          # every client packet, counted

Each tick is printed as C+n (n ticks after the matching client packet; the click
tick itself jitters by one -- the proxy files a click under the tick it saw it, so
time a mechanic from the server's first answer). Kept: every top-level server
packet, the recorder's own player-info entry (movement, facing, seq, spotanim,
hits, exactmove...), and npc-info entries of the npc the op names, of --npc
matches, and of any npc whose ext-info faces or targets the recorder. Dropped:
the clock varbits, other npcs' and players' plain movement, zone re-sends beyond
--burst lines of one kind per tick (counted, not hidden), stats.
--all keeps everything.
"""
import argparse
import re
import sys
from collections import Counter

TICK = re.compile(r"^\[(\d+)\]$")
ME = re.compile(r"local player index: (\d+)")
CLIENT = re.compile(r"^    \[(op[a-z]+\d*t?(?:_v\d)?|if_button\w*|resume_\w+|opheld\w*|if_subop|move_\w+|oploc\w*|opnpc\w*|opobj\w*|opplayer\w*|opworldentity\w*)\] ")
NPC_INDEX = re.compile(r"npc=\(index=(\d+)")
PLAYER_INDEX = re.compile(r"player=\(index=(\d+)")
NOISE = re.compile(r"\[varbit\] id=(date_|1239[12],)|\[varp_large\] id=(map_clock |3079,)|\[update_stat_v2\]|\[if_setcolour\] com=hpbar_hud|\[runclientscript\] id=pvp_icons")


def read_ticks(path):
    """-> (local player index, [(tick, [lines])])"""
    me, ticks, current = None, [], None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if me is None:
                m = ME.search(line)
                if m:
                    me = m.group(1)
            m = TICK.match(line)
            if m:
                current = (int(m.group(1)), [])
                ticks.append(current)
            elif current is not None and line.strip():
                current[1].append(line)
    return me, ticks


def depth(line):
    return len(line) - len(line.lstrip(" "))


def filter_tick(lines, me, watched, npc_words, keep_all, burst):
    if keep_all:
        return lines
    out, kinds = [], Counter()
    skipping_below = None  # drop the children of an entry we dropped
    for line in lines:
        d = depth(line)
        if skipping_below is not None:
            if d > skipping_below:
                continue
            skipping_below = None
        if NOISE.search(line):
            continue
        if d == 4:
            kind = line.strip().split("]")[0] + "]"
            kinds[kind] += 1
            if kinds[kind] > burst and not CLIENT.match(line):
                continue
            out.append(line)
            continue
        if d == 8 and re.match(r"\s+\[(idle|walk|run|crawl|teleport|telejump|add|del)\] (npc|player)=", line):
            npc = NPC_INDEX.search(line)
            player = PLAYER_INDEX.search(line)
            keep = (player and player.group(1) == me) or (npc and (
                npc.group(1) in watched or any(w in line for w in npc_words)))
            if not keep:
                # keep an npc entry anyway when its ext-info turns to / acts on the recorder
                skipping_below = d
                continue
        out.append(line)
    # re-attach entries whose ext-info faces the recorder: a second pass on the raw lines
    for i, line in enumerate(lines):
        if depth(line) == 8 and NPC_INDEX.search(line) and line not in out:
            children = []
            for child in lines[i + 1:]:
                if depth(child) <= 8:
                    break
                children.append(child)
            if any(("player=(index=%s," % me) in c and "[facing]" in c or "[face_pathingentity] player=(index=%s," % me in c
                   for c in children):
                out.append(line + "    <- turns to the recorder")
                out.extend(children)
    # an info block whose entries were all dropped says nothing
    out = [l for i, l in enumerate(out)
           if not (depth(l) == 4 and re.match(r"\s+\[(npc_info|player_info)", l)
                   and (i + 1 >= len(out) or depth(out[i + 1]) <= 4))]
    for kind, n in kinds.items():
        if n > burst:
            out.append("    %s x%d more this tick (zone re-send / burst)" % (kind, n - burst))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("transcript")
    parser.add_argument("--op", help="regex on the client packet line, e.g. 'oploc1.*stairs'")
    parser.add_argument("--from", dest="start", type=int)
    parser.add_argument("--to", dest="end", type=int)
    parser.add_argument("--before", type=int, default=2)
    parser.add_argument("--after", type=int, default=6, help="ticks after L (or C when there is no walk)")
    parser.add_argument("--max", type=int, default=5, help="at most this many matches")
    parser.add_argument("--npc", nargs="*", default=[], help="also keep npcs whose line holds any of these")
    parser.add_argument("--burst", type=int, default=4, help="top-level lines of one kind kept per tick")
    parser.add_argument("--all", action="store_true", help="no filtering")
    parser.add_argument("--list", action="store_true", help="count every client packet (with its target) instead")
    args = parser.parse_args()

    me, ticks = read_ticks(args.transcript)
    assert me is not None, "no 'local player index' in the header"
    if args.list:
        counts = Counter()
        for _, lines in ticks:
            for line in lines:
                if CLIENT.match(line):
                    counts[re.sub(r"index=\d+, |coord=\([^)]*\)|, sub=\d+", "", line.strip())] += 1
        for text, n in counts.most_common():
            print("%6d  %s" % (n, text))
        return

    position = {tick: i for i, (tick, _) in enumerate(ticks)}
    windows = []
    if args.op:
        pattern = re.compile(args.op)
        for i, (tick, lines) in enumerate(ticks):
            hit = next((l for l in lines if CLIENT.match(l) and pattern.search(l)), None)
            if hit:
                windows.append((tick, hit))
                if len(windows) >= args.max:
                    break
        if not windows:
            sys.exit("no client packet matches %r (try --list)" % args.op)
    else:
        assert args.start is not None, "give --op or --from/--to"
        windows.append((args.start, None))
        args.before, args.after = 0, (args.end or args.start + 10) - args.start

    moving_re = re.compile(r"^        \[(walk|run|crawl)\] player=\(index=%s," % me)

    def moving(i):
        return i < len(ticks) and any(moving_re.match(l) for l in ticks[i][1])

    for click, hit in windows:
        watched = set(NPC_INDEX.findall(hit or ""))
        first = position.get(click)
        if first is None:
            first = min(range(len(ticks)), key=lambda i: abs(ticks[i][0] - click))
        # L: the recorder's last step of the walk to the target (None: already there)
        j = first if moving(first) else first + 1 if moving(first + 1) else None
        arrive = None
        if j is not None and hit:
            # stop where another click takes over (it starts a new walk)
            while moving(j + 1) and not any(CLIENT.match(l) and l.strip() != hit.strip() for l in ticks[j + 1][1]):
                j += 1
            arrive = ticks[j][0]
        print("=" * 100)
        print("C = [%d]  %s" % (click, (hit or "").strip()))
        if hit:
            print("L = %s" % ("[%d] (last step; a loc/npc op fires at L+1)" % arrive if arrive is not None
                              else "none: no walk, the op fires on C"))
        end = position.get(arrive, first) if arrive is not None else first
        for i in range(max(0, first - args.before), min(len(ticks), end + args.after + 1)):
            tick, lines = ticks[i]
            shown = filter_tick(lines, me, watched, args.npc, args.all, args.burst)
            label = "C%+d" % (tick - click) + ("/L%+d" % (tick - arrive) if arrive is not None else "")
            print("%s [%d]%s" % (label, tick, "" if shown else "  (nothing)"))
            for line in shown:
                print("  " + line[2:][:260])


if __name__ == "__main__":
    main()
