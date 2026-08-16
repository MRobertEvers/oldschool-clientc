#!/usr/bin/env python3
"""Check skill_farming's crop tables against the cache's own multiloc tables.

`plant_state` and `stages` in `crops.dbrow` are indices into a patch wrapper's
multiloc list, and nothing else in the build checks them. Get one wrong and the
patch still works -- it just draws the wrong plant, or an adjacent crop's
diseased art, for one stage. That is the failure this exists to catch.

For every row it asserts, against `configs/all.loc`:

  * multiloc[plant_state]            is <crop>_seed
  * multiloc[plant_state + i]        belongs to the same crop, for 0 < i < stages
  * multiloc[plant_state + stages]   is exactly the row's `fullygrown`
  * multiloc[plant_state + i + 64]   is the `_watered` art, for 0 <= i < stages
    (allotments and flowers only, and only where the cache states one)

The +64 watered offset is the block layout documented in farming.constant.

Usage:
    python3 tools/check_farming_crop_states.py
Exits non-zero on the first inconsistency.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
ALL_LOC = os.path.join(CONTENT, "configs", "all.loc")
CROPS = os.path.join(CONTENT, "server", "scripts", "skill_farming", "configs",
                     "crops.dbrow")

WATERED_OFFSET = 64

# Which wrapper's multiloc table a table's rows index into.
WRAPPER_FOR_TABLE = {
    "farming_allotments": "farming_veg_patch_1",
    "farming_flowers": "farming_flower_patch_1",
}


def parse_blocks(text):
    return {
        m.group(1): m.group(2)
        for m in re.finditer(r"^\[([A-Za-z0-9_]+)\]\n((?:(?!^\[).*\n)*)", text, re.M)
    }


def multilocs(body):
    """varbit value -> child loc name. `multilocN` is value N-1."""
    return {
        int(m.group(1)) - 1: m.group(2)
        for m in re.finditer(r"^multiloc(\d+)=(\S+)$", body, re.M)
    }


def rows(text):
    for chunk in re.split(r"\n(?=\[)", text):
        m = re.match(r"\[(\w+)\]", chunk)
        if not m:
            continue
        fields = dict(re.findall(r"^data=(\w+),(\S+)$", chunk, re.M))
        table = re.search(r"^table=(\S+)$", chunk, re.M)
        if not table or "plant_state" not in fields:
            continue
        yield m.group(1), table.group(1), fields


def main():
    with open(ALL_LOC) as fh:
        blocks = parse_blocks(fh.read())
    tables = {t: multilocs(blocks[w]) for t, w in WRAPPER_FOR_TABLE.items()}

    with open(CROPS) as fh:
        text = fh.read()

    problems = []
    checked = 0
    for name, table, f in rows(text):
        ml = tables.get(table)
        if ml is None:
            continue
        plant, stages = int(f["plant_state"]), int(f["stages"])
        seed = ml.get(plant)
        if not seed or not seed.endswith("_seed"):
            problems.append("%s: multiloc[%d] is %r, expected a _seed art"
                            % (name, plant, seed))
            continue
        family = seed[: -len("_seed")]

        for i in range(1, stages):
            art = ml.get(plant + i)
            if not art or not art.startswith(family + "_"):
                problems.append("%s: stage %d (state %d) is %r, not a %s stage"
                                % (name, i, plant + i, art, family))

        grown = ml.get(plant + stages)
        if grown != f.get("fullygrown"):
            problems.append("%s: multiloc[%d] is %r but fullygrown says %r"
                            % (name, plant + stages, grown, f.get("fullygrown")))

        # Watered art, where the cache carries it. A fully grown crop has none,
        # which is why the range stops one short of `stages`.
        for i in range(0, stages):
            art = ml.get(plant + i + WATERED_OFFSET)
            if art is None:
                continue
            if art != ml[plant + i] + "_watered":
                problems.append(
                    "%s: watered slot %d is %r, expected %r — the +%d block "
                    "layout does not hold for this crop"
                    % (name, plant + i + WATERED_OFFSET, art,
                       ml[plant + i] + "_watered", WATERED_OFFSET))
        checked += 1

    for p in problems:
        print("FAIL " + p, file=sys.stderr)
    if problems:
        sys.exit("check_farming_crop_states: %d problem(s) across %d row(s)"
                 % (len(problems), checked))
    print("check_farming_crop_states: %d crop rows agree with all.loc" % checked)


if __name__ == "__main__":
    main()
