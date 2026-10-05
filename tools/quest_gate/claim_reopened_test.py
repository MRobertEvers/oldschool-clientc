#!/usr/bin/env python3
"""Fixture test: `claim.py batch <batch> --reopened` claims a row green on v3
only when the batch branch reopened it.

    python3 tools/quest_gate/claim_reopened_test.py

A seam pass reopens a row green on v3 (after a content or grader fix) on the
batch branch; v3 still reads it green, and green is not in
quest_queue.CLAIMABLE, so `claim.py batch` dropped it as "(green by <owner>)"
and the reopened row had no v3 claim (zogreflesheaters and anothersliceofham
on matthew-mbp-m4-b62). --reopened reads the branch's QUEUE.tsv and lets
claim_rows() take a green v3 row only when the branch shows it non-green with
owner = this batch.

The fixtures are in-memory rows; claim_rows() is the decision cmd_batch's
transaction applies to origin/v3's copy, so no git, no ledger, no push.

  case                         want
  reopened_by_this_batch       green on v3, branch: todo by <batch> -> claimed,
                               claim_prev = green|<old owner>
  reopened_owner_with_host     branch owner <batch>@<host> -> claimed
  reopened_content_bug         branch: content_bug by <batch> -> claimed
  not_reopened                 green on v3 and on the branch -> dropped
  absent_on_branch             green on v3, no branch row -> dropped
  reopened_by_another_batch    branch: todo by another batch -> dropped
  no_flag                      branch_rows None (no --reopened) -> dropped,
                               even though the branch reopened it
  todo_as_before               todo on v3 -> claimed with or without the flag
  claimed_by_other_kept        claimed by another batch on v3 -> dropped
  release_restores_green       release_row() puts green|<old owner> back
  asserts                      reopened_on_branch(None, ...) and an id not in
                               the rows abort, not return

Exit 0 when every case holds.
"""

import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import claim  # noqa: E402
quest_queue = claim.quest_queue

BATCH = "mac9-b99"
OWNER = BATCH + "@mac9"
FAILS = []


def row(test_id, status, owner=""):
    r = {c: "" for c in quest_queue.QUEUE_COLUMNS}
    r.update(test_id=test_id, status=status, owner=owner, tier="2")
    return r


V3 = [
    row("zogre", "green", "mac1-b61"),
    row("ham", "green", "mac1-b56"),
    row("hosted", "green", "mac1-b50"),
    row("plain", "green", "mac1-b40"),
    row("absent", "green", "mac1-b41"),
    row("rival", "green", "mac1-b42"),
    row("fresh", "todo"),
    row("held", "claimed", "mac2-b12@mac2"),
]
BRANCH = [
    row("zogre", "todo", BATCH),
    row("ham", "content_bug", BATCH),
    row("hosted", "todo", OWNER),
    row("plain", "green", "mac1-b40"),
    row("rival", "todo", "mac2-b12"),
    row("fresh", "todo"),
    row("held", "claimed", "mac2-b12@mac2"),
]


def check(name, ok, detail=""):
    print("%-28s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAILS.append(name)


def run(ids, branch_rows):
    rows = copy.deepcopy(V3)
    claimed, dropped = claim.claim_rows(rows, ids, OWNER, branch_rows)
    return rows, claimed, dropped


def one(name, test_id, flag, want_claimed):
    rows, claimed, dropped = run([test_id], copy.deepcopy(BRANCH) if flag else None)
    r = quest_queue.find_row(rows, test_id)
    if want_claimed:
        check(name, claimed == [test_id] and not dropped and r["status"] == "claimed" and r["owner"] == OWNER,
              "claimed=%r dropped=%r row=%s/%s" % (claimed, dropped, r["status"], r["owner"]))
    else:
        before = quest_queue.find_row(V3, test_id)
        check(name, claimed == [] and len(dropped) == 1 and dropped[0].startswith(test_id + " (")
              and r == before, "claimed=%r dropped=%r row=%s/%s" % (claimed, dropped, r["status"], r["owner"]))


def main():
    one("reopened_by_this_batch", "zogre", True, True)
    rows, _, _ = run(["zogre"], copy.deepcopy(BRANCH))
    check("reopened_claim_prev", quest_queue.find_row(rows, "zogre")["claim_prev"] == "green|mac1-b61",
          quest_queue.find_row(rows, "zogre")["claim_prev"])
    one("reopened_owner_with_host", "hosted", True, True)
    one("reopened_content_bug", "ham", True, True)
    one("not_reopened", "plain", True, False)
    one("absent_on_branch", "absent", True, False)
    one("reopened_by_another_batch", "rival", True, False)
    one("no_flag", "zogre", False, False)
    one("no_flag_ham", "ham", False, False)
    one("todo_as_before", "fresh", False, True)
    one("todo_with_flag", "fresh", True, True)
    one("claimed_by_other_kept", "held", True, False)

    rows, claimed, dropped = run(["zogre", "ham", "plain", "fresh"], copy.deepcopy(BRANCH))
    check("mixed_request", claimed == ["zogre", "ham", "fresh"] and len(dropped) == 1
          and "not reopened by %s" % BATCH in dropped[0], "claimed=%r dropped=%r" % (claimed, dropped))

    rows, _, _ = run(["zogre"], copy.deepcopy(BRANCH))
    r = quest_queue.find_row(rows, "zogre")
    quest_queue.release_row(r)
    check("release_restores_green", r == quest_queue.find_row(V3, "zogre"), repr(r))

    for name, call in (("assert_branch_rows", lambda: claim.reopened_on_branch(None, "zogre", BATCH)),
                       ("assert_batch", lambda: claim.reopened_on_branch(BRANCH, "zogre", "")),
                       ("assert_unknown_id", lambda: claim.claim_rows(copy.deepcopy(V3), ["nosuch"], OWNER, None))):
        try:
            call()
            check(name, False, "returned instead of asserting")
        except AssertionError:
            check(name, True)

    print("%d failure(s)" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
