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

Seam matthew-mbp-m4-b63-seam1 `helper_coverage_use_item_credit_only_gates_and_pocket_press_exemption`
adds four rules, each with a switch on helper_coverage.Grader that restores the reading before it
(the `*_off` cases run with it False and show the hole):

  gate_crossings   a goto whose two ends no walk joins with every door shut, every walk
                   on foot opening the same gate (margins 30/80/160), with no press of it
                   in the 500 ticks before (GATE_CROSSINGS). The fixture is Dwarf Cannon as
                   committed before b63 (test/quests/mcannon.lua at 69f7e7bd8, its
                   published ledger at OSRS-Content 22669be78), which read FULL: its gotos
                   hop in and out of Lawgof's railing yard (1,325 tiles, past
                   enclosure_entries' 400) and from the yard to Nulodion's workshop.
  solid_landings   a goto that lands ON a blocked tile, charged to a step of its own
                   "(goto onto <loc>)" (SOLID_LANDINGS): goto-gotoCave onto mcannoncave,
                   goto-searchCrates onto mcannoncrateboy, goto-inspectRailings3 onto a
                   diagonal railing.
  use effect       a "use X on Y" step is credited only from a PASS row its call wrote that
                   shows an effect, or a read row after it (USE_NEEDS_EFFECT): a bare
                   `local r, d = t.player.use_on(...)` and a row that only says
                   `map_flag` read UNMATCHED and name the line. Synthetic Haunted Mine
                   files (the key on the valve, as the b63 fixer's full-route copy wrote
                   it), plus that copy itself while build/orchestrator/fix_b63/ holds it.
  pocket presses   sealed_exits credits a press of a pocket's op loc only since the player
                   last arrived in the pocket and only when nothing after it shows the
                   player still inside (POCKET_PRESS_SINCE_ARRIVAL). Watchtower's committed
                   run (test/quests/itwatchtower.lua at 16ad11389, OSRS-Content c436ddab5a's
                   ledger) with its swing off Grew's island replaced by a goto: the rope
                   used on tree_ropeswing4_norope 9 rows before (the way IN) exempted it.

  case                          step(s)                                      want
  mcannon_rules_off             (every rule switched off)                    FULL
  mcannon_committed             inspectRailings1 (rail 4 out of the yard), talkToCaptainLawgof2,
                                gotoTower, gotoCave, talkToNulodion, talkToCaptainLawgof6
                                CHEAT:gate_crossings; (goto onto mcannoncave), (goto onto
                                mcannoncrateboy), (goto onto mcannon_dwarf_railing_fixed)
                                CHEAT:solid_landings                         TEST_GAP
  mcannon_gate_pressed          gotoTower with a departure stamp and the yard gate pressed
                                the row before                               DRIVEN
  mcannon_gate_not_pressed      the same stamp, no press                     CHEAT:gate_crossings
  mcannon_off_the_crate         goto-searchCrates lands on 2570,9850 beside the crate:
                                no (goto onto mcannoncrateboy) step
  hm_bare_use                   useKeyOnValve, a bare use_on                 UNMATCHED "line 4"
  hm_bare_use_off               the same, USE_NEEDS_EFFECT off               DRIVEN
  hm_row_no_effect              t.exec row `map_flag`, nothing after         UNMATCHED "shows no effect"
  hm_row_effect_after           the same row, then `useKeyOnValve.var` reads the varbit  DRIVEN
  hm_checked_answer             a bare call whose answer a t.check records, the check's
                                detail the server line                       DRIVEN
  hm_fullroute                  the fixer's copy (build/ only), its GUIDE-GAP marker line
                                removed                                      UNMATCHED "line 235"
  wt_goto_off_island            leaveGrewIsland: goto-leaveGrewIsland2 off Grew's island
                                                                             CHEAT:sealed_exits
  wt_goto_off_island_off        the same, POCKET_PRESS_SINCE_ARRIVAL off     FULL (the b62 escape)
  wt_swung_off_first            a row before the goto pressed tree_ropeswing3 and landed
                                outside the island                           FULL

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


