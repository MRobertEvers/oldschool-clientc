#!/usr/bin/env python3
"""Regenerate skill_farming/configs/farming_crop.loc.

Every growing-crop morph of a farming patch goes into one server-side category
so `[oplocu,_farming_crop]` can answer "use an item on a growing crop" once,
instead of one name binding per morph. There are 1,102 of them.

The source of truth is the multiloc table of each patch wrapper in
`configs/all.loc` -- the cache's own statement of which locs a patch can remorph
into. Weeds and bare-patch states are excluded: they already carry
`[oploc*,<name>]` bindings, and a name binding wins over a category one, so
including them would add a second answer that never runs.

Usage:
    python3 tools/gen_farming_crop_category.py [--check]

`--check` exits non-zero if the committed file does not match what would be
generated, which is what a build step wants.
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
ALL_LOC = os.path.join(CONTENT, "configs", "all.loc")
OUT = os.path.join(CONTENT, "server", "scripts", "skill_farming", "configs",
                   "farming_crop.loc")

WRAPPERS = [
    "farming_veg_patch_1",
    "farming_flower_patch_1",
    "farming_herb_patch_1",
    "farming_hops_patch_1",
    "farming_bush_patch_1",
    "farming_tree_patch_1",
    "farming_fruit_tree_patch_1",
]

HEADER = """// Generated: every growing-crop morph of a farming patch, in one category.
//
// Source is the multiloc tables of the seven patch wrappers in configs/all.loc
// (farming_veg_patch_1, farming_flower_patch_1, farming_herb_patch_1,
// farming_hops_patch_1, farming_bush_patch_1, farming_tree_patch_1,
// farming_fruit_tree_patch_1), minus every weeds / bare-patch state.
//
// See pack/category.pack for why the category exists. Regenerate rather than
// hand-edit: `tools/gen_farming_crop_category.py`.
"""


def parse_blocks(text):
    return {
        m.group(1): m.group(2)
        for m in re.finditer(r"^\[([A-Za-z0-9_]+)\]\n((?:(?!^\[).*\n)*)", text, re.M)
    }


def is_patch_state(name):
    """A weeds / bare-patch morph, which keeps its own name binding."""
    return "weeds" in name or name.endswith("_weeded")


def collect(blocks):
    crops, seen = [], set()
    for wrapper in WRAPPERS:
        body = blocks.get(wrapper)
        if body is None:
            sys.exit("gen_farming_crop_category: no [%s] in all.loc" % wrapper)
        for m in re.finditer(r"^multiloc\d+=(\S+)$", body, re.M):
            name = m.group(1)
            if name == "-1" or is_patch_state(name) or name in seen:
                continue
            if name not in blocks:
                sys.exit("gen_farming_crop_category: %s names %s, which all.loc "
                         "does not define" % (wrapper, name))
            seen.add(name)
            crops.append(name)
    return crops


def render(crops):
    return "\n".join([HEADER] + ["[%s]\ncategory=farming_crop\n" % c for c in crops])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the committed file is stale")
    args = ap.parse_args()

    with open(ALL_LOC) as fh:
        blocks = parse_blocks(fh.read())
    want = render(collect(blocks))

    if args.check:
        try:
            with open(OUT) as fh:
                have = fh.read()
        except FileNotFoundError:
            sys.exit("gen_farming_crop_category: %s is missing" % OUT)
        if have != want:
            sys.exit("gen_farming_crop_category: %s is stale -- rerun without "
                     "--check" % OUT)
        print("farming_crop.loc is up to date")
        return

    with open(OUT, "w") as fh:
        fh.write(want)
    print("wrote %s (%d crop morphs)" % (OUT, want.count("category=farming_crop")))


if __name__ == "__main__":
    main()
