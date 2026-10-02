#!/usr/bin/env python3
"""Cross-machine claims on the quest queue, and the batch branch's road to v3.

Several machines run the quest loop against one origin. The unit of work is a
BATCH OF QUESTS owned by one machine for its whole lifecycle (content parity,
seam passes, authoring, review, sample, green). The only thing a machine
writes to v3 directly is the claim: a QUEUE.tsv row with status `claimed`,
owner `<batch>@<host>`. Every other change -- content, driver, engine, tests,
evidence, docs, wip/ -- is committed on the batch's branch, named
`<host>-<batch>` (or `<batch>` when it already starts with `<host>-`), which
exists in BOTH repos (the parent and OSRS-Content), and reaches v3 through one
PR per repo when the batch is done. docs/QUEST_ORCHESTRATOR.md is the
protocol; this tool is the only writer of claims. tools/quest_gate/claim_test/run.sh
drives two machines through the whole lifecycle against throwaway remotes.

Usage
-----
    claim.py start <batch> <test_id>...       create the batch branch in both repos from
                                              origin/v3, switch to it, then claim
    claim.py batch <batch> <test_id>...       claim rows for a batch (on v3)
    claim.py release <batch> [<test_id>...]   put the batch's still-claimed rows back
                     [--note TEXT] [--stale]  (another host's batch, claim > 24 h old)
    claim.py status [--batch B [--require ID...]]
                                              every claim on origin/v3: host, age, PR;
                                              with --batch, check this checkout may run it
    claim.py pr-prepare <batch> [--push]      merge origin/v3 into the batch branch in both
                                              repos so its PRs are mergeable
    claim.py done <batch>                     pr-prepare, open/update the two PRs, mark the
                                              v3 claim rows "PR #<n> pending merge"
    claim.py pr-sync                          settle claims whose PRs merged or closed
    claim.py merge-tsv [--own-batch B] <path>...
                                              resolve a conflicted QUEUE/BATCHES/PARITY.tsv
    claim.py content-lock <pass>              take the whole-pack content lock (rare)
    claim.py content-unlock <pass> [--stale]  release it

WHERE A CLAIM IS WRITTEN. On branch v3 the checkout itself is the writer, as
before 2026-10-01. On any other branch the claim is written through the
coordination worktree build/v3_coord/ (a worktree of the same repository on
local branch `coord-v3`, created on first use, sparse -- it checks out only
QUEUE.tsv and CONTENT_LOCK; `git sparse-checkout` sets
extensions.worktreeConfig in the repository's config): fetched and fast-forwarded
before each write (merged, QUEUE.tsv row by row, if it ever holds an unpushed
commit), committed there, pushed `coord-v3:v3`. The batch branch never
carries a claim commit of its own; after a claim, a branch with no commits
of its own is fast-forwarded to origin/v3 so its QUEUE.tsv shows the claim.

Every v3 write (batch, release, done's PR note, pr-sync, content-lock,
content-unlock) is a transaction:
  1. git fetch origin; the writer must be LEVEL with origin/v3 and the file
     must carry no uncommitted edit -- else exit 2.
  2. change the file, `git commit -- <file>` (that path only), push HEAD:v3.
  3. a rejected push: fetch, merge origin/v3; a conflict in OUR file is
     resolved by origin's copy whole, then step 2's change is re-applied --
     a row another machine claimed in between is DROPPED (and named), never
     double-claimed. Push again; at most four rounds. Never forces.

CLAIMS. Claimable statuses: todo, blocked, content_bug -- never green, never
a row any batch holds (a held row may carry "PR #<n> pending merge"). Batch
names are unique across machines; a batch name another host holds rows
under is refused. A claim remembers the row's previous status and owner in
claim_prev; `release` restores them (and, with --note, sets last_failure).

THE BATCH BRANCH. Verdicts live on the branch: the cards' queue step and
sampler write green/blocked/content_bug/todo into the BRANCH's QUEUE.tsv;
on v3 the rows stay `claimed` until the batch's PR merges. `pr-prepare`
fetches both repos, merges origin/v3 into the branch -- OSRS-Content first,
then the parent -- and resolves the conflicts two machines' batches make
by construction:
  * QUEUE/BATCHES/PARITY.tsv: merge-tsv row by row (below), with this batch
    as --own-batch so the branch's own verdicts beat v3's claim of them.
  * OSRS-Content pack/*.alloc (compiler-written id ledgers): v3's copy is
    taken, then the branch's own new lines are re-allocated on top of it:
    a var namespace (varp/varbit/varc, whose names carry their id) keeps a
    line whose id is still free, and renumbers a colliding one to the next
    free id, renaming `varp<old>_<name>` to `varp<new>_<name>` in every
    tracked file of both repos; every other namespace is left to the pack
    rebuild ($QUEST_PACK_REBUILD, default `make -C src torirsserver-scripts`),
    which appends the branch's names with fresh ids. The rebuild runs
    whenever an alloc file conflicted; the tool then checks every name the
    branch had allocated is in the result.
  * the OSRS-Content gitlink: the merged submodule HEAD.
Anything else conflicted aborts both merges and exits 2 naming the files.
`--push` pushes HEAD to origin <branch> in both repos (the submodule only
when it has commits v3 lacks).

`done <batch>`: pr-prepare; every branch QUEUE row the batch still has
`claimed` is released there (no verdict -- todo again for the next machine);
push; open or update the OSRS-Content PR (when the content branch has
commits v3 lacks) and then the parent PR (base v3, head the branch; title
"quests: <batch> -- <greens>"; body: the per-quest table from the branch's
QUEUE rows, the contact sheets from BATCHES.tsv, the content PR and "merge
the content PR first, with a merge commit"); write "PR #<n> pending merge
(OSRS-Content #<m>)" onto the batch's v3 claim rows (still `claimed`); and
pr-prepare once more so that note does not conflict with the PR. Re-running
`done` updates the PRs.

`pr-sync` (every launch): for each batch whose v3 rows carry a PR note,
`gh pr view` both PRs. Both merged: the merge brought the branch's QUEUE rows,
so no row should still be claimed -- any that is, is released with a note.
Either one closed unmerged: the batch's rows are released ("PR #<n> closed
unmerged"). Otherwise it prints what is pending (and a CONFLICTING PR, which
`done` on its branch makes mergeable again).

`status`: the claims on origin/v3 grouped by batch -- host, rows, age, PR.
With `--batch B` it is the cards' gate: exit 0 only when this checkout is on
B's branch and B holds every `--require` id on origin/v3 under this host
(without --require: at least one row); exit 2 wrong branch; exit 3 a missing
claim (named).

`merge-tsv`: for a file git left conflicted in a merge (stages 1/2/3 in the
index), writes one row per key (test_id when the header has it, else the
first column -- BATCHES.tsv's batch) and `git add`s it. A key only one side
changed takes that side; a key both sides changed takes, for QUEUE.tsv, the
stronger status (a verdict green/blocked/content_bug beats claimed beats
todo; between two claims the EARLIER claimed_at holds) -- except that with
`--own-batch B`, a row theirs has claimed by B and ours changed is ours (the
batch's own verdict or note beats its own claim); for every other TSV ours.
Rows only one side added are kept. Then `git commit --no-edit` finishes.

THE CONTENT LOCK (test/quests/CONTENT_LOCK) is no longer the gate between
machines: the batch claim is. It is kept for the rare whole-pack change (a
var rename across every quest, a pack reshuffle) that no batch branch could
merge, and for a content pass run directly on v3 the pre-2026-10-01 way.

Exit codes: 0 done; 2 refused (not level, dirty file, unknown id, wrong
branch, a conflict this tool does not resolve, a push or gh error) -- nothing
was pushed to v3; 3 the request was not met: nothing claimable, a release
left another host's fresh claim, a lock held elsewhere, a missing claim.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import shlex
import socket
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# This directory holds queue.py, which shadows the stdlib `queue`; nothing
# here needs the stdlib one, so the local module is imported on purpose.
sys.path.insert(0, str(HERE))
import queue as quest_queue  # noqa: E402  (tools/quest_gate/queue.py)

REPO = HERE.parent.parent          # the checkout this tool runs in
CLAIM_REPO = REPO                  # where v3 claims are written (REPO on v3, else the coord worktree)
QUEUE_REL = "test/quests/QUEUE.tsv"
BATCHES_REL = "test/quests/BATCHES.tsv"
PARITY_REL = "tools/quest_gate/PARITY.tsv"
LOCK_REL = "test/quests/CONTENT_LOCK"
TSV_RELS = (QUEUE_REL, BATCHES_REL, PARITY_REL)
SUBMODULE = "OSRS-Content"
COORD_REL = "build/v3_coord"
COORD_BRANCH = "coord-v3"
REMOTE = "origin"
BRANCH = "v3"
PUSH_ROUNDS = 4
# Ours: "PR #12 pending merge (OSRS-Content #7)". Also read: the vm-b1 tooling's
# stamp, "PR #12 (vm-b1 -> v3)", so either side's status/pr-sync sees the other's.
PR_NOTE = re.compile(r"^PR #(\d+) (?:pending merge(?: \(OSRS-Content #(\d+)\))?|\([^)]* -> v3\))(?: -- )?")
ALLOC_PATH = re.compile(r"(^|/)pack/([^/]+)\.alloc$")
VAR_PREFIX = {"varp": "varp", "varbit": "varb", "varc": "varc"}   # tools/ss_allocate.py

EXIT_OK, EXIT_REFUSED, EXIT_NOTHING = 0, 2, 3


class Refused(Exception):
    pass


def git(*args, check=True, repo=None):
    proc = subprocess.run(["git", "-C", str(repo or REPO)] + list(args),
                          capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise Refused("git -C %s %s failed (%d): %s" % (repo or REPO, " ".join(args), proc.returncode,
                                                       (proc.stderr or proc.stdout).strip()))
    return proc


def host_name(args) -> str:
    name = getattr(args, "host", None) or os.environ.get("QUEST_HOST") or socket.gethostname()
    return name.split(".")[0]


def upstream() -> str:
    return "%s/%s" % (REMOTE, BRANCH)


def branch_for(batch: str, host: str) -> str:
    """The batch's branch name in both repos: <host>-<batch>, or the batch
    name itself when it already starts with "<host>-" (mac1-b47 on mac1)."""
    return batch if batch.startswith(host + "-") else "%s-%s" % (host, batch)


def current_branch(repo=None) -> str:
    return git("symbolic-ref", "--short", "-q", "HEAD", check=False, repo=repo).stdout.strip()


def submodule_dir() -> Path | None:
    path = REPO / SUBMODULE
    if (path / ".git").exists() and git("rev-parse", "--git-dir", check=False, repo=path).returncode == 0:
        return path
    return None


def count(range_spec: str, repo=None) -> int:
    return int(git("rev-list", "--count", range_spec, repo=repo).stdout.strip())


# ------------------------------------------------------------------ the v3 writer

def prepare_coord() -> Path:
    """build/v3_coord: a worktree of this repository on local branch coord-v3,
    level with origin/v3, holding no uncommitted claim write."""
    path = REPO / COORD_REL
    git("fetch", "--no-recurse-submodules", REMOTE)
    if not (path / ".git").exists():
        git("worktree", "prune", check=False)
        # Sparse: the parent's full tree is 3 GB; the writer needs two files.
        if git("rev-parse", "--verify", "-q", "refs/heads/" + COORD_BRANCH, check=False).returncode == 0:
            git("worktree", "add", "--no-checkout", str(path), COORD_BRANCH)
        else:
            git("worktree", "add", "--no-checkout", "--no-track", "-b", COORD_BRANCH, str(path), upstream())
        git("sparse-checkout", "set", "--no-cone", "/" + QUEUE_REL, "/" + LOCK_REL, repo=path)
        git("checkout", "-q", COORD_BRANCH, repo=path)
        git("branch", "--set-upstream-to=" + upstream(), COORD_BRANCH, repo=path)
        print("coord: created %s on %s (sparse: QUEUE.tsv and CONTENT_LOCK only; tracks %s)"
              % (path, COORD_BRANCH, upstream()))
    for rel in (QUEUE_REL, LOCK_REL):
        if git("diff", "--quiet", "HEAD", "--", rel, check=False, repo=path).returncode != 0:
            # Only this tool writes in the coordination worktree: an uncommitted
            # edit there is a write that crashed before its commit.
            head = git("show", "HEAD:%s" % rel, check=False, repo=path)
            with open(path / rel, "w", encoding="utf-8", newline="") as handle:
                handle.write(head.stdout if head.returncode == 0 else "")
            print("coord: dropped an uncommitted write of %s left by a crashed run" % rel, file=sys.stderr)
    dirty = [p for p in git("diff", "--name-only", "HEAD", check=False, repo=path).stdout.split() if p]
    if dirty:
        raise Refused("%s carries uncommitted edits (%s): only claim.py writes there -- inspect them, "
                      "then git worktree remove --force %s" % (path, ", ".join(dirty), path))
    ahead, behind = count("%s..HEAD" % upstream(), path), count("HEAD..%s" % upstream(), path)
    if behind and not ahead:
        git("merge", "--ff-only", upstream(), repo=path)
    elif behind and ahead:
        print("coord: %d unpushed claim commit(s) from an earlier run; merging %s and pushing them "
              "with this write" % (ahead, upstream()), file=sys.stderr)
        merge_upstream_taking_theirs(QUEUE_REL, path)
    return path


def select_claim_repo() -> Path:
    """REPO when the checkout is on v3 (the pre-2026-10-01 behaviour), else
    the coordination worktree."""
    global CLAIM_REPO
    CLAIM_REPO = REPO if current_branch() == BRANCH else prepare_coord()
    return CLAIM_REPO


def require_level(rel_path: str) -> None:
    git("fetch", "--no-recurse-submodules", REMOTE, repo=CLAIM_REPO)
    ahead = count("%s..HEAD" % upstream(), CLAIM_REPO)
    behind = count("HEAD..%s" % upstream(), CLAIM_REPO)
    if ahead and CLAIM_REPO != REPO:
        ahead = 0   # prepare_coord merged them; they are pushed with this write
    if ahead or behind:
        raise Refused("HEAD is not level with %s (ahead %d, behind %d): merge and push first "
                      "(git merge --no-edit %s; git push %s HEAD:%s), then the submodule"
                      % (upstream(), ahead, behind, upstream(), REMOTE, BRANCH))
    if git("diff", "--quiet", "HEAD", "--", rel_path, check=False, repo=CLAIM_REPO).returncode != 0:
        raise Refused("%s carries an uncommitted edit: commit or drop it first (a claim "
                      "commit must hold only the claim)" % rel_path)


def file_changed(rel_path: str, repo=None) -> bool:
    repo = repo or CLAIM_REPO
    return git("diff", "--quiet", "HEAD", "--", rel_path, check=False, repo=repo).returncode != 0 or \
        git("ls-files", "--error-unmatch", rel_path, check=False, repo=repo).returncode != 0


def commit_path(rel_path: str, message: str, repo=None) -> None:
    repo = repo or CLAIM_REPO
    git("add", "--", rel_path, repo=repo)
    git("commit", "-m", message, "--", rel_path, repo=repo)


def merge_upstream_taking_theirs(rel_path: str, repo=None) -> None:
    """Fetch + merge origin/v3. A conflict confined to rel_path is resolved by
    origin's copy (the caller re-applies its own change after); anything else
    aborts the merge and refuses."""
    repo = repo or CLAIM_REPO
    git("fetch", "--no-recurse-submodules", REMOTE, repo=repo)
    proc = git("merge", "--no-edit", upstream(), check=False, repo=repo)
    if proc.returncode == 0:
        return
    conflicted = [p for p in git("diff", "--name-only", "--diff-filter=U", repo=repo).stdout.split() if p]
    if conflicted and set(conflicted) == {rel_path}:
        theirs = git("show", "%s:%s" % (upstream(), rel_path), check=False, repo=repo)
        with open(Path(repo) / rel_path, "w", encoding="utf-8", newline="") as handle:
            handle.write(theirs.stdout if theirs.returncode == 0 else "")
        git("add", "--", rel_path, repo=repo)
        git("commit", "--no-edit", repo=repo)
        return
    if conflicted:
        git("merge", "--abort", check=False, repo=repo)
    raise Refused("merging %s did not go through (%s); merge aborted -- the local claim "
                  "commit is NOT pushed: resolve by hand, then git -C %s push %s HEAD:%s"
                  % (upstream(), ", ".join(conflicted) or (proc.stderr or proc.stdout).strip(),
                     repo, REMOTE, BRANCH))


def transact(rel_path: str, apply, message: str, level_checked: bool = False):
    """Run apply() (which edits rel_path and returns a result), commit and
    push to v3; on a rejected push merge origin and re-run apply()."""
    if not level_checked:
        require_level(rel_path)
    result = apply()
    if not file_changed(rel_path) and count("%s..HEAD" % upstream(), CLAIM_REPO) == 0:
        return result
    if file_changed(rel_path):
        commit_path(rel_path, message)
    for round_no in range(1, PUSH_ROUNDS + 1):
        push = git("push", REMOTE, "HEAD:%s" % BRANCH, check=False, repo=CLAIM_REPO)
        if push.returncode == 0:
            print("pushed to %s: %s" % (BRANCH, git("log", "-1", "--format=%h %s", repo=CLAIM_REPO).stdout.strip()))
            git("fetch", "--no-recurse-submodules", REMOTE, check=False, repo=CLAIM_REPO)
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


def follow_v3_if_untouched() -> None:
    """On a batch branch with no commits of its own, fast-forward to origin/v3
    so the branch's QUEUE.tsv shows the claim just pushed."""
    if CLAIM_REPO == REPO:
        return
    if count("%s..HEAD" % upstream()) == 0 and count("HEAD..%s" % upstream()) > 0:
        proc = git("merge", "--ff-only", upstream(), check=False)
        print("branch %s: %s" % (current_branch(), "fast-forwarded to %s" % upstream() if proc.returncode == 0
                                 else "left as is (%s)" % (proc.stderr or proc.stdout).strip().splitlines()[-1]))


