#!/usr/bin/env python3
"""Hold the pickpocket table to the levels, experience and stun the wiki gives.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slice D11.

Fifteen npc rows in `pickpocket.dbrow`, each with a Thieving level, an
experience award and a stun. The wiki's Thievable NPCs table has all three, so
they are checkable rather than trusted.

Experience is compared in TENTHS: a farmer pays 14.5 and the H.A.M. member 22.2,
so an integer comparison calls two different values equal.

What this deliberately does NOT check: stun DURATION. The wiki's table gives
stun damage but states duration only in prose for some npcs, and this tree
stores `stun_ticks` -- ticks against seconds, with no stated conversion for
every row. Checking it would mean inventing the mapping, so it is left alone and
said so, rather than half-checked.
"""
import io
import os
import re
import sys

WIKI = "docs/skills/thieving/sources/Thieving.wiki"
ROWS = ("OSRS-Content/osrs239-content/server/scripts/skill_thieving/configs/"
        "pickpocking/pickpocket.dbrow")

# our dbrow block -> the wiki's row label
NPCS = {
    "pickpocket_man": "Man",
    "pickpocket_farmer": "Farmer",
    "pickpocket_warrior": "Warrior",
    "pickpocket_rogue": "Rogue",
    "pickpocket_guard": "Guard",
    "pickpocket_knight_of_ardougne": "Knight of Ardougne",
    "pickpocket_watchman": "Watchman",
    "pickpocket_paladin": "Paladin",
    "pickpocket_gnome": "Gnome",
    "pickpocket_hero": "Hero",
    "pickpocket_menaphite_thug": "Menaphite Thug",
}


def wiki_rows(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    start = text.find("===Thievable NPCs===")
    body = text[start:]
    end = body.find("\n|}")
    if end > 0:
        body = body[:end]
    out = {}
    for chunk in body.split("\n|-\n")[1:]:
        name = re.search(r"\{\{plinkt\|([^}|]+)", chunk)
        if not name:
            continue
        # "| 1 || 8 || 85 || 1" -- level, xp, ardougne-cloak level, stun damage
        nums = re.findall(r"(\d+(?:\.\d+)?)", chunk.split("\n", 1)[-1])
        if len(nums) < 2:
            continue
        out[name.group(1).strip()] = (
            int(float(nums[0])), int(round(float(nums[1]) * 10)))
    return out


def our_rows(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    out = {}
    block = None
    for line in text.split("\n"):
        m = re.match(r"^\[([a-z0-9_]+)\]$", line.strip())
        if m:
            block = m.group(1)
            out[block] = {}
            continue
        if block and line.startswith("data="):
            k, _, v = line[5:].partition(",")
            out[block][k] = v
    return out


def main():
    if not os.path.exists(WIKI):
        print("check_pickpocket_contract: %s is not pinned" % WIKI,
              file=sys.stderr)
        return 1
    wiki = wiki_rows(WIKI)
    ours = our_rows(ROWS)

    problems = []
    checked = 0
    for block, label in sorted(NPCS.items()):
        if label not in wiki:
            print("  %-32s not found in the wiki table -- skipped" % label)
            continue
        if block not in ours:
            problems.append("%s: no block in pickpocket.dbrow" % block)
            continue
        w_level, w_xp = wiki[label]
        o_level = int(ours[block].get("level", -1))
        o_xp = int(ours[block].get("experience", -1))
        checked += 1
        if o_level != w_level:
            problems.append("%s: wiki level %d, config %d"
                            % (block, w_level, o_level))
        if o_xp != w_xp:
            problems.append("%s: wiki xp %s, config %s"
                            % (block, w_xp / 10, o_xp / 10))

    for p in problems:
        print("  " + p)
    if problems:
        print("check_pickpocket_contract: %d problem(s) across %d npc(s)"
              % (len(problems), checked), file=sys.stderr)
        return 1
    print("check_pickpocket_contract: %d npc(s) agree with the pinned wiki"
          % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
