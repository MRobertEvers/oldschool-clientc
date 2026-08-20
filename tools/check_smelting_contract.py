#!/usr/bin/env python3
"""Hold the smelting table to the pinned wiki.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D12.

The smelting table's row shape is unusual and cost me a wrong first parse: the
LEVEL is the line BEFORE the bar's name, not after it, and the experience is
two lines further on past the ore-requirement line:

    |15
    |{{plinkt|Iron bar|txt=Iron}}
    |{{plinkp|Iron ore}}<sup>x1</sup>
    |12.5

Scraping numbers in order out of the chunk gives the ore COUNT where the
experience should be -- "Bronze bar 1, 1, 1" -- which reads plausibly because
bronze really is level 1.

Blurite is skipped and the reason is worth knowing rather than fixing: its row
is preceded by a long `<ref>` footnote that separates the level cell from the
bar's name, so the pattern above cannot reach it. The footnote also records a
genuine oddity -- "smelting blurite bars has a requirement of level 8 Smithing,
attempting to do so with less than 13 Smithing will prompt the player that the
level requirement is 13". The data says 8, the game enforces 13, and this tree
carries 13, which is the behaviour rather than the table. Automating that would
mean teaching the parser to prefer a footnote over a column.

One row carries two experience figures and the wiki explains why in a footnote:
blurite is "8 (9.5)", because "Casting Super Heat on blurite ore grants 9.5 xp,
furnaces give 8 xp". The furnace figure is the one a smelting table wants; the
parser takes the first and the footnote is quoted here so the next reader knows
the second was seen and rejected, not missed.
"""
import io
import os
import re
import sys

WIKI = "docs/skills/mining/sources/Smithing.wiki"
ROWS = ("OSRS-Content/osrs239-content/server/scripts/skill_smithing/configs/"
        "smelting.dbrow")


def wiki_bars(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out = {}
    # |<level>\n|{{plinkt|<Bar> bar|...}}\n|<ore lines>\n|<xp>
    pattern = re.compile(
        r"\n\|(\d+)\s*\n\|\{\{plinkt\|([^}|]+)[^}]*\}\}\s*\n"
        r"(?:\|[^\n]*\n)*?\|(\d+(?:\.\d+)?)",
        re.M)
    for m in pattern.finditer(text):
        level = int(m.group(1))
        name = m.group(2).strip().lower()
        xp = int(round(float(m.group(3)) * 10))
        out.setdefault(name, (level, xp))
    return out


def our_bars(path):
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
        print("check_smelting_contract: %s is not pinned" % WIKI,
              file=sys.stderr)
        return 1
    wiki = wiki_bars(WIKI)
    ours = our_bars(ROWS)

    problems, skipped, checked = [], [], 0
    for block, data in sorted(ours.items()):
        bar = data.get("bar")
        if not bar or "levelrequired" not in data:
            continue
        key = bar.replace("_", " ").lower()
        row = wiki.get(key)
        if not row:
            skipped.append("%s: no wiki row named %r" % (block, key))
            continue
        w_level, w_xp = row
        o_level = int(data["levelrequired"])
        o_xp = int(data.get("experience", -1))
        checked += 1
        if o_level != w_level:
            problems.append("%s: wiki level %d, config %d"
                            % (block, w_level, o_level))
        if o_xp >= 0 and o_xp != w_xp:
            problems.append("%s: wiki xp %s, config %s"
                            % (block, w_xp / 10, o_xp / 10))

    for s in skipped:
        print("  skipped  " + s)
    for p in problems:
        print("  " + p)
    if problems:
        print("check_smelting_contract: %d problem(s) across %d bar(s)"
              % (len(problems), checked), file=sys.stderr)
        return 1
    print("check_smelting_contract: %d bar(s) agree with the pinned wiki, "
          "%d skipped" % (checked, len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