# ------------------------------------------------------------------ claims

def load_queue(repo=None):
    return quest_queue.load_rows(Path(repo or CLAIM_REPO) / QUEUE_REL)


def save_queue(rows, repo=None) -> None:
    quest_queue.write_rows(Path(repo or CLAIM_REPO) / QUEUE_REL, rows)


def v3_rows():
    """QUEUE.tsv as origin/v3 has it (call after a fetch)."""
    text = git("show", "%s:%s" % (upstream(), QUEUE_REL)).stdout
    rows = list(csv.DictReader(io.StringIO(text), delimiter="\t"))
    for row in rows:
        for col in quest_queue.QUEUE_COLUMNS:
            row.setdefault(col, "")
    return rows


def strip_pr_note(text: str) -> str:
    return PR_NOTE.sub("", text or "", count=1)


def pr_numbers(row) -> tuple[str, str]:
    m = PR_NOTE.match(row.get("last_failure", "") or "")
    return (m.group(1), m.group(2) or "") if m else ("", "")


def require_batch_branch(args, allow_v3=False) -> str:
    expected = branch_for(args.batch, host_name(args))
    branch = current_branch()
    if branch == BRANCH and allow_v3:
        return branch
    if branch != expected:
        raise Refused("this checkout is on %r; batch %s runs on its branch %r in both repos "
                      "(claim.py start %s <ids...> creates it from origin/v3)"
                      % (branch or "(detached)", args.batch, expected, args.batch))
    return branch


