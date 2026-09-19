#!/usr/bin/env python3
"""The design colour palettes are stated twice; hold the two together.

The five recolour palettes belong to the CLIENT -- `k_recol1d` in
`src/engine/entity_model_build.c`, transcribed from the reference's
`ClientPlayer.recol1d`. The wire carries one byte per row and the client
recolours the merged model with `palette[0] -> palette[colour]`.

The server has to know how long each is, so the design panel's arrows wrap in
the right place, and it does not link the renderer. So the counts are copied
into `player/configs/player_design.constant` -- and a copy that nothing checks
is a copy that drifts. The symptom would be one arrow on one row doing nothing
at one end, which is about as quiet as a bug gets.

  python3 tools/check_design_colours.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "src", "engine", "entity_model_build.c")
CONST = os.path.join(
    ROOT, "OSRS-Content", "osrs239-content", "server", "scripts",
    "player", "configs", "player_design.constant",
)
ROWS = ["hair", "torso", "legs", "feet", "skin"]


def client_counts():
    text = open(BUILD, encoding="utf8").read()
    counts = {}
    for row in ROWS:
        m = re.search(r"k_recol_%s\[\]\s*=\s*\{(.*?)\}" % row, text, re.S)
        if not m:
            print("check_design_colours: no k_recol_%s in %s" % (row, BUILD), file=sys.stderr)
            return None
        counts[row] = len([v for v in m.group(1).split(",") if v.strip()])
    return counts


def content_counts():
    text = open(CONST, encoding="utf8").read()
    counts = {}
    for row in ROWS:
        m = re.search(r"^\^design_colour_count_%s\s*=\s*(\d+)" % row, text, re.M)
        if not m:
            print("check_design_colours: no ^design_colour_count_%s in %s" % (row, CONST),
                  file=sys.stderr)
            return None
        counts[row] = int(m.group(1))
    return counts


def main():
    client = client_counts()
    content = content_counts()
    if client is None or content is None:
        return 1
    bad = [r for r in ROWS if client[r] != content[r]]
    if bad:
        for r in bad:
            print("check_design_colours: %s palette is %d in the client and %d in content"
                  % (r, client[r], content[r]), file=sys.stderr)
        return 1
    print("check_design_colours: %s agree (%s)"
          % (", ".join(ROWS), ", ".join(str(client[r]) for r in ROWS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
