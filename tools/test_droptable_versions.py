#!/usr/bin/env python3
"""Regression gate for per-version drop tables and the death_drop statement.

Two bugs this pins, both of which shipped and neither of which any existing
check could see (docs/NPC_WIKI_DROPTABLES_PLAN.md section 9):

  1. `write_group` emitted `results[0]`'s table under one label and pointed
     every `[ai_queue3]` on the page at it, so 48 npcs across 11 groups were
     served another version's loot -- the level-13 giant frog got the level-99
     one's table, the God Wars hellhound got the surface one's smouldering
     stone, and the level-149 TzHaar-Ket got the level-221's.

  2. `param=death_drop` went unstated, so every npc inherited the tree-wide
     `[default] param=death_drop,bones` -- TzHaar left bones they have no
     source for, demons left bones instead of ashes, and every variant family
     left its own remains *and* plain bones.

Run: python3 tools/test_droptable_versions.py
"""

from __future__ import annotations

import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wiki_droptable as wd  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, "OSRS-Content", "osrs239-content")
TABLES = os.path.join(CONTENT, "server", "scripts", "drop_tables", "scripts")
NPC_CONFIG = os.path.join(CONTENT, "server", "scripts", "npc", "configs",
                          "combat_stats.generated.npc")

failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    if not cond:
        failures.append(msg)


def result(gameval: str, rs2: list[str], dropversion: str | None) -> dict:
    return {"gameval": gameval, "rs2": rs2, "dropversion": dropversion,
            "title": "T", "revid": "1", "fetch_date": "d", "tertiary_present": False}


def test_partition_splits_differing_tables() -> None:
    """The unit the fix turns on. Two npcs whose tables differ must not share
    a group, however similar their pages are."""
    a = result("zombie_unarmed4", ["obj_add(npc_coord, coins, 1, ^lootdrop_duration);"], "Level 13")
    b = result("zombie_armed3", ["obj_add(npc_coord, bronze_axe, 1, ^lootdrop_duration);"], "Level 24")
    groups = wd.partition_by_table([a, b])
    check(len(groups) == 2,
          "two npcs with different tables must land in two groups, got %d" % len(groups))

    # ...and the converse, or every page would split into one label per npc.
    same = ["obj_add(npc_coord, coins, 1, ^lootdrop_duration);"]
    groups = wd.partition_by_table([result("a", list(same), "Level 1"),
                                    result("b", list(same), "Level 2")])
    check(len(groups) == 1,
          "two npcs whose tables are identical should share one group, got %d" % len(groups))
    if len(groups) == 1:
        check([r["gameval"] for r in groups[0]["results"]] == ["a", "b"],
              "group membership should keep insertion order (stable output)")


def test_every_binding_resolves_to_a_label() -> None:
    """A binding pointing at a label the file does not define is a compile
    error; a binding pointing at the *wrong* label is silent. Both are caught
    by reading them back out of the generated files."""
    files = sorted(glob.glob(os.path.join(TABLES, "wiki_*.rs2")))
    check(len(files) > 0, "no generated wiki_*.rs2 tables found")
    multi = 0
    for path in files:
        text = open(path, encoding="latin-1").read()
        labels = set(re.findall(r"^\[label,([A-Za-z0-9_]+)\]", text, re.M))
        binds = re.findall(r"^\[ai_queue3,_?([A-Za-z0-9_]+)\]\s*\n@([A-Za-z0-9_]+);",
                           text, re.M)
        if len(labels) > 1:
            multi += 1
        check(len(labels) == len(set(labels)),
              "%s defines a duplicate [label,...]" % os.path.basename(path))
        for gameval, label in binds:
            check(label in labels,
                  "%s binds %s to %s, which it never defines"
                  % (os.path.basename(path), gameval, label))
    # The 11 known multi-version pages. Stated as a floor rather than an
    # equality so adding a page cannot fail the suite for the wrong reason --
    # but a drop to zero means write_group went back to one label per page.
    check(multi >= 11,
          "expected at least 11 multi-table pages (Goblin, Zombie, Dungeon rat, "
          "TzHaar-Ket, ...), found %d -- has write_group stopped splitting?" % multi)


