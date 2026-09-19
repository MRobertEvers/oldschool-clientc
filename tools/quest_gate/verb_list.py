#!/usr/bin/env python3
"""Enumerate the verbs the quest driver exposes on `t`, from the Lua sources.

The conformance harness (test/quests/_conformance.lua) must have exactly one
ledger row per verb.  Nothing keeps those two in step except this: the driver's
own files are the source of truth, the harness declares the same names in a
machine-readable block, and `--check` refuses to agree when they differ.  A verb
added to script/plugins/quest_driver/*.lua with no row in the harness is a verb
nobody ever calls, which is the exact failure the previous swarm shipped.

A "verb" is a function reachable from the `t` table a quest test is resumed
with (QD_ROOT, quest_driver/core.lua):

  function QD.<ns>.<verb>(...)   -> "<ns>.<verb>"
  function QD.<verb>(...)        -> "<verb>"          (top level)
  QD.<verb> = <expression>       -> "<verb>"          (ok/fail/await/skill)

Excluded, because they are not a test's verbs:
  - any name whose last segment starts with `_` (a part's private helper, and
    the whole QD.read namespace, which is only helpers),
  - QD.core_* (the plugin/scheduler seam: core_bind is called from on_start
    with an `api` a test never has, core_next_shot is t.shot's own bookkeeping),
  - a namespace table constructor (`QD.read = {}`).
"""

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DRIVER_DIR = os.path.join(REPO_ROOT, "script", "plugins", "quest_driver")
HARNESS = os.path.join(REPO_ROOT, "test", "quests", "_conformance.lua")

FUNC_NS = re.compile(r"^function\s+QD\.([A-Za-z][A-Za-z0-9]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\(")
FUNC_TOP = re.compile(r"^function\s+QD\.([A-Za-z_][A-Za-z0-9_]*)\s*\(")
ASSIGN_TOP = re.compile(r"^QD\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")
HARNESS_ROW = re.compile(r'^\s*step\("([^"]+)"')
HARNESS_COUNT = re.compile(r"^--\s*@verb-count\s+(\d+)\s*$")


def private(segment):
    return segment.startswith("_") or segment.startswith("core_")


def verbs_from_sources(driver_dir=DRIVER_DIR):
    """Every verb name, with the file:line it is defined at."""
    found = {}
    names = sorted(f for f in os.listdir(driver_dir) if f.endswith(".lua"))
    assert names, driver_dir
    for name in names:
        path = os.path.join(driver_dir, name)
        with open(path, "r", encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                where = "script/plugins/quest_driver/%s:%d" % (name, number)
                match = FUNC_NS.match(line)
                if match:
                    namespace, verb = match.group(1), match.group(2)
                    if not private(verb):
                        found["%s.%s" % (namespace, verb)] = where
                    continue
                match = FUNC_TOP.match(line)
                if match:
                    verb = match.group(1)
                    if not private(verb):
                        found[verb] = where
                    continue
                match = ASSIGN_TOP.match(line)
                if match:
                    verb, rhs = match.group(1), match.group(2).strip()
                    if private(verb) or rhs.startswith("{"):
                        continue
                    found[verb] = where
    return found


def verbs_from_harness(path=HARNESS):
    """The names the harness plans a row for, read off its own `step("<name>"`
    call sites -- the code, not a comment beside it, so the two cannot drift."""
    declared = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            match = HARNESS_ROW.match(line)
            if match:
                declared.append(match.group(1))
    return declared


def count_from_harness(path=HARNESS):
    """The count the harness asserts at runtime (`-- @verb-count N`), so the
    Lua-side assert and this gate cannot disagree either.  None when absent."""
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            match = HARNESS_COUNT.match(line.strip())
            if match:
                return int(match.group(1))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="compare the sources against the harness; non-zero on any drift")
    parser.add_argument("--count", action="store_true", help="print the verb count only")
    arguments = parser.parse_args()

    found = verbs_from_sources()
    if arguments.count:
        print(len(found))
        return 0
    if not arguments.check:
        for verb in sorted(found):
            print("%s\t%s" % (verb, found[verb]))
        return 0

    declared = verbs_from_harness()
    asserted = count_from_harness()
    duplicates = sorted(v for v in set(declared) if declared.count(v) > 1)
    missing = sorted(set(found) - set(declared))
    extra = sorted(set(declared) - set(found))
    for verb in duplicates:
        print("verb_list: %s is declared twice in test/quests/_conformance.lua" % verb,
              file=sys.stderr)
    for verb in missing:
        print("verb_list: %s (%s) has no row in test/quests/_conformance.lua"
              % (verb, found[verb]), file=sys.stderr)
    for verb in extra:
        print("verb_list: test/quests/_conformance.lua declares %s, which no driver "
              "source defines" % verb, file=sys.stderr)
    bad_count = asserted != len(found)
    if bad_count:
        print("verb_list: test/quests/_conformance.lua asserts %s verbs, the driver "
              "defines %d" % (asserted, len(found)), file=sys.stderr)
    if duplicates or missing or extra or bad_count:
        return 1
    print("verb_list: %d verbs, one conformance row each" % len(found))
    return 0


if __name__ == "__main__":
    sys.exit(main())
