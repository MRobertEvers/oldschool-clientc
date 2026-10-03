#!/usr/bin/env python3
"""Fixture test: a goto from the surface into the cave under the entrance the
guide names is a cheat on every visit, not only the first.

    python3 tools/quest_gate/helper_coverage_frame_entry_test.py

The Eyes of Glouphrie (matthew-mbp-m4-b55 round 3, sampler revert 65805d323).
The guide is `new ConditionalStep(this, enterCave)` with `addStep(inCave, ...)`
at almost every stage. The test clicked eyeglo_brimstails_cave_entrance twice,
then came back into Brimstail's cave three times by goto_tile (ledger rows 76,
223, 260) and helper_coverage read FULL 23/23: enterCave already had a PASS
row. `frame_entries` reads the goto row's departure stamp.

The fixture is that accepted-then-reverted run: the Lua at 9813e5044 and its
published ledger (OSRS-Content selftest/quests/quest_theeyesofglouphrie/play/
ledger.tsv at 30d5622731), both read from git, graded against the real guide
and content. Each case edits the ledger and checks `enterCave`:

  goto_from_surface   as accepted: three gotos into the cave stamped with a
                      surface departure                          -> CHEAT
  departed_in_cave    the same three gotos stamped as starting inside the
                      cave (a hop within it)                     -> DRIVEN
  one_left            two of the three fixed, row 260 still from the Grand
                      Tree                                       -> CHEAT

Writes only temporary files. Exit 0 when every case holds.
"""

import os
import sys
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

ACCEPTED = "9813e5044"
LEDGER_COMMIT = "30d5622731"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_theeyesofglouphrie/play/ledger.tsv"
STEP = "enterCave"
INTO_CAVE = ("goto-repairMachine", "goto-killCreature1", "goto-allDead")
IN_CAVE = "2400,9820,0"


def accepted():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          ACCEPTED + ":test/quests/theeyesofglouphrie.lua"],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             LEDGER_COMMIT + ":" + LEDGER],
                            capture_output=True, text=True, check=True).stdout
    for step in INTO_CAVE:
        assert ("\t%s\tPASS\t" % step) in ledger, "the fixture ledger no longer carries %r" % step
    return lua, ledger


def depart_in_cave(ledger, steps):
    out = []
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] in steps:
            landing = fields[5].split(" from ")[0]
            assert landing.startswith("at "), fields[5]
            fields[5] = "%s from %s" % (landing, IN_CAVE)
            line = "\t".join(fields)
        out.append(line)
    return "\n".join(out)


def cases():
    lua, ledger = accepted()
    return {
        "goto_from_surface": (lua, ledger, "CHEAT"),
        "departed_in_cave": (lua, depart_in_cave(ledger, INTO_CAVE), "DRIVEN"),
        "one_left": (lua, depart_in_cave(ledger, INTO_CAVE[:2]), "CHEAT"),
    }


def grade_case(lua, ledger_text):
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        original = helper_coverage.ledger_path
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            grader = helper_coverage.Grader("theeyesofglouphrie",
                                            test_path=os.path.join(scratch, "theeyesofglouphrie.lua"),
                                            test_text=lua)
            report = grader.report()
        finally:
            helper_coverage.ledger_path = original
    step = next(s for s in report["steps"] if s["step"] == STEP)
    return step["class"], step["reason"]


def main():
    failures = 0
    table = cases()
    for name, (lua, ledger_text, want) in table.items():
        got, reason = grade_case(lua, ledger_text)
        ok = got == want
        failures += not ok
        print("%-4s %-18s want=%-7s got=%-7s %s" % ("ok" if ok else "FAIL", name, want, got, reason[:150]))
    print("%d/%d" % (len(table) - failures, len(table)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
