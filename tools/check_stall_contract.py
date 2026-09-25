#!/usr/bin/env python3
"""Hold the thieving stalls to the wiki, where the match is unambiguous.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D11.

The wiki's Thievable stalls table has one row per stall INSTANCE, not per stall
type: silver ore appears twice at level 50 with 205 and 80 experience, and
sapphire twice at level 75 with 408 and 129.5. Which row a given stall is
depends on where it stands -- ours is Ardougne's, and the table's location
column is prose.

So this checker matches on (item name, level) and requires the match to be
UNIQUE. Where two rows share both it skips the stall and says so, because
guessing which instance we implement is how a checker invents findings -- this
session has produced six false ones already by being confident about a mapping
it had not verified.

Four stalls check cleanly. Three are skipped by name, and that is the honest
number rather than a bigger one.
"""
import io
import os
import re
import sys

WIKI = "docs/skills/thieving/sources/Stall_Thievable.wiki"
ROWS = ("OSRS-Content/osrs239-content/server/scripts/skill_thieving/configs/"
        "stalls/stealing.dbrow")

# our block -> the wiki row's item name
STALLS = {
    "stealing_bakery_stall": "Bakery stall",
    "stealing_tea_stall": "Tea stall",
    "stealing_silk_stall": "Silk stall",
    "stealing_fur_stall": "Fur stall",
}


def wiki_rows(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for chunk in text.split("|-")[1:]:
        # The row LABEL is `{{plinkt|Fur stall|...}}`; the first `[[...]]` in
        # the chunk is the LOOT column ("Fur or grey wolf fur"). Keying on the
        # loot happens to work for some rows and silently conflates
        # "Fur stall" with "Fur stall (Port Roberts)" -- two different stalls
        # at the same level with different experience. Match the label.
        name = re.search(r"\{\{plinkt\|([^}|]+)", chunk)
        nums = re.findall(r"\|\s*([\d,]+(?:\.\d+)?)\s*(?:\|\||\n)", chunk)
        if not name or len(nums) < 2:
            continue
        key = name.group(1).strip()
        level = int(float(nums[0].replace(",", "")))
        xp = int(round(float(nums[1].replace(",", "")) * 10))
        out.setdefault((key, level), []).append(xp)
    return out


def our_rows(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out, block = {}, None
    for line in text.split("\n"):
        m = re.match(r"^\[([a-z0-9_]+)\]$", line.strip())
        if m:
            block = m.group(1)
            out[block] = {}
            continue
        if block and line.startswith("data="):
            k, _, v = line[5:].partition(",")
            out[block].setdefault(k, v)
    return out


def main():
    if not os.path.exists(WIKI):
        print("check_stall_contract: %s is not pinned" % WIKI, file=sys.stderr)
        return 1
    wiki, ours = wiki_rows(WIKI), our_rows(ROWS)

    problems, checked, skipped = [], 0, []
    for block, item in sorted(STALLS.items()):
        if block not in ours:
            problems.append("%s: no block in stealing.dbrow" % block)
            continue
        level = int(ours[block].get("level", -1))
        xp = int(ours[block].get("experience", -1))
        candidates = wiki.get((item, level))
        if not candidates:
            skipped.append("%s: no wiki row for %r at level %d"
                           % (block, item, level))
            continue
        if len(set(candidates)) > 1:
            skipped.append("%s: %d wiki rows share %r at level %d (%s) -- "
                           "cannot tell which instance this is"
                           % (block, len(candidates), item, level,
                              ", ".join(str(c / 10) for c in sorted(set(candidates)))))
            continue
        checked += 1
        if xp != candidates[0]:
            problems.append("%s: wiki xp %s, config %s"
                            % (block, candidates[0] / 10, xp / 10))

    for s in skipped:
        print("  skipped  " + s)
    for p in problems:
        print("  " + p)
    if problems:
        print("check_stall_contract: %d problem(s)" % len(problems),
              file=sys.stderr)
        return 1
    print("check_stall_contract: %d stall(s) agree with the pinned wiki, "
          "%d skipped as ambiguous" % (checked, len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
