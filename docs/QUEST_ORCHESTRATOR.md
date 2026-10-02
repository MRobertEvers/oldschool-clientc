# Quest orchestrator -- running the quest loop on any machine

This is what a Fable orchestrator does, on whichever machine it runs. Several machines run
the loop against one origin (`v3` in both repos). The tool is `tools/quest_gate/claim.py`;
its docstring is the reference, and `tools/quest_gate/claim_test/run.sh` drives two machines
through a whole batch lifecycle against throwaway remotes.

## The model (owner, 2026-10-01)

- **The unit of work is a BATCH OF QUESTS.** One machine owns it for its whole life: content
  parity, seam fixes, authoring, review, sample, green. Nobody else touches those quests
  until the batch is done.
- **v3 holds only the claims.** A claim is a `QUEUE.tsv` row with status `claimed` and owner
  `<batch>@<host>`. `claim.py` is the only thing that writes to v3 directly. The claim is
  the exclusivity: two machines can never hold the same quest.
- **The batch branch holds the work.** Content ports, driver and engine fixes, tests,
  evidence, docs, `wip/`, PARITY.tsv, BATCHES.tsv and the verdicts all go on the branch
  `<host>-<batch>` (just `<batch>` when it already starts with `<host>-`, as `mac1-b49` does
  on `mac1`). The branch exists in BOTH repos, the parent and OSRS-Content, cut from each
  one's `origin/v3`. The host is `$QUEST_HOST`, else the short hostname.
- **One PR per repo per batch.** Two PRs reach v3: the OSRS-Content PR first, then the parent
  PR, whose gitlink points at the content branch.
- **Never wait.** An orchestrator never waits on another machine's claim. Claim other quests
  instead, from any tier. A batch may include rows that never had a parity pass, because the
  batch runs parity itself. The only wait is when nothing at all is unclaimed.
- **One batch at a time per machine.** A checkout is on one branch. Authors load
  `script/plugins/quest_driver/*.lua` live from the checkout, so a seam agent's half-written
  function would crash an author mid-quest. The passes of a batch therefore run one after
  another, never side by side.

## A batch's life

```
claim.py pr-sync ; claim.py status                  # every launch: settle merged PRs, see the claims
claim.py start <batch> <ids...>                     # branch in both repos from origin/v3, claim on v3
content_parity  { batch, round: 1, quests }         # <batch>-parity
seam_pass       { batch, round: N, quests, context } # <batch>-seamN, as often as needed
author_batch    { batch, round: N, tests, relay, sheet_dir }   # <batch>-authorN
   ... seam and author rounds until each quest is green or truly blocked ...
claim.py done <batch>                               # the two PRs; v3 rows say "PR #n pending merge"
(owner merges: OSRS-Content PR first, with a merge commit; then the parent PR)
claim.py pr-sync                                    # next launch: confirms no row is left claimed
```

- **`start`** fetches both repos. It creates the branch from `origin/v3` with `git switch -c`
  in each repo and claims the ids. A quest another batch holds is **dropped** and named;
  pick another quest, never wait. Batch names are machine-unique (`<machine>-b<N>`). Check
  that a name appears nowhere first: `claim.py status`, `git log origin/v3 --oneline | grep
  <name>`, `BATCHES.tsv`, and `ls build/*_state`.
- **The cards in batch mode.** Pass `batch` and `round`, and never `pass`. Each card checks
  `git branch --show-current`. A card refuses to run if the mode and the branch disagree:
  batch mode on v3, or v3 mode on a branch. Its State step runs `claim.py status --batch
  <batch> --require <ids>` and stops unless the batch holds every quest on v3 under this
  host. There is no content lock and no Claim phase. The queue step and the sampler write
  verdicts into the **branch's** QUEUE.tsv. On v3 the rows stay `claimed` until `done`.
  Nothing in a batch ever runs `claim.py release`, except an early release (below).
- **Picking quests.** On a branch, `queue.py summary` shows the branch's QUEUE.tsv. To pick
  from v3's queue, run:
  `git show origin/v3:test/quests/QUEUE.tsv > build/v3_queue.tsv && python3
  tools/quest_gate/queue.py --file build/v3_queue.tsv summary`.
  Pick rows that are `todo`, `blocked` or `content_bug`. Any tier will do.
