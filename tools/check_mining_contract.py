#!/usr/bin/env python3
"""Hold the mining ore table to the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D12.

Same lens as D11's four tables. `mine.dbrow` carries a level and an experience
award per rock type; the wiki's ore table carries both.

Experience is compared in TENTHS -- blurite pays 17.5 and volcanic ash 10, so
integers would call several different awards equal.

Matched by ORE NAME (`{{plinkt|Iron ore|txt=Iron}}` -> our `ore_name,iron`),
which is a real shared key here, unlike the chest and door tables where our
blocks carry cache loc symbols and had to be matched by level.
"""
import io
import os
import re
import sys

WIKI = "docs/skills/mining/sources/Mining.wiki"
ROWS = ("OSRS-Content/osrs239-content/server/scripts/skill_mining/configs/"
        "mine.dbrow")


def wiki_ores(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for chunk in text.split("|-"):
        m = re.search(r"\{\{plinkt\|([^}|]+)(?:\|txt=([^}|]+))?", chunk)
        if not m:
            continue
        label = (m.group(2) or m.group(1)).strip().lower()
        nums = re.search(r"\n\|(\d+)(?:-\d+)?\|\|(\d+(?:\.\d+)?)", chunk)
        if not nums:
            continue
        out[label] = (int(nums.group(1)),
                      int(round(float(nums.group(2)) * 10)))
    return out


def our_rocks(path):
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
        print("check_mining_contract: %s is not pinned" % WIKI, file=sys.stderr)
        return 1
    wiki = wiki_ores(WIKI)
    ours = our_rocks(ROWS)

    problems, skipped, checked = [], [], 0
    for block, data in sorted(ours.items()):
        name = data.get("ore_name")
        if not name or "rock_level" not in data:
            continue
        row = wiki.get(name.lower())
        if not row:
            skipped.append("%s: no wiki ore row named %r" % (block, name))
            continue
        w_level, w_xp = row
        o_level = int(data["rock_level"])
        o_xp = data.get("rock_experience") or data.get("experience")
        checked += 1
        if o_level != w_level:
            problems.append("%s: wiki level %d, config %d"
                            % (block, w_level, o_level))
        if o_xp is not None and int(o_xp) != w_xp:
            problems.append("%s: wiki xp %s, config %s"
                            % (block, w_xp / 10, int(o_xp) / 10))

    for s in skipped:
        print("  skipped  " + s)
    for p in problems:
        print("  " + p)
    if problems:
        print("check_mining_contract: %d problem(s) across %d rock(s)"
              % (len(problems), checked), file=sys.stderr)
        return 1
    print("check_mining_contract: %d rock(s) agree with the pinned wiki, "
          "%d skipped" % (checked, len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
