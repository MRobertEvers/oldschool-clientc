#!/usr/bin/env python3
"""Fixture test: the gotos the b56 round-1 sampler found past doors, into a
sealed pocket and out of a walled room, which enclosure_entries (seam 1) read
FULL.

    python3 tools/quest_gate/helper_coverage_enclosure_hops_test.py

The fixtures are the four round-1 runs as committed at 7936d2d97 and their
published ledgers at OSRS-Content 994d64d507, read from git and graded against
the real guide, content and maps. Each case edits a ledger and checks some
steps. The rule each case proves, and what the seam-1 grader read:

  A  blocked landing: MapWalls.enclosure floods from a landing ON a loc (a
     table, a barrel, a staircase's footprint) instead of reading no room;
     a landing on a diagonal wall at a room's corner is read by where the
     player stood next (_landing_room)
  B  charge any: a goto no guide step is named after is charged to the next
     guide row, else to a "(goto ...)" step of its own (_charge_name)
  C  enclosure_exits: a goto OUT of a walled room past its closed door, to
     a tile at most 24 away (further may stand for a teleport out)
  D  sealed_entries: a goto into a pocket with no door and no climb, whose
     op locs (if any) were not pressed

  case                         rule  step(s)                          want
  afl_blocked_landing          A     takeHammer, makeEquipmentPile    CHEAT CHEAT
  afl_corner_next_reading      A     talkToAtza                       CHEAT
  afl_corner_departed_inside   A     talkToAtza (stamped inside)      DRIVEN
  afl_sealed_pocket            D     talkToVerity, talkToVerityEnd    CHEAT CHEAT
  afl_sealed_departed_inside   D     talkToVerity (stamped inside)    DRIVEN
  afl_exit                     C     returnToFoxAfterTrim, landed at 1701,3062 just outside Atza's
                                     house (the run went 83 tiles, to Fox)  CHEAT
  afl_exit_door_pressed        C     the same, door 1698,3064 pressed  DRIVEN
  twp_enter_hq                 A+B   goUpHQ, enterColosseum           CHEAT DRIVEN
  twp_enter_hq_door_pressed    A+B   goUpHQ (door 1657,3150 pressed)  DRIVEN
  wanted_uncharged             B     (goto past castledoor), (goto past fai_varrock_poor_door_flipped)  CHEAT CHEAT
  wanted_op_pocket             D     (goto into a sealed pocket): pos4.goto past the swamp cave's
                                     stepping stone, whose pocket's only op loc (tog_cave_down) was
                                     never pressed                    CHEAT
  dream_sink_house             A     fillVialWithWater                CHEAT

Writes only temporary files. Exit 0 when every case holds.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

LUA_COMMIT = "7936d2d97"
LEDGER_COMMIT = "994d64d507"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_%s/play/ledger.tsv"


def fixture(test_id):
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          "%s:test/quests/%s.lua" % (LUA_COMMIT, test_id)],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             "%s:%s" % (LEDGER_COMMIT, LEDGER % test_id)],
                            capture_output=True, text=True, check=True).stdout
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


def land(ledger, step, tile):
    """Row `step`'s goto landing moved to `tile` (its departure kept)."""
    def change(detail):
        assert detail.startswith("at "), detail
        return "at %s%s" % (tile, detail[len(detail.split(" from ")[0]):])
    return edit(ledger, step, 5, change)


def press_before(ledger, step, door, tile):
    """A press of `door`'s copy at `tile` on the row just before `step`."""
    names = [line.split("\t")[1] for line in ledger.split("\n") if len(line.split("\t")) >= 6]
    before = names[names.index(step) - 1]
    return edit(ledger, before, 5, lambda detail: "%s ## click_loc %s: pressed the copy at %s" % (
        detail, door, tile))


