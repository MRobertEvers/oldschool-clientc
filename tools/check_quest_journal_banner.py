#!/usr/bin/env python3
"""Every quest journal must carry the QUEST COMPLETE! banner.

A quest journal is a script body that calls `~quest_journal(<name>, $text)`
(interface_questjournal/scripts/quest_journal.rs2). Its done branch ends with
the red banner:

    $text = append($text, ^journal_complete);      // "<col=ff0000>"
    $text = append($text, "QUEST COMPLETE!");

LostCity_Content2 writes it in every one of its 57 quest journals
(scripts/quests/**/*_journal.rs2, "@red@QUEST COMPLETE!"), and the OSRS quest
journal shows it on every completed quest. A journal without it reads as
unfinished after a real completion, and the quest driver's quest.journal row
(script/plugins/quest_driver/ui.lua QD.ui.journal_read, which looks for
"QUEST COMPLETE") FAILs: Recruitment Drive (seam24) and Recipe for Disaster's
overview did exactly that.

The sweep is per script body: a [proc]/[label]/... that calls ~quest_journal(
must contain the literal "QUEST COMPLETE!" somewhere in its body (a journal
that returns early per stage calls ~quest_journal once per branch; the banner
lives in the done one). Journals that are not quest journals are listed in
NOT_A_QUEST_JOURNAL with the reason.

Exit status: 0 when every quest journal carries the banner, 1 otherwise.

  python3 tools/check_quest_journal_banner.py [--brief] [--scripts DIR]
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(REPO, "OSRS-Content", "osrs239-content", "server", "scripts")

BANNER = "QUEST COMPLETE!"
CALL = "~quest_journal("

# (relative path, script header) -> why it is not a quest journal.
NOT_A_QUEST_JOURNAL = {
    ("interface_questjournal/scripts/quest_journal.rs2", "[proc,quest_journal_unwritten]"):
        "placeholder for a quest this world does not run",
    ("skill_construction/scripts/poh_achievement_gallery.rs2", "[oploc1,poh_quest_list]"):
        "POH quest list reuses the journal interface",
    ("skill_construction/scripts/poh_achievement_gallery.rs2", "[label,poh_adventure_log_read]"):
        "POH adventure log reuses the journal interface",
    ("skill_construction/scripts/poh_achievement_gallery.rs2", "[debugproc,pohslayerlog]"):
        "POH slayer kill log reuses the journal interface",
}

HEADER = re.compile(r"(?m)^(\[[a-z_0-9]+,[^\]]+\])")


def sweep(scripts):
    checked = 0
    misses = []
    seen_exempt = set()
    for root, dirs, files in os.walk(scripts):
        dirs[:] = sorted(d for d in dirs if d != "build")
        for name in sorted(files):
            if not name.endswith(".rs2"):
                continue
            path = os.path.join(root, name)
            rel = os.path.relpath(path, scripts)
            with open(path, encoding="utf-8", errors="replace") as handle:
                source = handle.read()
            if CALL not in source:
                continue
            parts = HEADER.split(source)
            line = 1 + parts[0].count("\n")
            for index in range(1, len(parts), 2):
                header = parts[index]
                body = parts[index + 1]
                header_line = line
                line += header.count("\n") + body.count("\n")
                if CALL not in body:
                    continue
                if (rel, header) in NOT_A_QUEST_JOURNAL:
                    seen_exempt.add((rel, header))
                    continue
                checked += 1
                if BANNER not in body:
                    misses.append((rel, header_line, header))
    stale = sorted(set(NOT_A_QUEST_JOURNAL) - seen_exempt)
    return checked, misses, stale


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--brief", action="store_true")
    parser.add_argument("--scripts", default=SCRIPTS,
                        help="script tree to sweep (default: the osrs239 pack)")
    args = parser.parse_args()
    checked, misses, stale = sweep(args.scripts)
    for rel, line, header in misses:
        print("check-quest-journal-banner: %s:%d %s calls ~quest_journal "
              "with no \"%s\" banner in its done branch" % (rel, line, header, BANNER))
    for rel, header in stale:
        print("check-quest-journal-banner: NOT_A_QUEST_JOURNAL entry %s %s "
              "no longer calls ~quest_journal; remove it" % (rel, header))
    status = 1 if (misses or stale) else 0
    if not args.brief or status:
        print("check-quest-journal-banner: %d quest journals, %d without the banner%s"
              % (checked, len(misses), ", %d stale exemptions" % len(stale) if stale else ""))
    else:
        print("check-quest-journal-banner: %d quest journals, 0 without the banner" % checked)
    return status


if __name__ == "__main__":
    sys.exit(main())
