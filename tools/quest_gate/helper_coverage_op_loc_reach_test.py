#!/usr/bin/env python3
"""Fixture test: a walk over a loc the player must CLICK (a trap, a stepping stone, climbing rocks) is
not a route, in the sample tools' reach.py / goto_table.py and in helper_coverage's MapWalls flood.

    python3 tools/quest_gate/helper_coverage_op_loc_reach_test.py

Roving Elves (matthew-mbp-m4-b59, sent back in round 3, sampler-findings "Sample matthew-mbp-m4-b59,
round 3" (a)). Regicide's pitfalls, tripwires and woodsprings are `blockwalk=0` locs with an op (Jump,
Step-over, Pass), and regicide_traps.rs2:118-153 hurts a player who steps on them. reach.py walked over
them, so every goto from the Arandar gate to Islwyn's camp read REACH len=545 at margin 80, and three
b59 tests went back for gotos the tool had called clean. Now an op loc on a walkable tile, and a
zone-trigger tile (sample_tools/zone_triggers.tsv), blocks the flood unless --allow-op-locs is given,
and a hop that needs one reads NEEDS-OP naming the loc and tile.

The fixtures are read from git: the round-1 ledger the sampler sent back (parent 4686b1706 ->
OSRS-Content 9f83f98e1b) and the round-3 ledger that walked and pressed every trap (80432596c ->
dafbe3c8e3). Cases:

  r1_charged       goto_table on the round-1 ledger: rows 7/16/38/44/93 -> NEEDS-OP via a pitfall
                   and the woodspring
  r1_allow_op_locs the same rows with --allow-op-locs -> REACH (the old reading)
  r3_clean         the round-3 ledger -> no NEEDS-OP / NEEDS-DOOR / UNREACHABLE row
  lumbridge_open   three gotos across open Lumbridge -> REACH
  wheat_pass       a crop (Pick) is walked through: X Marks the Spot's Draynor wheat is not NEEDS-OP
  trip_zone_tiles  the tripwire at 2285,3188: its north and east tiles are zone-trigger tiles
  climbing_rocks   Troll Stronghold's blocking climbing rocks: NEEDS-OP names them, not UNREACHABLE
  mapwalls_pocket  MapWalls.enclosure at Zooknock (2804,9141,0): a 294-tile pocket with the spring
                   trap on its edge; None (open dungeon) with OP_LOCS_BLOCK False
  mm_grade         Monkey Madness (mm.lua 5e6acb314, ledger 5bc8ef66ca): goto-talkToZooknock grades
                   CHEAT (sealed pocket past mm_double_springtrap_trigger), DRIVEN with OP_LOCS_BLOCK False

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

TOOLS = os.path.join(helper_coverage.REPO_ROOT, "test", "quests", "orchestrator", "matthew-mbp-m4", "reports",
                     "sample_tools")
sys.path.append(TOOLS)
import goto_table  # noqa: E402
import reach  # noqa: E402

LEDGER = "osrs239-content/server/scripts/selftest/quests/%s/play/ledger.tsv"
R1, R3 = "9f83f98e1b", "dafbe3c8e3"          # content commits of parent 4686b1706 / 80432596c
MM_LUA, MM_LEDGER = "5e6acb314", "5bc8ef66ca"
CHARGED = ("7", "16", "38", "44", "93")


def content_file(commit, path):
    return subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                           commit + ":" + path], capture_output=True, text=True, check=True).stdout


def ledger_rows(commit, quest_dir, allow_op_locs=False):
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "ledger.tsv")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content_file(commit, LEDGER % quest_dir))
        return {row[0]: row for row in goto_table.rows(path, allow_op_locs)}


def r1_charged():
    rows = ledger_rows(R1, "quest_rovingelves")
    bad = [i for i in CHARGED if "NEEDS-OP" not in rows[i][3] or "regicide_pitfall" not in rows[i][3]
           or "regicide_trap_woodspring@2235,3181" not in rows[i][3]]
    return not bad, "rows %s: %s" % ("/".join(CHARGED), rows["7"][3].split("|")[1][:90])


def r1_allow_op_locs():
    rows = ledger_rows(R1, "quest_rovingelves", True)
    bad = [i for i in CHARGED if not rows[i][3].split("|")[1].startswith("REACH")]
    return not bad, "row 7 %s" % rows["7"][3].split("|")[1]


def r3_clean():
    rows = ledger_rows(R3, "quest_rovingelves")
    bad = [r for r in rows.values() if any(w in r[3] for w in ("NEEDS-OP", "NEEDS-DOOR", "UNREACHABLE"))]
    return not bad, "%d goto rows, charged: %s" % (len(rows), [r[0] for r in bad])


def lumbridge_open():
    got = [reach.answer(3206, 3233, tx, tz, 0, 40) for tx, tz in ((3222, 3218), (3235, 3203), (3190, 3260))]
    return all(g.startswith("REACH") for g in got), "; ".join(got)


def wheat_pass():
    got = reach.answer(3109, 3270, 3109, 3264, 0, 30)
    return "NEEDS-OP" not in got and "wheat" not in got, got


def trip_zone_tiles():
    area = reach.Area(2280, 2290, 3183, 3193, 0)
    got = {t: area.optile.get(t) for t in ((2285, 3188), (2285, 3189), (2286, 3188))}
    ok = (got[(2285, 3188)] or "").startswith("regicide_trap_tripwire@") and \
        got[(2285, 3189)] == "regicide_tripwire_walk@2285,3189" and (got[(2286, 3188)] or "").startswith(
            ("regicide_tripwire_walk@", "regicide_trap_tripwire@"))
    return ok, str(got)


def climbing_rocks():
    got = reach.answer(2856, 3613, 2896, 3619, 0, 30)
    return got.startswith("NEEDS-OP") and "troll_climbingrocks" in got, got[:120]


def mapwalls_pocket():
    helper_coverage.content_index()
    saved = helper_coverage.MapWalls.OP_LOCS_BLOCK
    try:
        helper_coverage.MapWalls.OP_LOCS_BLOCK = True
        room = helper_coverage.MapWalls().enclosure(2804, 9141, 0, 400)
        helper_coverage.MapWalls.OP_LOCS_BLOCK = False
        old = helper_coverage.MapWalls().enclosure(2804, 9141, 0, 400)
    finally:
        helper_coverage.MapWalls.OP_LOCS_BLOCK = saved
    ok = room is not None and any(s == "mm_double_springtrap_trigger" for s, _ in room["ops"]) and old is None
    return ok, "blocking: %s tiles; walking over traps: %s" % (room and len(room["tiles"]), old)


def mm_grade():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show", MM_LUA + ":test/quests/mm.lua"],
                         capture_output=True, text=True, check=True).stdout
    ledger_text = content_file(MM_LEDGER, LEDGER % "quest_mm")
    got = []
    saved, original = helper_coverage.MapWalls.OP_LOCS_BLOCK, helper_coverage.ledger_path
    with tempfile.TemporaryDirectory() as scratch:
        ledger = os.path.join(scratch, "ledger.tsv")
        with open(ledger, "w", encoding="utf-8") as handle:
            handle.write(ledger_text)
        helper_coverage.ledger_path = lambda test_id, quest_dir: ledger
        try:
            for flag in (True, False):
                helper_coverage.MapWalls.OP_LOCS_BLOCK = flag
                helper_coverage._MAP_WALLS = None
                report = helper_coverage.Grader("mm", test_path=os.path.join(scratch, "mm.lua"),
                                                test_text=lua).report()
                step = {s["step"]: s for s in report["steps"]}["talkToZooknock"]
                got.append((step["class"], step["reason"]))
        finally:
            helper_coverage.MapWalls.OP_LOCS_BLOCK = saved
            helper_coverage.ledger_path = original
            helper_coverage._MAP_WALLS = None
    ok = got[0][0] == "CHEAT" and "mm_double_springtrap_trigger" in got[0][1] and got[1][0] == "DRIVEN"
    return ok, "%s / %s: %s" % (got[0][0], got[1][0], got[0][1][:90])


def main():
    cases = [r1_charged, r1_allow_op_locs, r3_clean, lumbridge_open, wheat_pass, trip_zone_tiles,
             climbing_rocks, mapwalls_pocket, mm_grade]
    failures = 0
    for case in cases:
        ok, detail = case()
        failures += not ok
        print("%-4s %-17s %s" % ("ok" if ok else "FAIL", case.__name__, detail[:150]))
    print("%d/%d" % (len(cases) - failures, len(cases)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
