#!/usr/bin/env python3
"""Hold the thieving chest and door tables to the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D11.

The other half of D11's data. `check_pickpocket_contract.py` and
`check_stall_contract.py` between them found four stale experience awards in
thirteen rows, all inherited from LostCity, all with the LEVEL correct -- so
these eleven rows were worth pointing the same lens at.

They are clean. That is a result, not a non-result: it means the staleness is
not uniform across everything LostCity handed over, and the next person does not
have to re-check these.

Rows are matched by (level, experience) against the wiki's own tables rather
than by name, because our block names (`trapped_chest_trapchest1`,
`locked_door_picklock3_l`) carry the cache's loc symbol and not the chest's
English name -- there is no shared key to join on. A row is confirmed when
exactly one wiki row has that level, so an ambiguous level is skipped rather
than guessed.
"""
import io
import os
import re
import sys

WIKI = "docs/skills/thieving/sources/Thieving.wiki"
BASE = "OSRS-Content/osrs239-content/server/scripts/skill_thieving/configs"
CHESTS = os.path.join(BASE, "chests", "trapped_chest.dbrow")
DOORS = os.path.join(BASE, "doors", "locked_door.dbrow")


def wiki_table(text, start_marker, stop_marker):
    i = text.find(start_marker)
    if i < 0:
        return {}
    blk = text[i:text.find(stop_marker, i + 10)]
    out = {}
    for chunk in blk.split("|-")[1:]:
        nums = re.findall(r"\|\|\s*(\d+(?:\.\d+)?)\s*(?=\|\|)", chunk)
        if len(nums) < 2:
            continue
        level = int(float(nums[0]))
        xp = int(round(float(nums[1]) * 10))
        out.setdefault(level, set()).add(xp)
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


# Rows where the wiki disagrees with ITSELF, so neither figure can be the
# reference and "fixing" ours would be picking a side at random.
KNOWN_WIKI_CONFLICTS = {
    "locked_door_toollock2":
        "the Magic axe hut door: the Thieving table says level 23 for 22.5 xp, "
        "while the door's own article says level 32. Ours is level 23 for 25. "
        "Two wiki pages contradict each other on the level, so the experience "
        "cannot be trusted from either without settling that first.",
}


def check(label, rows, table, problems, skipped):
    checked = 0
    for block, data in sorted(rows.items()):
        if "level" not in data or "experience" not in data:
            continue
        if block in KNOWN_WIKI_CONFLICTS:
            skipped.append("%s %s: %s" % (label, block,
                                          KNOWN_WIKI_CONFLICTS[block]))
            continue
        level = int(data["level"])
        xp = int(data["experience"])
        candidates = table.get(level)
        if not candidates:
            skipped.append("%s %s: no wiki row at level %d"
                           % (label, block, level))
            continue
        if len(candidates) > 1:
            if xp in candidates:
                checked += 1
                continue
            skipped.append("%s %s: %d wiki rows at level %d (%s) and ours "
                           "matches none" % (label, block, len(candidates),
                                             level,
                                             ", ".join(str(c / 10) for c in sorted(candidates))))
            continue
        checked += 1
        only = next(iter(candidates))
        if xp != only:
            problems.append("%s %s: wiki xp %s at level %d, config %s"
                            % (label, block, only / 10, level, xp / 10))
    return checked


def main():
    if not os.path.exists(WIKI):
        print("check_thieving_tables: %s is not pinned" % WIKI, file=sys.stderr)
        return 1
    text = io.open(WIKI, encoding="utf-8", errors="replace").read()
    chest_table = wiki_table(text, "Chest!!", "==Doors")
    door_table = wiki_table(text, "!Door!!", "\n==")

    problems, skipped = [], []
    n = check("chest", our_rows(CHESTS), chest_table, problems, skipped)
    n += check("door", our_rows(DOORS), door_table, problems, skipped)

    for s in skipped:
        print("  skipped  " + s)
    for p in problems:
        print("  " + p)
    if problems:
        print("check_thieving_tables: %d problem(s)" % len(problems),
              file=sys.stderr)
        return 1
    print("check_thieving_tables: %d chest/door row(s) agree with the pinned "
          "wiki, %d skipped" % (n, len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
