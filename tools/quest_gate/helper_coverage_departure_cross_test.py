#!/usr/bin/env python3
"""Fixture test: a goto out of a place whose exit the guide names is a cheat.

    python3 tools/quest_gate/helper_coverage_departure_cross_test.py

Meat and Greet (matthew-mbp-m4-b55). The guide's sub-step
`leaveColosseumToReturnToEmelio` names `colosseum_exit_lobby`; the round-1
test left the Colosseum lobby with one goto_tile from 1819,9485 to Emelio and
helper_coverage read FULL (the sampler reverted the run). Two rules catch it
now: an unconditional trigger in another quest's file serves every quest
(`trigger_is_unconditional`; the exit's only [oploc1] is
quest_twilightspromise/scripts/twilightspromise.rs2), and the goto row's own
departure stamp is the "before" side in `teleported_across`.

The fixture is that accepted-then-reverted run: the Lua at 171bc81b0 and its
published ledger (OSRS-Content selftest/quests/quest_meatandgreet/play/
ledger.tsv at 343f1b4163), both read from git, graded against the real guide
and content. Each case edits the pair and checks the sub-step:

  goto_from_lobby    as accepted: goto_tile to Emelio, departure stamped
                     inside the lobby                             -> CHEAT
  exit_clicked       a click on colosseum_exit_lobby before the goto, the
                     goto then departs from outside               -> not CHEAT
  departed_outside   the same goto, its stamp outside the Colosseum (the
                     run was never on the sub-step's side)        -> not CHEAT

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

ACCEPTED = "171bc81b0"
LEDGER_COMMIT = "343f1b4163"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_meatandgreet/play/ledger.tsv"
STEP = "leaveColosseumToReturnToEmelio"
GOTO_LINE = 't.exec("goto-emelio.end", t.player.goto_tile, 1753, 3074, 0)'
CLICK_LINE = 't.exec("leaveColosseumToReturnToEmelio", t.player.click_loc, "colosseum_exit_lobby", 1)'
INSIDE = "at 1753,3074,0 from 1819,9485,0"
OUTSIDE = "at 1753,3074,0 from 1796,3106,0"


def accepted():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          ACCEPTED + ":test/quests/meatandgreet.lua"],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             LEDGER_COMMIT + ":" + LEDGER],
                            capture_output=True, text=True, check=True).stdout
    assert GOTO_LINE in lua, "the accepted Lua no longer carries %r" % GOTO_LINE
    assert "\tgoto-emelio.end\tPASS\t" in ledger and INSIDE in ledger
    return lua, ledger


def edit_detail(ledger, step, detail):
    out = []
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] == step:
            fields[5] = detail
            line = "\t".join(fields)
        out.append(line)
    return "\n".join(out)


def add_row_before(ledger, step, new_step, detail):
    out = []
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] == step:
            out.append("\t".join([fields[0], new_step, "PASS", "2", "", detail]))
        out.append(line)
    return "\n".join(out)


def cases():
    lua, ledger = accepted()
    clicked_lua = lua.replace(GOTO_LINE, CLICK_LINE + "\n        " + GOTO_LINE)
    clicked_ledger = add_row_before(
        edit_detail(ledger, "goto-emelio.end", OUTSIDE), "goto-emelio.end", STEP,
        "teleport: 1819,9485,0 -> 1796,3106,0 (a jump no walk makes, held 2 tick(s)) -- "
        "click_loc colosseum_exit_lobby -> colosseum_exit_lobby (base)")
    return {
        "goto_from_lobby": (lua, ledger, lambda klass: klass == "CHEAT"),
        "exit_clicked": (clicked_lua, clicked_ledger, lambda klass: klass != "CHEAT"),
        "departed_outside": (lua, edit_detail(ledger, "goto-emelio.end", OUTSIDE), lambda klass: klass != "CHEAT"),
    }


def grade_case(lua, ledger_text):
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        original = helper_coverage.ledger_path
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            grader = helper_coverage.Grader("meatandgreet", test_path=os.path.join(scratch, "meatandgreet.lua"),
                                            test_text=lua)
            report = grader.report()
        finally:
            helper_coverage.ledger_path = original
    step = next(s for s in report["steps"] if s["step"] == STEP)
    return step["class"], step["reason"]


def main():
    failures = 0
    table = cases()
    for name, (lua, ledger_text, holds) in table.items():
        got, reason = grade_case(lua, ledger_text)
        ok = holds(got)
        failures += not ok
        print("%-4s %-18s got=%-12s %s" % ("ok" if ok else "FAIL", name, got, reason[:170]))
    print("%d/%d" % (len(table) - failures, len(table)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
