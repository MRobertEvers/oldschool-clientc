#!/usr/bin/env python3
"""Derive Konar's per-task location lists from the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice C8.

Konar is the only Slayer master whose tasks name a place: "kill X **in** Y", and
a kill anywhere else gives no credit. The wiki's assignment table carries the
eligible locations per monster, so the list is data to extract rather than data
to type -- there are 60-odd tasks and several have five locations each.

Mod Ash on the roll (pinned in the page's own citation): "Yes, it's an equal
chance for each of the areas you're eligible to access." So the roll is uniform
over the ELIGIBLE subset, not weighted and not uniform over the full list -- a
location behind an unfinished quest is removed from the draw rather than
re-rolled, which is a different distribution.
"""
import argparse
import io
import os
import re
import sys

SRC = "docs/skills/slayer/sources/Konar_quo_Maten.wiki"


def slug(text):
    text = text.strip().lower()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    return text.strip("_")


def parse(path):
    with io.open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    # The assignment table is the one whose header names "Possible locations".
    start = text.find("!data-sort-value=\"\"|Possible locations")
    if start < 0:
        return []
    body = text[start:]
    end = body.find("\n|}")
    if end > 0:
        body = body[:end]

    rows = []
    for chunk in body.split("\n|-\n")[1:]:
        lines = [ln.rstrip() for ln in chunk.split("\n")]
        # Cell 1 is the monster; the location cell is the bullet list that
        # follows the next bare `|`.
        if not lines or not lines[0].startswith("|"):
            continue
        monster = lines[0][1:]
        m = re.search(r"\[\[([^\]|]+)", monster)
        monster = m.group(1) if m else monster
        locs = []
        seen_second_cell = False
        for ln in lines[1:]:
            if ln.startswith("*") and seen_second_cell:
                lm = re.search(r"\[\[([^\]|]+)", ln)
                if lm:
                    locs.append(lm.group(1))
            elif ln == "|":
                if seen_second_cell:
                    break
                seen_second_cell = True
            elif ln.startswith("|") and seen_second_cell:
                break
        if locs:
            rows.append((monster.strip(), locs))
    return rows


HEADER = """// Konar quo Maten -- per-task locations. GENERATED, do not hand-edit.
//
// Written by tools/gen_konar_locations.py from
// docs/skills/slayer/sources/Konar_quo_Maten.wiki (pinned). Hand edits belong
// in the manifest's [extra:] section.
//
// [wiki] "Unlike other Slayer masters, Konar's Slayer tasks also include a
// specific location where players must complete the entire task." A kill
// outside the named place gives NO credit, which is the whole reason she pays
// more points than Duradel.
//
// [wiki, Mod Ash] "Yes, it's an equal chance for each of the areas you're
// eligible to access." Uniform over the ELIGIBLE subset -- a location behind an
// unfinished quest is removed from the draw, not re-rolled into it.
"""


def render(rows):
    out = [HEADER, "[konar_task_location_count]", "inputtype=int",
           "outputtype=int", "default=0"]
    for i, (_monster, locs) in enumerate(rows):
        out.append("val=%d,%d" % (i, len(locs)))
    out.append("")
    out.append("[konar_task_location_name]")
    out.append("inputtype=int")
    out.append("outputtype=string")
    out.append("default=none")
    for i, (_monster, locs) in enumerate(rows):
        for j, loc in enumerate(locs):
            out.append("val=%d,%s" % (i * 8 + j, loc))
    out.append("")
    out.append("[konar_task_name]")
    out.append("inputtype=int")
    out.append("outputtype=string")
    out.append("default=none")
    for i, (monster, _locs) in enumerate(rows):
        out.append("val=%d,%s" % (i, monster))
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    rows = parse(SRC)
    if not rows:
        print("gen_konar_locations: parsed no rows from %s" % SRC,
              file=sys.stderr)
        return 1

    widest = max(len(l) for _m, l in rows)
    print("gen_konar_locations: %d task(s), %d location entr(ies), widest %d"
          % (len(rows), sum(len(l) for _m, l in rows), widest))
    if widest > 8:
        # The name enum packs eight slots per task; a wider row would collide
        # with the next task's first location and silently mislabel it.
        print("gen_konar_locations: a task has %d locations but the key packs "
              "8 per task -- widen the stride" % widest, file=sys.stderr)
        return 1

    dest = ("OSRS-Content/osrs239-content/server/scripts/skill_slayer/configs/"
            "konar_locations.enum")
    text = render(rows)
    if args.check:
        if not os.path.exists(dest):
            print("gen_konar_locations: %s is missing" % dest, file=sys.stderr)
            return 1
        with io.open(dest, encoding="utf-8") as fh:
            if fh.read() != text:
                print("gen_konar_locations: %s is stale" % dest,
                      file=sys.stderr)
                return 1
        print("gen_konar_locations: %s is up to date" % dest)
        return 0
    with io.open(dest, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_konar_locations: wrote %s" % dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
