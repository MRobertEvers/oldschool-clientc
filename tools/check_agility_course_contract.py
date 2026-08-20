#!/usr/bin/env python3
"""Hold the rooftop courses to the experience the wiki publishes.

Plan: docs/NEAR_REALITY_CONTENT_QUEUE.md slices D1/D3.

All nine rooftop courses were already implemented in this tree. This checks the
numbers rather than the presence: each course page publishes a per-obstacle
experience table AND a stated total per lap, and the two are independent facts
that must agree with our `stat_advance(agility, ...)` calls.

The comparison is in TENTHS. Rooftop obstacles award fractions -- the Draynor
wall is 5 xp but Al Kharid's rope swing is 8.5 -- and an integer comparison
would call a wrong value right.

What this cannot check: which obstacle got which award. It sums the course. A
port that swapped two obstacles' experience passes here and is still wrong, so
the sum is a floor on correctness, not a proof of it.
"""
import glob
import io
import os
import re
import sys

WIKI = "docs/skills/agility/sources"
SCRIPTS = "OSRS-Content/osrs239-content/server/scripts/skill_agility/scripts"

COURSES = {
    "Draynor_Village_Rooftop_Course": "rooftop_draynor.rs2",
    "Al_Kharid_Rooftop_Course": "rooftop_alkharid.rs2",
    "Varrock_Rooftop_Course": "rooftop_varrock.rs2",
    "Canifis_Rooftop_Course": "rooftop_canifis.rs2",
    "Falador_Rooftop_Course": "rooftop_falador.rs2",
    "Seers_Village_Rooftop_Course": "rooftop_seers.rs2",
    "Pollnivneach_Rooftop_Course": "rooftop_pollnivneach.rs2",
    "Rellekka_Rooftop_Course": "rooftop_rellekka.rs2",
    "Ardougne_Rooftop_Course": "rooftop_ardougne.rs2",
}


def wiki_obstacle_tenths(path):
    """Per-obstacle experience, taking the BASE award where two are listed.

    Two courses -- Rellekka and Pollnivneach -- pay more on the final obstacle
    once the matching Achievement Diary is done, and the wiki writes that as

        |475 (without diary)
        {{+=|xp|615|echo=2}} (with diary)

    Only the WITH-diary figure is inside the template, so a naive sweep of
    `{{+=|xp|...}}` silently reads the diary-boosted number as the base one and
    reports a correct implementation as 140 experience short. The base is the
    plain number on the preceding line.
    """
    text = io.open(path, encoding="utf-8", errors="replace").read()
    total = 0
    found = 0
    diary_rows = 0
    lines = text.split("\n")
    for i, line in enumerate(lines):
        m = re.search(r"\{\{\+=\|xp\|(\d+(?:\.\d+)?)", line)
        if not m:
            continue
        value = float(m.group(1))
        if "with diary" in line and i > 0:
            base = re.match(r"\|(\d+(?:\.\d+)?)\s*\(without diary\)",
                            lines[i - 1].strip())
            if base:
                value = float(base.group(1))
                diary_rows += 1
        total += int(round(value * 10))
        found += 1
    return total, found, diary_rows


def wiki_stated_total(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"Players get ([\d,]+(?:\.\d+)?) experience from completing",
                  text)
    if not m:
        return None
    return int(round(float(m.group(1).replace(",", "")) * 10))


def ours_tenths(path):
    """Sum every Agility award in a course script.

    Not every award is a bare `stat_advance`. `~agility_force_move` takes the
    experience as its FIRST argument and advances the stat itself, so counting
    only `stat_advance` misses most of a course -- my first run reported seven
    of nine courses short, and every one of those was this. A checker that is
    wrong about the tree is worse than no checker: it manufactures work.
    """
    if not os.path.exists(path):
        return None, 0
    text = io.open(path, encoding="utf-8", errors="replace").read()
    total = 0
    found = 0
    for m in re.finditer(r"stat_advance\(agility,\s*(\d+)\)", text):
        total += int(m.group(1))
        found += 1
    for m in re.finditer(r"~agility_force_move\(\s*(\d+)", text):
        total += int(m.group(1))
        found += 1
    return total, found


def main():
    failures = 0
    for course, script in sorted(COURSES.items()):
        wpath = os.path.join(WIKI, course + ".wiki")
        spath = os.path.join(SCRIPTS, script)
        if not os.path.exists(wpath):
            print("check_agility_course_contract: %s is not pinned" % course,
                  file=sys.stderr)
            failures += 1
            continue
        wiki_sum, wiki_n, diary_rows = wiki_obstacle_tenths(wpath)
        stated = wiki_stated_total(wpath)
        our_sum, our_n = ours_tenths(spath)
        if our_sum is None:
            print("check_agility_course_contract: %s has no script (%s)"
                  % (course, script), file=sys.stderr)
            failures += 1
            continue

        # The wiki's own two figures first: a per-obstacle table that does not
        # sum to the stated total means the page changed under us and neither
        # number can be trusted as the reference.
        note = ""
        if diary_rows:
            note = " [%d diary-boosted obstacle(s); compared against the base]" % diary_rows
        if stated is not None and wiki_sum and stated != wiki_sum:
            note = " [wiki self-inconsistent: table %s vs stated %s]" % (
                wiki_sum / 10, stated / 10)

        ok = our_sum == wiki_sum or (stated is not None and our_sum == stated)
        print("%-34s wiki %-7s ours %-7s obstacles wiki %d / ours %d  %s%s"
              % (course.replace("_Rooftop_Course", ""),
                 wiki_sum / 10, our_sum / 10, wiki_n, our_n,
                 "ok" if ok else "MISMATCH", note))
        if not ok:
            failures += 1

    if failures:
        print("check_agility_course_contract: %d course(s) disagree with the "
              "pinned wiki" % failures, file=sys.stderr)
        return 1
    print("check_agility_course_contract: %d course(s) agree with the pinned "
          "wiki" % len(COURSES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
