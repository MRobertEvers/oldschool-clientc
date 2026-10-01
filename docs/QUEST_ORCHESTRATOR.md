# Quest orchestrator -- running the quest loop on any machine

This is what a Fable orchestrator does, on whichever machine it runs. Several machines may
run the loop against one origin (`v3` in both repos) at once. Claims and the content lock
are tracked files, so a `git fetch` shows every machine's holds. The tool is
`tools/quest_gate/claim.py`; its docstring is the reference.

## The loop

1. **Content parity** (`content_parity.workflow.js`) makes each quest's content match its
   source.
2. **Seam pass** (`seam_pass.workflow.js`) fixes the driver, engine and content seams that
   the blocked rows name.
3. **Author batch** (`author_batch.workflow.js`) has Sonnet authors write the tests. Then a
   reviewer per quest, one queue write, an Opus sampler, and a contact sheet.

- **On one machine the three never overlap.** Authors load `script/plugins/quest_driver/*.lua`
  live from the checkout, so a seam agent's half-written function crashes an author mid-quest.
  The order is: batch, its sampler push, its sheet, then the seam pass, then the next batch.
- **Author batches may run on several machines at once**, each on rows it has claimed.
- **A content pass (parity or seam) holds the content lock**, so only one runs at a time
  across all machines. Both kinds edit OSRS-Content. The script compiler allocates ids into
  `pack/varp.alloc`, `dbrow.alloc`, `varn.alloc` and the other `pack/*.alloc` files, and two
  machines allocating at once hand out the same id twice. An author batch on machine B may
  run while machine A holds the lock. B's checkout does not see A's uncommitted edits, and
  B's sampler merges A's pushed result.

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

Until the card update of 2026-10-01 is on a machine, a relay started there keeps its leg
records under `build/` and cannot be resumed elsewhere. After the update,
`test/quests/wip/<id>/` is the shared record.

## Launch checklist (every pass, every batch)

1. **Fetch both repos and get level.** Run `git fetch origin` and
   `git -C OSRS-Content fetch origin`. Merge in both: `git merge --no-edit origin/v3`, then
   in the submodule `git -C OSRS-Content merge --no-edit origin/v3`. Push anything you were
   ahead by. Then check:
   - `git rev-list --count origin/v3..HEAD` and `git rev-list --count HEAD..origin/v3` are
     both 0.
   - The same two counts in OSRS-Content are both 0.
   - `git submodule status OSRS-Content` shows no `+`.
2. **Pick a fresh, machine-unique name**: `<machine>-b<N>`, `<machine>-seam<N>` or
   `<machine>-parity<N>`, for example `mac1-b47`, `mac1-seam38` or `win1-parity3g`. Each
   machine keeps one prefix. Names from before 2026-10-01 (`sonnet-b46`, `seam37`) have no
   prefix. The name must appear nowhere yet. Check all of these:
   - `ls build/author_state build/seam_state build/parity_state`
   - `git log origin/v3 --oneline | grep <name>`
   - the owner column of `test/quests/QUEUE.tsv`
   - `test/quests/BATCHES.tsv`

   `claim.py` refuses a batch name that another host already holds rows under.
3. **Author batch: pick rows that are free.** `python3 tools/quest_gate/queue.py summary`
   lists every claimed row with its owner and age. Pick from rows that are `todo`, `blocked`
   or `content_bug` and not claimed. The batch's first step runs
   `claim.py batch <batch> <ids...>`. A quest it could not claim is dropped from the batch
   and named in the log. You may also claim before launching, with the same command; the
   step then re-claims what it already holds as a no-op.
4. **Content pass: the first step takes the lock** (`claim.py content-lock <pass>`). Exit 3
   means another machine holds it: that launch does nothing, so wait for that pass to close.
5. Launch from a substantive turn or a wakeup prompt, never from a bare "Hello?": workflow
   agents see the turn's user message.

## Closing (the closers and the sampler)

Before committing their own work, the parity closer, the seam closer and the batch's
sampler all do the same steps:

1. Fetch and merge both repos: `git fetch origin && git merge --no-edit origin/v3`, and
   the same in OSRS-Content.