def test_known_version_splits() -> None:
    """Three npcs whose correct table is a matter of record, each from a page
    whose versions were previously collapsed into one."""
    cases = [
        # (file, npc, an item its OWN version drops, an item the OTHER's does)
        ("wiki_dungeon_rat.rs2", "dungeon_rat", "raw_rat_meat", "rats_tail"),
        ("wiki_dungeon_rat.rs2", "dungeon_rat2", "rats_tail", "raw_rat_meat"),
    ]
    for filename, gameval, mine, theirs in cases:
        path = os.path.join(TABLES, filename)
        if not os.path.exists(path):
            failures.append("%s missing" % filename)
            continue
        text = open(path, encoding="latin-1").read()
        m = re.search(r"^\[ai_queue3,_?%s\]\s*\n@([A-Za-z0-9_]+);" % re.escape(gameval),
                      text, re.M)
        if not m:
            failures.append("%s has no binding for %s" % (filename, gameval))
            continue
        label = m.group(1)
        body = re.split(r"^\[label,", text, flags=re.M)
        chunk = next((c for c in body if c.startswith(label + "]")), None)
        if chunk is None:
            failures.append("%s: no body for label %s" % (filename, label))
            continue
        check(mine in chunk,
              "%s: %s should drop %s (its own version)" % (filename, gameval, mine))
        check(theirs not in chunk,
              "%s: %s must NOT drop %s -- that is the other version's table"
              % (filename, gameval, theirs))


def test_death_drop_is_always_stated() -> None:
    """Every roster block states the param. An unstated one silently inherits
    `[default] param=death_drop,bones`, which is the whole bug family."""
    text = open(NPC_CONFIG, encoding="latin-1").read()
    blocks = re.split(r"^\[([A-Za-z0-9_]+)\]$", text, flags=re.M)[1:]
    missing = []
    values: dict[str, int] = {}
    for i in range(0, len(blocks), 2):
        name, body = blocks[i], blocks[i + 1]
        m = re.search(r"^param=death_drop,(\S+)$", body, re.M)
        if not m:
            missing.append(name)
        else:
            values[m.group(1)] = values.get(m.group(1), 0) + 1
    check(not missing,
          "%d npc block(s) do not state param=death_drop and will inherit bones: %s"
          % (len(missing), ", ".join(missing[:6])))
    # The families that were wrong. Each must be represented, or the derivation
    # has silently stopped seeing that half of the corpus.
    for family in ("null", "bones", "big_bones", "dragon_bones", "wolf_bones",
                   "zogre_bones", "vile_ashes", "ashes", "malicious_ashes"):
        check(values.get(family, 0) > 0,
              "no npc states param=death_drop,%s -- derivation regressed?" % family)


def test_no_table_repeats_the_death_drop() -> None:
    """The table and the param must not both emit an npc's remains. This is
    the double-drop half: 128 npcs left their variant *and* plain bones."""
    dd: dict[str, str] = {}
    current = None
    for line in open(NPC_CONFIG, encoding="latin-1"):
        s = line.strip()
        m = re.match(r"^\[([A-Za-z0-9_]+)\]$", s)
        if m:
            current = m.group(1)
            continue
        m = re.match(r"^param=death_drop,(\S+)$", s)
        if m and current:
            dd[current] = m.group(1)

    for path in sorted(glob.glob(os.path.join(TABLES, "wiki_*.rs2"))):
        text = open(path, encoding="latin-1").read()
        for chunk in re.split(r"^\[label,", text, flags=re.M)[1:]:
            label = chunk.split("]", 1)[0]
            # Unconditional (column-0) obj_adds are the `Always` drops.
            always = re.findall(r"^obj_add\(npc_coord, ([a-z_0-9]+),", chunk, re.M)
            bound = re.findall(r"^\[ai_queue3,_?([A-Za-z0-9_]+)\]\s*\n@%s;"
                               % re.escape(label), text, re.M)
            for gameval in bound:
                if dd.get(gameval) in always:
                    failures.append(
                        "%s: %s has param=death_drop,%s and its table also drops it"
                        % (os.path.basename(path), gameval, dd[gameval]))


def main() -> int:
    for fn in (test_partition_splits_differing_tables,
               test_every_binding_resolves_to_a_label,
               test_known_version_splits,
               test_death_drop_is_always_stated,
               test_no_table_repeats_the_death_drop):
        fn()
    if failures:
        print("FAIL (%d)" % len(failures))
        for f in failures:
            print("  " + f)
        return 1
    print("droptable versions: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