def cmd_start(args) -> int:
    branch = branch_for(args.batch, host_name(args))
    git("fetch", "--no-recurse-submodules", REMOTE)
    sub = submodule_dir()
    if sub:
        git("fetch", "--no-recurse-submodules", REMOTE, repo=sub)
    for repo in [REPO] + ([sub] if sub else []):
        if current_branch(repo) == branch:
            print("%s: already on %s" % (repo, branch))
            continue
        if git("rev-parse", "--verify", "-q", "refs/heads/" + branch, check=False, repo=repo).returncode == 0:
            raise Refused("%s: branch %s exists but is not checked out: switch to it yourself "
                          "(git -C %s switch %s) -- start never moves you onto an existing branch"
                          % (repo, branch, repo, branch))
        git("switch", "--no-track", "-c", branch, upstream(), repo=repo)
        print("%s: created %s from %s" % (repo, branch, upstream()))
    return cmd_batch(args)


def cmd_batch(args) -> int:
    owner = "%s@%s" % (args.batch, host_name(args))
    require_batch_branch(args, allow_v3=True)
    select_claim_repo()
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
    follow_v3_if_untouched()
    return EXIT_OK


def release_rows(rows, batch, test_ids, me, stale, note, any_host=False):
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
        row["last_failure"] = note if note is not None else strip_pr_note(row.get("last_failure", ""))
        released.append("%s -> %s" % (row["test_id"], row["status"]))
    return released, refused


