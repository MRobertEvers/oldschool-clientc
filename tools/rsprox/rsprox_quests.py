"""rsprox_quests.py <transcript.txt>... : one TSV row per quest completion in RSProx transcripts.

A completion is the game message "you've completed a quest: <name>"; the quest
scroll (interface 153) opened in the same tick carries the reward lines, and
varp 101 is the quest point total.
"""
import json
import os
import re
import sys

TICK = re.compile(r"^\[(\d+)\]$")
MSG = re.compile(r'\[message_game\] .*completed a quest: (.*)"$')
QP = re.compile(r"\[varp_(?:small|large)\] id=(?:qp \()?101\)?, old=(-?\d+), new=(-?\d+)")
TEXT = re.compile(r'\[if_settext\] com=(?:\S+ \()?153:(\d+)\)?, text="(.*)"$')
OPEN = re.compile(r"\[if_opensub\] .*id=(?:questscroll \()?153\)?, ")
TAG = re.compile(r"<[^>]*>")

DATES = {}
search = os.path.expanduser("~/.rsprox/binary/archive/search/interface_153.json")
for row in json.load(open(search))["results"]:
    DATES[row["submission"]["id"]] = row["submission"]["date"]

print("session\tdate\tversion\tworld\tactivity\ttick\tminutes\tquest\tqp_before\tqp_after\tscroll\trewards")
for path in sys.argv[1:]:
    session = int(os.path.basename(path).split(".")[0])
    version = world = activity = "?"
    tick = 0
    qp_now = None
    last_qp = None
    rows = []         # completions in this file
    pending = None    # the completion whose scroll lines are still arriving
    opens = 0
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            stripped = line.strip()
            m = TICK.match(stripped)
            if m:
                tick = int(m.group(1))
                pending = None
                continue
            if stripped.startswith("version:"):
                version = stripped.split(":", 1)[1].strip()
            elif stripped.startswith("world:"):
                world = stripped.split(",")[0].split(":")[1].strip()
                activity = stripped.rsplit("activity:", 1)[1].strip()
            m = QP.search(stripped)
            if m:
                qp_before, qp_now = int(m.group(1)), int(m.group(2))
                if pending is not None:
                    pending["qp_before"], pending["qp_after"] = qp_before, qp_now
                else:
                    last_qp = (qp_before, qp_now, tick)
                continue
            m = MSG.search(stripped)
            if m:
                pending = {"tick": tick, "quest": TAG.sub("", m.group(1)).strip(),
                           "qp_before": "", "qp_after": "", "scroll": "", "rewards": {}}
                # the QP bump may land just before the message in the same tick
                if last_qp is not None and last_qp[2] == tick:
                    pending["qp_before"], pending["qp_after"] = last_qp[0], last_qp[1]
                rows.append(pending)
                continue
            m = TEXT.search(stripped)
            if m and rows and rows[-1]["tick"] == tick:
                child, text = int(m.group(1)), TAG.sub("", m.group(2)).strip()
                if child == 4:
                    rows[-1]["scroll"] = text
                elif 9 <= child <= 15 and text:
                    rows[-1]["rewards"][child] = text
                continue
            if OPEN.search(stripped):
                opens += 1
    for r in rows:
        rewards = " | ".join(r["rewards"][k] for k in sorted(r["rewards"]))
        print("\t".join(str(x) for x in (session, DATES.get(session, "?"), version, world, activity,
                                         r["tick"], "%.1f" % (r["tick"] * 0.6 / 60), r["quest"],
                                         r["qp_before"], r["qp_after"], r["scroll"], rewards)))
    if opens != len(rows):
        print("# %d: %d scroll opens vs %d messages" % (session, opens, len(rows)), file=sys.stderr)
