#!/usr/bin/env python3
"""Resolve per-toplevel drive coordinates from a capture's ROLE_WIDGET lines.

A hover or a click in a jobs file is a pixel, and a pixel only means something
on the toplevel it was measured on. `TORIRS_SIM_HOVER=627,228` is the first
inventory slot ON 548; on 161 the sidebar sits elsewhere, on 164 it moves with
the window, and on 601 there is no docked sidebar at all. Reusing 548's number
on another toplevel does not fail loudly -- the pointer lands on empty chrome,
the plugin correctly draws nothing, and the shot looks like a plugin that does
not work. That is indistinguishable from a real defect and wastes the run.

So the coordinates are not written down. They are read out of a capture:

    python3 lane_coords.py <gate-out-dir>       # dir of <lane>/log.txt

Every lane's log carries `ROLE_WIDGET role=<name> ... box=x,y,w,h`, which is the
client's own answer for where that role ended up on that toplevel. This prints
the centre of each role this harness needs to point at, per lane, ready to paste
into a jobs file.
"""

import os
import re
import sys

# role -> what a drive wants to do to it
WANTED = {
    "panel_inventory": "item-stats hover: first inventory slot",
    "report_button": "screenshot plugin: the Report button it REPLACEs",
    "frame_chat": "chat block",
    "panel_minimap": "minimap",
}

ROLE = re.compile(r"^ROLE_WIDGET role=(\S+) .*?box=(-?\d+),(-?\d+),(\d+),(\d+) hidden=(\d)")


def boxes(path):
    """Last box seen per role -- the settled one, not a mid-mount reading."""
    found = {}
    with open(path, errors="ignore") as handle:
        for line in handle:
            match = ROLE.match(line)
            if match:
                role, x, y, w, h, hidden = match.groups()
                found[role] = (int(x), int(y), int(w), int(h), int(hidden))
    return found


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    root = argv[0]
    lanes = sorted(d for d in os.listdir(root)
                   if os.path.isfile(os.path.join(root, d, "log.txt")))
    for lane in lanes:
        found = boxes(os.path.join(root, lane, "log.txt"))
        print(f"\n{lane}")
        for role, why in WANTED.items():
            if role not in found:
                print(f"   {role:18} ABSENT on this toplevel  ({why})")
                continue
            x, y, w, h, hidden = found[role]
            # The first inventory slot, not the panel's middle: an item sits in
            # the top-left cell, and the panel's centre is usually empty air.
            if role == "panel_inventory":
                cx, cy = x + 20, y + 23
                note = "first slot"
            else:
                cx, cy = x + w // 2, y + h // 2
                note = "centre"
            flag = "  [HIDDEN]" if hidden else ""
            print(f"   {role:18} box={x},{y},{w},{h}  {note}={cx},{cy}{flag}")
            print(f"   {'':18} {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