def cases():
    afl, afl_ledger = fixture("atfirstlight")
    twp, twp_ledger = fixture("twilightspromise")
    wanted, wanted_ledger = fixture("wanted")
    dream, dream_ledger = fixture("dreammentor")
    for text, rows in ((afl_ledger, ("goto-talkToAtza", "goto-takeHammer", "goto-makeEquipmentPile",
                                     "goto-talkToVerity", "goto-returnToFoxAfterTrim")),
                       (twp_ledger, ("goto-enterHQ",)), (wanted_ledger, ("pos2.goto", "goto-mage", "pos4.goto")),
                       (dream_ledger, ("goto-fillVialWithWater",))):
        for row in rows:
            assert ("\t%s\tPASS\t" % row) in text, "the fixture ledger no longer carries %r" % row
    # the run's goto-returnToFoxAfterTrim left Atza's house (1698,3063) for
    # Fox, 83 tiles away (a teleport out may stand for that); landed just
    # outside the house's east door instead, it is a walk out past it
    out_of_house = land(afl_ledger, "goto-returnToFoxAfterTrim", "1701,3062,0")
    return {
        "afl_blocked_landing": ("atfirstlight", afl, afl_ledger,
                                {"takeHammer": "CHEAT:enclosure_entries", "makeEquipmentPile": "CHEAT:enclosure_entries"}),
        "afl_corner_next_reading": ("atfirstlight", afl, afl_ledger, {"talkToAtza": "CHEAT:enclosure_entries"}),
        "afl_corner_departed_inside": ("atfirstlight", afl, depart(afl_ledger, "goto-talkToAtza", "1697,3062,0"),
                                       {"talkToAtza": "DRIVEN"}),
        "afl_sealed_pocket": ("atfirstlight", afl, afl_ledger, {"talkToVerity": "CHEAT:sealed_entries", "talkToVerityEnd": "CHEAT:sealed_entries"}),
        "afl_sealed_departed_inside": ("atfirstlight", afl, depart(afl_ledger, "goto-talkToVerity", "1558,9466,0"),
                                       {"talkToVerity": "DRIVEN"}),
        "afl_exit": ("atfirstlight", afl, out_of_house, {"returnToFoxAfterTrim": "CHEAT:enclosure_exits"}),
        "afl_exit_door_pressed": ("atfirstlight", afl,
                                  press_before(out_of_house, "goto-returnToFoxAfterTrim", "fortis_door_l", "1698,3064,0"),
                                  {"returnToFoxAfterTrim": "DRIVEN"}),
        "twp_enter_hq": ("twilightspromise", twp, twp_ledger, {"goUpHQ": "CHEAT:enclosure_entries", "enterColosseum": "DRIVEN"}),
        "twp_enter_hq_door_pressed": ("twilightspromise", twp,
                                      press_before(twp_ledger, "goto-enterHQ", "fortis_door_l_reverse", "1657,3150,0"),
                                      {"goUpHQ": "DRIVEN"}),
        "wanted_uncharged": ("wanted", wanted, wanted_ledger,
                             {"(goto past castledoor)": "CHEAT:enclosure_entries",
                              "(goto past fai_varrock_poor_door_flipped)": "CHEAT:enclosure_entries"}),
        "wanted_op_pocket": ("wanted", wanted, wanted_ledger, {"(goto into a sealed pocket)": "CHEAT:sealed_entries"}),
        "dream_sink_house": ("dreammentor", dream, dream_ledger,
                       {"fillVialWithWater": "CHEAT:enclosure_entries"}),
    }


def grade_case(test_id, lua, ledger_text):
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        grader = helper_coverage.Grader(test_id, test_path=os.path.join(scratch, test_id + ".lua"),
                                        test_text=lua, ledger_file=ledger)
        report = grader.report()
    return {s["step"]: (s["class"], s["reason"]) for s in report["steps"]}


def main():
    failures = 0
    table = cases()
    for name, (test_id, lua, ledger_text, want) in table.items():
        by_name = grade_case(test_id, lua, ledger_text)
        # "CHEAT:<rule>": the class, and the rule's tag in the reason (the
        # other rules may charge the same step: each case proves its own)
        got = {}
        for step, expect in want.items():
            klass, reason = by_name.get(step, ("-", ""))
            rule = expect.partition(":")[2]
            got[step] = klass + (":" + rule if rule and "(%s)" % rule in reason else "")
        ok = got == want
        failures += not ok
        first = next(iter(want))
        print("%-4s %-28s want=%-30s got=%-30s %s" % (
            "ok" if ok else "FAIL", name, "/".join(want.values()), "/".join(got.values()),
            by_name.get(first, ("-", ""))[1][:100]))
    print("%d/%d" % (len(table) - failures, len(table)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