- **Giving a quest back early.** If the batch cannot finish a quest, run `claim.py release
  <batch> <id> --note "<why>"`. The row is free on v3 at once, so another machine may take
  it. Its work stays on this branch and reaches v3 with this batch's PR.
- **`done <batch>`** does all of the following:
  - runs pr-prepare;
  - releases on the branch every row the batch still has `claimed`, with no verdict (todo
    again for the next machine);
  - pushes;
  - opens or updates the OSRS-Content PR, when the content branch has commits v3 lacks;
  - opens or updates the parent PR (title `quests: <batch> -- <greens>`; body: the per-quest
    verdict table, the contact sheets from BATCHES.tsv, and "merge the content PR first");
  - writes `PR #<n> pending merge (OSRS-Content #<m>)` onto the batch's v3 rows, which stay
    `claimed`;
  - runs pr-prepare once more, so that note does not conflict.

  Re-running `done` updates both PRs.
- **`pr-sync`** runs at every launch. A batch counts as merged only when **both** PRs are.
  The merge itself carries the branch's verdict rows onto v3, so pr-sync only confirms that
  no row is still claimed; any row that is gets released with a note. If either PR is closed
  unmerged, the batch's rows are released ("PR #n closed unmerged"). A `CONFLICTING` PR is
  fixed by re-running `done <batch>` on its branch.

## pr-prepare: why a batch PR is always mergeable

`claim.py pr-prepare <batch> [--push]` fetches both repos and merges `origin/v3` into the
batch branch, OSRS-Content first. It resolves by rule the conflicts that two machines'
batches make by construction:

- **QUEUE / BATCHES / PARITY.tsv** go through `merge-tsv`, row by row, with the batch as
  `--own-batch`. The batch's own verdict or note beats v3's claim of the same row. Otherwise
  a verdict beats `claimed`, which beats `todo`; between two claims the earlier one holds.
- **The pack id ledgers.** These are the compiler-written `pack/varp.alloc`, `dbrow.alloc`,
  `varn.alloc` and the other `pack/*.alloc` files in OSRS-Content. **Take v3's copy, then
  re-allocate the branch's own new lines on top of it:**
  - A var namespace (varp, varbit, varc) carries the id in the name. A line whose id is still
    free keeps it. A line whose id collides gets the next free id, and `varp<old>_<name>` is
    renamed to `varp<new>_<name>` in every tracked file of both repos (scripts, tests).
  - Every other namespace is re-allocated by the pack rebuild, `make -C src
    torirsserver-scripts`, which appends the branch's names with fresh ids.
  - The tool then checks that every name the branch allocated is in the result.
  - This is the merge rule for anyone resolving these files by hand, too: never keep both
    sides' lines, and never hand-edit ids.
- **The OSRS-Content gitlink** becomes the merged submodule HEAD.
- **Anything else conflicted** aborts both merges and exits 2, naming the files. Resolve
  only conflicts in files the batch changed, commit, and run it again.

`--push` pushes HEAD to `origin <branch>` in both repos; the submodule is pushed only when it
has commits that v3 lacks.

## Closing on a batch branch (closers and the sampler)

Commit by explicit path, the submodule first, then the parent with the `OSRS-Content`
gitlink. Then run `claim.py pr-prepare <batch> --push`. Exit 0 means both repos are pushed
to the branch. Never push to v3, never force, rebase, reset or stash. Exit 2 means a conflict
the tool does not resolve. Fix it if it is in the pass's own files; otherwise stop and
report.

## How claims reach v3 from a branch

On v3, `claim.py` writes in the checkout, as it did before 2026-10-01. On any other branch it
writes through `build/v3_coord/`. That is a sparse worktree of the same repository, holding
only QUEUE.tsv and CONTENT_LOCK, on local branch `coord-v3`. claim.py creates it on first use
and fast-forwards it before each write. The write is committed there and pushed as
`coord-v3:v3`. A rejected push is retried: fetch, merge, re-apply. A row another machine
claimed in between is dropped, never double-claimed. After a claim, a branch with no commits
of its own is fast-forwarded so that its QUEUE.tsv shows the claim.

## The content lock: retired as a gate

`test/quests/CONTENT_LOCK` and `claim.py content-lock/unlock` stay for two cases:
- a **whole-pack change** that no batch branch could merge (a var rename across every quest,
  a pack reshuffle): pass `whole_pack: true` to the card;
