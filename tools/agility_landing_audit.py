#!/usr/bin/env python3
"""Every coordinate an Agility course sends a player to must be a real tile.

A course script is a list of destinations: `p_telejump(2_52_46_30_38)` and
friends. Nothing else in the tree checks that those tiles exist — a course with
a landing in unmapped space compiles, passes `tools/agility_xp_audit.py`, and
passes `::agilityrun`, because none of the three knows what a map square is. It
fails only when a player runs the lap and ends up inside a wall or off the edge
of the world.

This runs every coord literal in the course scripts through `walkable_probe`,
which builds the server's own collision map from the real map squares.

What it is good for, both earned on the first course authored without a
corpus (Pollnivneach):

  * a mis-encoded literal. The local halves are x%64 and z%64, and computing
    them off the wrong base puts the tile hundreds of tiles away — five of nine.
  * a landing on a tile a loc occupies, or one the map flags as blocked floor.

One trap, paid for once: an empty flag word means an ORDINARY OPEN TILE, not
missing data. The map's BLOCK setting is what stamps the floor flag, so no
flags at all means nothing blocks the tile — the middle of Lumbridge's field
reads zero. A version of this audit that read zero as "no data" reported three
quarters of every course's landings as broken and nearly had eight working
rooftop courses "corrected" onto tiles they never needed.

    tools/agility_landing_audit.py [--cache cache.osrs239]

Needs `make -C src walkable-probe` first. Not wired into the script build,
because it needs the cache and a compiled C binary rather than just the content
tree — run it when a course's coordinates change.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

COORD = re.compile(r"\b(\d)_(\d+)_(\d+)_(\d+)_(\d+)\b")

# Course scripts whose coord literals are landings. Shared helpers are excluded:
# `agility.rs2` has none, and the marks enum is checked by the same rule through
# its own file.
SCRIPT_GLOBS = (
    "server/scripts/skill_agility/scripts/rooftop_*.rs2",
    "server/scripts/skill_agility/scripts/gnome_course.rs2",
    "server/scripts/skill_agility/scripts/barbarian_course.rs2",
    "server/scripts/skill_agility/scripts/wilderness_course.rs2",
    "server/scripts/skill_agility/scripts/shayzien_course.rs2",
    "server/scripts/skill_agility/scripts/shayzien_advanced.rs2",
    "server/scripts/skill_agility/scripts/apeatoll_course.rs2",
    "server/scripts/skill_agility/scripts/werewolf_course.rs2",
    "server/scripts/skill_agility/scripts/penguin_course.rs2",
    "server/scripts/skill_agility/configs/agility_marks.enum",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", default="OSRS-Content/osrs239-content")
    parser.add_argument("--cache", default="cache.osrs239")
    parser.add_argument("--probe", default="src/build_opt/walkable_probe")
    args = parser.parse_args()

    if not os.path.exists(args.probe):
        print(
            "agility_landing_audit: %s missing — run `make -C src walkable-probe`"
            % args.probe,
            file=sys.stderr,
        )
        return 2

    checked = 0
    failures = 0
    for pattern in SCRIPT_GLOBS:
        for path in sorted(glob.glob(os.path.join(args.tree, pattern))):
            text = open(path, encoding="utf-8").read()
            # Comments state placements and dead reckoning; only live code counts.
            code = "\n".join(
                line for line in text.split("\n") if not line.lstrip().startswith("//")
            )
            for level, msx, msz, lx, lz in sorted(set(COORD.findall(code))):
                x = int(msx) * 64 + int(lx)
                z = int(msz) * 64 + int(lz)
                out = subprocess.run(
                    [args.probe, args.cache, str(x), str(z), level],
                    capture_output=True,
                    text=True,
                ).stdout
                verdict = [l for l in out.splitlines() if l.startswith("%d %d" % (x, z))]
                checked += 1
                line = verdict[0] if verdict else "no answer from the probe"
                if "standable" in line:
                    continue
                print(
                    "agility_landing_audit: %s sends a player to (%d,%d,%s) — %s"
                    % (os.path.basename(path), x, z, level, line),
                    file=sys.stderr,
                )
                failures += 1

    if failures:
        print(
            "agility_landing_audit: %d of %d landings are blocked"
            % (failures, checked),
            file=sys.stderr,
        )
        return 1
    print(
        "agility_landing_audit: %d landings checked, all standable"
        % checked
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