def cmd_release(args) -> int:
    me = host_name(args)
    select_claim_repo()
    outcome = {"released": [], "refused": []}

    def apply():
        rows = load_queue()
        released, refused = release_rows(rows, args.batch, args.test_ids, me, args.stale, args.note)
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


def cmd_status(args) -> int:
    git("fetch", "--no-recurse-submodules", REMOTE)
    rows = v3_rows()
    batches = {}
    for row in rows:
        if row.get("status") == "claimed":
            batches.setdefault(quest_queue.claim_batch(row.get("owner", "")), []).append(row)
    print("claims on %s (%d rows, %d batches):" % (upstream(), sum(len(v) for v in batches.values()), len(batches)))
    for batch, held in sorted(batches.items()):
        hosts = sorted({r["owner"].split("@", 1)[1] if "@" in r["owner"] else "?" for r in held})
        ages = [a for a in (quest_queue.claim_age_hours(r) for r in held) if a is not None]
        pr, content_pr = pr_numbers(held[0])
        print("  %-24s host %-18s %2d rows  oldest %s%s  %s" % (
            batch, ",".join(hosts), len(held), "%.1f h" % max(ages) if ages else "?",
            "  STALE" if ages and max(ages) > quest_queue.STALE_CLAIM_HOURS else "",
            ("PR #%s%s" % (pr, " + OSRS-Content #%s" % content_pr if content_pr else "")) if pr else "no PR"))
        print("      %s" % " ".join(r["test_id"] for r in held))
    if not args.batch:
        return EXIT_OK
    host = host_name(args)
    expected = branch_for(args.batch, host)
    branch = current_branch()
    if branch != expected:
        print("status %s: REFUSED -- this checkout is on %r, the batch runs on %r (both repos)"
              % (args.batch, branch or "(detached)", expected))
        return EXIT_REFUSED
    owner = "%s@%s" % (args.batch, host)
    held = {r["test_id"] for r in batches.get(args.batch, []) if r.get("owner") == owner}
    missing = [t for t in (args.require or []) if t not in held]
    if missing or not held:
        print("status %s: MISSING -- not claimed by %s on %s: %s" % (
            args.batch, owner, upstream(), " ".join(missing) or "(the batch holds no row)"))
        return EXIT_NOTHING
    print("status %s: ok -- branch %s; %s holds %s on %s" % (
        args.batch, branch, owner, " ".join(sorted(held)), upstream()))
    return EXIT_OK


