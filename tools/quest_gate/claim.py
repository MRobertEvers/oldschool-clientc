#!/usr/bin/env python3
"""Cross-machine claims on the quest queue, batch branches, and the content lock.

Several machines run the quest loop against one origin (branch v3). A batch
must not author a quest another machine is authoring. v3 carries the claim
ledger: a claim is a QUEUE.tsv row with status `claimed` and owner
`<batch>@<host>`. Everything else a batch produces -- content, fixes, tests,
evidence, docs -- lives on the batch branch `<batch>` (the same name in both
repos) and reaches v3 through one PR per batch (docs/QUEST_ORCHESTRATOR.md).
This tool is the only writer of the ledger and of test/quests/CONTENT_LOCK
(the lock is kept for the older serialised-content model; a batch branch
does not take it).

Usage
-----
    claim.py status                           who holds what on origin/v3, the lock,
                                              and each batch branch's state
    claim.py pr-sync                          release the leftover claims of every batch
                                              whose branch is merged into origin/v3
                     [--closed <batch>...]    ... and of batches whose PR was closed unmerged
    claim.py batch <batch> <test_id>...       claim rows for a batch
    claim.py release <batch> [<test_id>...]   put the batch's still-claimed rows back
                     [--stale] [--note TEXT]  (another host's batch, claim > 24 h old; the
                                              note lands in last_failure for the next taker)
    claim.py done <batch> [--pr N] [--no-build]
                                              finish a batch: merge origin/v3 into the
                                              branch in both repos (v3's pack/*.alloc win,
                                              then the pack is rebuilt), push both branches,
                                              print the PR table; with --pr, stamp the v3
                                              claims with the PR number
    claim.py content-lock <pass>              take the content lock
    claim.py content-unlock <pass> [--stale]  release it
    claim.py merge-tsv <path>...              resolve a conflicted QUEUE/BATCHES/PARITY.tsv
                                              row by row during a git merge

Every write to the ledger or the lock is a transaction on origin/v3 itself,
never on the checked-out branch: the tool fetches, adds a throwaway
worktree at origin/v3 (build/claim_v3_worktree), changes the one file there,
commits it (that path only) and pushes HEAD:v3. A rejected push (another
machine pushed first) merges origin/v3 into the worktree -- a conflict in
OUR file is resolved by taking origin's copy whole and re-applying the
change, so a row another machine claimed in between is DROPPED (and
named), never double-claimed -- and pushes again, at most four rounds. The
worktree is removed afterwards. Never forces. When the checkout itself is
on v3 and level, it is fast-forwarded so the caller sees the claim.

Exit codes: 0 done (the final list is printed); 2 refused (unknown id, push
error, a merge the tool cannot resolve, `done` off its branch); 3 the
caller's request was not met: no requested row was claimable, a release
left another host's fresh claim in place, or the content lock is held by
another pass (its holder is printed).

Claim shape (queue.py owns the columns): status `claimed`, owner
`<batch>@<host>`, claimed_at = UTC ISO time, claim_prev = `<status>|<owner>`
before the claim; `release` restores claim_prev. A row the batch finished
needs no release -- its verdict (green/blocked/content_bug) arrives on v3
with the batch's PR and merge-tsv lets a verdict beat the claim.
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
import re
import shutil
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
SUBMODULE = "OSRS-Content"
ALLOC_GLOB = "osrs239-content/pack/*.alloc"
REMOTE = "origin"
BRANCH = "v3"
WORKTREE_REL = "build/claim_v3_worktree"
PUSH_ROUNDS = 4
BATCH_NAME = re.compile(r"^[A-Za-z0-9_.]+-b\d+$")

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


def current_branch(repo=None) -> str:
    return git("branch", "--show-current", repo=repo).stdout.strip()


# ------------------------------------------------------------------ the v3 worktree

class V3Worktree:
    """A throwaway checkout of origin/v3 under build/, so a ledger write never
    depends on what branch the real checkout is on."""

    def __init__(self):
        self.path = REPO / WORKTREE_REL

    def __enter__(self):
        git("fetch", REMOTE)
        if self.path.exists():
            git("worktree", "remove", "--force", str(self.path), check=False)
            shutil.rmtree(self.path, ignore_errors=True)
        git("worktree", "prune", check=False)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        git("worktree", "add", "--detach", str(self.path), upstream())
        return self

    def __exit__(self, *exc):
        git("worktree", "remove", "--force", str(self.path), check=False)
        shutil.rmtree(self.path, ignore_errors=True)
        git("worktree", "prune", check=False)
        return False


def wt_file_changed(wt: Path, rel_path: str) -> bool:
    return git("diff", "--quiet", "HEAD", "--", rel_path, repo=wt, check=False).returncode != 0 or \
        git("ls-files", "--error-unmatch", rel_path, repo=wt, check=False).returncode != 0


def wt_commit_path(wt: Path, rel_path: str, message: str) -> None:
    git("add", "--", rel_path, repo=wt)
    git("commit", "-m", message, "--", rel_path, repo=wt)


def wt_merge_upstream_taking_theirs(wt: Path, rel_path: str) -> None:
    """Fetch + merge origin/v3 into the worktree. A conflict confined to
    rel_path is resolved by origin's copy (the caller re-applies its own
    change after); anything else refuses."""
    git("fetch", REMOTE)
    proc = git("merge", "--no-edit", upstream(), repo=wt, check=False)
    if proc.returncode == 0:
        return
    conflicted = [p for p in git("diff", "--name-only", "--diff-filter=U", repo=wt).stdout.split() if p]
    if conflicted and set(conflicted) == {rel_path}:
        theirs = git("show", "%s:%s" % (upstream(), rel_path), repo=wt, check=False)
        with open(wt / rel_path, "w", encoding="utf-8", newline="") as handle:
            handle.write(theirs.stdout if theirs.returncode == 0 else "")
        git("add", "--", rel_path, repo=wt)
        git("commit", "--no-edit", repo=wt)
        return
    git("merge", "--abort", repo=wt, check=False)
    raise Refused("merging %s into the v3 worktree did not go through (%s): nothing was pushed"
                  % (upstream(), ", ".join(conflicted) or (proc.stderr or proc.stdout).strip()))


def sync_checkout_to_v3() -> None:
    """After a push to v3: a checkout that sits on v3 and was level is
    fast-forwarded so the caller sees the ledger it just wrote. A batch
    branch is left alone (it merges v3 at `done`)."""
    if current_branch() != BRANCH:
        return
    if git("merge", "--ff-only", upstream(), check=False).returncode != 0:
        print("note: the checkout's v3 is not fast-forwardable to %s; it was left as is" % upstream(),
              file=sys.stderr)


def transact(rel_path: str, apply, message: str):
    """Run apply(wt) (which edits rel_path inside the v3 worktree and returns
    a result), commit and push to v3; on a rejected push merge origin and
    re-run apply(). Returns the result of the last apply()."""
    with V3Worktree() as tree:
        wt = tree.path
        result = apply(wt)
        if not wt_file_changed(wt, rel_path):
            return result
        wt_commit_path(wt, rel_path, message)
        for round_no in range(1, PUSH_ROUNDS + 1):
            push = git("push", REMOTE, "HEAD:%s" % BRANCH, repo=wt, check=False)
            if push.returncode == 0:
                print("pushed: %s" % git("log", "-1", "--format=%h %s", repo=wt).stdout.strip())
                git("fetch", REMOTE, check=False)
                sync_checkout_to_v3()
                return result
            text = push.stderr + push.stdout
            if not any(word in text for word in ("rejected", "non-fast-forward", "fetch first")):
                raise Refused("push failed: %s" % text.strip())
            print("push rejected (round %d): another machine pushed first -- merging %s and "
                  "re-checking" % (round_no, upstream()), file=sys.stderr)
            wt_merge_upstream_taking_theirs(wt, rel_path)
            result = apply(wt)
            if wt_file_changed(wt, rel_path):
                wt_commit_path(wt, rel_path, message + " (after merging %s)" % upstream())
        raise Refused("push still rejected after %d rounds; nothing was pushed" % PUSH_ROUNDS)


# ------------------------------------------------------------------ claims

def load_queue(root: Path):
    return quest_queue.load_rows(root / QUEUE_REL)


def save_queue(root: Path, rows) -> None:
    quest_queue.write_rows(root / QUEUE_REL, rows)


def origin_queue():
    """QUEUE.tsv as origin/v3 has it right now (after a fetch)."""
    git("fetch", REMOTE)
    text = git("show", "%s:%s" % (upstream(), QUEUE_REL)).stdout
    reader = csv.DictReader(io.StringIO(text), delimiter="\t")
    return [dict(r) for r in reader]


def cmd_batch(args) -> int:
    owner = "%s@%s" % (args.batch, host_name(args))
    rows = origin_queue()
    unknown = [t for t in args.test_ids if quest_queue.find_row(rows, t) is None]
    if unknown:
        raise Refused("unknown test_id(s) in %s: %s" % (QUEUE_REL, ", ".join(unknown)))
    clash = sorted({r["owner"] for r in rows if r.get("status") == "claimed"
                    and quest_queue.claim_batch(r.get("owner", "")) == args.batch and r.get("owner") != owner})
    if clash:
        raise Refused("batch name %s is already claimed under %s: batch names are unique across "
                      "machines (<machine>-b<N>)" % (args.batch, ", ".join(clash)))

    outcome = {}

    def apply(wt):
        rows = load_queue(wt)
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
            save_queue(wt, rows)
        outcome["claimed"], outcome["dropped"] = claimed, dropped
        return outcome

    transact(QUEUE_REL, apply, "quests: claim %s on %s" % (args.batch, host_name(args)))
    for d in outcome["dropped"]:
        print("dropped: %s" % d)
    if not outcome["claimed"]:
        print("nothing left to claim for %s" % owner)
        return EXIT_NOTHING
    print("claimed by %s: %s" % (owner, " ".join(outcome["claimed"])))
    return EXIT_OK


def release_rows(rows, batch: str, me: str, test_ids, stale: bool, note: str, any_host: bool = False):
    released, refused = [], []
    for row in rows:
        if row.get("status") != "claimed" or quest_queue.claim_batch(row.get("owner", "")) != batch:
            continue
        if test_ids and row["test_id"] not in test_ids:
            continue
        row_host = row["owner"].split("@", 1)[1] if "@" in row["owner"] else ""
        if row_host != me and not any_host:
            age = quest_queue.claim_age_hours(row)
            if not stale or age is None or age <= quest_queue.STALE_CLAIM_HOURS:
                refused.append("%s (held by %s, %s h old)" % (
                    row["test_id"], row["owner"], "?" if age is None else "%.1f" % age))
                continue
        quest_queue.release_row(row)
        if note:
            row["last_failure"] = "released by %s: %s%s" % (
                batch, note, (" -- " + row["last_failure"]) if row.get("last_failure") else "")
        released.append("%s -> %s" % (row["test_id"], row["status"]))
    return released, refused


def cmd_release(args) -> int:
    me = host_name(args)
    outcome = {"released": [], "refused": []}

    def apply(wt):
        rows = load_queue(wt)
        released, refused = release_rows(rows, args.batch, me, args.test_ids, args.stale, args.note or "")
        if released:
            save_queue(wt, rows)
        outcome["released"], outcome["refused"] = released, refused
        return outcome

    transact(QUEUE_REL, apply, "quests: release %s on %s" % (args.batch, me))
    for r in outcome["refused"]:
        print("not released: %s -- another host's claim; --stale releases one older than %d h "
              "whose batch has no commits since (check git log first)" % (r, quest_queue.STALE_CLAIM_HOURS))
    if outcome["released"]:
        print("released: %s" % " ".join(outcome["released"]))
    elif not outcome["refused"]:
        print("released: nothing (no claimed row of %s%s)" % (
            args.batch, " matches " + " ".join(args.test_ids) if args.test_ids else " is left"))
    return EXIT_NOTHING if outcome["refused"] else EXIT_OK


# ------------------------------------------------------------------ status / pr-sync

def remote_batch_branches() -> dict:
    """{branch: sha} for every origin branch that looks like a batch name."""
    out = {}
    for line in git("ls-remote", "--heads", REMOTE).stdout.splitlines():
        sha, ref = line.split("\t", 1)
        name = ref[len("refs/heads/"):]
        if BATCH_NAME.match(name):
            out[name] = sha
    return out


def branch_merged(sha: str) -> bool:
    return git("merge-base", "--is-ancestor", sha, upstream(), check=False).returncode == 0


def cmd_status(args) -> int:
    rows = origin_queue()
    branches = remote_batch_branches()
    claimed = [r for r in rows if r.get("status") == "claimed"]
    lock = git("show", "%s:%s" % (upstream(), LOCK_REL), check=False).stdout.strip()
    print("origin/v3 at %s" % git("rev-parse", "--short", upstream()).stdout.strip())
    print("content lock: %s" % (lock or "free"))
    if not claimed:
        print("claimed rows: none")
    else:
        print("claimed rows (%d):" % len(claimed))
        by_batch = {}
        for r in claimed:
            by_batch.setdefault(r["owner"], []).append(r)
        for owner, rs in sorted(by_batch.items()):
            batch = quest_queue.claim_batch(owner)
            sha = branches.get(batch)
            state = "no branch on origin" if not sha else (
                "branch MERGED into v3" if branch_merged(sha) else "branch %s" % sha[:9])
            ages = [quest_queue.claim_age_hours(r) for r in rs]
            age = max(a for a in ages if a is not None) if any(a is not None for a in ages) else None
            print("  %-28s %2d row(s) %7s  %s" % (owner, len(rs),
                                                   "?" if age is None else "%.1f h" % age, state))
            for r in rs:
                print("      %-22s tier %s  prev=%s" % (r["test_id"], r.get("tier", ""), r.get("claim_prev", "")))
    others = sorted(b for b in branches if b not in {quest_queue.claim_batch(r["owner"]) for r in claimed})
    if others:
        print("batch branches with no claim left: %s" % ", ".join(
            "%s (%s)" % (b, "merged" if branch_merged(branches[b]) else "open") for b in others))
    return EXIT_OK


def cmd_pr_sync(args) -> int:
    rows = origin_queue()
    branches = remote_batch_branches()
    merged = {b for b, sha in branches.items() if branch_merged(sha)}
    closed = set(args.closed or [])
    settle = sorted({quest_queue.claim_batch(r["owner"]) for r in rows if r.get("status") == "claimed"}
                    & (merged | closed))
    if not settle:
        print("pr-sync: nothing to settle (no claimed row belongs to a merged%s batch)"
              % (" or --closed" if closed else ""))
        return EXIT_OK
    outcome = {"released": []}

    def apply(wt):
        rows = load_queue(wt)
        released = []
        for batch in settle:
            why = "PR merged into v3" if batch in merged else "PR closed unmerged"
            rel, _ = release_rows(rows, batch, "", None, False,
                                  "%s; the batch did not finish this row" % why, any_host=True)
            released += ["%s [%s]" % (x, batch) for x in rel]
        if released:
            save_queue(wt, rows)
        outcome["released"] = released
        return outcome

    transact(QUEUE_REL, apply, "quests: pr-sync -- leftover claims of %s released" % ", ".join(settle))
    print("settled: %s" % (" ".join(outcome["released"]) or "nothing (every row of %s already carries a verdict)" % ", ".join(settle)))
    return EXIT_OK


# ------------------------------------------------------------------ done

def merge_v3_into_branch(repo: Path, label: str, take_theirs_globs, build_cmd=None) -> list:
    """git merge origin/v3 in `repo` (on its batch branch). Conflicts in the
    named globs take origin's copy; TSV conflicts go through merge-tsv;
    anything else refuses with the merge aborted."""
    git("fetch", REMOTE, repo=repo)
    proc = git("merge", "--no-edit", upstream(), repo=repo, check=False)
    notes = []
    if proc.returncode == 0:
        return notes
    conflicted = [p for p in git("diff", "--name-only", "--diff-filter=U", repo=repo).stdout.split() if p]
    unresolved = []
    for path in conflicted:
        if any(Path(path).match(g) for g in take_theirs_globs):
            theirs = git("show", "%s:%s" % (upstream(), path), repo=repo)
            with open(repo / path, "w", encoding="utf-8", newline="") as handle:
                handle.write(theirs.stdout)
            git("add", "--", path, repo=repo)
            notes.append("%s: took origin/v3's copy" % path)
        elif path.endswith(".tsv") and repo == REPO:
            notes.append(merge_tsv(path))
        else:
            unresolved.append(path)
    if unresolved:
        git("merge", "--abort", repo=repo, check=False)
        raise Refused("%s: merging %s conflicts outside what `done` resolves: %s -- merge by hand "
                      "on the batch branch, then run done again" % (label, upstream(), ", ".join(unresolved)))
    git("commit", "--no-edit", repo=repo)
    return notes


def cmd_done(args) -> int:
    batch = args.batch
    me = host_name(args)
    sub = REPO / SUBMODULE
    if current_branch() != batch or current_branch(sub) != batch:
        raise Refused("done runs on the batch branch in both repos: parent is on %r, %s on %r, wanted %r"
                      % (current_branch(), SUBMODULE, current_branch(sub), batch))
    for repo, label in ((sub, SUBMODULE), (REPO, "parent")):
        dirty = [l for l in git("status", "--porcelain", repo=repo).stdout.splitlines()
                 if l and not l.startswith("?? build/")]
        dirty = [l for l in dirty if not (repo == REPO and l.split()[-1] == SUBMODULE)]
        if dirty:
            raise Refused("%s has uncommitted changes; a batch lands only committed work:\n  %s"
                          % (label, "\n  ".join(dirty[:12])))
    notes = []
    # 1. the submodule: v3's alloc files win, then the pack is rebuilt so the
    #    batch's new ids are allocated after everything v3 already has.
    notes += merge_v3_into_branch(sub, SUBMODULE, [ALLOC_GLOB])
    if not args.no_build:
        make = subprocess.run(["make", "-C", str(REPO / "src"), "torirsserver-scripts"],
                              capture_output=True, text=True)
        if make.returncode != 0:
            raise Refused("the script pack did not rebuild after merging %s into %s:\n%s"
                          % (upstream(), SUBMODULE, (make.stderr or make.stdout)[-3000:]))
        changed = [l.split()[-1] for l in git("status", "--porcelain", "--", "osrs239-content/pack",
                                              repo=sub).stdout.splitlines()]
        if changed:
            git("add", "--", *changed, repo=sub)
            git("commit", "-m", "quests: pack alloc files rebuilt on %s after merging %s"
                % (batch, upstream()), "--", *changed, repo=sub)
            notes.append("%s: alloc files re-allocated after the merge (%d file(s))" % (SUBMODULE, len(changed)))
    # 2. the parent: merge v3 (TSVs row by row), point the gitlink at the
    #    merged submodule.
    notes += merge_v3_into_branch(REPO, "parent", [])
    if git("diff", "--quiet", "HEAD", "--", SUBMODULE, check=False).returncode != 0:
        git("add", "--", SUBMODULE)
        git("commit", "-m", "quests: %s -- %s merged with %s" % (batch, SUBMODULE, upstream()), "--", SUBMODULE)
    # 3. push both branches.
    for repo, label in ((sub, SUBMODULE), (REPO, "parent")):
        push = git("push", "-u", REMOTE, "HEAD:refs/heads/%s" % batch, repo=repo, check=False)
        if push.returncode != 0:
            raise Refused("%s: pushing %s failed: %s" % (label, batch, (push.stderr or push.stdout).strip()))
    for n in notes:
        print(n)
    # 4. the table, from the branch's own ledger.
    rows = load_queue(REPO)
    mine = [r for r in rows if quest_queue.claim_batch(r.get("owner", "")) == batch
            or (r.get("claim_prev") and r.get("owner", "").startswith(batch))]
    if not mine:
        mine = [r for r in rows if batch in (r.get("owner", "") + r.get("last_failure", ""))]
    table = ["| quest | status | note |", "|---|---|---|"]
    for r in mine:
        table.append("| %s | %s | %s |" % (r["test_id"], r.get("status", ""),
                                            (r.get("last_failure", "") or "").replace("|", "/")[:160]))
    body = "\n".join(["Batch `%s` on `%s`: %d quest(s)." % (batch, me, len(mine)), ""] + table)
    out = REPO / "build" / ("%s.pr.md" % batch)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(body + "\n", encoding="utf-8")
    print(body)
    print("\nPR body written to %s -- open the PR %s -> %s (gh pr create --base %s --head %s, or the "
          "GitHub tool), then run: claim.py done %s --pr <N> --no-build to stamp the v3 claims"
          % (out, batch, BRANCH, BRANCH, batch, batch))
    # 5. with --pr: stamp the v3 claims.
    if args.pr:
        stamp = "PR #%d (%s -> %s)" % (args.pr, batch, BRANCH)
        outcome = {"stamped": []}

        def apply(wt):
            rows = load_queue(wt)
            stamped = []
            for row in rows:
                if row.get("status") == "claimed" and quest_queue.claim_batch(row.get("owner", "")) == batch:
                    if not row.get("last_failure", "").startswith(stamp):
                        row["last_failure"] = "%s%s" % (stamp, (" -- " + row["last_failure"]) if row.get("last_failure") else "")
                        stamped.append(row["test_id"])
            if stamped:
                save_queue(wt, rows)
            outcome["stamped"] = stamped
            return outcome

        transact(QUEUE_REL, apply, "quests: %s is %s" % (batch, stamp))
        print("stamped on v3: %s" % (" ".join(outcome["stamped"]) or "nothing new"))
    return EXIT_OK


# ------------------------------------------------------------------ content lock

def read_lock(root: Path) -> str:
    path = root / LOCK_REL
    return path.read_text(encoding="utf-8").strip() if path.exists() else ""


def write_lock(root: Path, text: str) -> None:
    (root / LOCK_REL).write_text(text + ("\n" if text else ""), encoding="utf-8")


def cmd_content_lock(args) -> int:
    mine = "%s@%s" % (args.pass_name, host_name(args))
    outcome = {}

    def apply(wt):
        held = read_lock(wt)
        if not held:
            write_lock(wt, "%s %s" % (mine, quest_queue.utc_now_iso()))
            outcome["held_by"] = None
        elif held.split()[0] == mine:
            outcome["held_by"] = None
        else:
            outcome["held_by"] = held
        return outcome

    transact(LOCK_REL, apply, "quests: content-lock %s on %s" % (args.pass_name, host_name(args)))
    if outcome.get("held_by"):
        print("content lock held: %s" % outcome["held_by"])
        return EXIT_NOTHING
    print("content lock: %s" % mine)
    return EXIT_OK


def cmd_content_unlock(args) -> int:
    mine = "%s@%s" % (args.pass_name, host_name(args))
    outcome = {}

    def apply(wt):
        held = read_lock(wt)
        outcome["held"] = held
        outcome["left"] = ""
        if not held:
            return outcome
        holder = held.split()[0]
        stamp = held.split()[1] if len(held.split()) > 1 else ""
        age = quest_queue.claim_age_hours({"claimed_at": stamp})
        if holder == mine or (args.stale and holder.split("@", 1)[0] == args.pass_name
                              and age is not None and age > quest_queue.STALE_CLAIM_HOURS):
            write_lock(wt, "")
        else:
            outcome["left"] = held
        return outcome

    transact(LOCK_REL, apply, "quests: content-unlock %s on %s" % (args.pass_name, host_name(args)))
    if outcome.get("left"):
        print("content lock NOT released: held by %s, not %s (--stale releases the same pass "
              "name from another host after %d h)" % (outcome["left"], mine, quest_queue.STALE_CLAIM_HOURS))
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
    p = sub.add_parser("status", help="who holds what on origin/v3")
    p.set_defaults(func=cmd_status)
    p = sub.add_parser("pr-sync", help="release the leftover claims of merged (or --closed) batches")
    p.add_argument("--closed", nargs="*", metavar="BATCH", help="batches whose PR was closed without merging")
    p.set_defaults(func=cmd_pr_sync)
    p = sub.add_parser("batch", help="claim rows for a batch")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="+")
    p.set_defaults(func=cmd_batch)
    p = sub.add_parser("release", help="restore the batch's still-claimed rows")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="*")
    p.add_argument("--stale", action="store_true", help="release another host's claim older than 24 h")
    p.add_argument("--note", help="why; recorded in last_failure for whoever claims the row next")
    p.set_defaults(func=cmd_release)
    p = sub.add_parser("done", help="merge v3 into the batch branch, push it, print the PR table")
    p.add_argument("batch")
    p.add_argument("--pr", type=int, help="the PR's number, once opened: stamps the v3 claims with it")
    p.add_argument("--no-build", action="store_true", help="skip the script-pack rebuild after the alloc merge")
    p.set_defaults(func=cmd_done)
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
