#!/usr/bin/env python3
"""Fixture test: a goto into a room the map walls in, past its closed door, is a
cheat even when the guide never names the door.

    python3 tools/quest_gate/helper_coverage_enclosure_entry_test.py

Tale of the Righteous (matthew-mbp-m4-b56, sampler revert of 8f3c5b544). The
guide's returnToPhileasTent (stage 15 -> 16) is an arrival in Phileas's house
and talkToPhileasAgain a talk inside it; the test reached both by goto_tile
1542,3570,0 (ledger rows 93 and 36), past wallkit_shayzien_door01_l_reverse at
1540,3570 (maps/m24_55.jl2), and helper_coverage read FULL 30/30: the guide has
no ObjectStep on that door, so door_entries had nothing to key on.
`enclosure_entries` reads the map's walls (MapWalls) instead.

The fixture is that accepted-then-reverted run: the Lua at 8f3c5b544 and its
published ledger (OSRS-Content selftest/quests/quest_taleoftherighteous/play/
ledger.tsv at b6c144c108), both read from git, graded against the real guide,
content and map. Each case edits the ledger and checks the two steps:

  as_accepted        both gotos as run: from the prison (another map frame)
                     and from Shiro's room (level 1, another building)
                                                     -> CHEAT, CHEAT
  departed_inside    both gotos stamped as starting inside the house
                                                     -> DRIVEN, DRIVEN
  upstairs_same_house row 93 stamped from the floor above the house (its own
                     ladder): another floor, the climb rules' business
                                                     -> CHEAT, DRIVEN
  door_pressed       a PASS row presses the door copy just before row 93
                                                     -> CHEAT, DRIVEN
  door_pressed_long_ago the same press, 600 ticks before row 93 (the door
                     has shut again: doors.rs2 loc_add(..., 500))
                                                     -> CHEAT, CHEAT
  outside_same_level row 36 stamped from the street west of the door
                                                     -> CHEAT, CHEAT

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

ACCEPTED = "8f3c5b544"
LEDGER_COMMIT = "b6c144c108"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_taleoftherighteous/play/ledger.tsv"
TEST_ID = "taleoftherighteous"
STEPS = ("talkToPhileasAgain", "returnToPhileasTent")
AGAIN, RETURN = "goto-talkToPhileasAgain", "returnToPhileasTent"
DOOR = "wallkit_shayzien_door01_l_reverse"
INSIDE = "1543,3571,0"


def accepted():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          ACCEPTED + ":test/quests/%s.lua" % TEST_ID],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             LEDGER_COMMIT + ":" + LEDGER],
                            capture_output=True, text=True, check=True).stdout
    for step in (AGAIN, RETURN):
        assert ("\t%s\tPASS\t" % step) in ledger, "the fixture ledger no longer carries %r" % step
    return lua, ledger


def edit(ledger, step, column, change):
    """`ledger` with `change(old)` written into `column` of row `step`."""
    out = []
    found = False
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] == step:
            fields[column] = change(fields[column])
            found = True
            line = "\t".join(fields)
        out.append(line)
    assert found, step
    return "\n".join(out)


def depart(ledger, step, tile):
    def change(detail):
        landing = detail.split(" from ")[0]
        assert landing.startswith("at "), detail
        return "%s from %s" % (landing, tile)
    return edit(ledger, step, 5, change)


def press_door(ledger, step):
    return edit(ledger, step, 5, lambda detail: "%s ## click_loc %s: pressed the copy at 1540,3570,0" % (
        detail, DOOR))


def cases():
    lua, ledger = accepted()
    rows = [line.split("\t") for line in ledger.split("\n")]
    names = [fields[1] for fields in rows if len(fields) >= 6]
    before_return = names[names.index(RETURN) - 1]       # quest.stage.house
    two_before = names[names.index(RETURN) - 2]          # returnToShiro-dialog
    long_ago = edit(press_door(ledger, two_before), before_return, 3, lambda _: "600")
    return {
        "as_accepted": (lua, ledger, ("CHEAT", "CHEAT")),
        "departed_inside": (lua, depart(depart(ledger, AGAIN, INSIDE), RETURN, INSIDE), ("DRIVEN", "DRIVEN")),
        "upstairs_same_house": (lua, depart(ledger, RETURN, "1543,3571,1"), ("CHEAT", "DRIVEN")),
        "door_pressed": (lua, press_door(ledger, before_return), ("CHEAT", "DRIVEN")),
        "door_pressed_long_ago": (lua, long_ago, ("CHEAT", "CHEAT")),
        "outside_same_level": (lua, depart(ledger, AGAIN, "1535,3570,0"), ("CHEAT", "CHEAT")),
    }


def grade_case(lua, ledger_text):
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        original = helper_coverage.ledger_path
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            grader = helper_coverage.Grader(TEST_ID, test_path=os.path.join(scratch, TEST_ID + ".lua"),
                                            test_text=lua)
            report = grader.report()
        finally:
            helper_coverage.ledger_path = original
    by_name = {s["step"]: s for s in report["steps"]}
    return tuple(by_name[name]["class"] for name in STEPS), by_name[STEPS[1]]["reason"]


def main():
    failures = 0
    table = cases()
    for name, (lua, ledger_text, want) in table.items():
        got, reason = grade_case(lua, ledger_text)
        ok = got == want
        failures += not ok
        print("%-4s %-22s want=%-15s got=%-15s %s" % ("ok" if ok else "FAIL", name, "/".join(want), "/".join(got),
                                                     reason[:110]))
    print("%d/%d" % (len(table) - failures, len(table)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
