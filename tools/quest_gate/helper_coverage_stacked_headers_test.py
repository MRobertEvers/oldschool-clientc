#!/usr/bin/env python3
"""Fixture test: STACKED trigger headers share the body below the last of them.

    python3 tools/quest_gate/helper_coverage_stacked_headers_test.py

Ethically Acquired Antiquities (matthew-mbp-m4-b71). On a witness-roll-1 run
the grader called talkToAcademic and talkToTourist CONTENT_GAP:
"[opnpc1,fortis_academic_01] ... never reads %varb11193_eaa -- talking to them
cannot advance this quest". The content is

    [opnpc1,fortis_academic_01]
    [opnpc1,fortis_academic_02]
    ...
    @eaa_academic_talk;

and [label,eaa_academic_talk] reads the stage. reaches_var already followed
@labels and ~procs; what it never saw was the `@`, because every body reader
stopped at the first `[` -- the next header of the stack -- so every header but
the last read an empty body. The compiler aliases them (ssc_compile.c
alias_script), so the content means what it says. Cases:

  stack_first      body_lines for the first, middle and last header of a stack
                   are the same one-line body
  stack_gaps       blank and comment lines between stacked headers are skipped
  own_comment      a single header keeps a comment that opens its own body
  empty_last       a trailing header with no body reads empty
  reaches_first    reaches_var on the first header of a stack follows its @label
  reaches_no       ...and still says no when the label never names the var
  unconditional    trigger_is_unconditional reads the shared body
  eaa_live         the real content: [opnpc1,fortis_academic_01] and
                   [opnpc1,varlamore_tourist_m_1] reach %varb11193_eaa (skipped
                   when the OSRS-Content checkout is absent)

Writes only temporary files. Exit 0 when every case holds.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

FIXTURE = """\
// a fixture quest
[opnpc1,academic_01]
[opnpc1,academic_02]

// the third academic
[opnpc1,academic_03]
@fixture_academic_talk;

[label,fixture_academic_talk]
if (%fixture_stage ! 4) {
    ~chatnpc("Fascinating exhibits.");
    return;
}
%fixture_stage = 6;

[opnpc1,bystander_01]
[opnpc1,bystander_02]
@fixture_bystander_talk;

[label,fixture_bystander_talk]
~chatnpc("Busy day.");

[oploc1,door_left]
[oploc1,door_right]
p_telejump(0_50_50_10_10);

[opnpc1,single]
// opens its own body
~chatnpc("Hello.");

[opnpc1,trailing]
"""

FAILED = []


def check(name, condition, detail=""):
    print("%-14s %s%s" % (name, "ok" if condition else "FAIL", "" if condition else "  " + detail))
    if not condition:
        FAILED.append(name)


def reset(content_root):
    helper_coverage.CONTENT_ROOT = content_root
    helper_coverage._CONTENT_INDEX = None
    helper_coverage.SCRIPTS.clear()
    helper_coverage._BODIES.clear()


def header_line(lines, header):
    return lines.index(header) + 1


def main():
    real_root = helper_coverage.CONTENT_ROOT
    lines = FIXTURE.split("\n")

    first = helper_coverage.body_lines(lines, header_line(lines, "[opnpc1,academic_01]"))
    middle = helper_coverage.body_lines(lines, header_line(lines, "[opnpc1,academic_02]"))
    last = helper_coverage.body_lines(lines, header_line(lines, "[opnpc1,academic_03]"))
    check("stack_first", first == middle == last and first[0] == "@fixture_academic_talk;",
          "%r / %r / %r" % (first, middle, last))
    check("stack_gaps", "// the third academic" not in first, repr(first))
    single = helper_coverage.body_lines(lines, header_line(lines, "[opnpc1,single]"))
    check("own_comment", single[:2] == ["// opens its own body", '~chatnpc("Hello.");'], repr(single))
    trailing = helper_coverage.body_lines(lines, header_line(lines, "[opnpc1,trailing]"))
    check("empty_last", not "".join(trailing).strip(), repr(trailing))

    with tempfile.TemporaryDirectory() as scratch:
        root = os.path.join(scratch, "server", "scripts")
        os.makedirs(os.path.join(root, "quests", "quest_fixture"))
        rel = os.path.join("quests", "quest_fixture", "fixture.rs2")
        with open(os.path.join(root, rel), "w", encoding="utf-8") as handle:
            handle.write(FIXTURE)
        reset(root)
        try:
            check("reaches_first",
                  helper_coverage.reaches_var(rel, header_line(lines, "[opnpc1,academic_01]"), "fixture_stage"))
            check("reaches_no",
                  not helper_coverage.reaches_var(rel, header_line(lines, "[opnpc1,bystander_01]"),
                                                  "fixture_stage"))
            check("unconditional",
                  helper_coverage.trigger_is_unconditional(rel, header_line(lines, "[oploc1,door_left]")))
        finally:
            reset(real_root)

    eaa = os.path.join("quests", "quest_ethicallyacquiredantiquities", "scripts",
                       "ethicallyacquiredantiquities.rs2")
    if not os.path.isfile(os.path.join(real_root, eaa)):
        print("%-14s skipped (no OSRS-Content checkout)" % "eaa_live")
    else:
        with open(os.path.join(real_root, eaa), "r", encoding="utf-8") as handle:
            live = handle.read().split("\n")
        for header in ("[opnpc1,fortis_academic_01]", "[opnpc1,varlamore_tourist_m_1]"):
            check("eaa_live", helper_coverage.reaches_var(eaa, header_line(live, header), "varb11193_eaa"), header)

    print("FAILED: %s" % ", ".join(FAILED) if FAILED else "all cases hold")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
