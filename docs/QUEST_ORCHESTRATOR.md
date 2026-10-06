# Quest orchestrator -- running the quest loop on any machine

This is what a Fable orchestrator does, on whichever machine it runs. Several machines run
the loop against one origin (`v3` in both repos) at once. The tool is
`tools/quest_gate/claim.py`; its docstring is the reference.

## The model: one batch, one branch, one PR

The unit of work is a **batch of quests that one machine owns for the whole lifecycle**:
content parity, seam fixes, authoring, review, sampling, green.

- **`v3` carries only the claim ledger**: `test/quests/QUEUE.tsv` rows marked
  `claimed` / `<batch>@<host>`. A claim on `v3` is the lock -- two machines never hold the
  same quest. `claim.py` writes the ledger on `origin/v3` itself (through a throwaway
  worktree), so it works from any branch at any time.
- **Two kinds of commit go to `v3` directly** (owner, 2026-10-01): `claim.py`'s ledger
  commits (claim, release, `done --pr`, `pr-sync`), and **protocol work** -- this document,
  the workflow cards and the shared quest tooling (see "Changing the protocol or shared
  tooling"). Protocol work never rides a batch branch or a batch PR. Everything else -- the
  quest work of a batch -- goes on the batch branch and reaches `v3` only through its PRs.
- **Everything else lives on the batch branch** `<batch>` -- the same name in the parent and
  in `OSRS-Content` -- and reaches `v3` through **one PR per batch** (`claim.py done`).
  Content, engine and driver fixes, tests, evidence, docs, relay files: all on the branch.
  The closers and the sampler commit on the branch and push the branch; they never touch
  `v3` and never merge `v3` into the branch mid-batch.
- **No content lock.** Each batch allocates compiler ids on its own branch; when the batch
  finishes, `claim.py done` merges `origin/v3` into the branch taking `v3`'s `pack/*.alloc`
  files and rebuilds the pack, so the batch's new ids land after everything `v3` already has.
  `CONTENT_LOCK` stays for the older serialised model and is otherwise untouched.
- Batch names are `<hostname>-b<N>`, never reused. The passes inside a batch are named
  after it: `<batch>-parity`, `<batch>-seam1`, `<batch>-seam2`, ... and the author batch
  is `<batch>` itself.
- **One batch per checkout at a time.** A checkout sits on one branch, and the passes load
  the driver and the content live from it. A second batch on the same machine needs its own
  checkout (and its own `WT` in that checkout's workflow copies).

## The loop

1. **Content parity** (`content_parity.workflow.js`) makes each quest's content match its
   source.
2. **Seam pass** (`seam_pass.workflow.js`) fixes the driver, engine and content seams that
   the blocked rows name.
3. **Author batch** (`author_batch.workflow.js`) has Sonnet authors write the tests. Then a
   reviewer per quest, one queue write, an Opus sampler, and a contact sheet.

- **Inside a batch the three never overlap on one machine.** Authors load
  `script/plugins/quest_driver/*.lua` live from the checkout, so a seam agent's half-written
  function crashes an author mid-quest. The order is: parity, then seam passes for the
  seams parity names, then the author batch, then seam passes for what the batch blocked on,
  then the author batch again, until each quest is green or honestly blocked.
- **Batches run on several machines at once**, each on rows it has claimed, each on its own
  branch. Another machine's half-finished content is invisible to you until its PR merges.
- **Every machine runs the whole loop.** No machine is "authors only" or "content only":
  any tier is fine, the batch runs its own parity. Never wait on another machine's claim;
  if the rows you wanted are taken, take others.

## Who runs what

Read from the three workflow scripts; each `agent()` call pins its model.

| role | script | model |
|---|---|---|
| quest author, relay leg runner | author_batch | Sonnet 5.5 (`claude-sonnet-5-5`, the `author_model` default) |
| reviewer, claim agent, queue writer, sheet builder | author_batch | Sonnet 5.5 |
| state reader (every script) | all three | Sonnet 5.5 |
| sampler, and its closer when nothing was accepted | author_batch | Opus |
| parity worker | content_parity | Sonnet 5.5 (the `worker_model` default) |
| seam triage, seam fixer | seam_pass | Opus (triage transcribing a given file: Sonnet 5.5) |
| parity closer, seam closer | content_parity, seam_pass | Opus |
| orchestrator | -- | the session the owner started (Fable or Opus); never an agent model |

**An orchestrator never passes `author_model` or `worker_model`.** The defaults are the
owner's pins. `claude-sonnet-5-5` is spelled out because the `sonnet` alias is still Sonnet 5.

Until the card update of 2026-10-01 is on a machine, a relay started there keeps its leg
records under `build/` and cannot be resumed elsewhere. After the update,
`test/quests/wip/<id>/` is the shared record.

## Launch checklist (every batch)

1. **Start level with `v3`, settle finished batches, see who holds what:**
   ```sh
   git fetch origin && git checkout v3 && git merge --ff-only origin/v3
   git -C OSRS-Content fetch origin && git -C OSRS-Content checkout v3 \
       && git -C OSRS-Content merge --ff-only origin/v3
   python3 tools/quest_gate/claim.py pr-sync     # releases leftover claims of merged batches
   python3 tools/quest_gate/claim.py status      # claims, lock, each batch branch's state
   ```
2. **Pick 4-8 unclaimed rows** (`python3 tools/quest_gate/queue.py summary`): status `todo`,
   `blocked` or `content_bug`, no owner. Any tier.
3. **Pick a fresh, machine-unique batch name** `<hostname>-b<N>`. It must appear nowhere:
   `claim.py status`, `git ls-remote --heads origin | grep <name>`, `test/quests/BATCHES.tsv`,
   `ls build/author_state build/seam_state build/parity_state`.
4. **Cut the branch and claim:**
   ```sh
   git checkout -b <batch> origin/v3
   git -C OSRS-Content checkout -b <batch> origin/v3
   python3 tools/quest_gate/claim.py batch <batch> <ids...>
   ```
   `claim.py` tells you which rows it kept; a row another machine took in between is
   dropped and named. Launch only with the rows it kept. A row green on `v3` is never
   claimable, except a seam reopen: once the batch branch's `QUEUE.tsv` reopens it (non-green,
   owner `<batch>`), claim it with `claim.py batch <batch> --reopened <ids...>` -- the tool
   reads the branch's copy (origin's when pushed, else the local ref), claims only rows that
   copy shows reopened by this batch, and records `green|<old owner>` in claim_prev so
   `release` restores the green unchanged.