# ------------------------------------------------------------------ pr-prepare

def conflicted_paths(repo) -> list[str]:
    return [p for p in git("diff", "--name-only", "--diff-filter=U", repo=repo).stdout.splitlines() if p]


def merging(repo) -> bool:
    return git("rev-parse", "-q", "--verify", "MERGE_HEAD", check=False, repo=repo).returncode == 0


def alloc_lines(text: str) -> list[tuple[int, str]]:
    out = []
    for line in (text or "").splitlines():
        m = re.match(r"^(\d+)=(\S+)\s*$", line)
        if m:
            out.append((int(m.group(1)), m.group(2)))
    return out


def stage_text(repo, stage: int, rel: str) -> str:
    proc = git("show", ":%d:%s" % (stage, rel), check=False, repo=repo)
    return proc.stdout if proc.returncode == 0 else ""


def rename_token(old: str, new: str, repos) -> list[str]:
    """Rename a whole-word token in every tracked file that holds it."""
    touched = []
    for repo in repos:
        found = git("grep", "-l", "-w", "-F", old, check=False, repo=repo).stdout.splitlines()
        for rel in found:
            path = Path(repo) / rel
            text = path.read_text(encoding="utf-8")
            path.write_text(re.sub(r"\b%s\b" % re.escape(old), new, text), encoding="utf-8")
            git("add", "--", rel, repo=repo)
            touched.append("%s:%s" % (Path(repo).name, rel))
    return touched


def resolve_alloc(sub: Path, rel: str, notes: list) -> list[str]:
    """v3's copy of a conflicted pack/<ns>.alloc, plus the branch's own new
    lines re-allocated on top of it. Returns the names the branch allocated
    that the pack rebuild must still produce."""
    ns = ALLOC_PATH.search(rel).group(2)
    base, ours, theirs = (stage_text(sub, s, rel) for s in (1, 2, 3))
    base_lines = set(alloc_lines(base))
    ours_new = [(i, n) for i, n in alloc_lines(ours) if (i, n) not in base_lines]
    their_map = dict(alloc_lines(theirs))
    their_names = set(their_map.values())
    text = theirs if theirs.endswith("\n") or not theirs else theirs + "\n"
    prefix = VAR_PREFIX.get(ns)
    pending = []
    if not prefix:
        pending = [n for _, n in ours_new if n not in their_names]
        notes.append("%s: took v3's copy; %d branch name(s) for the rebuild to re-allocate: %s"
                     % (rel, len(pending), " ".join(pending) or "-"))
    else:
        used = set(their_map) | {i for i, _ in ours_new}
        nxt = max(used) + 1 if used else 0
        for ident, name in sorted(ours_new):
            if name in their_names:
                continue
            m = re.match(r"^%s(\d+)_(.+)$" % prefix, name)
            if ident not in their_map:
                text += "%d=%s\n" % (ident, name)
                their_map[ident] = name
                notes.append("%s: kept %d=%s (id still free on v3)" % (rel, ident, name))
                continue
            new_name = "%s%d_%s" % (prefix, nxt, m.group(2)) if m else name
            text += "%d=%s\n" % (nxt, new_name)
            their_map[nxt] = new_name
            touched = rename_token(name, new_name, [sub, REPO]) if m else []
            notes.append("%s: %d=%s collided with v3's %d=%s -> renumbered %d=%s; renamed in %s"
                         % (rel, ident, name, ident, their_map.get(ident), nxt, new_name,
                            ", ".join(touched) or "no tracked file"))
            nxt += 1
    (sub / rel).write_text(text, encoding="utf-8")
    git("add", "--", rel, repo=sub)
    return pending


def run_rebuild() -> None:
    cmd = os.environ.get("QUEST_PACK_REBUILD") or "make -C %s torirsserver-scripts" % shlex.quote(str(REPO / "src"))
    print("pr-prepare: rebuilding the pack: %s" % cmd)
    proc = subprocess.run(cmd, shell=True, cwd=str(REPO), capture_output=True, text=True)
    if proc.returncode != 0:
        raise Refused("pack rebuild failed (%d): %s" % (proc.returncode, (proc.stderr or proc.stdout).strip()[-1500:]))


def abort_merges(repos) -> None:
    for repo in repos:
        if repo and merging(repo):
            git("merge", "--abort", check=False, repo=repo)


