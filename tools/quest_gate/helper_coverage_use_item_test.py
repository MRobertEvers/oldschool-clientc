#!/usr/bin/env python3
"""Fixture test: a guide step that USES an item on its target ("Use the serum
208 on Razmire") is driven only by a use_on of THAT item, or one of its family.

    python3 tools/quest_gate/helper_coverage_use_item_test.py

Shades of Mort'ton (matthew-mbp-m4-b57, sampler finding (a), reverted at
85d3fb737). The run cured Razmire and Ulsquire with Serum 207 and made sacred
oil with olive oil; it never made or used Serum 208 and never used Serum 207
on the flame. helper_coverage still read use207OnFlame, use208OnRazmire and
use208OnUlsquire DRIVEN and the verdict FULL: "an action at line 366 names
'razmire_keelgan_afflicted'" -- a by_symbol lookup -- because the guide's
item had been read as its ctor's first id only (no addAlternates, no dose
family), so the serum 207 cure was not even seen as a guide item.

The fixture is that run: the Lua at 03d4ac076 and its published ledger
(OSRS-Content selftest/quests/quest_mortton/play/ledger.tsv at b52050c293),
read from git, graded against the real guide and content. Cases:

  as_accepted        serum 207 on both npcs, olive oil(3) on the altar
                       use207OnFlame, use208OnRazmire, use208OnUlsquire: not DRIVEN,
                       the reason names the guide's item and the one used;
                       useOilOnFlame (guide: olive oil(4)) and use207OnUlsquire
                       (guide: Serum 207 (1) + alternates): DRIVEN -- dose family
  serum208_on_razmire  line 368 uses mort_serum_perm3 (a dose of the guide's
                     Serum 208 (1)) and its row lost it -> use208OnRazmire DRIVEN
  unbound_ledger_208 line 635's item is a variable the test never binds; the
                     row's backpack diff lost mort_serum_perm2 -> use208OnUlsquire DRIVEN
  unbound_ledger_207 the same, the diff lost mort_serum3 -> use208OnUlsquire not DRIVEN
  never_ran          line 368 uses Serum 208 but the ledger has no razmire.cure
                     row (the run never got there) -> use208OnRazmire not DRIVEN

and three guide-only checks that the rule leaves non-use steps alone: Another
Slice of H.A.M.'s useSpecial (a special attack, not an item use), Nature
Spirit's killGhasts (named for the kill its text ends in), and Mort'ton's
repairTemple (items carried, no target).

QUEST_GATE_USE_ITEM_RULE=0 switches the rule off (every as_accepted step
reads DRIVEN again): this test must then fail. Writes no files. Exit 0 when
every case holds.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

ACCEPTED = "03d4ac076"
LEDGER_COMMIT = "b52050c293"
LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_mortton/play/ledger.tsv"
TEST_ID = "mortton"
NOT_DRIVEN = ("use207OnFlame", "use208OnRazmire", "use208OnUlsquire")
FAMILY_DRIVEN = ("useOilOnFlame", "use207OnUlsquire")
RAZMIRE = 't.exec("razmire.cure", t.player.use_on, "mort_serum3", razmire_afflicted)'
ULSQUIRE = 't.exec("ulsquire.cure", t.player.use_on, "mort_serum3", ulsquire_afflicted)'


def accepted():
    lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                          ACCEPTED + ":test/quests/%s.lua" % TEST_ID],
                         capture_output=True, text=True, check=True).stdout
    ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                             LEDGER_COMMIT + ":" + LEDGER],
                            capture_output=True, text=True, check=True).stdout
    assert RAZMIRE in lua and ULSQUIRE in lua, "the fixture Lua no longer carries the two cures"
    for step in ("razmire.cure", "ulsquire.cure", "temple.sanctifyOil"):
        assert ("\t%s\tPASS\t" % step) in ledger, "the fixture ledger no longer carries %r" % step
    return lua, ledger


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


def grader(test_id, lua, ledger_text):
    original = helper_coverage.ledger.read
    helper_coverage.ledger.read = lambda path: parse(ledger_text)
    try:
        return helper_coverage.Grader(test_id, test_path="%s.lua" % test_id, test_text=lua,
                                      ledger_file="<fixture>")
    finally:
        helper_coverage.ledger.read = original


def grade(lua, ledger_text):
    original = helper_coverage.ledger.read
    helper_coverage.ledger.read = lambda path: parse(ledger_text)
    try:
        report = grader(TEST_ID, lua, ledger_text).report()
    finally:
        helper_coverage.ledger.read = original
    return {s["step"]: (s["class"], s["reason"]) for s in report["steps"]}


def lost(ledger, step, item):
    """`ledger` with row `step`'s backpack diff saying `item` left the pack."""
    out = []
    for line in ledger.split("\n"):
        fields = line.split("\t")
        if len(fields) >= 6 and fields[1] == step:
            fields[5] = "map_flag [backpack: lost %s 1->0]" % item
            line = "\t".join(fields)
        out.append(line)
    return "\n".join(out)


