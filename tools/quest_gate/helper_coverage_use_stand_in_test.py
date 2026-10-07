#!/usr/bin/env python3
"""Fixture test: a "use X on <loc>" step is driven by a use on the loc this
cache puts in the guide's loc's place (Grader.USE_ON_STAND_IN), and a step is
credited from its own row before another step's (Grader.OWN_ROW_FIRST).

    python3 tools/quest_gate/helper_coverage_use_stand_in_test.py

Batch matthew-mbp-m4-b69. Quest Helper names a later cache's loc; rev 239
takes the use on another loc at the same spot, so three driven steps read
CONTENT_GAP and their tests declared a GUIDE-GAP to pass the gate:

  icthlarin   useSymbolOnSarcopagus: the guide's deserttreasure_sarcophigi_wall
              (3312,9197; no name, no ops) shares its tiles with multiloc
              ics_sarcophigi_door_2, whose state ics_sarcophigi_door_2_op takes
              the symbol ([oplocu] icthlarin_ceremony.rs2:90).
  zombiequeen useKeyOnDoor: the guide's right leaf hillsidedoorr_multi
              (2916,3091); the key goes on the left leaf's closed state
              hillsideclosedl, the same @zq_tombdoor_outer_use
              (quest_zombiequeen.rs2:175-176).
  elena       useRopeOnGrill: the guide's plague_grill (2514,9739) takes no use;
              the rope is tied to plaguesewerpipe_open, 2514,9737
              (sewerpipe.rs2:53).

The synthetic files are the three runs' own use_on lines and ledger rows
(build/quest_gate/{icthlarin,zombiequeen,elena}/ledger.tsv, 2026-10-06),
graded on the real guides, content and maps. The rule stays strict: no
effect on the row, a copy pressed away from the guide's point, a loc with no
use trigger, or a far loc whose use is another handler credits nothing.
Each `.rule_off` case runs with the switch False and must fail (the reading
before the rule sees the gap).

OWN_ROW_FIRST: Tribal Totem's talkToKangaiMauAgain was credited to row 16
`talkToKangaiMau` (the first talk; the row name is 15 of the step name's 20
letters) ahead of its own later row `talkToKangaiMauAgain`.

When the runs' own files are on disk (build/quest_gate/<id>/<id>.lua and
ledger.tsv), each is graded too with the step's GUIDE-GAP marker line
removed in memory: it must read FULL. Writes no files. Exit 0 when every
case holds.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

HEADER = "quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n"
LUA = """return {
    id = "%s",
    setup = { "::clearinv" },
    run = function(t)
%s
        t.finish(0)
    end,
}
"""

ICTH_USE = ('        local sarc = t.player.by_symbol("loc", "%s")\n'
            '        t.exec("useSymbolOnSarcopagus", t.player.use_on, "ics_little_unholy_symbol", sarc)')
ICTH_ROW = ("1\tuseSymbolOnSarcopagus\tPASS\t5\t216-useSymbolOnSarcopagus\t%s -- use_on %s: pressed the copy "
            "at %s (element 536877967); use_on %s -> ics_sarcophigi_door_2 (base)\n")
ICTH_EFFECT = ("page none->mesbox (text) [backpack: gained ics_little_holy_symbol 0->1; "
               "lost ics_little_unholy_symbol 1->0]")

ZQ_USE = ('        local tomb_door = t.player.by_symbol("loc", "%s")\n'
          '        t.exec("useKeyOnDoor", t.player.use_on, "zqbonekey", tomb_door)')
ZQ_ROW = ("1\tuseKeyOnDoor\tPASS\t21\t182-useKeyOnDoor\tchat_message -- use_on %s -> %s (base); click_minimenu: "
          "hunted pose 2 (reach 90) -- hovered +0,-16 off the projected 382,308 (11 pixel(s) tried)\n")

ELENA_USE = ('        local pipe = t.player.by_symbol("loc", "%s")\n'
             '        t.exec("useRopeOnGrill", t.player.use_on, "rope", pipe)\n'
             '        t.exec("useRopeOnGrill-dialog", t.chat.play, { "*" })\n'
             '        t.ticks(1)\n'
             '        t.expect("quest.stage.tied_rope", t.quest.expect_stage("tied_rope"))')
ELENA_ROWS = ("1\tuseRopeOnGrill\tPASS\t8\t105-useRopeOnGrill\t%s\n"
              "2\tuseRopeOnGrill-dialog\tPASS\t2\t107-useRopeOnGrill-dialog\t%s\n"
              "3\tquest.stage.tied_rope\tPASS\t1\t\t%s\n")
ELENA_EFFECT = ("page none->mesbox (text) [backpack: lost rope 1->0]",
                "1 page(s): any:*=mesbox 'You tie the end of the r'",
                "9 -- varp165_elenaquest (varp) = 9 (tied_rope)")

TOTEM_TALKS = ('        t.exec("talkToKangaiMau", t.player.talk_to, "kangai_mau", 1)\n'
               '        t.exec("talkToKangaiMau-dialog", t.chat.play, { "*" })\n'
               '        t.exec("talkToKangaiMauAgain.inside", t.check, true, "at 2794,3182,0")\n'
               '        t.exec("talkToKangaiMauAgain", t.player.talk_to, "kangai_mau", 1)')
TOTEM_ROWS = ("1\ttalkToKangaiMau\tPASS\t1\t019-talkToKangaiMau\tmap_flag: dialogue npc is up\n"
              "2\ttalkToKangaiMau-dialog\tPASS\t3\t029-talkToKangaiMau-dialog\t9 page(s): npc:Hello.\n"
              "3\ttalkToKangaiMauAgain.inside\tPASS\t1\t168-talkToKangaiMauAgain.inside\tat 2794,3182,0\n"
              "4\ttalkToKangaiMauAgain\tPASS\t5\t169-talkToKangaiMauAgain\tmap_flag: dialogue npc is up\n")


def parse(text):
    """ledger.read's (rows, summary), from text."""
    rows, summary = [], None
    for line in text.split("\n"):
        if not line or line.startswith("quest-ledger") or line.startswith("index\t"):
            continue
        fields = line.split("\t")
        if fields[0] == "SUMMARY":
            summary = fields
            continue
        fields += [""] * (6 - len(fields))
        rows.append({"index": fields[0], "step": fields[1], "verdict": fields[2],
                     "ticks": fields[3], "shots": fields[4], "detail": fields[5]})
    return rows, summary