- a pass run directly on v3 in the pre-2026-10-01 way. That is the cards without `round`,
  which still take the lock.

The batch claim is the exclusivity now. A held lock never blocks a batch.

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
| orchestrator | -- | Fable; never an agent model |

**An orchestrator never passes `author_model` or `worker_model`.** The defaults are the
owner's pins. `claude-sonnet-5-5` is spelled out because the `sonnet` alias is still Sonnet 5.

## Launch checklist

1. **Fetch both repos**: `git fetch origin` and `git -C OSRS-Content fetch origin`.
2. **`claim.py pr-sync`**, then **`claim.py status`**: see which batches are held, by whom,
   and how old they are.
3. **Mid-batch:** stay on the batch's branch (both repos) and launch the next card in batch
   mode.
4. **New batch:** pick unclaimed quests from v3's queue, then `claim.py start <batch>
   <ids...>`. Never wait on a held quest.
5. Launch from a substantive turn or a wakeup prompt, never from a bare "Hello?". Workflow
   agents see the turn's user message.

## Work in progress: `test/quests/wip/<id>/`

These files are tracked, so a relay's record travels with the batch branch:

| file | written by |
|---|---|
| `leg<K>.json` | relay runner of leg K (`outcome: done` means a later runner skips it) |
| `leg<K>.progress.md` | relay runner of leg K, appended after every run |
| `relay.md` | every relay runner, one `## leg K` block of at most ten lines |
| `parked.lua` | the reviewer, for a rejected, never-committed file |
| other `*.lua` / `*.md` | a seam fixer, a scratch copy the row names |

- The sampler commits them, on the branch.
- When a quest goes green, the sampler runs `git rm -r test/quests/wip/<id>`.
- Checkpoints, ledgers and shots stay under `build/`. A runner on a new machine makes a full
  run first.
- Tools never read wip/ as tests.
- To resume another batch's released quest before its PR merges, copy the record from that
  branch: `git archive origin/<branch> test/quests/wip/<id> | tar -x`. Never merge the
  other batch's branch.

## Recovery

- **A stale claim**: older than 24 hours, with no commits on its batch's branch since
  (`git log origin/<branch> -1`). Anyone may release it with `claim.py release <batch>
  --stale`. Say whose claim you released.
- **A batch killed on its own machine**: switch back to its branch in both repos and relaunch
  the card with the same args. The state dir and `wip/` are the resume.
- **`claim.py` exit 2** means it refused (wrong branch, not level, a conflict it does not
  resolve, a gh error). Nothing was pushed to v3. **Exit 3** means a request was not met: no
  claimable row, a missing claim, or a lock held elsewhere.
- **The coordination worktree is wedged**: `git worktree remove --force build/v3_coord`.
  claim.py recreates it. Its only commits are claims.

## The owner's standing rules (QUEST_SUITE_KIT.md Status and QUEST_HANDOFF_2026-09-29.md)

- **The guide is the spec.** Every Quest Helper step is driven by a real row, or the file
  stops at `t.blocked` with a `content_bug` naming the leg. All of these are rejected:
  - `goto_tile` past a gated door, stair or puzzle;
  - `::give` of an item the guide has you obtain;
  - a debugproc doing quest work;
  - `::setvar` on a quest var mid-run;
  - a narrating `mes()`.
- **Content sources:** LostCity where it has the quest, else the OSRS wiki plus the Quest
  Helper guide. Cite the source for every content edit. Bosses are fought for real.
- **Models:** the table above. Fable orchestrates only.
- **Every batch publishes a contact sheet artifact and a `BATCHES.tsv` row** (on the branch).
- **Never** `git stash`, `reset`, `checkout -- <path>`, `clean`, `--amend`, `add -A` or
  `add -u`. Commit by explicit path. Never stage `lib/emsdk-macos-toolchain.zip`.
- **Never edit another session's files** while they carry uncommitted edits. That includes
  `docs/quests/CUTSCENES.tsv` and `docs/quests/cutscenes/`.
- **Render skip is on by default.** Pass `--no-publish` on every scratch and gate run.
- **Var names carry kind and id** (`varp101_qp`; PR #99). `lint_quest.py` refuses a bare name.
- **Never use `resumeFromRunId`.** Relaunch with the same args.
