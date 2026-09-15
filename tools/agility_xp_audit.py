#!/usr/bin/env python3
"""Check every Agility course's per-lap experience against the OSRS Wiki.

The XP a course pays lives in its triggers, not in a table a script can read,
so `::agilityrun` cannot see it. This does: it parses the course scripts, sums
every experience-granting call in each one, and compares the total with the
lap total that course's own wiki page states.

Why a total and not per obstacle: the per-obstacle split is already pinned by
the scripts themselves, and the lap total is the number the wiki states in one
place and a player can measure in one lap. Four of these courses were wrong by
24-146 XP when this was written (docs/AGILITY_COMPLETION_PLAN.md §0.1), and
nothing in the tree would have noticed.

    tools/agility_xp_audit.py [--tree OSRS-Content/osrs239-content]

Exits non-zero on any mismatch, and is run by `make -C src torirsserver-scripts`.
"""

import argparse
import os
import re
import sys

# course file -> (wiki page, published lap total in XP)
#
# Every one of these totals is the sum of that page's own per-obstacle table,
# which is why it is safe to check the sum rather than each row: the page is
# internally consistent, so a wrong row here moves the total.
COURSES = {
    "skill_agility/scripts/gnome_course.rs2": (
        "Gnome_Stronghold_Agility_Course", 110.5),
    "skill_agility/scripts/rooftop_draynor.rs2": (
        "Draynor_Village_Rooftop_Course", 120.0),
    "skill_agility/scripts/rooftop_alkharid.rs2": (
        "Al_Kharid_Rooftop_Course", 216.0),
    "skill_agility/scripts/rooftop_varrock.rs2": (
        "Varrock_Rooftop_Course", 269.7),
    "skill_agility/scripts/rooftop_canifis.rs2": (
        "Canifis_Rooftop_Course", 240.0),
    "skill_agility/scripts/rooftop_falador.rs2": (
        "Falador_Rooftop_Course", 586.0),
    "skill_agility/scripts/rooftop_seers.rs2": (
        "Seers%27_Village_Rooftop_Course", 570.0),
    "skill_agility/scripts/rooftop_rellekka.rs2": (
        "Rellekka_Rooftop_Course", 780.0),
    "skill_agility/scripts/rooftop_ardougne.rs2": (
        "Ardougne_Rooftop_Course", 889.0),
    "skill_agility/scripts/barbarian_course.rs2": (
        "Barbarian_Outpost_Agility_Course", 153.3),
    "skill_agility/scripts/wilderness_course.rs2": (
        "Wilderness_Agility_Course", 571.4),
    "skill_agility/scripts/rooftop_pollnivneach.rs2": (
        "Pollnivneach_Rooftop_Course", 890.0),
    "skill_agility/scripts/shayzien_course.rs2": (
        "Shayzien_Agility_Course", 153.5),
    # 485, not the page's 508: the advanced course shares its ladder, monkey
    # bars and first tightrope with the basic one, and those 23 XP are paid by
    # shayzien_course.rs2's own triggers. Counting them here too would pay a
    # player twice for one climb.
    "skill_agility/scripts/shayzien_advanced.rs2": (
        "Shayzien_Agility_Course", 485.0),
    "skill_agility/scripts/apeatoll_course.rs2": (
        "Ape_Atoll_Agility_Course", 580.0),
    # 350, not the page's 730: the other 380 is the stick hand-in, which needs
    # the Agility Trainer NPC and a ground-spawn lifetime (A15 follow-up).
    "skill_agility/scripts/werewolf_course.rs2": (
        "Werewolf_Agility_Course", 350.0),
    "skill_agility/scripts/penguin_course.rs2": (
        "Penguin_Agility_Course", 540.0),
    # Both Colossal Wyrm routes live in one file because they share three
    # obstacles and the zip line. The expected sum is the basic lap (601.6)
    # plus the four advanced-only obstacles (280) plus the zip line's advanced
    # top-up (320.8, which takes its 341.2 to the published 662) = 1,202.4.
    "skill_agility/scripts/wyrm_course.rs2": (
        "Colossal_Wyrm_Agility_Course", 1202.4),
    "skill_agility/scripts/prif_course.rs2": (
        "Prifddinas_Agility_Course", 1285.2),
    # 159 of obstacles, not the page's 2,750: this course pays per obstacle
    # used rather than per lap, and the other 2,432 is Turgall's delivery,
    # which is a hand-in rather than an obstacle. Both halves are in the file;
    # only the obstacle half is a per-lap sum.
    "skill_agility/scripts/dorgesh_course.rs2": (
        "Dorgesh-Kaan_Agility_Course", 159.0),
    # 206.8 = one of each obstacle, which is what this file contains. The
    # wiki's 722 "per lap" is the whole spiral climb, where each obstacle type
    # is used three or four times on the way up; the course pays per use, so a
    # full climb earns the 722 without any trigger paying it. The doorway's
    # bonus (300 + level x 8, capped at 1,000) is arithmetic rather than a
    # literal and cannot be summed here either.
    "skill_agility/scripts/pyramid_course.rs2": (
        "Agility_Pyramid", 206.8),
}

# Courses that also pay a second skill on lap completion. Barbarian Outpost is
# the only one in the game, and its Strength half is exactly the kind of thing
# that goes missing in a port without being noticed.
OFF_SKILL = {
    "skill_agility/scripts/barbarian_course.rs2": ("strength", 41.3),
}