def drop(ledger, step):
    return "\n".join(line for line in ledger.split("\n") if line.split("\t")[1:2] != [step])


def cases():
    lua, ledger = accepted()
    perm = lua.replace(RAZMIRE, RAZMIRE.replace('"mort_serum3"', '"mort_serum_perm3"'))
    unbound = lua.replace(ULSQUIRE, ULSQUIRE.replace('"mort_serum3"', "serum_never_bound"))

    def as_accepted(got):
        bad = [s for s in NOT_DRIVEN if got[s][0] == "DRIVEN"]
        bad += [s for s in NOT_DRIVEN if got[s][0] == "UNMATCHED" and "mort_serum" not in got[s][1]]
        bad += [s for s in FAMILY_DRIVEN if got[s][0] != "DRIVEN"]
        return bad
    return {
        "as_accepted": (lua, ledger, as_accepted),
        "serum208_on_razmire": (perm, lost(ledger, "razmire.cure", "mort_serum_perm3"),
                                lambda got: [] if got["use208OnRazmire"][0] == "DRIVEN" else ["use208OnRazmire"]),
        "unbound_ledger_208": (unbound, lost(ledger, "ulsquire.cure", "mort_serum_perm2"),
                               lambda got: [] if got["use208OnUlsquire"][0] == "DRIVEN" else ["use208OnUlsquire"]),
        "unbound_ledger_207": (unbound, ledger,
                               lambda got: ["use208OnUlsquire"] if got["use208OnUlsquire"][0] == "DRIVEN" else []),
        "never_ran": (perm, drop(ledger, "razmire.cure"),
                      lambda got: ["use208OnRazmire"] if got["use208OnRazmire"][0] == "DRIVEN" else []),
    }


def guide_only():
    """(name, failures): steps the rule must not read as an item use."""
    out = []
    for test_id, step in (("anothersliceofham", "useSpecial"), ("druidspirit", "killGhasts"),
                          (TEST_ID, "repairTemple")):
        g = grader(test_id, "", "")
        wanted = g.use_item_wanted(g.guide.steps[step])
        out.append(("not_a_use:%s.%s" % (test_id, step), [] if wanted is None else [repr(wanted)]))
    return out


# ---- seam matthew-mbp-m4-b66-seam1: a same-named npc, another copy of a loc
#
# Scorpion Catcher's three scorpions are questscorpiona (Taverley), questscorpionb
# (the Barbarian Outpost) and questscorpionc (the monastery): three npcs this
# cache has, all named "Kharid scorpion". same_thing's display rule read any
# of them as any other, so the b66 fixer's run (catches a, b, c in that order:
# fix_b66/scorpcatcher.final.lua lines 234/299/349, run3's ledger rows 35/50/64,
# whose details are copied below) credited catchMonasteryScorpion to the use on
# questscorpionb and catchOutpostScorpion to the use on questscorpiona. And
# Cog's climbWhiteLadder (ladder_from_cellar 2575,9655) read DRIVEN off row 39
# `enterBasement-black`, which climbed ladder_cellar 2566,3242 and only says it
# landed "beside ladder_from_cellar 2566,9642" (test/quests/cog.lua at
# 5e3675055, its published ledger at OSRS-Content 6762eaf875).

