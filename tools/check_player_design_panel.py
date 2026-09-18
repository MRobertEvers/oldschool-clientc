#!/usr/bin/env python3
"""The design panel's arrows are addressed by child id; hold the ids to the panel.

`src/game/rs_design_panel.c` recognises an arrow by its component's child id --
the seven body-part rows from 15 and the five colour rows from 46, four
components to a row -- so that a click can be stepped on the client before the
server answers it. The panel itself is content
(`OSRS-Content/osrs239-content/interfaces/player_design.compack`), and content
moves.

A row inserted into the panel would slide every id after it by four. Nothing
would fail: the prediction would step the WRONG row for one tick and then the
server's appearance would put it back, which reads as a rendering glitch rather
than as a moved component. So the stride is checked against the compack's own
names instead of trusted.

  python3 tools/check_player_design_panel.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL = os.path.join(ROOT, "src", "game", "rs_design_panel.c")
COMPACK = os.path.join(
    ROOT, "OSRS-Content", "osrs239-content", "interfaces", "player_design.compack",
)

# The panel's rows, in the order the client numbers them: design parts are the
# idk table's own bodypart numbering, colour slots the order the appearance
# block carries them.
KIT_ROWS = ["head", "jaw", "torso", "arms", "hands", "legs", "feet"]
COLOUR_ROWS = ["hair", "torso_col", "legs_col", "feet_col", "skin"]


def client_constants():
    """The five enum values rs_design_panel.c states."""
    text = open(PANEL, encoding="utf8").read()
    wanted = [
        "DESIGN_KIT_ROW_FIRST",
        "DESIGN_KIT_ROW_COUNT",
        "DESIGN_COLOUR_ROW_FIRST",
        "DESIGN_COLOUR_ROW_COUNT",
        "DESIGN_ROW_STRIDE",
    ]
    out = {}
    for name in wanted:
        m = re.search(r"^\s*%s\s*=\s*(\d+)" % name, text, re.M)
        if not m:
            print("check_player_design_panel: no %s in %s" % (name, PANEL), file=sys.stderr)
            return None
        out[name] = int(m.group(1))
    return out


def compack_children():
    """`<id>=<name>` -> {name: id}."""
    out = {}
    for line in open(COMPACK, encoding="utf8"):
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        cid, _, name = line.partition("=")
        if not name:
            continue
        out[name.strip()] = int(cid)
    return out


def check_rows(kind, rows, first, count, stride, children, problems):
    if count != len(rows):
        problems.append(
            "%s row count is %d in the client and %d in the panel" % (kind, count, len(rows))
        )
        return
    for i, row in enumerate(rows):
        for offset, side in ((0, "left"), (1, "right")):
            name = "%s_%s" % (row, side)
            want = first + i * stride + offset
            have = children.get(name)
            if have is None:
                problems.append("%s: the panel has no `%s`" % (kind, name))
            elif have != want:
                problems.append(
                    "%s: `%s` is child %d, the client steps it at %d" % (kind, name, have, want)
                )


def main():
    client = client_constants()
    if client is None:
        return 1
    children = compack_children()
    if not children:
        print("check_player_design_panel: %s is empty" % COMPACK, file=sys.stderr)
        return 1

    problems = []
    check_rows(
        "kit",
        KIT_ROWS,
        client["DESIGN_KIT_ROW_FIRST"],
        client["DESIGN_KIT_ROW_COUNT"],
        client["DESIGN_ROW_STRIDE"],
        children,
        problems,
    )
    check_rows(
        "colour",
        COLOUR_ROWS,
        client["DESIGN_COLOUR_ROW_FIRST"],
        client["DESIGN_COLOUR_ROW_COUNT"],
        client["DESIGN_ROW_STRIDE"],
        children,
        problems,
    )
    if problems:
        for line in problems:
            print("check_player_design_panel: %s" % line, file=sys.stderr)
        return 1

    print(
        "check_player_design_panel: %d kit rows from %d and %d colour rows from %d, stride %d"
        % (
            client["DESIGN_KIT_ROW_COUNT"],
            client["DESIGN_KIT_ROW_FIRST"],
            client["DESIGN_COLOUR_ROW_COUNT"],
            client["DESIGN_COLOUR_ROW_FIRST"],
            client["DESIGN_ROW_STRIDE"],
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