def pr_prepare(batch: str, branch: str, push: bool) -> dict:
    """Merge origin/v3 into the batch branch in both repos; returns what it did."""
    sub = submodule_dir()
    notes = []
    git("fetch", "--no-recurse-submodules", REMOTE)
    if sub:
        git("fetch", "--no-recurse-submodules", REMOTE, repo=sub)
    sub_head_before = git("rev-parse", "HEAD", repo=sub).stdout.strip() if sub else ""
    # 1. the parent: TSVs row by row, the gitlink after the submodule.
    proc = git("merge", "--no-commit", upstream(), check=False)
    parent_conflicts = conflicted_paths(REPO) if proc.returncode != 0 else []
    if proc.returncode != 0 and not parent_conflicts:
        raise Refused("git merge %s refused in %s: %s" % (upstream(), REPO, (proc.stderr or proc.stdout).strip()))
    other = [p for p in parent_conflicts if p not in TSV_RELS and p != SUBMODULE]
    if other:
        abort_merges([REPO])
        raise Refused("merging %s into %s conflicts outside the TSVs and the gitlink: %s -- merge "
                      "aborted; resolve by hand (only files this batch changed), commit, run again"
                      % (upstream(), branch, ", ".join(other)))
    # 2. the submodule: pack/*.alloc by v3's copy + re-allocation.
    pending, alloc_conflicts = [], []
    if sub:
        proc = git("merge", "--no-commit", upstream(), check=False, repo=sub)
        sub_conflicts = conflicted_paths(sub) if proc.returncode != 0 else []
        if proc.returncode != 0 and not sub_conflicts:
            abort_merges([REPO])
            raise Refused("git merge %s refused in %s: %s" % (upstream(), sub, (proc.stderr or proc.stdout).strip()))
        alloc_conflicts = [p for p in sub_conflicts if ALLOC_PATH.search(p)]
        other = [p for p in sub_conflicts if p not in alloc_conflicts]
        if other:
            abort_merges([sub, REPO])
            raise Refused("merging %s into %s's %s conflicts outside pack/*.alloc: %s -- both merges "
                          "aborted; resolve by hand, commit, run again" % (upstream(), SUBMODULE, branch, ", ".join(other)))
        for rel in alloc_conflicts:
            pending += resolve_alloc(sub, rel, notes)
    for rel in parent_conflicts:
        if rel in TSV_RELS:
            notes.append(merge_tsv(rel, own_batch=batch))
    if alloc_conflicts:
        run_rebuild()
        have = set()
        for rel in alloc_conflicts:
            have |= {n for _, n in alloc_lines((sub / rel).read_text(encoding="utf-8"))}
            git("add", "--", rel, repo=sub)
        missing = [n for n in pending if n not in have]
        if missing:
            abort_merges([sub, REPO])
            raise Refused("the pack rebuild did not re-allocate the branch's names %s -- both merges "
                          "aborted" % " ".join(missing))
        notes.append("rebuild re-allocated: %s" % (" ".join(
            "%s=%s" % (i, n) for r in alloc_conflicts for i, n in alloc_lines((sub / r).read_text(encoding="utf-8"))
            if n in pending) or "nothing pending"))
    sub_moved = False
    if sub:
        if merging(sub):
            git("commit", "--no-edit", "-m", "Merge %s into %s [pr-prepare %s]" % (upstream(), branch, batch), repo=sub)
            sub_moved = True
        else:
            sub_moved = git("rev-parse", "HEAD", repo=sub).stdout.strip() != sub_head_before
    # 3. the gitlink = the merged submodule HEAD.
    if sub and (SUBMODULE in parent_conflicts or sub_moved
                or git("diff", "--quiet", "HEAD", "--", SUBMODULE, check=False).returncode != 0):
        git("add", "--", SUBMODULE)
    if merging(REPO):
        git("commit", "--no-edit", "-m", "Merge %s into %s [pr-prepare %s]" % (upstream(), branch, batch))
    elif git("diff", "--cached", "--quiet", check=False).returncode != 0:
        git("commit", "-m", "quests: %s -- OSRS-Content gitlink after pr-prepare" % batch, "--", SUBMODULE)
    result = {"notes": notes, "parent_ahead": count("%s..HEAD" % upstream()),
              "sub_ahead": count("%s..HEAD" % upstream(), sub) if sub else 0}
    if push:
        push_branch(branch, sub, result)
    return result


def push_branch(branch, sub, result) -> None:
    if sub and result["sub_ahead"]:
        git("push", REMOTE, "HEAD:refs/heads/%s" % branch, repo=sub)
        print("pushed %s %s: %s" % (SUBMODULE, branch, git("log", "-1", "--format=%h %s", repo=sub).stdout.strip()))
    git("push", REMOTE, "HEAD:refs/heads/%s" % branch)
    print("pushed %s: %s" % (branch, git("log", "-1", "--format=%h %s").stdout.strip()))


def cmd_pr_prepare(args) -> int:
    branch = require_batch_branch(args)
    result = pr_prepare(args.batch, branch, args.push)
    for note in result["notes"]:
        print("pr-prepare: %s" % note)
    print("pr-prepare %s: %s is %d commit(s) ahead of %s, %s %d; mergeable" % (
        args.batch, branch, result["parent_ahead"], upstream(), SUBMODULE, result["sub_ahead"]))
    return EXIT_OK


# ------------------------------------------------------------------ done / pr-sync

def gh(*args, cwd=None):
    proc = subprocess.run(["gh"] + list(args), capture_output=True, text=True, cwd=str(cwd or REPO))
    if proc.returncode != 0:
        raise Refused("gh %s failed (%d): %s" % (" ".join(args[:3]), proc.returncode, (proc.stderr or proc.stdout).strip()))
    return proc.stdout


def gh_view(ref: str, fields: str, cwd=None):
    proc = subprocess.run(["gh", "pr", "view", ref, "--json", fields], capture_output=True, text=True,
                          cwd=str(cwd or REPO))
    if proc.returncode != 0:
        return None
    return json.loads(proc.stdout)