def report(test_id, test_path, lua, ledger_text, flags_off=()):
    """The Grader's report on `lua` and `ledger_text`, with the named Grader
    switches False."""
    saved = {flag: getattr(helper_coverage.Grader, flag) for flag in flags_off}
    original = helper_coverage.ledger.read
    helper_coverage.ledger.read = lambda path: parse(ledger_text)
    for flag in flags_off:
        setattr(helper_coverage.Grader, flag, False)
    try:
        return helper_coverage.Grader(test_id, test_path=test_path, test_text=lua,
                                      ledger_file="<fixture>").report()
    finally:
        helper_coverage.ledger.read = original
        for flag, value in saved.items():
            setattr(helper_coverage.Grader, flag, value)


def grade(test_id, lua, ledger_text, flags_off=()):
    """{step: (class, reason)}."""
    got = report(test_id, "%s.lua" % test_id, lua, ledger_text, flags_off)
    return {s["step"]: (s["class"], s["reason"]) for s in got["steps"]}


def driven(step, *words):
    return lambda got: [] if got[step][0] == "DRIVEN" and all(w in got[step][1] for w in words) else [step]


def not_driven(step):
    return lambda got: [step] if got[step][0] == "DRIVEN" else []


def icthlarin(loc="ics_sarcophigi_door_2_op", effect=ICTH_EFFECT, copy="3312,9195,0"):
    return "icthlarin", LUA % ("icthlarin", ICTH_USE % loc), HEADER + ICTH_ROW % (effect, loc, copy, loc)


def zombiequeen(loc="hillsideclosedl", base="hillsidedoorl_multi"):
    return "zombiequeen", LUA % ("zombiequeen", ZQ_USE % loc), HEADER + ZQ_ROW % (loc, base)


def elena(loc="plaguesewerpipe_open", effect=ELENA_EFFECT):
    return "elena", LUA % ("elena", ELENA_USE % loc), HEADER + ELENA_ROWS % effect