2. If the merge conflicts in `QUEUE.tsv`, `BATCHES.tsv` or `PARITY.tsv`, run
   `python3 tools/quest_gate/claim.py merge-tsv <the conflicted tsv>...`, then
   `git commit --no-edit`. merge-tsv keeps one row per key (test_id; for BATCHES.tsv, the
   batch) and keeps rows that only one side added. When both sides changed the same row:
   - **QUEUE.tsv:** a verdict (green, blocked or content_bug) beats `claimed`, which beats
     `todo`. Between two claims, the earlier `claimed_at` holds, because that is the claim
     every other machine saw. Between two verdicts, ours wins.
   - **Other TSVs:** ours wins.
3. Resolve any other conflict by hand, and only if it is in the closer's own files.
   Otherwise stop and report.
4. Commit the submodule first and push it. In the parent, stage the gitlink of the merged
   submodule (`git add OSRS-Content`), commit, and push.
5. If a push is rejected, fetch and merge again, then push once more. Never force.

**After closing:**
- A content pass closer runs `claim.py content-unlock <pass>`.
- The sampler runs `claim.py release <batch>` for rows the batch did not finish. A row
  whose verdict was written with `queue.py set` is no longer claimed and needs no release.

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
| `test/quests/QUEUE.tsv` | `claim.py` (claim and release); the batch's Queue phase and sampler; the seam closer and parity closer (reopen) |
| `test/quests/BATCHES.tsv` | the orchestrator, one row per batch after publishing its contact sheet |
| `tools/quest_gate/PARITY.tsv` | the parity closer |
| `test/quests/CONTENT_LOCK` | `claim.py` only. Empty when free, else `<pass>@<host> <UTC ISO time>` |
| `test/quests/wip/<id>/` | see above |

QUEUE.tsv has two claim columns:
- `claimed_at`: the UTC ISO time of the claim, empty when the row is not claimed.
- `claim_prev`: the row's status and owner before the claim, as `<status>|<owner>`, so that
  a release restores the row exactly.

A claimed row has status `claimed` and owner `<batch>@<host>`. Writing any other status
with `queue.py set` ends the claim.

## Recovery

- **A stale claim may be released by anyone.** A claim is stale when it is older than 24
  hours and its batch has no commits since. Check `git log origin/v3 --oneline | grep <batch>`
  for commits; `queue.py summary` marks claims older than 24 h as STALE. Then run
  `claim.py release <batch> --stale`. Without `--stale`, `release` only touches this host's
  claims. Say in your report whose claim you released.
- **A stale content lock**, under the same 24-hour rule: `claim.py content-unlock <pass> --stale`.
- **A batch killed on its own machine:** relaunch it there with the same args. It resumes
  from `build/author_state/<batch>/` and `test/quests/wip/`.
- **Moving a batch's quests to another machine:**
  1. On the old machine (or `--stale` from the new one), run `claim.py release <batch>`.
  2. Commit and push the wip/ files the batch wrote.
  3. On the new machine, claim the quests under a new batch name. Relays resume from
     `wip/<id>/`.
- **`claim.py` exit 2** means it refused (not level, a dirty file, an unknown id, or a
  conflict outside its file). Nothing was pushed. Fix the cause and run it again.
- **Exit 3** means nothing to claim, a lock held by someone else, or a release that left
  another host's fresh claim in place.

## The owner's standing rules (from QUEST_SUITE_KIT.md Status and QUEST_HANDOFF_2026-09-29.md)

- **The guide is the spec.** Every Quest Helper step is driven by a real row, or the file
  stops at `t.blocked` with a `content_bug` naming the leg. All of these are rejected:
  - `goto_tile` past a gated door, stair or puzzle;
  - `::give` of an item the guide has you obtain;
  - a debugproc doing quest work;
  - `::setvar` on a quest var mid-run;
  - a narrating `mes()`.
- **Content sources:** LostCity where it has the quest, else the OSRS wiki plus the Quest
  Helper guide. Cite the source for every content edit. Bosses are fought for real.
- **Models:** the table under "Who runs what". Fable orchestrates only: it never does worker
  work, except landing a pass that a closer proved but could not commit.
- **Every batch publishes a contact sheet artifact and a `BATCHES.tsv` row.**
- **Commit and push both repos between passes, submodule first.** Never `git stash`,
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