# -- seam matthew-mbp-m4-b63-seam1

MCANNON_LUA_COMMIT = "69f7e7bd8"
MCANNON_LEDGER_COMMIT = "22669be78"
MCANNON_LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_mcannon/play/ledger.tsv"
WATCHTOWER_LUA_COMMIT = "16ad11389"
WATCHTOWER_LEDGER_COMMIT = "c436ddab5a"
WATCHTOWER_LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_itwatchtower/play/ledger.tsv"
HM_FULLROUTE = os.path.join(helper_coverage.REPO_ROOT, "build", "orchestrator", "fix_b63")
VERDICT = "(verdict)"
ABSENT = "absent"
RULES = ("GATE_CROSSINGS", "SOLID_LANDINGS", "USE_NEEDS_EFFECT", "POCKET_PRESS_SINCE_ARRIVAL")


def git_show(repo, commit, path):
    return subprocess.run(["git", "-C", repo, "show", commit + ":" + path],
                          capture_output=True, text=True, check=True).stdout


def mcannon_committed():
    lua = git_show(helper_coverage.REPO_ROOT, MCANNON_LUA_COMMIT, "test/quests/mcannon.lua")
    ledger = git_show(os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), MCANNON_LEDGER_COMMIT,
                      MCANNON_LEDGER)
    for row, detail in (("goto-gotoTower", "at 2570,3439,0"), ("goto-gotoCave", "at 2622,3392,0"),
                        ("goto-searchCrates", "at 2571,9850,0"), ("goto-talkToNulodion", "at 3011,3453,0")):
        assert ("\t%s\tPASS\t" % row) in ledger and detail in ledger, "the mcannon fixture no longer carries %r" % row
    return lua, ledger


def watchtower_goto_off_island(press_first=False):
    """The committed Watchtower run with walk-leaveGrewIsland2 dropped and
    leaveGrewIsland2's cross_trap replaced by a goto off the island, stamped
    from the tile Grew's talk left the player on."""
    lua = git_show(helper_coverage.REPO_ROOT, WATCHTOWER_LUA_COMMIT, "test/quests/itwatchtower.lua")
    ledger = git_show(os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), WATCHTOWER_LEDGER_COMMIT,
                      WATCHTOWER_LEDGER)
    assert "\tuseRopeOnBranchAgain\tPASS\t" in ledger and "landed 2505,3087,0" in ledger
    out = []
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] == "walk-leaveGrewIsland2":
            if press_first:
                fields[1] = "swingOff"
                fields[5] = ("cross_trap tree_ropeswing3 at 2511,3090,0: landed 2511,3096,0 -- click_loc "
                             "tree_ropeswing3: pressed the copy at 2511,3090,0")
                out.append("\t".join(fields))
            continue
        if len(fields) >= 6 and fields[1] == "leaveGrewIsland2":
            fields[1], fields[4], fields[5] = "goto-leaveGrewIsland2", "", "at 2511,3096,0 from 2510,3086,0"
            line = "\t".join(fields)
        out.append(line)
    return lua, "\n".join(out)


HM_HEAD = "quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n1\tquest.bind\tPASS\t1\t\tbound\n"


def hm_case(body, rows):
    """A synthetic Haunted Mine file: `body` is the run's Lua (line 1 is
    `return {`, so the use_on sits on line 4 when it is body's first
    line), `rows` the ledger rows after quest.bind."""
    lua = ('return {\n    id = "hauntedmine",\n    run = function(t)\n' + body +
           '\n        t.finish(0)\n    end,\n}\n')
    ledger = HM_HEAD + "".join("%d\t%s\tPASS\t1\t\t%s\n" % (index, step, detail)
                               for index, (step, detail) in enumerate(rows, 2))
    return lua, ledger


HM_VALVE = '        local valve = t.player.by_symbol("loc", "hauntedmine_lift_valve")\n'


