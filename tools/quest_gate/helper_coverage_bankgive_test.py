#!/usr/bin/env python3
"""Fixture test: a setup `::bankgive <obj> <n>` is graded exactly like a setup
`::give <obj> <n>` of the same item -- same step, same verdict.

    python3 tools/quest_gate/helper_coverage_bankgive_test.py
    python3 tools/quest_gate/helper_coverage_bankgive_test.py --rule-off

`::bankgive` (cheat_bank.rs2) stocks the BANK, and a banked item is one
t.bank.withdraw away from the pack, so staging an item the guide has the player
OBTAIN that way is the same CHEAT as `::give`-ing it. Before GIVE_CHEATS,
`_cheat_effects` reached ::bankgive through its debugproc branch, whose
`inv_add\\(\\s*\\w+\\s*,\\s*(\\w+)` read no item out of
`inv_add(bank, $stored, $banked)` (a local, not a symbol), so the cheat was
dropped as a reset adapter and the step it skipped graded UNMATCHED instead of
CHEAT (seam pass matthew-mbp-m4-b56-seam2's report).

The fixture is a tiny synthetic Cook's Assistant: the real QUEUE row, guide
(CooksAssistant.java) and content, a Lua whose setup is the only thing that
varies, and a one-row ledger. Nothing in run() drives a gathering step, so each
one is graded on the setup cheat alone:

  none           setup has no item cheat           getEgg/milkCow not CHEAT
  give_egg       setup "::give egg 1"              getEgg CHEAT
  bankgive_egg   setup "::bankgive egg 1"          getEgg CHEAT, the give_egg
                                                   verdict for every step
  give_milk      setup "::give bucket_milk 1"      milkCow CHEAT
  bankgive_milk  setup "::bankgive bucket_milk 1"  milkCow CHEAT, the give_milk
                                                   verdict for every step

`--rule-off` grades with GIVE_CHEATS = ("give",) -- the tool as it was -- in
this process only (no source is edited): the two bankgive cases must FAIL
there, which is what proves the cases test the rule. Writes only temporary
files. Exit 0 when every case holds.
"""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

TEST_ID = "cooks_assistant"
STEPS = ("getEgg", "milkCow")

LUA = """return {
    id = "cooks_assistant",
    setup = { "::clearinv", %s },
    run = function(t)
        t.quest.bind{ row = "quest_cook", varp = "varp29_cookquest" }
        t.check("cooksassistant.reset", true, "the one row: no step is driven")
    end,
}
"""

LEDGER = ("quest-ledger-v1\n"
          "index\tstep\tverdict\tticks\tshots\tdetail\n"
          "1\tcooksassistant.reset\tPASS\t1\t\tthe one row: no step is driven\n")


def grade(setup_cheat):
    lua = LUA % (('"%s"' % setup_cheat) if setup_cheat else '"::cook"')
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(LEDGER)
        grader = helper_coverage.Grader(TEST_ID, test_path=os.path.join(scratch, TEST_ID + ".lua"),
                                        test_text=lua, ledger_file=ledger)
        report = grader.report()
    return {s["step"]: (s["class"], s["reason"]) for s in report["steps"]}


def main():
    if "--rule-off" in sys.argv[1:]:
        helper_coverage.GIVE_CHEATS = ("give",)
    graded = {name: grade(cheat) for name, cheat in (
        ("none", None),
        ("give_egg", "::give egg 1"),
        ("bankgive_egg", "::bankgive egg 1"),
        ("give_milk", "::give bucket_milk 1"),
        ("bankgive_milk", "::bankgive bucket_milk 1"),
    )}
    for name in graded:
        for step in STEPS:
            assert step in graded[name], "the guide no longer has %s" % step

    def classes(name):
        return {step: klass for step, (klass, _) in graded[name].items()}

    checks = [
        ("none", "getEgg", lambda: graded["none"]["getEgg"][0] != "CHEAT"),
        ("none", "milkCow", lambda: graded["none"]["milkCow"][0] != "CHEAT"),
        ("give_egg", "getEgg", lambda: graded["give_egg"]["getEgg"][0] == "CHEAT"),
        ("bankgive_egg", "getEgg", lambda: graded["bankgive_egg"]["getEgg"][0] == "CHEAT"),
        ("bankgive_egg", "== give_egg", lambda: classes("bankgive_egg") == classes("give_egg")),
        ("give_milk", "milkCow", lambda: graded["give_milk"]["milkCow"][0] == "CHEAT"),
        ("bankgive_milk", "milkCow", lambda: graded["bankgive_milk"]["milkCow"][0] == "CHEAT"),
        ("bankgive_milk", "== give_milk", lambda: classes("bankgive_milk") == classes("give_milk")),
    ]
    failures = 0
    for name, what, check in checks:
        ok = check()
        failures += not ok
        step = what if what in STEPS else STEPS[0] if "egg" in name else STEPS[1]
        klass, reason = graded[name][step]
        print("%-4s %-14s %-13s got=%-11s %s" % ("ok" if ok else "FAIL", name, what, klass, reason[:90]))
    print("%d/%d%s" % (len(checks) - failures, len(checks),
                       " (GIVE_CHEATS switched off: the bankgive rows must FAIL)"
                       if "--rule-off" in sys.argv[1:] else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
