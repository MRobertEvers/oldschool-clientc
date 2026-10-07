#!/usr/bin/env python3
"""Fixture test: a goto longer than GATE_MAX_TILES with no on-foot route is a cheat.

    python3 tools/quest_gate/helper_coverage_long_hop_test.py

Seam matthew-mbp-m4-b70-longgoto. gate_crossings skipped every hop longer
than Grader.GATE_MAX_TILES (1,200 tiles: the bounded margin floods cost too
much past it), so the b70 queenofthieves fixer's first goto, Lumbridge
3206,3233 -> Great Kourend 1555,3564 (1,651 tiles; Kourend is reached by
Veos's boat from Port Sarim), was left unjudged and the gate stayed green.
The same hole passed any island or region reached by goto: Karamja, Zeah,
Lunar Isle, Ape Atoll, Fossil Island, Morytania. Now such a hop is judged by
MapWalls.long_hop_route: an unbounded flood from both ends (doors open,
crossing locs enterable) -- one runs out of tiles -> "no on-foot route"
CHEAT; they meet -> nothing charged (which gate a joined long hop opens is
still not judged). LONG_HOPS_JUDGED False is the reading before.

Map cases (MapWalls.joined_on_foot / long_hop_route, level 0):

  lumbridge_kourend      3206,3233 -> 1555,3564   not joined -> unreachable
  lumbridge_rellekka     3206,3233 -> 2660,3660   joined (membergater)
  lumbridge_ardougne     3206,3233 -> 2662,3300   joined
  lumbridge_<island>     Karamja, Lunar Isle, Ape Atoll, Fossil Island,
                         Canifis (Morytania past the Salve)  not joined
  kourend_inside         1555,3564 -> 1796,3781  joined (both on Zeah)

Grader cases: the committed queenofthieves.lua (HEAD) and its published
ledger (OSRS-Content 5ab18e9fc9), its first goto (goto-barman) moved to the
open tile outside the pub door (1555,3564) so no enclosure rule judges it:

  kourend_goto           Lumbridge -> 1555,3564        -> "no on-foot route" CHEAT
  kourend_switch_off     the same, LONG_HOPS_JUDGED off -> not charged (the hole)
  kourend_after_boat     departure stamped at Port Piscarilius (1824,3691:
                         the boat already landed)      -> not charged
  rellekka_long_goto     goto-barman retargeted to Rellekka 2660,3660 from
                         Lumbridge, GATE_MAX_TILES 100 so the hop takes the
                         long path                     -> not charged

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

G = helper_coverage.Grader
LEDGER_COMMIT = "5ab18e9fc9"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_queenofthieves/play/ledger.tsv"
GOTO_LINE = 't.exec("goto-barman", t.player.goto_tile, 1550, 3560, 0)'
GOTO_DETAIL = "at 1550,3560,0 from 3206,3233,0"
LUMBRIDGE = (3206, 3233)

MAP_CASES = {
    "lumbridge_kourend": (LUMBRIDGE, (1555, 3564), False),
    "lumbridge_rellekka": (LUMBRIDGE, (2660, 3660), True),
    "lumbridge_ardougne": (LUMBRIDGE, (2662, 3300), True),
    "lumbridge_karamja": (LUMBRIDGE, (2918, 3176), False),
    "lumbridge_lunar_isle": (LUMBRIDGE, (2141, 3942), False),
    "lumbridge_ape_atoll": (LUMBRIDGE, (2755, 2785), False),
    "lumbridge_fossil_island": (LUMBRIDGE, (3724, 3808), False),
    "lumbridge_canifis": (LUMBRIDGE, (3500, 3480), False),
    "kourend_inside": ((1555, 3564), (1796, 3781), True),
}


def committed():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show", "HEAD:test/quests/queenofthieves.lua"],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             LEDGER_COMMIT + ":" + LEDGER], capture_output=True, text=True, check=True).stdout
    assert GOTO_LINE in lua, "queenofthieves.lua no longer carries %r" % GOTO_LINE
    assert "\tgoto-barman\tPASS\t" in ledger and GOTO_DETAIL in ledger
    return lua, ledger


def moved(lua, ledger, landing, departure):
    """The pair with goto-barman landing at `landing` from `departure`."""
    new_line = 't.exec("goto-barman", t.player.goto_tile, %d, %d, 0)' % landing
    new_detail = "at %d,%d,0 from %d,%d,0" % (landing + departure)
    return lua.replace(GOTO_LINE, new_line), ledger.replace(GOTO_DETAIL, new_detail)


def grade_report(lua, ledger_text, switches=()):
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
            grader = helper_coverage.Grader("queenofthieves", test_path=os.path.join(scratch, "queenofthieves.lua"),
                                            test_text=lua)
            return grader.report()
        finally:
            helper_coverage.ledger_path = original
            for klass, attr, value in saved:
                setattr(klass, attr, value)


def no_route_charges(report):
    """[reason] of every non-neutral step whose reason charges goto-barman with no on-foot route."""
    return [s["reason"] for s in report["steps"]
            if s["class"] == "CHEAT" and "'goto-barman'" in s["reason"] and "no on-foot route" in s["reason"]]


def main():
    failures = 0
    total = 0
    walls = helper_coverage.map_walls()
    for name, (start, end, joined) in MAP_CASES.items():
        got = walls.joined_on_foot(start, end, 0)
        kind = walls.long_hop_route(start, end, 0)[0]
        ok = got == joined and kind == ("none" if joined else "unreachable")
        failures += not ok
        total += 1
        print("%-4s %-26s %s -> %s joined=%s route=%s" % ("ok" if ok else "FAIL", name, start, end, got, kind))
    lua, ledger = committed()
    outside = (1555, 3564)
    cases = {
        "kourend_goto": (moved(lua, ledger, outside, LUMBRIDGE), (),
                         lambda r: any("UNREACHABLE, 1651 tiles" in reason for reason in no_route_charges(r))),
        "kourend_switch_off": (moved(lua, ledger, outside, LUMBRIDGE), ((G, "LONG_HOPS_JUDGED", False),),
                               lambda r: not no_route_charges(r)),
        "kourend_after_boat": (moved(lua, ledger, outside, (1824, 3691)), (), lambda r: not no_route_charges(r)),
        "rellekka_long_goto": (moved(lua, ledger, (2660, 3660), LUMBRIDGE), ((G, "GATE_MAX_TILES", 100),),
                               lambda r: not no_route_charges(r)),
    }
    for name, ((case_lua, case_ledger), switches, holds) in cases.items():
        report = grade_report(case_lua, case_ledger, switches)
        ok = holds(report)
        failures += not ok
        total += 1
        found = no_route_charges(report)
        print("%-4s %-26s %-9s %s" % ("ok" if ok else "FAIL", name, report["verdict"],
                                      found[0][:170] if found else "goto-barman: no no-route charge"))
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
