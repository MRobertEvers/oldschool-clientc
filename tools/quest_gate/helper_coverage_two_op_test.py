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

A second fixture, seam `npc_step_credited_by_a_row_that_only_names_the_npc`
(batch matthew-mbp-m4-b62): an NpcStep's noun match ("retrieve it from
Harold" against a row whose name holds `harold`) counts only a row whose own
action reached that npc. Death Plateau's optional `talkToHarold3` ("If you
lost the Combination, retrieve it from Harold") read DRIVEN through ledger
row 17 `goToHaroldStairs1.castleDoorOut`, a pass_door on the castle door.
The fixture is the b62 green run: test/quests/death.lua at f7728f806 and
its published ledger (OSRS-Content play/ledger.tsv at ba3189266c), both read
from git, checking talkToHarold3:

  death_as_committed   the run as committed: no row talks to Harold a third
                       time; the sub-step's parent is shown in its place
                                                               -> ALTERNATIVE
  death_row_reaches    row 17's detail reports an accepted press on
                       death_guard_equiproom (a row that did reach him)
                                                               -> DRIVEN

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

DEATH_LUA_COMMIT = "f7728f806"
DEATH_LEDGER_COMMIT = "ba3189266c"
DEATH_LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_death/play/ledger.tsv"
DEATH_DOOR_ROW = "goToHaroldStairs1.castleDoorOut"
HAROLD_PRESS = "talk_to death_guard_equiproom op1 -> ok map_flag; dialogue npc is up"


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


def death_accepted():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          DEATH_LUA_COMMIT + ":test/quests/death.lua"],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             DEATH_LEDGER_COMMIT + ":" + DEATH_LEDGER],
                            capture_output=True, text=True, check=True).stdout
    door = [line.split("\t") for line in ledger.split("\n") if line.split("\t")[1:2] == [DEATH_DOOR_ROW]]
    assert len(door) == 1 and door[0][2] == "PASS" and door[0][5].startswith("pass_door castledoubledoorl"), \
        "the b62 death ledger no longer carries row %r as a pass_door" % DEATH_DOOR_ROW
    assert "talkToHarold3" not in ledger, "the b62 death ledger now has a talkToHarold3 row"
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
    death_lua, death_ledger = death_accepted()
    return {
        "picklock_then_goto": ("losttribe", "enterHamLair", lua, ledger, "CHEAT", None),
        "climb_down": ("losttribe", "enterHamLair", lua.replace(GOTO_LINE, CLIMB_LINE),
                       edit_row(ledger, "goto-enterHamLair", new_step="enterHamLair",
                                detail=JUMP + "click_loc osf_trapdoor_open -> ham_multi_trapdoor (base)"),
                       "DRIVEN", None),
        "picklock_moved": ("losttribe", "enterHamLair", lua, edit_row(ledger, "ham.picklock", prefix=JUMP),
                           "DRIVEN", None),
        "death_as_committed": ("death", "talkToHarold3", death_lua, death_ledger, "ALTERNATIVE",
                               "%r names harold, but its own action does not reach that npc" % DEATH_DOOR_ROW),
        "death_row_reaches": ("death", "talkToHarold3", death_lua,
                              edit_row(death_ledger, DEATH_DOOR_ROW, detail=HAROLD_PRESS), "DRIVEN",
                              "its detail names 'death_guard_equiproom'"),
    }


def grade_case(test_id, step_name, lua, ledger_text):
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        original = helper_coverage.ledger_path
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            grader = helper_coverage.Grader(test_id, test_path=os.path.join(scratch, test_id + ".lua"),
                                            test_text=lua)
            report = grader.report()
        finally:
            helper_coverage.ledger_path = original
    step = next(s for s in report["steps"] if s["step"] == step_name)
    return step["class"], step["reason"]


def main():
    failures = 0
    table = cases()
    for name, (test_id, step_name, lua, ledger_text, want, says) in table.items():
        got, reason = grade_case(test_id, step_name, lua, ledger_text)
        ok = got == want and (says is None or says in reason)
        failures += not ok
        print("%-4s %-20s want=%-10s got=%-10s %s" % ("ok" if ok else "FAIL", name, want, got, reason[:160]))
    print("%d/%d" % (len(table) - failures, len(table)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
