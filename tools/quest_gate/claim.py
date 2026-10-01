#!/usr/bin/env python3
"""Cross-machine claims on the quest queue, and the content lock.

Several machines run the quest loop against one origin (branch v3). A batch
must not author a quest another machine is authoring, and two content passes
must not allocate compiler ids (pack/varp.alloc, dbrow.alloc, varn.alloc) at
the same time. Both holds live in git, so every machine sees them after a
fetch: a claim is a QUEUE.tsv row with status `claimed`, and the content lock
is the one line of test/quests/CONTENT_LOCK. docs/QUEST_ORCHESTRATOR.md is
the protocol; this tool is its only writer.

Usage
-----
    claim.py batch <batch> <test_id>...       claim rows for a batch
    claim.py release <batch> [<test_id>...]   put the batch's still-claimed rows back
                     [--stale]                (another host's batch, claim > 24 h old)
    claim.py content-lock <pass>              take the content lock
    claim.py content-unlock <pass> [--stale]  release it
    claim.py merge-tsv <path>...              resolve a conflicted QUEUE/BATCHES/PARITY.tsv
                                              row by row during a git merge

Every write (batch, release, content-lock, content-unlock) is a transaction:
  1. git fetch origin; HEAD must be LEVEL with origin/v3 (nothing ahead,
     nothing behind) and the file must carry no uncommitted edit -- else exit 2.
  2. change the file, `git commit -- <file>` (that path only), push HEAD:v3.
  3. a rejected push (another machine pushed first): fetch, merge origin/v3;
     a conflict in OUR file is resolved by taking origin's copy whole, then
     step 2's change is re-applied to the merged file -- so a row another
     machine claimed in between is DROPPED (and named), never double-claimed,
     and a lock another machine took in between is respected. Push again;
     at most four rounds. Never forces.
A conflict in any other file, or a merge git refuses (a dirty file the merge
would overwrite), aborts the merge and exits 2 naming the files; the local
commit is left for the orchestrator to push by hand.

Exit codes: 0 done (the final list is printed); 2 refused (not level, dirty
file, unknown id, push error) -- nothing was pushed; 3 the caller's request
was not met: no requested row was claimable, a release left another host's
fresh claim in place, or the content lock is held by another pass (its
holder is printed).

Claim shape (queue.py owns the columns): status `claimed`, owner
`<batch>@<host>`, claimed_at = UTC ISO time, claim_prev = `<status>|<owner>`
before the claim; `release` restores claim_prev. A row the batch finished
needs no release -- writing its verdict with queue.py set ends the claim.
Claimable statuses: todo, blocked, content_bug (never green, never a row
another batch holds). Batch names are unique across machines (mac1-b47); a
batch name another host already holds rows under is refused.

`merge-tsv`: for a file git left conflicted in a merge (stages 1/2/3 in the
index), writes one row per key (test_id when the header has it, else the
first column -- BATCHES.tsv's batch) and `git add`s it. A key only one side
changed takes that side; a key both sides changed takes, for QUEUE.tsv, the
stronger status (a verdict green/blocked/content_bug beats claimed beats
todo; between two claims the EARLIER claimed_at holds, as the pushed claim
is the one every other machine saw), and for every other TSV ours. Rows
only one side added are kept. Then `git commit --no-edit` finishes the merge.
"""

from __future__ import annotations

import argparse
import csv
import io
import os
import socket
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# This directory holds queue.py, which shadows the stdlib `queue`; nothing
# here needs the stdlib one, so the local module is imported on purpose.
sys.path.insert(0, str(HERE))
import queue as quest_queue  # noqa: E402  (tools/quest_gate/queue.py)

REPO = HERE.parent.parent
QUEUE_REL = "test/quests/QUEUE.tsv"
LOCK_REL = "test/quests/CONTENT_LOCK"
REMOTE = "origin"
BRANCH = "v3"
PUSH_ROUNDS = 4