def seam_cases():
    lua, ledger = mcannon_committed()
    gate_row = "quest.stage.tasked_with_checking_guard_tower"
    stamped = edit_row(ledger, "goto-gotoTower", detail="at 2570,3439,0 from 2567,3460,0")
    yard = {"inspectRailings1": "CHEAT:gate_crossings", "talkToCaptainLawgof2": "CHEAT:gate_crossings",
            "gotoTower": "CHEAT:gate_crossings", "gotoCave": "CHEAT:gate_crossings",
            "talkToNulodion": "CHEAT:gate_crossings", "talkToCaptainLawgof6": "CHEAT:gate_crossings",
            "(goto onto mcannoncave)": "CHEAT:solid_landings",
            "(goto onto mcannoncrateboy)": "CHEAT:solid_landings",
            "(goto onto mcannon_dwarf_railing_fixed)": "CHEAT:solid_landings", VERDICT: "TEST_GAP"}
    table = {
        "mcannon_rules_off": ("mcannon", lua, ledger, {VERDICT: "FULL"}, RULES),
        "mcannon_committed": ("mcannon", lua, ledger, yard, ()),
        "mcannon_gate_pressed": ("mcannon", lua,
                                 edit_row(stamped, gate_row, prefix="click_loc mcannon_dwarf_railing_gate: pressed "
                                          "the copy at 2567,3456,0 -- "),
                                 {"gotoTower": "DRIVEN"}, ()),
        "mcannon_gate_not_pressed": ("mcannon", lua, stamped, {"gotoTower": "CHEAT:gate_crossings"}, ()),
        "mcannon_off_the_crate": ("mcannon", lua, edit_row(ledger, "goto-searchCrates", detail="at 2570,9850,0"),
                                  {"(goto onto mcannoncrateboy)": ABSENT, "searchCrates": "DRIVEN"}, ()),
    }
    bare = hm_case(HM_VALVE.replace("        local valve", "        local ur, ud = t.player.use_on("
                                    "\"hauntedmine_lift_key\", t.player.by_symbol(\"loc\", \"hauntedmine_lift_valve\"))"
                                    "\n        local valve") +
                   '        t.note("useKeyOnValve attempt: " .. tostring(ur) .. " " .. tostring(ud))',
                   [("useKeyOnValve-note", "none")])
    table["hm_bare_use"] = ("hauntedmine", bare[0], bare[1],
                            {"useKeyOnValve": ("UNMATCHED", "line 4 is a bare use_on")}, ())
    table["hm_bare_use_off"] = ("hauntedmine", bare[0], bare[1], {"useKeyOnValve": "DRIVEN"}, ("USE_NEEDS_EFFECT",))
    row_exec = HM_VALVE + '        t.exec("useKeyOnValve", t.player.use_on, "hauntedmine_lift_key", valve)'
    no_effect = hm_case(row_exec, [("useKeyOnValve", "map_flag")])
    table["hm_row_no_effect"] = ("hauntedmine", no_effect[0], no_effect[1],
                                 {"useKeyOnValve": ("UNMATCHED", "shows no effect")}, ())
    effect = hm_case(row_exec + '\n        t.exec("useKeyOnValve.var", t.var.await_server, '
                     '"varb2394_hauntedmine_liftpowerednow", 1, 4)',
                     [("useKeyOnValve", "map_flag"),
                      ("useKeyOnValve.var", "varb2394_hauntedmine_liftpowerednow = 1 (varbit) after 1 tick(s)")])
    table["hm_row_effect_after"] = ("hauntedmine", effect[0], effect[1], {"useKeyOnValve": "DRIVEN"}, ())
    checked = hm_case(HM_VALVE + '        local ur, ud = t.player.use_on("hauntedmine_lift_key", valve)\n'
                      '        t.check("useKeyOnValve", ur == "ok", "use_on -> " .. tostring(ur) .. " " .. tostring(ud))',
                      [("useKeyOnValve", "use_on -> ok chat_message: You unlock the valve with the Zealot's key")])
    table["hm_checked_answer"] = ("hauntedmine", checked[0], checked[1], {"useKeyOnValve": "DRIVEN"}, ())
    full_lua = os.path.join(HM_FULLROUTE, "hauntedmine.fullroute.lua")
    full_ledger = os.path.join(HM_FULLROUTE, "hauntedmine.fullroute.run21.ledger.tsv")
    if os.path.exists(full_lua) and os.path.exists(full_ledger):
        text = "\n".join(line for line in open(full_lua, encoding="utf-8").read().split("\n")
                         if "GUIDE-GAP: useKeyOnValve" not in line)
        table["hm_fullroute"] = ("hauntedmine", text, open(full_ledger, encoding="utf-8").read(),
                                 {"useKeyOnValve": ("UNMATCHED", "line 235 is a bare use_on")}, ())
    else:
        print("skip hm_fullroute: %s is gone (build/ output, not a git fixture)" % HM_FULLROUTE)
    wt_lua, wt_ledger = watchtower_goto_off_island()
    table["wt_goto_off_island"] = ("itwatchtower", wt_lua, wt_ledger, {"leaveGrewIsland": "CHEAT:sealed_exits"}, ())
    table["wt_goto_off_island_off"] = ("itwatchtower", wt_lua, wt_ledger, {VERDICT: "FULL"},
                                       ("POCKET_PRESS_SINCE_ARRIVAL",))
    first_lua, first_ledger = watchtower_goto_off_island(press_first=True)
    table["wt_swung_off_first"] = ("itwatchtower", first_lua, first_ledger, {VERDICT: "FULL"}, ())
    return table


