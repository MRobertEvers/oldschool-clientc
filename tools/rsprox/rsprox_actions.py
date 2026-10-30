#!/usr/bin/env python3
"""Every distinct thing the recorder did in a session, and every move it made.

The scope of a content port is the RECORDING, not the files you were asked
about. Tree Gnome Village (2026-10-27, index 4394) was rebuilt from its
quest's npcs and locs while the same session broke three teleport tablets our
tree had no Break for, climbed ten ladders, chopped trees, drank potions,
dropped items and picked up the warlord's drop -- none of it looked at,
because no list of what was clicked was ever made. Run this first; give every
row a verdict (this content / generic content checked once / the recorder's
own kit) in the analysis doc.

    python3 tools/rsprox/rsprox_actions.py <idx.txt> [--from T] [--to T] [--all]

Prints:
  ACTIONS    each distinct op/button (npc, loc, obj, held item, interface
             button, use-on, objdialog answer), first tick and count. Dialogue
             continues, the chat-filter and stone buttons are folded away unless
             --all.
  TELEPORTS  every player [teleport] (plane changes, tablets, ladders,
             guides, scenes) with the last action before it.
"""

import argparse
import re
import sys

ACTION = re.compile(r"^    \[(op[a-z]*\d*(?:_v\d)?|opnpct|oploct|opobjt|opplayert|opheldt?|if_buttonx|if_subop|if_buttont|resume_p_objdialog|resume_p_countdialog|resume_p_namedialog|resume_p_stringdialog)\] (.*)$")
TICK = re.compile(r"^\[(\d+)\]")
TELEPORT = re.compile(r"^\s+\[teleport\] player=\(index=(\d+), name=[^,]*, coord=\(([^)]*)\)\), newcoord=\(([^)]*)\)")
FOLD = re.compile(r"chatbox:chat_|toplevel_[a-z_]*:stone|chatmenu:|messagebox:|chat_left:|chat_right:|objectbox:|resume_pausebutton")


def key_of(body):
    k = re.sub(r"coord=\([^)]*\)|index=\d+, |sub=\d+, ", "", body)
    k = re.sub(r", multinpc=[^,)]*", "", k)
    return k.strip(" ,")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("transcript")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", dest="end", type=int, default=1 << 30)
    ap.add_argument("--all", action="store_true", help="keep dialogue continues and chat/stone buttons")
    a = ap.parse_args()

    tick = 0
    local = None
    first, count, order = {}, {}, []
    last_action = None
    teleports = []
    for line in open(a.transcript, encoding="utf-8", errors="replace"):
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            continue
        if local is None:
            m = re.match(r"^local player index: (\d+)", line)
            if m:
                local = m.group(1)
        if tick < a.start or tick > a.end:
            continue
        m = ACTION.match(line)
        if m:
            body = "[%s] %s" % (m.group(1), m.group(2))
            if not a.all and FOLD.search(body):
                continue
            last_action = (tick, body)
            k = key_of(body)
            if k not in first:
                first[k] = tick
                count[k] = 0
                order.append(k)
            count[k] += 1
            continue
        m = TELEPORT.match(line)
        if m and (local is None or m.group(1) == local):
            teleports.append((tick, m.group(2), m.group(3), last_action))

    print("ACTIONS (first tick, count) -- give each one a verdict")
    for k in order:
        print("  [%5d] x%-3d %s" % (first[k], count[k], k[:160]))
    print()
    print("TELEPORTS (tick, from -> to, the last action before it)")
    for t, src, dst, act in teleports:
        cause = "[%d] %s" % (act[0], act[1][:110]) if act else "-"
        print("  [%5d] (%s) -> (%s)   %s" % (t, src, dst, cause))
    return 0


if __name__ == "__main__":
    sys.exit(main())