def cases():
    """{name: (test_id, lua, ledger, step, check, flags)}: `flags` the
    switches that, turned off, must make the case fail."""
    stand_in = ("USE_ON_STAND_IN",)
    sarc, key, rope = "useSymbolOnSarcopagus", "useKeyOnDoor", "useRopeOnGrill"
    return {
        "icthlarin_multiloc_on_the_tiles": icthlarin() + (sarc, driven(
            sarc, "ics_sarcophigi_door_2_op stands in for the guide's deserttreasure_sarcophigi_wall",
            "icthlarin_ceremony.rs2:90"), stand_in),
        "icthlarin_no_effect": icthlarin(effect="map_flag") + (sarc, not_driven(sarc), ()),
        "icthlarin_copy_off_the_point": icthlarin(copy="3309,9199,0") + (sarc, not_driven(sarc), ()),
        # egypt_statue1 stands at 3311,9198, within two tiles, and takes no use
        "icthlarin_loc_with_no_use": icthlarin(loc="egypt_statue1") + (sarc, not_driven(sarc), ()),
        "zombiequeen_other_leaf": zombiequeen() + (key, driven(
            key, "hillsideclosedl stands in for the guide's hillsidedoorr_multi", "@zq_tombdoor_outer_use"),
            stand_in),
        # the tomb's exit door takes the key too, by another handler, far away
        "zombiequeen_far_door": zombiequeen(loc="hillsideexitclosedl", base="hillsideexitclosedl") + (
            key, not_driven(key), ()),
        "elena_pipe_beside_the_grill": elena() + (rope, driven(
            rope, "plaguesewerpipe_open stands in for the guide's plague_grill", "sewerpipe.rs2:53"), stand_in),
        # the use's own row says only that the press was sent, and no read follows it
        "elena_no_effect": ("elena", elena()[1],
                            HEADER + "1\tuseRopeOnGrill\tPASS\t8\t105-useRopeOnGrill\tmap_flag\n",
                            rope, not_driven(rope), ()),
        "totem_own_row_first": ("totem", LUA % ("totem", TOTEM_TALKS), HEADER + TOTEM_ROWS, "talkToKangaiMauAgain",
                                driven("talkToKangaiMauAgain", "ledger row 4 'talkToKangaiMauAgain'"),
                                ("OWN_ROW_FIRST",)),
    }


RUNS = os.path.join(helper_coverage.REPO_ROOT, "build", "quest_gate")
REAL = (("icthlarin", "useSymbolOnSarcopagus"), ("zombiequeen", "useKeyOnDoor"), ("elena", "useRopeOnGrill"))


def real_runs():
    """[(name, failed or None to skip, text)]: the b69 runs' own Lua (the
    copy the run used) and ledger, each step's GUIDE-GAP marker removed,
    must read FULL with the step DRIVEN."""
    out = []
    for test_id, step in REAL:
        lua_path = os.path.join(RUNS, test_id, test_id + ".lua")
        ledger_path = os.path.join(RUNS, test_id, "ledger.tsv")
        if not (os.path.isfile(lua_path) and os.path.isfile(ledger_path)):
            out.append(("real_" + test_id, None, "%s is gone" % os.path.dirname(lua_path)))
            continue
        with open(lua_path, "r", encoding="utf-8") as handle:
            lines = handle.read().split("\n")
        marker = re.compile(r"^\s*--\s*GUIDE-GAP:\s*%s\b" % re.escape(step))
        kept = [line for line in lines if not marker.match(line)]
        with open(ledger_path, "r", encoding="utf-8") as handle:
            ledger_text = handle.read()
        got = report(test_id, lua_path, "\n".join(kept), ledger_text)
        classes = {s["step"]: s["class"] for s in got["steps"]}
        bad = got["verdict"] != "FULL" or classes.get(step) != "DRIVEN"
        out.append(("real_" + test_id, bad, "%s, %s %s (%d marker line(s) removed)" % (
            got["verdict"], step, classes.get(step), len(lines) - len(kept))))
    return out


def main():
    failures, total = 0, 0
    for name, (test_id, lua, ledger_text, step, check, flags) in cases().items():
        got = grade(test_id, lua, ledger_text)
        bad = check(got)
        total += 1
        failures += bool(bad)
        print("%-4s %-34s %s=%s %s%s" % ("FAIL" if bad else "ok", name, step, got[step][0], got[step][1][:100],
                                         (" -- wrong: " + ",".join(bad)) if bad else ""))
        if flags:
            off = check(grade(test_id, lua, ledger_text, flags))
            total += 1
            failures += not off
            print("%-4s %-34s with %s off: %s" % ("ok" if off else "FAIL", name + ".rule_off", "/".join(flags),
                                                   "fails as it should" if off else "still passes"))
    for name, bad, text in real_runs():
        if bad is None:
            print("skip %-34s %s" % (name, text))
            continue
        total += 1
        failures += bool(bad)
        print("%-4s %-34s %s" % ("FAIL" if bad else "ok", name, text))
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
