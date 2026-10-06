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

Hops with no on-foot route, and gotos read by what they did (seam
matthew-mbp-m4-b65-seam1). Each case is a committed Lua and its published
ledger read from git (OSRS-Content selftest/quests/<quest_dir>/play/), graded
with the new reading and, where the case proves a switch, with it off:

  pryingtimes_pre_b65   753ee4a3c / 74e86ee0d4: the ::pryingtimes placement
                        (Lumbridge -> the Pandemonium, an island) and
                        goto-thurgo (off the island, its departure read past
                        a t.sail.cargo_deliver row) -> "no on-foot route"
                        CHEAT on both; neither with NO_ROUTE_CHARGED off,
                        and goto-thurgo not with SAIL_NO_MOVE_KEPT off
  makinghistory_pre_b65 9a9d57559 / 776bd1593e: row 5 goto-talkToJorral
                        crosses membergater 2933,3320 only at margin 250
  itwatchtower_16ad     16ad11389 / c436ddab5a (read FULL before):
                        goto-goUpTrellis from Lumbridge -> CHEAT, membergater
  fishingcompo_renamed  8742ae506 / 35419bfb8c with enterHemenster renamed
                        goToHemenster: the cross_gate row is no goto -> FULL;
                        with GOTO_BY_ACTION off it reads CHEAT (the bug)
  imp_diagonal_door     imp's goto into the Wizards' Tower ladder room names
                        the diagonal fai_wiztower_poor_door 3107,3162
  death / grandtree     no new charge (death keeps its one membergater
                        charge, grandtree stays FULL)
  *_b65                 the b65 green ledgers of pryingtimes, makinghistory
                        and itwatchtower stay FULL

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


PLAY_LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_%s/play/ledger.tsv"
NEUTRAL = ("DRIVEN", "TRAVEL", "ALTERNATIVE", "EQUIVALENT", "BRING_ALONG")


def committed(test_id, lua_rev, ledger_rev, rename=None):
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show", "%s:test/quests/%s.lua" % (lua_rev, test_id)],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             ledger_rev + ":" + PLAY_LEDGER % test_id],
                            capture_output=True, text=True, check=True).stdout
    if rename:
        old, new = rename
        assert old in lua, "%s@%s no longer carries %r" % (test_id, lua_rev, old)
        lua = lua.replace(old, new)
        lines = []
        for line in ledger.split("\n"):
            fields = line.split("\t")
            if len(fields) > 1:
                fields[1] = fields[1].replace(old, new)
            lines.append("\t".join(fields))
        ledger = "\n".join(lines)
    return lua, ledger


def grade_report(test_id, lua, ledger_text, switches=()):
    """The report of `test_id` graded from (lua, ledger_text), with each
    (class, attribute, value) of `switches` set for the call."""
    saved = [(klass, attr, getattr(klass, attr)) for klass, attr, _ in switches]
    for klass, attr, value in switches:
        setattr(klass, attr, value)
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        original = helper_coverage.ledger_path
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            grader = helper_coverage.Grader(test_id, test_path=os.path.join(scratch, test_id + ".lua"),
                                            test_text=lua)
            return grader.report()
        finally:
            helper_coverage.ledger_path = original
            for klass, attr, value in saved:
                setattr(klass, attr, value)


def charges(report):
    """{step: reason} of every step graded outside NEUTRAL."""
    return {s["step"]: s["reason"] for s in report["steps"] if s["class"] not in NEUTRAL}


def charged(report, step_prefix, *words):
    """Does a charged step starting with `step_prefix` give a reason holding every word?"""
    return any(step.startswith(step_prefix) and all(w in reason for w in words)
               for step, reason in charges(report).items())


G = helper_coverage.Grader
W = helper_coverage.MapWalls
OLD_HOPS = ((G, "NO_ROUTE_CHARGED", False), (W, "NO_ROUTE", False))