SCORP_LUA = """return {
    id = "scorpcatcher",
    run = function(t)
        local scorpion_a, scorpion_a_result = t.player.by_symbol("npc", "questscorpiona")
        t.exec("catch.scorpiona", t.player.use_on, "scorpioncageempty", scorpion_a)
        local scorpion_b, scorpion_b_result = t.player.by_symbol("npc", "questscorpionb")
        t.exec("catch.scorpionb", t.player.use_on, "scorpioncagea", scorpion_b)
        local scorpion_c, scorpion_c_result = t.player.by_symbol("npc", "questscorpionc")
        t.exec("catch.scorpionc", t.player.use_on, "scorpioncageab", scorpion_c)
        t.finish(0)
    end,
}
"""
SCORP_LINES = {"a": 5, "b": 7, "c": 9}
SCORP_LEDGER = "\n".join([
    "quest-ledger-v1",
    "1\tcatch.scorpiona\tPASS\t2\t001-catch.scorpiona\tchat_message [backpack: gained scorpioncagea 0->1; "
    "lost scorpioncageempty 1->0]",
    "2\tcatch.scorpionb\tPASS\t2\t002-catch.scorpionb\tchat_message [backpack: gained scorpioncageab 0->1; "
    "lost scorpioncagea 1->0]",
    "3\tcatch.scorpionc\tPASS\t2\t003-catch.scorpionc\tchat_message [backpack: gained scorpioncagefull 0->1; "
    "lost scorpioncageab 1->0]",
    ""])
# The use on the Taverley scorpion written as a row named after the OUTPOST
# step: the row's name must not stand in for the use's target.
NAMED_LUA = SCORP_LUA.replace('"catch.scorpiona"', '"catchOutpostScorpion"')
NAMED_LEDGER = SCORP_LEDGER.replace("\tcatch.scorpiona\t", "\tcatchOutpostScorpion\t")
COG_LUA_COMMIT = "5e3675055"
COG_LEDGER_COMMIT = "6762eaf875"
COG_LEDGER = "osrs239-content/server/scripts/selftest/quests/quest_cog/play/ledger.tsv"


def grade_id(test_id, lua, ledger_text):
    original = helper_coverage.ledger.read
    helper_coverage.ledger.read = lambda path: parse(ledger_text)
    try:
        report = grader(test_id, lua, ledger_text).report()
    finally:
        helper_coverage.ledger.read = original
    return {s["step"]: (s["class"], s["reason"]) for s in report["steps"]}


def credits_line(got, step, line):
    return got[step][0] == "DRIVEN" and got[step][1].startswith("line %d " % line)


def b66_cases():
    """{name: (test_id, lua, ledger, check)}; check(got) -> [wrong steps]."""
    cog_lua = subprocess.run(["git", "-C", helper_coverage.REPO_ROOT, "show",
                              COG_LUA_COMMIT + ":test/quests/cog.lua"],
                             capture_output=True, text=True, check=True).stdout
    cog_ledger = subprocess.run(["git", "-C", os.path.join(helper_coverage.REPO_ROOT, "OSRS-Content"), "show",
                                 COG_LEDGER_COMMIT + ":" + COG_LEDGER],
                                capture_output=True, text=True, check=True).stdout
    assert "\tenterBasement-black\tPASS\t" in cog_ledger and "beside ladder_from_cellar 2566,9642" in cog_ledger, \
        "the cog fixture ledger no longer carries row 39's landing"

    def same_name(got):
        bad = []
        if not credits_line(got, "catchTaverleyScorpion", SCORP_LINES["a"]):
            bad.append("catchTaverleyScorpion")
        if not credits_line(got, "catchMonasteryScorpion", SCORP_LINES["c"]):
            bad.append("catchMonasteryScorpion")
        outpost = got["catchOutpostScorpion"]
        # the use on questscorpionb with cage a (the guide wants cage ac
        # there): line 7 or UNMATCHED, never another scorpion's line
        if outpost[0] == "DRIVEN" and not outpost[1].startswith("line %d " % SCORP_LINES["b"]):
            bad.append("catchOutpostScorpion")
        return bad

    def named_row(got):
        return ["catchOutpostScorpion"] if got["catchOutpostScorpion"][0] == "DRIVEN" else []

    def white_ladder(got):
        return ["climbWhiteLadder"] if "enterBasement-black" in got["climbWhiteLadder"][1] else []

    return {
        "scorp_same_name": ("scorpcatcher", SCORP_LUA, SCORP_LEDGER, same_name),
        "scorp_named_row_elsewhere": ("scorpcatcher", NAMED_LUA, NAMED_LEDGER, named_row),
        "cog_white_ladder_other_copy": ("cog", cog_lua, cog_ledger, white_ladder),
    }


