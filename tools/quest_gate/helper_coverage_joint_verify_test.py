#!/usr/bin/env python3
"""Fixture test: a BRANCH-IN sibling that is not green vouches only while both
siblings are in the SAME verify round.

    python3 tools/quest_gate/helper_coverage_joint_verify_test.py

Throne of Miscellania (matthew-mbp-m4-b59): misc courts Brand and misc_astrid
courts Astrid, and each names the other in its BRANCH-IN markers. Both were
reopened for the door rule in one batch. Each fixer came back with fail=0 and
no CHEAT row, and each gate was RED on "sibling ... is todo, not green": the
rule asked each row to wait for the other. helper_coverage.joint_verify lets a
sibling vouch when the orchestrator has marked it VERIFY ONLY for the batch
this test is in. Cases, on QUEUE rows alone (the sibling's own grading still
has to read the step DRIVEN; verify_equivalent checks that after this):

  both_verify_only     both todo, VERIFY ONLY, one batch          -> vouches
  self_green           this row already green, sibling VERIFY ONLY -> vouches
  sibling_plain_todo   sibling todo with a reopen note             -> refused
  sibling_sent_back    sibling todo, REVERTED by the sampler       -> refused
  other_batch          sibling VERIFY ONLY for another batch       -> refused
  no_owner             sibling VERIFY ONLY, no owner               -> refused
  self_plain_todo      this row todo with a reopen note            -> refused
  sibling_blocked      sibling blocked, note says VERIFY ONLY      -> refused

Writes no files. Exit 0 when every case holds.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.append(HERE)

import helper_coverage  # noqa: E402

BATCH = "matthew-mbp-m4-b59"
VERIFY = "VERIFY ONLY (%s; expected outcome GREEN): an Opus fixer's finished file" % BATCH
REOPEN = "RE-AUTHOR after 7de9a81bd: helper_coverage enclosure_entries grades a goto as CHEAT"
REVERTED = "REVERTED by sampler (%s): a goto leaves the throne room" % BATCH


def row(status, note, owner=BATCH):
    return {"status": status, "last_failure": note, "owner": owner}


CASES = [
    ("both_verify_only", row("todo", VERIFY), row("todo", VERIFY), True),
    ("self_green", row("green", ""), row("todo", VERIFY), True),
    ("sibling_plain_todo", row("todo", VERIFY), row("todo", REOPEN), False),
    ("sibling_sent_back", row("green", ""), row("todo", REVERTED), False),
    ("other_batch", row("todo", VERIFY), row("todo", VERIFY, "matthew-mbp-m4-b58"), False),
    ("no_owner", row("todo", VERIFY, ""), row("todo", VERIFY, ""), False),
    ("self_plain_todo", row("todo", REOPEN), row("todo", VERIFY), False),
    ("sibling_blocked", row("todo", VERIFY), row("blocked", VERIFY), False),
]


def main():
    failures = 0
    for name, mine, sibling, want in CASES:
        got = helper_coverage.joint_verify(mine, sibling)
        bad = got != want
        failures += bad
        print("%-4s %-20s vouches=%s%s" % ("FAIL" if bad else "ok", name, got, (" -- want %s" % want) if bad else ""))
    print("%d/%d" % (len(CASES) - failures, len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