def open_or_update_pr(branch: str, title: str, body: str, cwd) -> str:
    """Returns the PR number of the open PR from <branch> into v3."""
    view = gh_view(branch, "number,state,url", cwd)
    body_path = REPO / "build" / ("pr_body_%s_%s.md" % (Path(cwd).name, branch))
    body_path.parent.mkdir(parents=True, exist_ok=True)
    body_path.write_text(body, encoding="utf-8")
    if view and view.get("state") == "OPEN":
        gh("pr", "edit", str(view["number"]), "--title", title, "--body-file", str(body_path), cwd=cwd)
        print("updated PR #%s %s" % (view["number"], view.get("url", "")))
        return str(view["number"])
    out = gh("pr", "create", "--base", BRANCH, "--head", branch, "--title", title,
             "--body-file", str(body_path), cwd=cwd).strip()
    m = re.search(r"/pull/(\d+)", out)
    if not m:
        raise Refused("gh pr create printed no PR url: %s" % out)
    print("opened PR #%s %s" % (m.group(1), out.splitlines()[-1]))
    return m.group(1)


def pr_body(batch, ids, branch_rows, content_pr, branch) -> str:
    lines = ["Batch `%s` (branch `%s`), per docs/QUEST_ORCHESTRATOR.md: the quests below were claimed on "
             "v3 for this batch; this PR carries everything the batch did. Verdicts as the branch's "
             "QUEUE.tsv records them:" % (batch, branch), "",
             "| quest | tier | verdict | note |", "|---|---|---|---|"]
    for test_id in ids:
        row = quest_queue.find_row(branch_rows, test_id) or {}
        note = (row.get("last_failure") or "").replace("|", "/").replace("\n", " ")
        lines.append("| %s | %s | %s | %s |" % (test_id, row.get("tier", ""), row.get("status", "?"),
                                                note[:240] + ("..." if len(note) > 240 else "")))
    sheets = []
    path = REPO / BATCHES_REL
    if path.exists():
        for row in csv.DictReader(open(path, encoding="utf-8"), delimiter="\t"):
            name = row.get("batch", "")
            if name == batch or name.startswith(batch + "-"):
                sheets.append("- %s: %s (green %s, blocked %s, content_bug %s)" % (
                    name, row.get("artifact", ""), row.get("green", ""), row.get("blocked", ""), row.get("content_bug", "")))
    lines += ["", "Contact sheets:"] + (sheets or ["- none recorded in BATCHES.tsv"])
    if content_pr:
        lines += ["", "**Merge OSRS-Content PR #%s first, with a merge commit (never squash):** this PR's "
                  "OSRS-Content gitlink points at that branch's tip." % content_pr]
    lines += ["", "After both merge, `claim.py pr-sync` (every launch) confirms no row of this batch is "
              "left claimed on v3.", "", "🤖 Generated with [Claude Code](https://claude.com/claude-code)"]
    return "\n".join(lines) + "\n"


def cmd_done(args) -> int:
    branch = require_batch_branch(args)
    host = host_name(args)
    owner = "%s@%s" % (args.batch, host)
    result = pr_prepare(args.batch, branch, push=False)
    for note in result["notes"]:
        print("pr-prepare: %s" % note)
    ids = [r["test_id"] for r in v3_rows() if r.get("status") == "claimed" and r.get("owner") == owner]
    if not ids:
        raise Refused("batch %s holds no row on %s under %s: nothing to finish" % (args.batch, upstream(), owner))
    # Rows the batch never gave a verdict go back to todo on the branch.
    rows = load_queue(REPO)
    released = []
    for row in rows:
        if row["test_id"] in ids and row.get("status") == "claimed":
            old = strip_pr_note(row.get("last_failure", ""))
            quest_queue.release_row(row)
            row["last_failure"] = "%s ended without a verdict%s" % (args.batch, " -- " + old if old else "")
            released.append(row["test_id"])
    if released:
        save_queue(rows, REPO)
        commit_path(QUEUE_REL, "quests: %s done -- released without a verdict: %s" % (args.batch, " ".join(released)), REPO)
        print("released on the branch (no verdict): %s" % " ".join(released))
    sub = submodule_dir()
    result["parent_ahead"] = count("%s..HEAD" % upstream())
    push_branch(branch, sub, result)
    content_pr = ""
    if sub and result["sub_ahead"]:
        content_pr = open_or_update_pr(branch, "quests: %s -- content" % args.batch,
                                       "Content for batch `%s`; the parent PR from `%s` points its gitlink here. "
                                       "Merge this one first, with a merge commit.\n\n"
                                       "🤖 Generated with [Claude Code](https://claude.com/claude-code)\n"
                                       % (args.batch, branch), sub)
    branch_rows = load_queue(REPO)
    greens = [t for t in ids if (quest_queue.find_row(branch_rows, t) or {}).get("status") == "green"]
    title = "quests: %s -- %s" % (args.batch, ", ".join(greens) + " green" if greens else "no greens")
    pr = open_or_update_pr(branch, title, pr_body(args.batch, ids, branch_rows, content_pr, branch), REPO)
    note = "PR #%s pending merge%s" % (pr, " (OSRS-Content #%s)" % content_pr if content_pr else "")
    select_claim_repo()

    def apply():
        rows = load_queue()
        for row in rows:
            if row["test_id"] in ids and row.get("status") == "claimed" and row.get("owner") == owner:
                old = strip_pr_note(row.get("last_failure", ""))
                row["last_failure"] = note + (" -- " + old if old else "")
        save_queue(rows)
        return None

    transact(QUEUE_REL, apply, "quests: %s -- %s" % (args.batch, note))
    # The note just changed v3's claim rows: merge it in so the PR stays mergeable.
    again = pr_prepare(args.batch, branch, push=True)
    for n in again["notes"]:
        print("pr-prepare: %s" % n)
    print("done %s: %s on %s's rows %s" % (args.batch, note, upstream(), " ".join(ids)))
    return EXIT_OK