def b66_symbols():
    """(name, failures): same_thing on two symbols this cache has."""
    out = []
    for kind, guide_symbol, test_string, want in (
            ("npc", "questscorpionc", "questscorpionb", False),     # same display name
            ("loc", "mourning_temple_pillar_1_1", "mourning_temple_pillar_1_10", False),  # a spelling prefix
            ("npc", "gertrudescat", "gertrude", False),
            ("npc", "questscorpionb", "questscorpionb", True)):
        got = helper_coverage.same_thing(kind, guide_symbol, test_string)
        out.append(("same_thing:%s~%s" % (guide_symbol, test_string), [] if got == want else [repr(got)]))
    return out


def run_b66(show=True):
    failures = 0
    for name, (test_id, lua, ledger_text, check) in b66_cases().items():
        got = grade_id(test_id, lua, ledger_text)
        bad = check(got)
        failures += bool(bad)
        if show:
            steps = ("climbWhiteLadder",) if test_id == "cog" else \
                ("catchTaverleyScorpion", "catchMonasteryScorpion", "catchOutpostScorpion")
            print("%-4s %-28s %s%s" % ("FAIL" if bad else "ok", name,
                                        "; ".join("%s=%s %s" % (s, got[s][0], got[s][1][:48]) for s in steps),
                                        (" -- wrong: " + ",".join(bad)) if bad else ""))
    for name, bad in b66_symbols():
        failures += bool(bad)
        if show:
            print("%-4s %s%s" % ("FAIL" if bad else "ok", name, (" -- read as " + bad[0]) if bad else ""))
    return failures, len(b66_cases()) + len(b66_symbols())


def rules_off():
    """Every b66 case must FAIL with the seam's three rules switched off (the
    reading before it): the fixtures see the bug, without editing a source."""
    saved = (helper_coverage.DISTINCT_SYMBOLS, helper_coverage.Grader.USE_ON_OWN_TARGET,
             helper_coverage.Grader.ROW_AT_POINT)
    helper_coverage.DISTINCT_SYMBOLS = False
    helper_coverage.Grader.USE_ON_OWN_TARGET = False
    helper_coverage.Grader.ROW_AT_POINT = False
    try:
        failures, total = run_b66(show=False)
    finally:
        (helper_coverage.DISTINCT_SYMBOLS, helper_coverage.Grader.USE_ON_OWN_TARGET,
         helper_coverage.Grader.ROW_AT_POINT) = saved
    # the self-match row holds either way
    return failures, total - 1


def main():
    failures = 0
    table = cases()
    for name, (lua, ledger_text, check) in table.items():
        got = grade(lua, ledger_text)
        bad = check(got)
        failures += bool(bad)
        shown = ", ".join("%s=%s" % (s, got[s][0]) for s in NOT_DRIVEN + FAMILY_DRIVEN)
        print("%-4s %-22s %s%s" % ("FAIL" if bad else "ok", name, shown, (" -- wrong: " + ",".join(bad)) if bad else ""))
    if not failures:
        print("     reason: %s" % grade(*table["as_accepted"][:2])["use208OnRazmire"][1][:200])
    extra = guide_only()
    for name, bad in extra:
        failures += bool(bad)
        print("%-4s %s%s" % ("FAIL" if bad else "ok", name, (" -- read as " + bad[0][:120]) if bad else ""))
    b66_failures, b66_total = run_b66()
    failures += b66_failures
    off_failures, off_want = rules_off()
    off_bad = off_failures != off_want
    failures += off_bad
    print("%-4s rules_off: %d of the %d b66 cases fail with DISTINCT_SYMBOLS/USE_ON_OWN_TARGET/ROW_AT_POINT off" % (
        "FAIL" if off_bad else "ok", off_failures, off_want))
    total = len(table) + len(extra) + b66_total + 1
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