5. **Run the flow on the branch** with the Workflow tool and the repo's scripts (saved
   copies with the `WT` constant set to this checkout): `content_parity.workflow.js` as pass
   `<batch>-parity`; `seam_pass.workflow.js` as `<batch>-seamN` for the seams parity or an
   author batch names (write the triage file yourself); `author_batch.workflow.js` as
   `<batch>`, with `relay` = the leg counts `ladder.py` prints for quests over ~30 steps.
   Never pass `author_model` or `worker_model`. Launch from a substantive turn, never from a
   bare "Hello?": workflow agents see the turn's user message.
6. **After each author batch** publish the contact sheet (Artifact tool: root = the sheet
   dir, every `*.webp`, icon "map"; two publishes if over 64 MB) and append its row to
   `test/quests/BATCHES.tsv` on the branch.

## Closing a pass (the closers and the sampler)

The parity closer, the seam closer and the batch's sampler commit on the batch branch:

1. Commit the submodule first (explicit paths) and push the branch:
   `git -C OSRS-Content push -u origin <batch>`.
2. In the parent, stage the gitlink of the committed submodule (`git add OSRS-Content`) with
   the pass's own files, commit, and push: `git push -u origin <batch>`.
3. A rejected push on the batch branch means another agent of this batch pushed first:
   `git pull --no-rebase origin <batch>` in that repo, then push again. Never force.
