# Quest suite handoff -- 2026-09-29

Where the automated quest suite stands, and how to pick it up from a cold
start. Branch `lane-quest-driver` in this checkout (3draster itself; there is
no separate worktree), OSRS-Content submodule on `lane-quest-driver`. Both are
pushed after every pass.

## State

| Tier | Quests | Green | Coverage | Notes |
|---|---|---|---|---|
| 1 | 40 | 40 | 40 FULL, 0 GUIDE-GAP | merged to v3 as PR #96 (908500675) |
| 2 | 19 | 19 | 19 FULL, 0 GUIDE-GAP | on `lane-quest-driver`, NOT merged to v3 (c0bc59f0a, content 27dbb1259b) |
| 3 | 43 | 0 | -- | parity under way: parity3a landed f8762c0c4; parity3b closing |
| 4 | 77 | 0 | -- | not started |
| 5 | 10 | 0 | -- | unknown difficulty, not started |

Tier 2's per-quest table is in `docs/QUEST_SUITE_KIT.md` Status and
`build/close_tier2_table.tsv`. Every batch's contact sheet is linked in
`test/quests/BATCHES.tsv` (sonnet-b27..b32 for tier 2).

## Tier 3 roster and sources

LostCity quests (the port must match `LostCity_Content2/scripts/quests/<dir>`
leg by leg): arena arthur chompybird death demon dragon druidspirit elena
gobdip grandtree hazeelcult ikov imp itexam itgronigen junglepotion legends
mcannon priestperil totem troll upass waterfall zanaris zombiequeen.

Wiki + Quest Helper quests (pin a brief under `docs/quests/`): deserttreasure
deviousminds forgettabletale gardenoftranquility giantdwarf hauntedmine horror
icthlarin losttribe mm ratcatchers redreef regicide routequest tbwt
theslugmenace troll_love viking.

Parity batches, in sevens:

| Pass | Quests | State |
|---|---|---|
| parity3a | arena arthur chompybird death demon dragon druidspirit | landed f8762c0c4 (content b6aef218bc) |
| parity3b | elena gobdip grandtree hazeelcult ikov imp itexam | 7/7 worker reports on disk; closer running the suite |
| parity3c | itgronigen junglepotion legends mcannon priestperil totem troll | next |
| parity3d | upass waterfall zanaris zombiequeen deserttreasure deviousminds forgettabletale | |
| parity3e | gardenoftranquility giantdwarf hauntedmine horror icthlarin losttribe mm | |
| parity3f | ratcatchers redreef regicide routequest tbwt theslugmenace troll_love viking | |

## Next steps, in order

1. Land parity3b (its closer commits and pushes; if it died, relaunch the
   parity script with the SAME args -- it resumes from
   `build/parity_state/parity3b/`).
2. seam28, triage file `build/seam_state/seam28_triage.md`, at least:
   - Death Plateau's soldier ambience timers, parked at
     `build/parity_state/parity3a/parked/` because they shift the shared
     random stream and move Bob out of A Tail of Two Cats' reach. The test
     must FIND a wandering npc, never assume its tile.
   - parity3b's closer notes: the goblin combat selftest rows move with a
     new spawn file (RNG again), and whatever else its report names.
3. Author batch sonnet-b33 on the 14 parity3a+3b quests.
4. parity3c .. parity3f, a seam pass after every two, an author batch after
   every seam pass, until all 43 are green, FULL, zero GUIDE-GAP.

## How to launch a pass

Saved scripts (resumable from their state dirs; never `resumeFromRunId`):
`~/.claude/projects/-Users-matthewevers-Documents-git-repos-3draster/39a89e15-5b41-4d33-a51f-14d13d14be21/workflows/scripts/`

| Script | Args |
|---|---|
| `quest-content-parity-wf_8fac42d8-dfc.js` | `pass`, `quests`, `context`, `worker_model` (default claude-sonnet-5-5) |
| `quest-seam-pass-wf_729986e8-d1f.js` | `pass`, `reuse_triage` (a triage .md), `context` |
| `quest-author-batch-wf_880014c2-3fb.js` | `batch`, `tests`, `author_model` (default claude-sonnet-5-5) |

Repo copies: `tools/quest_gate/{content_parity,seam_pass,author_batch}.workflow.js`.

Rules that cost time when broken:

- Pass names must be fresh: `ls build/parity_state build/seam_state build/author_state` first.
- Launch from a substantive or wakeup turn. Workflow agents see the turn's
  user message, and a bare "Hello?" made fixers refuse engine work.
- Before every launch: `git fetch` both repos, ahead-count 0. Every pass
  commits and pushes both repos (submodule first).
- Sonnet 5.5 (`claude-sonnet-5-5`; the `sonnet` alias is still Sonnet 5)
  authors, reviewers and parity workers; Opus fixers, samplers, closers; Opus
  authors for a quest whose Sonnet author compacted twice. The orchestrator
  does not do worker work, except landing a pass a closer proved but could
  not commit.
- Every batch publishes a contact sheet artifact and a `BATCHES.tsv` row.
- A rejected or reverted test file is parked under
  `build/author_state/<batch>/rejected/` and named in its QUEUE row.
- Sources: LostCity where the quest is in LostCity, else the OSRS wiki and
  Quest Helper. No cheats past the guide's own work. The boss is fought for
  real. `--no-publish` on every scratch and gate run.
- Never stage `lib/emsdk-macos-toolchain.zip` (the owner's deletion).
