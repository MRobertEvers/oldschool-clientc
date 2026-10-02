#!/usr/bin/env python3
"""Fixture test: a two-op loc credits a travel step only for its travel op.

    python3 tools/quest_gate/helper_coverage_two_op_test.py

Seam35 `two_op_loc_credits_the_wrong_op`. The Lost Tribe's H.A.M. trapdoor
(`ham_multi_trapdoor`) is a multiloc: closed it offers Open / Pick-Lock
(`osf_trapdoor_closed`, whose `[oploc5]` only sets %ham_thief), open it offers
Climb-down / Close (`osf_trapdoor_open`, whose `[oploc1]` climbs;
losttribe_ham.rs2:46-58). The guide's `enterHamLair` ("Enter the H.A.M lair")
was credited to a Pick-Lock press while the test then ::goto'd into the lair
(sampler revert c32508b14).

The fixture is that accepted-then-reverted run: the Lua at 65ce7e0e1 (read
from git) and its published ledger (OSRS-Content
selftest/quests/quest_losttribe/play/ledger.tsv at d2b3188d1e, also read from
git: later runs republish that file), graded against the real guide and
content. Each case edits the pair and checks enterHamLair:

  picklock_then_goto   as accepted: Pick-Lock, then goto_tile into the
                       lair                                      -> CHEAT
  climb_down           the goto replaced by a Climb-down press on the open
                       state, a row named after the step          -> DRIVEN
  picklock_moved       the Pick-Lock row's detail shows the player put
                       across the loc (a teleport readout)        -> DRIVEN

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

ACCEPTED = "65ce7e0e1"
LEDGER_COMMIT = "d2b3188d1e"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_losttribe/play/ledger.tsv"
GOTO_LINE = 't.exec("goto-enterHamLair", t.player.goto_tile, 3152, 9644, 0)'
CLIMB_LINE = 't.exec("enterHamLair", t.player.click_loc, "osf_trapdoor_open", 1)'
JUMP = "teleport: 3166,3253,0 -> 3152,9644,0 (a jump no walk makes, held 2 tick(s)) -- "


def accepted():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          ACCEPTED + ":test/quests/losttribe.lua"],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             LEDGER_COMMIT + ":" + LEDGER],
                            capture_output=True, text=True, check=True).stdout
    assert GOTO_LINE in lua, "the accepted Lua no longer carries %r" % GOTO_LINE
    assert "\tham.picklock\tPASS\t" in ledger and "\tgoto-enterHamLair\tPASS\t" in ledger
    return lua, ledger


def edit_row(ledger, step, new_step=None, detail=None, prefix=None):
    out = []
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] == step:
            if new_step:
                fields[1] = new_step
            if detail is not None:
                fields[5] = detail
            if prefix:
                fields[5] = prefix + fields[5]
            line = "\t".join(fields)
        out.append(line)
    return "\n".join(out)


def cases():
    lua, ledger = accepted()
    return {
        "picklock_then_goto": (lua, ledger, "CHEAT"),
        "climb_down": (lua.replace(GOTO_LINE, CLIMB_LINE),
                       edit_row(ledger, "goto-enterHamLair", new_step="enterHamLair",
                                detail=JUMP + "click_loc osf_trapdoor_open -> ham_multi_trapdoor (base)"),
                       "DRIVEN"),
        "picklock_moved": (lua, edit_row(ledger, "ham.picklock", prefix=JUMP), "DRIVEN"),
    }


def grade_case(lua, ledger_text):
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        original = helper_coverage.ledger_path
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            grader = helper_coverage.Grader("losttribe", test_path=os.path.join(scratch, "losttribe.lua"),
                                            test_text=lua)
            report = grader.report()
        finally:
            helper_coverage.ledger_path = original
    step = next(s for s in report["steps"] if s["step"] == "enterHamLair")
    return step["class"], step["reason"]


def main():
    failures = 0
    table = cases()
    for name, (lua, ledger_text, want) in table.items():
        got, reason = grade_case(lua, ledger_text)
        ok = got == want
        failures += not ok
        print("%-4s %-20s want=%-10s got=%-10s %s" % ("ok" if ok else "FAIL", name, want, got, reason[:160]))
    print("%d/%d" % (len(table) - failures, len(table)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