4. **Never** `git merge origin/v3` into the branch mid-batch, never push quest work to
   `v3`, never take or release the content lock. The one thing a closer or sampler pushes
   to `v3` is protocol work: the doc lines it folds into `docs/quest_authoring/` (see
   "Changing the protocol or shared tooling"), committed in a `v3` worktree and then
   cherry-picked onto the branch. `QUEUE.tsv` rows the pass writes (`queue.py set`,
   the reopen of a row after parity) are the batch's view and ride the branch; `v3`'s claim
   stays until the PR merges and merge-tsv lets the verdict beat the claim. A reopen of a
   row that is green on `v3` has no `v3` claim yet: push the branch, then claim it with
   `claim.py batch <batch> --reopened <ids...>`, so another machine cannot take it.

## Finishing a batch

```sh
python3 tools/quest_gate/claim.py done <batch>                 # on the branch, both repos clean
# a change that is not the batch's and must never be staged (the owner's deleted zip):
#   claim.py done <batch> --ignore lib/emsdk-macos-toolchain.zip
```
**Before `done`, check for var-id collisions.** `done` takes `v3`'s `pack/*.alloc` and
rebuilds, which re-allocates the batch's new ids after `v3`'s. That is right for every
namespace except varp, varbit and varc: their NAMES carry the id (`varp7213_b_thing`, PR #99),
so a collision leaves a name whose prefix no longer matches its id, and
`OSRS-Content/tools/var_prefix_names.py` would then mint `varp7214_varp7213_b_thing`. So,
after `git -C OSRS-Content merge origin/v3` and before the rebuild: for each var this batch
added whose id `v3` now uses for another name, rename it in BOTH repos to a free id
(`git grep -l <old name>` in the parent and the submodule, rewrite each by explicit path,
update the `.varp`/`.varbit`/`.varc` header and the alloc line), rebuild, and confirm
`python3 OSRS-Content/tools/var_prefix_names.py` (dry run) prints `0 name(s) in 0 file(s)`.
`done` does not do this for you.
`done` merges `origin/v3` into the branch in both repos -- `pack/*.alloc` conflicts take
`v3`'s copy and the pack is rebuilt (`make -C src torirsserver-scripts`) so the batch's ids
are re-allocated after `v3`'s; `QUEUE.tsv`, `BATCHES.tsv` and `PARITY.tsv` conflicts go
through merge-tsv; any other conflict refuses with the merge aborted, for you to resolve on
the branch by hand -- then pushes both branches and prints the honest per-quest table
(`build/<batch>.pr.md`). Open **two PRs, content first**, because the parent's gitlink must point at a commit
that is on `OSRS-Content`'s `v3`:
1. `OSRS-Content`: `gh pr create --repo MRobertEvers/OSRS-Content --base v3 --head <batch>`.
2. The parent: `gh pr create --base v3 --head <batch>`, with the table as its body and a
   first line naming the content PR and "merge the content PR first, with a merge commit".
Never squash or rebase either PR: a squashed content PR leaves the parent's gitlink on a
commit that is not on `v3`. Then stamp the claims with the PARENT PR's number:
```sh
python3 tools/quest_gate/claim.py done <batch> --pr <N> --no-build
```
**The orchestrator merges its own PRs and starts the next batch without waiting (owner,
2026-10-01/02).** When every quest of the batch is green, or honestly blocked / content_bug
with its claim released (`claim.py release <batch> <id> --note "<why>"`, so another machine
may take it), run `claim.py done`, take both PRs out of draft with the final table, and merge
them once their checks are green and they have no conflict: **the OSRS-Content PR first, then
the parent PR, both as merge commits** (`gh pr merge <n> --merge`; never `--squash` or
`--rebase`, which would strand the parent's gitlink). Then `claim.py pr-sync`, fetch and
fast-forward `v3` in both repos, and claim and launch `<hostname>-b<N+1>` in the same turn.
Report the table, the PRs and the next batch to the owner; do not wait for an answer.

When the PR merges, `v3` receives the batch's verdicts (merge-tsv: a verdict beats a claim),
and the next `claim.py pr-sync` on any machine releases whatever rows the batch left
`claimed`. A PR closed without merging is settled with `claim.py pr-sync --closed <batch>`.

## Changing the protocol or shared tooling

**Protocol work is committed directly to `v3`** (owner, 2026-10-01), so both machines run
one protocol. Protocol work is: this document, `docs/QUEST_SUITE_KIT.md`,
`docs/quest_authoring/` (relay.md, INDEX.md and the topic files), `tools/quest_gate/*.py`
(`claim.py`, `queue.py`, `run.py`, `gate.py`, `helper_coverage.py`, `lint_quest.py`,
`new_quest.py`, `ladder.py`, ...) and `tools/quest_gate/*.workflow.js`.

- **Make it in a v3 worktree, not on the batch branch:**
  `git worktree add --detach build/orchestrator/worktrees/v3 origin/v3` (after a fetch),
  edit, commit by explicit path, `git push origin HEAD:v3`; on a rejected push fetch, rebase
  onto `origin/v3` and push again (never force); remove the worktree. The checkout stays on
  its batch branch and a running pass is not disturbed.
- **Look before you change:** `git log origin/v3 -5 -- <the files>`. If the other machine
  changed the same files in the last hour, build on its version; never push a rival
  rewrite. On 2026-10-01 both machines rewrote `claim.py`, the cards and this document at
  once, and one version (PR #100) had to be thrown away; the same evening a commit written
  from a stale copy of this document silently reverted an hour-old protocol change.
  **Edit this document only on a fresh `origin/v3`**, and read `git log -p origin/v3 -3 --
  docs/QUEST_ORCHESTRATOR.md` first so you build on the latest text rather than your copy.
- **A batch branch takes a protocol commit between passes, never during one:**
  `git cherry-pick <sha>` of the `v3` commit (it touches only protocol paths, so it never
  conflicts with quest work), and refresh the card copies in
  `test/quests/orchestrator/<host>/workflows/` from `origin/v3`. A running pass finishes
  under the cards it started with (see "Never stop a running pass").
- **Quest content, tests, evidence, `QUEUE.tsv` verdicts, `BATCHES.tsv`, `PARITY.tsv` and
  `wip/` are never protocol work**, even when a protocol change motivated them.

## Work in progress that follows the quest: `test/quests/wip/<id>/`

`build/` is gitignored, so a relay parked in `build/author_state/<batch>/` is invisible to
every other machine. What another machine needs to resume a quest is tracked here instead:

| file | written by | what it is |
|---|---|---|
| `leg<K>.json` | relay runner of leg K | the leg's report; `outcome: done` means a later runner skips it |
| `leg<K>.progress.md` | relay runner of leg K | the leg's notebook, appended after every run |
| `relay.md` | every relay runner | the hand-off: one `## leg K` block of at most ten lines per leg |
| `parked.lua` | reviewer | a rejected, never-committed test file (the QUEUE row says `RELAY STATE: parked at test/quests/wip/<id>/parked.lua`) |
| other `*.lua` / `*.md` | a seam fixer | a named scratch copy the row points at (`seam36_leg7.lua`) |

- **Not tracked:**
  - Checkpoints (`build/quest_gate/<id>/checkpoints/*.ckpt`). They are binary, hashed
    against this machine's binary and pack, and stale everywhere else. A runner on a new
    machine makes a full run first.
  - Ledgers and shots.
  - A non-relay author's notebook, `${STATE}/<id>.author.progress.md`. It stays in the
    batch state.
- **Who commits wip/:** the sampler, with the queue rows. Reviewers write files there but
  never commit them.
- **When a quest goes green:** the sampler that pushes the green row removes the directory
  with `git rm -r test/quests/wip/<id>`.
- Tools never read wip/ as tests. `run.py --all`, `gate.py --all`, `quest_list.py` and
  `make test-quests` list only top-level `test/quests/*.lua`. To lint with `git ls-files`, give
  it an unquoted shell glob, so that it does not descend into `wip/`.

## The tracked files and who writes them

| file | writers |
|---|---|
| `test/quests/QUEUE.tsv` | on `v3`: `claim.py` only (claim, release, pr-sync, done --pr). On a batch branch: the batch's Queue phase and sampler, the seam closer and parity closer (reopen); the PR brings those rows to `v3`. A reopen of a row green on `v3` is claimed there with `claim.py batch <batch> --reopened <ids>` |
| `test/quests/BATCHES.tsv` | the orchestrator, one row per batch after publishing its contact sheet |
| `tools/quest_gate/PARITY.tsv` | the parity closer |
| `test/quests/CONTENT_LOCK` | `claim.py` only; unused by the batch-branch model (empty when free, else `<pass>@<host> <UTC ISO time>`) |
| `test/quests/wip/<id>/` | see above |

QUEUE.tsv has two claim columns:
- `claimed_at`: the UTC ISO time of the claim, empty when the row is not claimed.
- `claim_prev`: the row's status and owner before the claim, as `<status>|<owner>`, so that
  a release restores the row exactly.

A claimed row has status `claimed` and owner `<batch>@<host>`. Writing any other status
with `queue.py set` ends the claim.

## Where an orchestrator keeps its files (owner, 2026-10-02)

**Never store a work product, even a partial one, in a session scratchpad or under `build/`
(gitignored).** A scratchpad is for one-off throwaway scripts only. An orchestrator's own
files live in the TRACKED folder `test/quests/orchestrator/<host>/`, committed and pushed on
the batch branch:

- `workflows/` -- this machine's copies of the three workflow scripts (the `WT` constant set
  to this checkout). Launch from these paths.
- `backup/<UTC time>/` -- snapshots of uncommitted work taken before anything risky: a
  `git diff HEAD` patch per repo, tarballs of untracked files, the passes' reports and
  notebooks. Commit and push each snapshot as soon as it is taken.

Temporary git worktrees (a cherry-pick onto `v3`, a mutation check per CLAUDE.md) go under
`build/orchestrator/worktrees/` and are removed when done; they hold no work product. The
passes' own state stays where the scripts put it (`build/<kind>_state/<pass>/`), so the
orchestrator copies each finished report and notebook into a backup snapshot.

## Never stop a running pass (owner, 2026-10-02)

**An orchestrator never stops a workflow while any of its agents is working.** Stopping one
throws away every live agent's context and kills its runs; its notebook survives, but the
work since the last note is gone. On 2026-10-02 the vm orchestrator stopped `vm-b1-parity`
twice in fifteen minutes -- once for a protocol change, once for a card it had not finished
checking -- and two parity workers lost their audits both times. The owner's rule since:
never lose work like that.

- **A protocol or card change mid-pass waits.** Fix the scripts on the branch for the NEXT
  launch. The running pass finishes under the cards it started with.
- **When a later phase would do something wrong** (a closer that would push to `v3`, say),
  do not stop the pass early. Launch content passes with `stop_after_parity: true` whenever
  a card change is in flight: the workers finish and persist, the pass returns before the
  closer, and a relaunch without the flag closes with the fixed card and reuses every
  report. If the pass is already running without the flag, wait until the journal shows
  every worker finished and the closer only just started, then stop -- the closer's own
  notebook is the only thing at risk, and it is empty.
- **Before ANY relaunch, read the whole card** you are about to run, every phase, and grep it
  for what the change forbids (`push origin v3`, `HEAD:v3`, `content-lock`, ...). A relaunch
  you then have to stop is the same loss twice.
- A pass is stopped only when the owner says so, or when it is provably doing damage that
  waiting would make worse (pushing to the wrong branch right now). Say what was stopped,
  which agents were live, and what their notebooks kept, in the same turn.

## Recovery

- **A stale claim may be released by anyone.** A claim is stale when it is older than 24
  hours and its batch branch has no commits since (`git log origin/<batch>`). Run
  `claim.py release <batch> --stale`. Without `--stale`, `release` only touches this host's
  claims. Say in your report whose claim you released.
- **A new author round passes `round: N`.** A persisted review is final for its launch, so a
  relaunch of `author_batch.workflow.js` replays it from disk: a quest the reviewer REJECTED last
  round is NOT authored again (the sampler's send-backs are, they are listed in `sample.json`).
  Pass `round: 2` for the second round, `3` for the third: the workflow moves the non-accepted
  author/review files of that launch's `tests` into `build/author_state/<batch>/round<N-1>/` and
  authors them afresh. A launch that replays a non-accepted review says so on a `REPLAY:` line
  and in `replayed` of its result (b55 round 2 authored one of three quests before this existed).
- **A pass killed on its own machine:** relaunch it with the same args. It resumes from
  `build/<kind>_state/<pass>/` and, for relays, `test/quests/wip/`.
- **Moving a batch's quests to another machine:** the old machine runs
  `claim.py release <batch> <ids> --note "<state>"` (or `--stale` from the new one) and pushes
  its branch; the new machine claims them under its own batch name and starts from the
  pushed branch's `wip/` files and content.
- **`claim.py` exit 2** means it refused (an unknown id, a push error, a merge it could not
  resolve, `done` off its branch or with uncommitted work). Nothing was pushed to `v3`. Fix
  the cause and run it again.
- **Exit 3** means nothing to claim, a lock held by someone else, or a release that left
  another host's fresh claim in place.

## The owner's standing rules (from QUEST_SUITE_KIT.md Status and QUEST_HANDOFF_2026-09-29.md)

- **The guide is the spec.** Every Quest Helper step is driven by a real row, or the file
  stops at `t.blocked` with a `content_bug` naming the leg. All of these are rejected:
  - `goto_tile` into or out of a closed space. A goto lands only on an open, walkable tile
    OUTSIDE: every door (a plain one-click house door too, not only a locked or quest-gated
    one), bar counter, stair, gate or puzzle between the player and the target is clicked, on
    every visit, going in and coming out (owner, 2026-10-03) -- including the room a setup
    cheat stands the player in and the run's first goto (owner, 2026-10-05: both are judged
    from the fixture's tile). `helper_coverage` reads the map's walls for this (a walled room,
    a sealed pocket, an only-way gate or crossing loc -- the Wilderness Ditch, the Shantay
    Pass -- on any hop up to 1,200 tiles, the setup placement) but does not grade plain
    climbs, a room over 400 tiles entered past a door that is not the only way, or locs
    content adds at run time: reviewers and samplers judge every goto against the walls
    (`test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/reach.py` and
    `goto_table.py`, which read the checkout they live in);
  - `::give` of an item the guide has you obtain;
  - a debugproc doing quest work;
  - `::setvar` on a quest var mid-run;
  - a narrating `mes()`.
- **Content sources:** LostCity where it has the quest, else the OSRS wiki plus the Quest
  Helper guide. Cite the source for every content edit. Bosses are fought for real.
- **Models:** the table under "Who runs what". Fable orchestrates only: it never does worker
  work, except landing a pass that a closer proved but could not commit.
- **Every batch publishes a contact sheet artifact and a `BATCHES.tsv` row.**
- **Commit and push the batch branch in both repos between passes, submodule first.** Use
  the attribution trailer your session's instructions name. Never `git stash`,
  `reset`, `checkout -- <path>`, `clean`, `--amend`, `add -A` or `add -u`. Commit by
  explicit path. Never stage `lib/emsdk-macos-toolchain.zip`, which the owner deleted.
- **Never edit another session's files while they carry uncommitted edits.** That includes
  author attempts in `test/quests/<id>.lua`, `docs/quests/CUTSCENES.tsv` and
  `docs/quests/cutscenes/`.
- **Render skip is on by default** (`run.py` sets `TORIRS_RENDER_SKIP=1`). Pass
  `--no-publish` on every scratch run and gate run.
- **Var names carry kind and id** (`varp101_qp`, `varb<id>_<name>`; PR #99).
  `lint_quest.py` refuses a bare name.
- **Never use `resumeFromRunId`.** Relaunch with the same args; the state directory is the
  resume.

## Raids are another orchestrator's

Theatre of Blood, Chambers of Xeric and Tombs of Amascut belong to a separate raid
orchestrator ([`RAID_ORCHESTRATOR.md`](RAID_ORCHESTRATOR.md)), which uses these same claim
and branch mechanics with a measured encounter spec in place of the quest guide. Its rows
are tier 6 in `QUEUE.tsv`. **A quest orchestrator never claims a tier 6 row**, and the raid
orchestrator never claims tiers 1-5; the shared file is what keeps the two exclusive.
