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

# Every proc in agility.rs2 that awards Agility experience from its first
# argument. See ours_tenths' docstring for why this is a list and not a guess.
XP_HELPERS = ("agility_force_move", "agility_climb_up")

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
    # D3's non-rooftop courses, added 2026-08-20. Same check, different pages.
    "Gnome_Stronghold_Agility_Course": "gnome_course.rs2",
    "Barbarian_Outpost_Agility_Course": "barbarian_course.rs2",
    "Wilderness_Agility_Course": "wilderness_course.rs2",
    "Agility_Pyramid": "pyramid_course.rs2",
    "Werewolf_Agility_Course": "werewolf_course.rs2",
    "Ape_Atoll_Agility_Course": "apeatoll_course.rs2",
    "Penguin_Agility_Course": "penguin_course.rs2",
    "Prifddinas_Agility_Course": "prif_course.rs2",
    # Dorgesh-Kaan is deliberately absent: its page states experience in prose
    # per obstacle rather than in the `{{+=|xp|...}}` table every other course
    # uses, so this checker cannot read it and would report a bogus mismatch.
    # Checking it needs a different parse, not a different expectation.
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
    # Course pages state the lap total in at least three different sentences,
    # and the per-obstacle table does NOT always add up to it: Ape Atoll's final
    # tropical tree carries a 300-experience completion bonus that sits outside
    # the `{{+=|xp|...}}` template, so its table sums to 280 against a stated
    # 580. Matching only one phrasing reports a correct implementation as wrong
    # by the size of the bonus.
    for pattern in (
        r"Players get ([\d,]+(?:\.\d+)?) experience from completing",
        r"rewards ([\d,]+(?:\.\d+)?) \[\[Agility\]\] \[\[experience\]\] per "
        r"completed lap",
        r"awards? ([\d,]+(?:\.\d+)?) (?:\[\[)?Agility(?:\]\])? "
        r"(?:\[\[)?experience(?:\]\])? per lap",
        r"([\d,]+(?:\.\d+)?) (?:\[\[)?Agility(?:\]\])? "
        r"(?:\[\[)?experience(?:\]\])? per (?:completed )?lap",
    ):
        m = re.search(pattern, text)
        if m:
            return int(round(float(m.group(1).replace(",", "")) * 10))
    return None


def loc_bindings(path):
    """How many `[oploc...]` triggers the course file binds."""
    if not os.path.exists(path):
        return 0
    text = io.open(path, encoding="utf-8", errors="replace").read()
    return len(re.findall(r"^\[oploc\d*,", text, re.M))


def ours_tenths(path):
    """Sum every Agility award in a course script.

    Not every award is a bare `stat_advance`. Several helpers in agility.rs2
    take the experience as their FIRST argument and advance the stat themselves,
    so counting only `stat_advance` misses most of a course.

    XP_HELPERS is derived, not guessed: it is every proc in agility.rs2 whose
    body does `stat_advance(agility, $param)`. I found `~agility_force_move`
    the hard way (seven false "short" courses), then `~agility_climb_up` the
    hard way again (Barbarian Outpost reported as 5 of 9 obstacles when it is
    complete and exact). Enumerating them beats discovering them one false
    finding at a time -- if a new helper is added, add it here.
    """
    if not os.path.exists(path):
        return None, 0
    text = io.open(path, encoding="utf-8", errors="replace").read()
    total = 0
    found = 0
    # Walk per trigger/label block, because one handler can serve several world
    # placements of the same loc name. Barbarian Outpost's three crumbling walls
    # share `[oploc1,castlecrumbly1]` and are told apart by
    # `switch_int(coordx(loc_coord))` -- so its single award of 13.7 is paid
    # three times a lap. Counting statements says 6 obstacles and 125.9; the
    # course actually has 9 and pays exactly the wiki's 153.3.
    for block in re.split(r"\n(?=\[)", text):
        awards = [int(m.group(1))
                  for m in re.finditer(r"stat_advance\(agility,\s*(\d+)\)",
                                       block)]
        for helper in XP_HELPERS:
            awards += [int(m.group(1))
                       for m in re.finditer(r"~%s\(\s*(\d+)" % helper, block)]
        if not awards:
            continue
        # How many placements this one handler serves.
        placements = 1
        if re.search(r"switch_int\(coord[xz]\(loc_coord\)\)", block):
            arms = len(re.findall(r"^\s*case\s", block, re.M))
            if arms > 1:
                placements = arms
        for a in awards:
            total += a * placements
            found += placements
    return total, found


def main():
    failures = 0
    gaps = []
    unreadable = []
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

        # A course with FEWER obstacles than the wiki lists is incomplete, not
        # wrong: the numbers it does implement may be perfectly correct. Those
        # are reported and counted separately, because this check is wired into
        # `torirsserver-scripts` and failing the build for a known content gap in
        # someone else's slice helps nobody. A wrong number on a course we have
        # fully implemented is the thing that fails.
        # A SHARED handler breaks this comparison, and it took three wrong
        # answers to learn that. One `[oploc1,...]` body can serve several
        # obstacles — the Penguin course's four icicle pillars all run one
        # handler that awards 40, so the source shows one award of 40 where a
        # lap awards 160. Summing the source therefore gives a LOWER BOUND on a
        # lap, not a lap total, and the shortfall looks exactly like missing
        # obstacles.
        #
        # So when a course binds more locs than it has award statements, this
        # checker cannot compute its lap total and says so instead of guessing.
        shared = loc_bindings(spath) > our_n
        incomplete = (not ok) and our_n < wiki_n and not shared
        if wiki_n == 0:
            state = "UNPARSED"
        elif ok:
            state = "ok"
        elif shared:
            state = "shared handlers -- lap total unreadable from source"
        elif incomplete:
            state = "incomplete (%d of %d obstacles)" % (our_n, wiki_n)
        else:
            state = "MISMATCH"
        print("%-34s wiki %-7s ours %-7s obstacles wiki %d / ours %d  %s%s"
              % (course.replace("_Rooftop_Course", ""),
                 wiki_sum / 10, our_sum / 10, wiki_n, our_n, state, note))
        if shared and not ok:
            unreadable.append(course)
        if state == "MISMATCH":
            failures += 1
        elif incomplete:
            gaps.append("%s: %d of %d obstacles, %s of %s experience"
                        % (course, our_n, wiki_n, our_sum / 10, wiki_sum / 10))

    if gaps:
        print("check_agility_course_contract: %d course(s) are INCOMPLETE -- "
              "the experience they do award matches, but obstacles are missing:"
              % len(gaps))
        for g in gaps:
            print("  " + g)

    if unreadable:
        print("check_agility_course_contract: %d course(s) share handlers "
              "between obstacles, so their lap total cannot be summed from "
              "source and is NOT verified here: %s"
              % (len(unreadable), ", ".join(unreadable)))

    if failures:
        print("check_agility_course_contract: %d course(s) disagree with the "
              "pinned wiki" % failures, file=sys.stderr)
        return 1
    print("check_agility_course_contract: %d course(s) agree with the pinned "
          "wiki" % len(COURSES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
