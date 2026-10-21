#!/usr/bin/env python3
"""The Gauntlet in RSProx transcripts: one row per run.

    python3 tools/rsprox/rsprox_gauntlet.py runs <transcript>...

A run starts on "You enter the Gauntlet." and ends on the completion lines,
"You leave the Gauntlet.", the death line, or the end of the file. Per run:
the entry tick, corrupted (varbit gauntlet_corrupted 9292), the start room
(varbit gauntlet_start 9291), the tick gauntlet_boss_started (9177) went 1,
the end tick and how it ended, the rooms found and the npc types seen.
"""
import argparse
import os
import re
from collections import Counter

TICK = re.compile(r"^\[(\d+)\]$")
VARBIT = re.compile(r"\[varbit\] id=([a-z0-9_]+) \((\d+)\), old=(-?\d+), new=(-?\d+)")
MESSAGE = re.compile(r'\[message_game\] type=([a-z]+), message="(.*)"$')
NPC_ADD = re.compile(r"\[add\] npc=\(index=\d+, id=([a-z0-9_]+) \((\d+)\)")


def runs(path):
    out = []
    cur = None
    tick = 0
    corrupted = 0
    start = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = TICK.match(line)
            if m:
                tick = int(m.group(1))
                continue
            m = VARBIT.search(line)
            if m:
                name, new = m.group(1), int(m.group(4))
                if name == "gauntlet_corrupted":
                    corrupted = new
                if name == "gauntlet_start" and new:
                    start = new
                if cur is not None:
                    # gauntlet_corrupted is written on the next run's entry tick,
                    # before its entry line: it belongs to that run, not this one.
                    if name == "gauntlet_start" and new:
                        cur["start"] = new
                    elif name == "gauntlet_boss_started" and new == 1 and cur["boss"] is None:
                        cur["boss"] = tick
                    elif name == "gauntlet_boss_started" and new == 0 and cur["boss"] is not None:
                        cur["end"] = cur["end"] or tick
                        cur["how"] = cur["how"] or "boss ended"
                        out.append(cur)
                        cur = None
                    elif name.startswith("gauntlet_room_") and new == 1:
                        cur["rooms"].add(int(name.split("_")[2]))
                continue
            m = MESSAGE.search(line)
            if m:
                text = m.group(2)
                if text == "You enter the Gauntlet.":
                    if cur is not None:
                        cur["end"] = tick
                        cur["how"] = cur["how"] or "re-entered"
                        out.append(cur)
                    cur = dict(enter=tick, corrupted=corrupted, start=start, boss=None, end=None,
                               how=None, rooms=set(), npcs=Counter(), prep=None)
                elif cur is not None:
                    if text.startswith("Preparation time"):
                        cur["prep"] = re.sub(r"<[^>]+>", "", text)
                    if text.startswith("Preparation time") or "hallenge duration" in text:
                        cur["how"] = "complete"
                        cur["end"] = tick
                    elif text == "You leave the Gauntlet.":
                        cur["how"] = cur["how"] or "left"
                        cur["end"] = tick
                        out.append(cur)
                        cur = None
                    elif text == "Oh dear, you are dead!":
                        cur["how"] = "dead"
                        cur["end"] = tick
                continue
            if cur is not None:
                m = NPC_ADD.search(line)
                if m:
                    cur["npcs"][m.group(1)] += 1
    if cur is not None:
        cur["end"] = cur["end"] or tick
        cur["how"] = cur["how"] or "eof"
        out.append(cur)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cmd", choices=["runs"])
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--npcs", action="store_true", help="print the npc types seen per run")
    args = parser.parse_args()
    for path in args.paths:
        name = os.path.basename(path)
        for run in runs(path):
            print("%s enter=%d %s start=%s boss=%s end=%s how=%s rooms=%d %s" % (
                name, run["enter"], "CG" if run["corrupted"] else "G", run["start"], run["boss"],
                run["end"], run["how"], len(run["rooms"]), run["prep"] or ""))
            if args.npcs:
                print("    " + ", ".join("%s x%d" % kv for kv in sorted(run["npcs"].items())))


if __name__ == "__main__":
    main()