def cmd_pr_sync(args) -> int:
    git("fetch", "--no-recurse-submodules", REMOTE)
    rows = v3_rows()
    pending = {}
    for row in rows:
        if row.get("status") == "claimed":
            pr, content_pr = pr_numbers(row)
            if pr:
                pending.setdefault((quest_queue.claim_batch(row["owner"]), pr, content_pr), []).append(row["test_id"])
    if not pending:
        print("pr-sync: no claimed row on %s carries a PR -- nothing to settle" % upstream())
        return EXIT_OK
    sub = submodule_dir()
    settle = []
    for (batch, pr, content_pr), ids in sorted(pending.items()):
        parent = gh_view(pr, "state,mergedAt,url,mergeable") or {}
        content = (gh_view(content_pr, "state,mergedAt,url", sub or REPO) or {}) if content_pr else {"state": "MERGED"}
        states = (parent.get("state", "?"), content.get("state", "?"))
        label = "PR #%s%s" % (pr, " + OSRS-Content #%s" % content_pr if content_pr else "")
        if states == ("MERGED", "MERGED"):
            settle.append((batch, ids, "%s merged without a verdict for this row" % label))
            print("pr-sync %s: %s merged; %d row(s) still claimed -- releasing" % (batch, label, len(ids)))
        elif "CLOSED" in states:
            settle.append((batch, ids, "%s closed unmerged" % label))
            print("pr-sync %s: %s closed unmerged -- releasing %s" % (batch, label, " ".join(ids)))
        else:
            hint = " (CONFLICTING: run claim.py done %s on its branch)" % batch if parent.get("mergeable") == "CONFLICTING" else ""
            print("pr-sync %s: %s pending (parent %s, content %s)%s" % (batch, label, states[0], states[1], hint))
    if not settle:
        return EXIT_OK
    select_claim_repo()

    def apply():
        rows = load_queue()
        for batch, ids, note in settle:
            release_rows(rows, batch, ids, host_name(args), False, note, any_host=True)
        save_queue(rows)
        return None

    transact(QUEUE_REL, apply, "quests: pr-sync -- %s" % ", ".join(b for b, _, _ in settle))
    return EXIT_OK


# ------------------------------------------------------------------ content lock

def read_lock() -> str:
    path = CLAIM_REPO / LOCK_REL
    return path.read_text(encoding="utf-8").strip() if path.exists() else ""


def write_lock(text: str) -> None:
    (CLAIM_REPO / LOCK_REL).write_text(text + ("\n" if text else ""), encoding="utf-8")


def cmd_content_lock(args) -> int:
    mine = "%s@%s" % (args.pass_name, host_name(args))
    select_claim_repo()
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
    select_claim_repo()
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


def merge_tsv(rel_path: str, own_batch: str | None = None) -> str:
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
            if own_batch and st == "claimed" and quest_queue.claim_batch(rt.get("owner", "")) == own_batch:
                win = ro       # the batch's own verdict/note beats its own claim (+ PR note)
            elif QUEUE_RANK.get(so, 0) != QUEUE_RANK.get(st, 0):
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
        print(merge_tsv(rel, own_batch=args.own_batch))
    print("now finish the merge: git commit --no-edit")
    return EXIT_OK


def main() -> int:
    global REPO, CLAIM_REPO
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", help="host name in the owner (default: $QUEST_HOST or the short hostname)")
    ap.add_argument("--repo", type=Path, help="checkout to act on (default: the one holding this tool)")
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("start", help="create the batch branch in both repos from origin/v3, then claim")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="+")
    p.set_defaults(func=cmd_start)
    p = sub.add_parser("batch", help="claim rows for a batch")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="+")
    p.set_defaults(func=cmd_batch)
    p = sub.add_parser("release", help="restore the batch's still-claimed rows")
    p.add_argument("batch")
    p.add_argument("test_ids", nargs="*")
    p.add_argument("--note", help="last_failure for the released rows (why another machine may take them)")
    p.add_argument("--stale", action="store_true", help="release another host's claim older than 24 h")
    p.set_defaults(func=cmd_release)
    p = sub.add_parser("status", help="every claim on origin/v3; with --batch, the cards' gate")
    p.add_argument("--batch")
    p.add_argument("--require", nargs="*", default=[])
    p.set_defaults(func=cmd_status)
    p = sub.add_parser("pr-prepare", help="merge origin/v3 into the batch branch in both repos")
    p.add_argument("batch")
    p.add_argument("--push", action="store_true", help="push the branch in both repos afterwards")
    p.set_defaults(func=cmd_pr_prepare)
    p = sub.add_parser("done", help="finish a batch: pr-prepare, the two PRs, the v3 PR note")
    p.add_argument("batch")
    p.set_defaults(func=cmd_done)
    p = sub.add_parser("pr-sync", help="settle claims whose PRs merged or closed")
    p.set_defaults(func=cmd_pr_sync)
    p = sub.add_parser("content-lock", help="take test/quests/CONTENT_LOCK (whole-pack changes only)")
    p.add_argument("pass_name", metavar="pass")
    p.set_defaults(func=cmd_content_lock)
    p = sub.add_parser("content-unlock", help="release test/quests/CONTENT_LOCK")
    p.add_argument("pass_name", metavar="pass")
    p.add_argument("--stale", action="store_true", help="release the same pass name from another host after 24 h")
    p.set_defaults(func=cmd_content_unlock)
    p = sub.add_parser("merge-tsv", help="resolve conflicted TSVs row by row during a merge")
    p.add_argument("--own-batch", help="a QUEUE row theirs has claimed by this batch takes ours when ours changed it")
    p.add_argument("paths", nargs="+")
    p.set_defaults(func=cmd_merge_tsv)
    args = ap.parse_args()
    if args.repo:
        REPO = args.repo.resolve()
    CLAIM_REPO = REPO
    try:
        return args.func(args)
    except Refused as exc:
        print("claim.py: refused: %s" % exc, file=sys.stderr)
        return EXIT_REFUSED


if __name__ == "__main__":
    sys.exit(main())
