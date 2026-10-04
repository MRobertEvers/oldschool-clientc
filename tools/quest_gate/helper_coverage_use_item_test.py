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
    total = len(table) + len(extra)
    print("%d/%d" % (total - failures, total))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