# Experience a course pays that is NOT per-lap obstacle experience, and so is
# not part of the total the wiki states per lap. Dorgesh-Kaan's delivery is the
# only one: 2,432 for handing Turgall a part, which is a hand-in and can happen
# without touching an obstacle at all.
#
#   file -> [xp_tenths_to_ignore]
NOT_LAP_XP = {
    "skill_agility/scripts/dorgesh_course.rs2": [24320],
}

# Obstacles that appear ONCE in the script but are run more than once per lap,
# because several placements share one trigger. Barbarian Outpost's three
# crumbling walls are one `[oploc1,castlecrumbly1]` block: summing the literals
# would count 13.7 once instead of three times and report the course 27.4 short.
#
#   file -> [(xp_tenths_of_the_shared_call, times_run_per_lap)]
SHARED_TRIGGERS = {
    "skill_agility/scripts/barbarian_course.rs2": [(137, 3)],
    # Werewolf's three hurdle rows are one `[oploc1,werewolf_hurdle_*]` block,
    # jumped once each per lap: 3 x 20 is the wiki's single "Hurdle 60".
    "skill_agility/scripts/werewolf_course.rs2": [(200, 3)],
    # Penguin's four icicle pillars share one trigger, 40 each.
    "skill_agility/scripts/penguin_course.rs2": [(400, 4)],
    # Dorgesh-Kaan's route crosses a cable-walk and a cable-swing twice each.
    "skill_agility/scripts/dorgesh_course.rs2": [(250, 2), (220, 2)],
}

# Every way a course script grants Agility experience. `~agility_force_move`
# and `~agility_climb_up` take it as their first argument; a course that grants
# XP some third way must be added here or its total silently under-reports,
# which is why the unknown-helper check below exists.
XP_PATTERNS = (
    re.compile(r"stat_advance\(agility,\s*(\d+)\)"),
    re.compile(r"~agility_force_move\((\d+)"),
    re.compile(r"~agility_climb_up\((\d+)"),
)

# Helpers that are known NOT to grant experience of their own. Anything else
# starting `~agility_` inside a course file is reported rather than assumed
# free, because a new XP-granting helper is exactly how a total drifts.
KNOWN_FREE_HELPERS = {
    "agility_exactmove",
    "agility_delay_fail",
    "agility_lap_advance",
    "agility_lap_complete",
    "agility_lap_reset",
    "agility_mark_roll",
    "agility_pet_roll",
    "agility_pet_base",
    "agility_pet_denominator",
    "agility_success",
    "agility_success_chance",
    "agility_success_stop_level",
    "agility_sound_for_seq",
    "agility_walk",
}
XP_HELPERS = {"agility_force_move", "agility_climb_up"}

HELPER_CALL = re.compile(r"~(agility_[a-z0-9_]+)\(")


def course_xp_tenths(text):
    return sum(int(m) for pattern in XP_PATTERNS for m in pattern.findall(text))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", default="OSRS-Content/osrs239-content")
    args = parser.parse_args()

    scripts = os.path.join(args.tree, "server", "scripts")
    failures = 0

    for rel, (page, want) in sorted(COURSES.items()):
        path = os.path.join(scripts, rel)
        if not os.path.exists(path):
            print("agility_xp_audit: missing %s" % path, file=sys.stderr)
            failures += 1
            continue
        text = open(path, encoding="utf-8").read()

        for helper in sorted(set(HELPER_CALL.findall(text))):
            if helper not in KNOWN_FREE_HELPERS and helper not in XP_HELPERS:
                print(
                    "agility_xp_audit: %s calls unknown helper ~%s — if it grants "
                    "experience, add it to XP_PATTERNS; if not, to "
                    "KNOWN_FREE_HELPERS" % (rel, helper),
                    file=sys.stderr,
                )
                failures += 1

        if rel in OFF_SKILL:
            skill, want_off = OFF_SKILL[rel]
            got_off = sum(
                int(m) for m in re.findall(
                    r"stat_advance\(%s,\s*(\d+)\)" % skill, text)) / 10.0
            if abs(got_off - want_off) > 0.001:
                print(
                    "agility_xp_audit: %s pays %.1f %s xp per lap, wiki says %.1f"
                    % (rel, got_off, skill, want_off),
                    file=sys.stderr,
                )
                failures += 1

        tenths = course_xp_tenths(text)
        for value in NOT_LAP_XP.get(rel, ()):
            if ("stat_advance(agility, %d)" % value) not in text:
                print(
                    "agility_xp_audit: %s declares a non-lap award of %d tenths "
                    "that the script no longer contains" % (rel, value),
                    file=sys.stderr,
                )
                failures += 1
                continue
            tenths -= value
        for value, times in SHARED_TRIGGERS.get(rel, ()):
            if ("stat_advance(agility, %d)" % value) not in text:
                print(
                    "agility_xp_audit: %s declares a shared trigger paying %d "
                    "tenths that the script no longer contains" % (rel, value),
                    file=sys.stderr,
                )
                failures += 1
                continue
            tenths += value * (times - 1)
        got = tenths / 10.0
        if abs(got - want) > 0.001:
            print(
                "agility_xp_audit: %s pays %.1f xp per lap, wiki says %.1f "
                "(https://oldschool.runescape.wiki/w/%s)" % (rel, got, want, page),
                file=sys.stderr,
            )
            failures += 1

    if failures:
        print("agility_xp_audit: %d problem(s)" % failures, file=sys.stderr)
        return 1
    print("agility_xp_audit: %d courses match their published lap totals" % len(COURSES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