EXIT_OK, EXIT_REFUSED, EXIT_NOTHING = 0, 2, 3


class Refused(Exception):
    pass


def git(*args, check=True, repo=None):
    proc = subprocess.run(["git", "-C", str(repo or REPO)] + list(args),
                          capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise Refused("git %s failed (%d): %s" % (" ".join(args), proc.returncode,
                                                 (proc.stderr or proc.stdout).strip()))
    return proc


def host_name(args) -> str:
    name = getattr(args, "host", None) or os.environ.get("QUEST_HOST") or socket.gethostname()
    return name.split(".")[0]


def upstream() -> str:
    return "%s/%s" % (REMOTE, BRANCH)


def require_level(rel_path: str) -> None:
    git("fetch", REMOTE)
    ahead = int(git("rev-list", "--count", "%s..HEAD" % upstream()).stdout.strip())
    behind = int(git("rev-list", "--count", "HEAD..%s" % upstream()).stdout.strip())
    if ahead or behind:
        raise Refused("HEAD is not level with %s (ahead %d, behind %d): merge and push first "
                      "(git merge --no-edit %s; git push %s HEAD:%s), then the submodule"
                      % (upstream(), ahead, behind, upstream(), REMOTE, BRANCH))
    if git("diff", "--quiet", "HEAD", "--", rel_path, check=False).returncode != 0:
        raise Refused("%s carries an uncommitted edit: commit or drop it first (a claim "
                      "commit must hold only the claim)" % rel_path)


def file_changed(rel_path: str) -> bool:
    return git("diff", "--quiet", "HEAD", "--", rel_path, check=False).returncode != 0 or \
        git("ls-files", "--error-unmatch", rel_path, check=False).returncode != 0


def commit_path(rel_path: str, message: str) -> None:
    git("add", "--", rel_path)
    git("commit", "-m", message, "--", rel_path)


def merge_upstream_taking_theirs(rel_path: str) -> None:
    """Fetch + merge origin/v3. A conflict confined to rel_path is resolved by
    origin's copy (the caller re-applies its own change after); anything else
    aborts the merge and refuses."""
    git("fetch", REMOTE)
    proc = git("merge", "--no-edit", upstream(), check=False)
    if proc.returncode == 0:
        return
    conflicted = [p for p in git("diff", "--name-only", "--diff-filter=U").stdout.split() if p]
    if conflicted and set(conflicted) == {rel_path}:
        theirs = git("show", "%s:%s" % (upstream(), rel_path), check=False)
        with open(REPO / rel_path, "w", encoding="utf-8", newline="") as handle:
            handle.write(theirs.stdout if theirs.returncode == 0 else "")
        git("add", "--", rel_path)
        git("commit", "--no-edit")
        return
    if conflicted:
        git("merge", "--abort", check=False)
    raise Refused("merging %s did not go through (%s); merge aborted -- the local claim "
                  "commit is NOT pushed: resolve by hand, then git push %s HEAD:%s"
                  % (upstream(), ", ".join(conflicted) or (proc.stderr or proc.stdout).strip(),
                     REMOTE, BRANCH))


def transact(rel_path: str, apply, message: str, level_checked: bool = False):
    """Run apply() (which edits rel_path and returns a result), commit and
    push; on a rejected push merge origin and re-run apply(). Returns the
    result of the last apply()."""
    if not level_checked:
        require_level(rel_path)
    result = apply()
    if not file_changed(rel_path):
        return result
    commit_path(rel_path, message)
    for round_no in range(1, PUSH_ROUNDS + 1):
        push = git("push", REMOTE, "HEAD:%s" % BRANCH, check=False)
        if push.returncode == 0:
            print("pushed: %s" % git("log", "-1", "--format=%h %s").stdout.strip())
            return result
        text = push.stderr + push.stdout
        if not any(word in text for word in ("rejected", "non-fast-forward", "fetch first")):
            raise Refused("push failed: %s" % text.strip())
        print("push rejected (round %d): another machine pushed first -- merging %s and "
              "re-checking" % (round_no, upstream()), file=sys.stderr)
        merge_upstream_taking_theirs(rel_path)
        result = apply()
        if file_changed(rel_path):
            commit_path(rel_path, message + " (after merging %s)" % upstream())
    raise Refused("push still rejected after %d rounds; the local commits are unpushed" % PUSH_ROUNDS)


# ------------------------------------------------------------------ claims

def load_queue():
    return quest_queue.load_rows(REPO / QUEUE_REL)


def save_queue(rows) -> None:
    quest_queue.write_rows(REPO / QUEUE_REL, rows)


def cmd_batch(args) -> int:
    owner = "%s@%s" % (args.batch, host_name(args))
    require_level(QUEUE_REL)
    rows = load_queue()
    unknown = [t for t in args.test_ids if quest_queue.find_row(rows, t) is None]
    if unknown:
        raise Refused("unknown test_id(s) in %s: %s" % (QUEUE_REL, ", ".join(unknown)))
    clash = sorted({r["owner"] for r in rows if r.get("status") == "claimed"
                    and quest_queue.claim_batch(r.get("owner", "")) == args.batch and r.get("owner") != owner})
    if clash:
        raise Refused("batch name %s is already claimed under %s: batch names are unique across "
                      "machines (<machine>-b<N>)" % (args.batch, ", ".join(clash)))

    outcome = {}

    def apply():
        rows = load_queue()
        claimed, dropped = [], []
        for test_id in args.test_ids:
            row = quest_queue.find_row(rows, test_id)
            if row.get("status") == "claimed" and row.get("owner") == owner:
                claimed.append(test_id)
            elif row.get("status") in quest_queue.CLAIMABLE:
                quest_queue.claim_row(row, owner)
                claimed.append(test_id)
            else:
                dropped.append("%s (%s%s)" % (test_id, row.get("status"),
                                              " by " + row["owner"] if row.get("owner") else ""))
        if claimed:
            save_queue(rows)
        outcome["claimed"], outcome["dropped"] = claimed, dropped
        return outcome

    transact(QUEUE_REL, apply, "quests: claim %s on %s" % (args.batch, host_name(args)), level_checked=True)
    for d in outcome["dropped"]:
        print("dropped: %s" % d)
    if not outcome["claimed"]:
        print("nothing left to claim for %s" % owner)
        return EXIT_NOTHING
    print("claimed by %s: %s" % (owner, " ".join(outcome["claimed"])))
    return EXIT_OK


def cmd_release(args) -> int:
    me = host_name(args)
    outcome = {"released": [], "refused": []}

    def apply():
        rows = load_queue()
        released, refused = [], []
        for row in rows:
            if row.get("status") != "claimed" or quest_queue.claim_batch(row.get("owner", "")) != args.batch:
                continue
            if args.test_ids and row["test_id"] not in args.test_ids:
                continue
            row_host = row["owner"].split("@", 1)[1] if "@" in row["owner"] else ""
            if row_host != me:
                age = quest_queue.claim_age_hours(row)
                if not args.stale or age is None or age <= quest_queue.STALE_CLAIM_HOURS:
                    refused.append("%s (held by %s, %s h old)" % (
                        row["test_id"], row["owner"], "?" if age is None else "%.1f" % age))
                    continue
            quest_queue.release_row(row)
            released.append("%s -> %s" % (row["test_id"], row["status"]))
        if released:
            save_queue(rows)
        outcome["released"], outcome["refused"] = released, refused
        return outcome

    transact(QUEUE_REL, apply, "quests: release %s on %s" % (args.batch, me))
    for r in outcome["refused"]:
        print("not released: %s -- another host's claim; --stale releases one older than %d h "
              "whose batch has no commits since (check git log first)" % (r, quest_queue.STALE_CLAIM_HOURS))
    if outcome["released"]:
        print("released: %s" % " ".join(outcome["released"]))
    elif not outcome["refused"]:
        print("released: nothing (no row of %s is still claimed)" % args.batch)
    return EXIT_NOTHING if outcome["refused"] else EXIT_OK


# ------------------------------------------------------------------ content lock

def read_lock() -> str:
    path = REPO / LOCK_REL
    return path.read_text(encoding="utf-8").strip() if path.exists() else ""


def write_lock(text: str) -> None:
    (REPO / LOCK_REL).write_text(text + ("\n" if text else ""), encoding="utf-8")


def cmd_content_lock(args) -> int:
    mine = "%s@%s" % (args.pass_name, host_name(args))
    outcome = {}

    def apply():
        held = read_lock()
        if not held:
            write_lock("%s %s" % (mine, quest_queue.utc_now_iso()))
            outcome["held_by"] = None
        elif held.split()[0] == mine:
            outcome["held_by"] = None
        else:
            outcome["held_by"] = held
        return outcome

    require_level(LOCK_REL)
    if read_lock() and read_lock().split()[0] != mine:
        print("content lock held: %s" % read_lock())
        return EXIT_NOTHING
    transact(LOCK_REL, apply, "quests: content-lock %s on %s" % (args.pass_name, host_name(args)),
             level_checked=True)
    if outcome["held_by"]:
        print("content lock held: %s" % outcome["held_by"])
        return EXIT_NOTHING
    print("content lock: %s" % read_lock())
    return EXIT_OK


def cmd_content_unlock(args) -> int:
    mine = "%s@%s" % (args.pass_name, host_name(args))
    outcome = {}

    def apply():
        held = read_lock()
        outcome["held"] = held
        if not held:
            return outcome
        holder = held.split()[0]
        stamp = held.split()[1] if len(held.split()) > 1 else ""
        age = quest_queue.claim_age_hours({"claimed_at": stamp})
        if holder == mine or (args.stale and holder.split("@", 1)[0] == args.pass_name
                              and age is not None and age > quest_queue.STALE_CLAIM_HOURS):
            write_lock("")
        return outcome

    transact(LOCK_REL, apply, "quests: content-unlock %s on %s" % (args.pass_name, host_name(args)))
    if read_lock():
        print("content lock NOT released: held by %s, not %s (--stale releases the same pass "
              "name from another host after %d h)" % (read_lock(), mine, quest_queue.STALE_CLAIM_HOURS))
        return EXIT_NOTHING
    print("content lock: free%s" % ("" if outcome.get("held") else " (it was already free)"))
    return EXIT_OK


# ------------------------------------------------------------------ merge-tsv

QUEUE_RANK = {"todo": 0, "claimed": 1, "green": 2, "blocked": 2, "content_bug": 2}


def read_stage(stage: int, rel_path: str):
    proc = git("show", ":%d:%s" % (stage, rel_path), check=False)
    if proc.returncode != 0:
        return None, []
    reader = csv.reader(io.StringIO(proc.stdout), delimiter="\t")
    table = [r for r in reader]
    return (table[0] if table else None), table[1:]


def merge_tsv(rel_path: str) -> str:
    base_h, base = read_stage(1, rel_path)
    ours_h, ours = read_stage(2, rel_path)
    theirs_h, theirs = read_stage(3, rel_path)
    if ours_h is None or theirs_h is None:
        raise Refused("%s is not a conflicted file in a merge (no stage 2/3 in the index)" % rel_path)
    header = list(ours_h) + [c for c in theirs_h if c not in ours_h]
    key_col = "test_id" if "test_id" in header else header[0]
    is_queue = os.path.basename(rel_path) == "QUEUE.tsv"

    def keyed(h, rows):
        out = {}
        order = []
        for r in rows:
            d = {c: (r[i] if i < len(r) else "") for i, c in enumerate(h)}
            out[d.get(key_col, "")] = d
            order.append(d.get(key_col, ""))
        return out, order

    b, _ = keyed(base_h or header, base)
    o, o_order = keyed(ours_h, ours)
    t, t_order = keyed(theirs_h, theirs)
    notes = []

    def pick(k):
        ro, rt, rb = o.get(k), t.get(k), b.get(k)
        if ro == rt:
            return ro
        if rt == rb:
            return ro          # only ours changed (or ours added / deleted)
        if ro == rb:
            return rt          # only theirs changed
        if ro is None or rt is None:
            return ro or rt    # one side deleted, the other edited: keep the edit
        if is_queue:
            so, st = ro.get("status", ""), rt.get("status", "")
            if QUEUE_RANK.get(so, 0) != QUEUE_RANK.get(st, 0):
                win = ro if QUEUE_RANK.get(so, 0) > QUEUE_RANK.get(st, 0) else rt
            elif so == "claimed" and st == "claimed":
                win = ro if (ro.get("claimed_at") or "~") <= (rt.get("claimed_at") or "~") else rt
            else:
                win = ro
            notes.append("%s: both sides changed it; kept %s (%s)" % (
                k, "ours" if win is ro else "theirs", win.get("status")))
            return win
        notes.append("%s: both sides changed it; kept ours" % k)
        return ro

    keys = list(o_order) + [k for k in t_order if k not in o]
    merged = []
    for k in keys:
        row = pick(k)
        if row is not None:
            merged.append([row.get(c, "") for c in header])
    buf = io.StringIO()
    writer = csv.writer(buf, delimiter="\t", lineterminator="\n")
    writer.writerow(header)
    writer.writerows(merged)
    with open(REPO / rel_path, "w", encoding="utf-8", newline="") as handle:
        handle.write(buf.getvalue())
    git("add", "--", rel_path)
    return "%s: %d rows merged by %s%s" % (rel_path, len(merged), key_col,
                                          "".join("\n  " + n for n in notes))


def cmd_merge_tsv(args) -> int:
    for path in args.paths:
        rel = os.path.relpath(os.path.abspath(path), REPO) if os.path.isabs(path) else path
        print(merge_tsv(rel))
    print("now finish the merge: git commit --no-edit")
    return EXIT_OK


def main() -> int:
    global REPO
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", help="host name in the owner (default: $QUEST_HOST or the short hostname)")
    ap.add_argument("--repo", type=Path, help="checkout to act on (default: the one holding this tool)")
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("batch", help="claim rows for a batch")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="+")
    p.set_defaults(func=cmd_batch)
    p = sub.add_parser("release", help="restore the batch's still-claimed rows")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="*")
    p.add_argument("--stale", action="store_true", help="release another host's claim older than 24 h")
    p.set_defaults(func=cmd_release)
    p = sub.add_parser("content-lock", help="take test/quests/CONTENT_LOCK")
    p.add_argument("pass_name", metavar="pass")
    p.set_defaults(func=cmd_content_lock)
    p = sub.add_parser("content-unlock", help="release test/quests/CONTENT_LOCK")
    p.add_argument("pass_name", metavar="pass")
    p.add_argument("--stale", action="store_true", help="release the same pass name from another host after 24 h")
    p.set_defaults(func=cmd_content_unlock)
    p = sub.add_parser("merge-tsv", help="resolve conflicted TSVs row by row during a merge")
    p.add_argument("paths", nargs="+")
    p.set_defaults(func=cmd_merge_tsv)
    args = ap.parse_args()
    if args.repo:
        REPO = args.repo.resolve()
    try:
        return args.func(args)
    except Refused as exc:
        print("claim.py: refused: %s" % exc, file=sys.stderr)
        return EXIT_REFUSED


if __name__ == "__main__":
    sys.exit(main())