def grade_report(test_id, lua, ledger_text, off=()):
    saved = {name: getattr(helper_coverage.Grader, name) for name in RULES}
    for name in off:
        setattr(helper_coverage.Grader, name, False)
    try:
        with tempfile.TemporaryDirectory() as scratch:
            ledger = os.path.join(scratch, "ledger.tsv")
            with open(ledger, "w", encoding="utf-8") as handle:
                handle.write(ledger_text)
            grader = helper_coverage.Grader(test_id, test_path=os.path.join(scratch, test_id + ".lua"),
                                            test_text=lua, ledger_file=ledger)
            return grader.report()
    finally:
        for name, value in saved.items():
            setattr(helper_coverage.Grader, name, value)


def check_seam_case(report, wants):
    """[(step, want, got, reason)] for each want that does not hold."""
    steps = {s["step"]: s for s in report["steps"]}
    bad = []
    for step, want in wants.items():
        if step == VERDICT:
            if report["verdict"] != want:
                bad.append((step, want, report["verdict"], ""))
            continue
        got = steps.get(step)
        if want == ABSENT:
            if got is not None:
                bad.append((step, want, got["class"], got["reason"]))
            continue
        if got is None:
            bad.append((step, want, "(no such step)", ""))
            continue
        says = None
        if isinstance(want, tuple):
            want, says = want
        klass, _, rule = want.partition(":")
        ok = got["class"] == klass and (not rule or "(%s)" % rule in got["reason"]) and \
            (says is None or says in got["reason"])
        if not ok:
            bad.append((step, want if says is None else "%s %r" % (want, says), got["class"], got["reason"]))
    return bad


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
    seam = seam_cases()
    for name, (test_id, lua, ledger_text, wants, off) in seam.items():
        bad = check_seam_case(grade_report(test_id, lua, ledger_text, off), wants)
        failures += bool(bad)
        print("%-4s %-26s %s" % ("ok" if not bad else "FAIL", name,
                                 "; ".join("%s want=%s got=%s %s" % (step, want, got, reason[:200])
                                           for step, want, got, reason in bad) or
                                 ", ".join("%s=%s" % (step, want if not isinstance(want, tuple) else want[0])
                                           for step, want in wants.items())))
    total = len(table) + len(seam)
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
