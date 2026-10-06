# The raid loop's own files

The raid loop (docs/RAID_ORCHESTRATOR.md) takes Theatre of Blood, Chambers of Xeric and
Tombs of Amascut from "implemented from tables" to "played for real, tick-measured,
defensible". It runs apart from the quest loop (owner, 2026-10-02): no QUEUE.tsv rows, no
claims, no docs/quest_authoring/ edits, nothing pushed to v3. Its work lives on one branch
(`matthew-mbp-m4-raid-b1`, parent and OSRS-Content) in a git worktree under
`build/orchestrator/worktrees/raid`, and reaches v3 by one PR when the owner asks.

| Where | What |
|---|---|
| `tools/raid_gate/workflows/raid_seam.workflow.js` | the driver seam pass (section 4): one Opus fixer per seam, an Opus closer; triage read from `SEAM_TRIAGE_<date>.md` here |
| `tools/raid_gate/workflows/raid_spec.workflow.js` | the spec pass (section 3): a corpus phase per raid, one Sonnet worker per encounter, an Opus closer |
| `tools/raid_gate/spec_check.py` | validates an encounter spec table (`docs/minigames/<raid>/encounters/<room>.tsv`) |
| `tools/raid_gate/run.py`, `gate.py` | run and grade a raid room test under `test/raids/` through the quest driver's tooling (seam `raid_tests_directory`) |
| `SEAM_TRIAGE_<date>.md` | the hand-written triage a seam pass transcribes |
| `SEAM_LEDGER.md`, `SPEC_LEDGER.md` | one line per seam / per room after each pass |
| `DRIVER_NOTES.md` | what a room-test author needs to know about the raid verbs (prayer, npc state, hazards, step on tick, tick log, room entry) |
| `CONTENT_BUGS.md` | every disagreement between our server and a grade A-C source, found by a spec pass; found is not fixed, write the row |

Order of passes: seam1 (the driver), then the ToB spec pass, then ToB room tests
(`test/raids/tob_*.lua`, one leg per room, each with a tick ledger asserting its spec
table), then ToA, then CoX.
