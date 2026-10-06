#!/usr/bin/env python3
"""Fixture test: lint_quest.py refuses a `::bankgive` anywhere outside setup
and an UNMARKED, un-baselined `::give` outside setup (check_mid_run_gives).

    python3 tools/quest_gate/lint_quest_mid_run_give_test.py
    python3 tools/quest_gate/lint_quest_mid_run_give_test.py --rule-off

The owner's rule (docs/QUEST_ORCHESTRATOR.md): no `::give` of an item the guide
has the player obtain, and no `::give` after setup. `::bankgive` (cheat_bank.rs2)
is refused by the driver only once t.quest.bind has run, so one written in run()
before the bind reaches the server: the lint is its static twin.

Each case is a tiny synthetic quest file linted through lint_text, the path
`lint_quest.py <file>` takes; only this rule's findings are counted (their
messages say "outside setup", "kit-give" or "mid_run_gives_baseline"). The
baseline is a fixture dict in place of mid_run_gives_baseline.tsv.

`--rule-off` replaces check_mid_run_gives with a no-op in this process only
(no source is edited): every case that expects a finding must FAIL there,
which is what proves the cases test the rule. Exit 0 when every case holds.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import lint_quest  # noqa: E402

TEST_ID = "midrungive_fixture"


def quest(setup, body, test_id=TEST_ID):
    return """return {
    id = "%s",
    setup = { "::clearinv", %s },
    run = function(t)
%s
    end,
}
""" % (test_id, setup, body)


BIND = '        t.quest.bind{ row = "quest_cook", varp = "varp29_cookquest" }\n'

LEGS = """return {
    id = "%s",
    setup = { "::clearinv", "::give egg 1", "::bankgive shark 5" },
    bind = { row = "quest_cook", varp = "varp29_cookquest" },
    legs = {
        { name = "one", run = function(t)
            t.check("one.end", true, "a quiet boundary")
        end },
        { name = "two", run = function(t)
            t.cheat("::give pot_flour 1")
            t.finish()
        end },
    },
}
""" % TEST_ID

# (name, text, test_id, baseline {test_id: {cheat: count}}, findings this rule must make)
CASES = [
    ("setup_only", quest('"::give egg 1", "::bankgive shark 5"', BIND), TEST_ID, {}, 0),
    ("run_give", quest('"::cook"', BIND + '        t.cheat("::give egg 1")'), TEST_ID, {}, 1),
    ("run_give_concat", quest('"::cook"', '        local item = "egg"\n'
                                         '        t.cheat("::give " .. item .. " 1")'), TEST_ID, {}, 1),
    ("run_give_in_exec", quest('"::cook"', '        t.exec("give.egg", t.cheat, "::give egg 1")'),
     TEST_ID, {}, 1),
    ("run_give_local_helper", quest('"::cook"', '        local function cheat(c) return t.cheat(c) end\n'
                                               '        cheat("::give egg 1")'), TEST_ID, {}, 1),
    ("bankgive_before_bind", quest('"::cook"', '        t.cheat("::bankgive shark 5")\n' + BIND),
     TEST_ID, {}, 1),
    ("bankgive_marked", quest('"::cook"', BIND + '        t.cheat("::bankgive shark 5") '
                                                '-- lint: kit-give leg two food'), TEST_ID, {}, 1),
    ("give_marked_same_line", quest('"::cook"', BIND + '        t.cheat("::give shark 5") '
                                                      '-- lint: kit-give leg two restages its food'),
     TEST_ID, {}, 0),
    ("give_marked_line_above", quest('"::cook"', BIND + '        -- lint: kit-give leg two restages its food\n'
                                                       '        t.cheat("::give shark 5")'), TEST_ID, {}, 0),
    ("marker_without_reason", quest('"::cook"', BIND + '        t.cheat("::give shark 5") -- lint: kit-give'),
     TEST_ID, {}, 1),
    ("marker_covers_nothing", quest('"::cook"', BIND + '        -- lint: kit-give food\n'
                                                      '        t.ticks(1)\n'
                                                      '        t.cheat("::give shark 5")'), TEST_ID, {}, 2),
    ("detail_text_quotes_cheat", quest('"::give clay 6"', BIND + '        local r = "ok"\n'
                                       '        t.check("clay", r == "ok", "::give clay 6 -> " .. tostring(r))'),
     TEST_ID, {}, 0),
    ("comment_quotes_cheat", quest('"::cook"', BIND + '        -- an old version did t.cheat("::give egg 1")'),
     TEST_ID, {}, 0),
    ("legs_give_in_a_leg", LEGS, TEST_ID, {}, 1),
    ("baselined", quest('"::cook"', BIND + '        t.cheat("::give egg 1")'), TEST_ID,
     {TEST_ID: {"::give egg 1": 1}}, 0),
    ("baseline_exhausted", quest('"::cook"', BIND + '        t.cheat("::give egg 1")\n'
                                                   '        t.cheat("::give egg 1")'), TEST_ID,
     {TEST_ID: {"::give egg 1": 1}}, 1),
    ("baseline_other_cheat", quest('"::cook"', BIND + '        t.cheat("::give egg 2")'), TEST_ID,
     {TEST_ID: {"::give egg 1": 1}}, 2),
    ("baseline_stale", quest('"::cook"', BIND + '        t.ticks(1)'), TEST_ID,
     {TEST_ID: {"::give egg 1": 1}}, 1),
    ("baseline_never_excuses_bankgive", quest('"::cook"', BIND + '        t.cheat("::bankgive egg 1")'), TEST_ID,
     {TEST_ID: {"::bankgive egg 1": 1}}, 2),
    ("harness_file", quest('"::cook"', '        t.cheat("::bankgive shark 5")\n'
                                      '        t.cheat("::give egg 1")', "_fixture"), "_fixture", {}, 0),
]

RULE_WORDS = ("outside setup", "kit-give", "mid_run_gives_baseline")


def rule_findings(text, test_id, baseline):
    lint_quest._give_baseline_cache = baseline
    findings = lint_quest.lint_text(text, allow_check=True, packs=None, test_id=test_id)
    return [(line, message) for line, message in findings if any(w in message for w in RULE_WORDS)]


def main():
    if "--rule-off" in sys.argv[1:]:
        lint_quest.check_mid_run_gives = lambda *args, **kwargs: []
    failures = 0
    for name, text, test_id, baseline, want in CASES:
        got = rule_findings(text, test_id, baseline)
        ok = len(got) == want
        failures += not ok
        shown = "; ".join("%d: %s" % (line, message[:60]) for line, message in got)
        print("%-4s %-32s want=%d got=%d %s" % ("ok" if ok else "FAIL", name, want, len(got), shown[:120]))
    print("%d/%d%s" % (len(CASES) - failures, len(CASES),
                       " (check_mid_run_gives switched off: every case wanting a finding must FAIL)"
                       if "--rule-off" in sys.argv[1:] else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