def hop_cases():
    """{name: (test_id, lua, ledger, switches, holds(report) -> bool, what)}."""
    pry = committed("pryingtimes", "753ee4a3c", "74e86ee0d4")
    mh = committed("makinghistory", "9a9d57559", "776bd1593e")
    wt = committed("itwatchtower", "16ad11389", "c436ddab5a")
    fc = committed("fishingcompo", "8742ae506", "35419bfb8c", ("enterHemenster", "goToHemenster"))
    imp = committed("imp", "HEAD", "HEAD")
    death = committed("death", "HEAD", "ba3189266c")
    tree = committed("grandtree", "HEAD", "35b00c603e")
    placement = "(setup placement) ::pryingtimes"
    return {
        "pryingtimes_placement": ("pryingtimes", pry[0], pry[1], (),
                                  lambda r: charged(r, placement, "no on-foot route (UNREACHABLE"),
                                  "setup placement Lumbridge -> Pandemonium: no on-foot route"),
        "pryingtimes_thurgo": ("pryingtimes", pry[0], pry[1], (),
                               lambda r: any("'goto-thurgo'" in reason and "no on-foot route" in reason
                                             for reason in charges(r).values()),
                               "goto-thurgo off the island, departure read past cargo_deliver"),
        "pryingtimes_old_hops": ("pryingtimes", pry[0], pry[1], OLD_HOPS,
                                 lambda r: not any("no on-foot route" in reason for reason in charges(r).values()),
                                 "NO_ROUTE off: neither charged (the hole)"),
        "pryingtimes_any_sail": ("pryingtimes", pry[0], pry[1], ((G, "SAIL_NO_MOVE_KEPT", False),),
                                 lambda r: not any("'goto-thurgo'" in reason for reason in charges(r).values()),
                                 "SAIL_NO_MOVE_KEPT off: goto-thurgo's departure unknown again"),
        "makinghistory_jorral": ("makinghistory", mh[0], mh[1], (),
                                 lambda r: charged(r, "talkToJorral", "'goto-talkToJorral'",
                                                   "membergater at 2933,3320,0", "margin 250 only"),
                                 "row 5 crosses membergater 2933,3320 at margin 250 only"),
        "makinghistory_old_hops": ("makinghistory", mh[0], mh[1], OLD_HOPS,
                                   lambda r: not charged(r, "talkToJorral"),
                                   "NO_ROUTE off: row 5 uncharged (the hole)"),
        "itwatchtower_trellis": ("itwatchtower", wt[0], wt[1], (),
                                 lambda r: charged(r, "goUpTrellis", "'goto-goUpTrellis'", "membergater"),
                                 "16ad11389 read FULL; goto-goUpTrellis from Lumbridge needs membergater"),
        "itwatchtower_old_hops": ("itwatchtower", wt[0], wt[1], OLD_HOPS,
                                  lambda r: r["verdict"] == "FULL", "NO_ROUTE off: FULL (the hole)"),
        "fishingcompo_renamed": ("fishingcompo", fc[0], fc[1], (),
                                 lambda r: r["verdict"] == "FULL",
                                 "cross_gate row named goToHemenster.gateIn is no goto"),
        "fishingcompo_by_name": ("fishingcompo", fc[0], fc[1], ((G, "GOTO_BY_ACTION", False),),
                                 lambda r: charged(r, "goToHemenster", "'goToHemenster.gateIn'"),
                                 "GOTO_BY_ACTION off: read as a goto onto the gate (the bug)"),
        "imp_diagonal_door": ("imp", imp[0], imp[1], (),
                              lambda r: charged(r, "moveToTower", "fai_wiztower_poor_door at 3107,3162,0"),
                              "the diagonal ladder-room door is a door"),
        "death_no_new_charge": ("death", death[0], death[1], (),
                                lambda r: set(charges(r)) == {"talkToDenulth1"},
                                "only the membergater charge it had"),
        "grandtree_full": ("grandtree", tree[0], tree[1], (), lambda r: r["verdict"] == "FULL", "stays FULL"),
        "pryingtimes_b65": ("pryingtimes",) + committed("pryingtimes", "88e216ba0", "d72a6de05c") +
                           ((), lambda r: r["verdict"] == "FULL", "b65 green stays FULL"),
        "makinghistory_b65": ("makinghistory",) + committed("makinghistory", "5b7ce6c11", "6f52a3eb31") +
                             ((), lambda r: r["verdict"] == "FULL", "b65 green stays FULL"),
        "itwatchtower_head": ("itwatchtower",) + committed("itwatchtower", "e96f9dede", "408efd51b4") +
                             ((), lambda r: r["verdict"] == "FULL", "camelot-teleport start stays FULL"),
    }


def main():
    failures = 0
    table = cases()
    for name, (lua, ledger_text, holds) in table.items():
        got, reason = grade_case(lua, ledger_text)
        ok = holds(got)
        failures += not ok
        print("%-4s %-22s got=%-12s %s" % ("ok" if ok else "FAIL", name, got, reason[:170]))
    hops = hop_cases()
    for name, (test_id, lua, ledger_text, switches, holds, what) in hops.items():
        report = grade_report(test_id, lua, ledger_text, switches)
        ok = holds(report)
        failures += not ok
        first = next(iter(charges(report).items()), ("", ""))
        print("%-4s %-22s %-9s %s -- %s" % ("ok" if ok else "FAIL", name, report["verdict"], what,
                                            ("%s: %s" % first)[:150]))
    total = len(table) + len(hops)
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
